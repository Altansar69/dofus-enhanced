#include "tools-ui.hpp"
#include "soul-panel.hpp"
#include "module.hpp"
#include <algorithm>
#include <cmath>
#include <vector>
namespace toolsUi {
namespace {
using namespace managed;
constexpr const char* Menus="Core.UILogic.Components.ContextMenu";
struct Entry {
    std::string key,title; int icon=0; bool (*visible)()=nullptr; bool (*toggle)()=nullptr;
    Ref window,header,button,manipulator,cached,compositor,service;
    Ref menu,item,callback;
    bool armed=false;
};
std::vector<Entry> entries;
Method add{},remove{},appendTitle{},appendSeparator{},appendItem{};
bool installed=false;
bool Ready(){return module&&module->ready&&module->servicesReady&&!module->stopped&&!module->stopRequested;}
Entry& Find(const char* key){auto i=std::find_if(entries.begin(),entries.end(),[&](const Entry& e){return e.key==key;});if(i==entries.end())throw std::runtime_error("Unknown native tool panel");return *i;}
Obj Root(Obj menu){if(!menu)return nullptr;Obj root=api.Field(menu,"dofusContextualMenuRoot");return root&&api.Call(root,"get_menuData")==menu?root:nullptr;}
bool Present(Obj menu,Obj item){Ref rows(api.Call(menu,"get_menuItems"));int n=api.Value<int>(api.Call(rows.get(),"get_Count"));if(n<0||n>256)return false;for(int i=0;i<n;++i)if(api.Call(rows.get(),"get_Item",{&i})==item)return true;return false;}
void Close(Entry& e){e.armed=false;Ref root(Root(e.menu.get()));if(root.get()&&api.Call(root.get(),"get_panel")){bool delayed=false;api.Call(root.get(),"RemoveMenu",{&delayed});}e.callback.reset();e.item.reset();e.menu.reset();}
bool Current(const Entry& e){return Ready()&&e.window.get()&&e.manipulator.get()&&e.cached.get()==module->cached.get()&&e.compositor.get()==module->compositor.get()&&e.service.get()==module->FindService("fpz")&&api.Call(e.window.get(),"get_panel")&&api.Call(e.button.get(),"get_panel")==api.Call(e.window.get(),"get_panel")&&api.Value<bool>(api.Call(e.button.get(),"get_enabledInHierarchy"));}
bool Populate(Obj owner,Obj event){
    auto found=std::find_if(entries.begin(),entries.end(),[&](const Entry& e){return owner&&owner==e.manipulator.get();});if(found==entries.end())return false;
    auto& e=*found;
    try{
        if(!Current(e)||!event)return true;Ref menu(api.Call(event,"get_menu"));if(!menu.get()||menu.get()==e.menu.get())return true;
        Close(e);e.menu.reset(menu.get());Ref title(api.string_new(e.title.c_str())),text(api.string_new("Masquer le module")),empty(api.string_new(""));
        Ref help(api.string_new("Réactiver ce panneau avec son bouton à côté de Capture d’âme, en haut à droite."));
        api.Invoke(appendTitle,e.menu.get(),{title.get()});api.Invoke(appendSeparator,e.menu.get());
        Ref rows(api.Call(e.menu.get(),"get_menuItems"));int index=api.Value<int>(api.Call(rows.get(),"get_Count"));if(index<0||index>128)throw std::runtime_error("Tool menu bounds");
        Ref asset(api.object_new(api.class_from_type(api.method_get_param(appendItem,7)))),color(api.object_new(api.class_from_type(api.method_get_param(appendItem,10))));
        bool disabled=false,close=true;unsigned delay=500;int style=1,icon=137;
        api.Invoke(appendItem,e.menu.get(),{text.get(),nullptr,&disabled,&close,help.get(),&delay,empty.get(),api.object_unbox(asset.get()),&style,&icon,api.object_unbox(color.get()),empty.get(),empty.get()});
        e.item.reset(api.Call(rows.get(),"get_Item",{&index}));e.callback.reset(api.Call(e.item.get(),"get_m_callback"));e.armed=true;
    }catch(const std::exception& error){e.armed=false;Log(std::string("TOOL_PANEL_MENU_ERROR ")+error.what());}
    return true;
}
bool Callback(Obj callback,Obj view){
    for(auto& e:entries)if(callback&&callback==e.callback.get()){
        try{if(!e.armed||!Current(e)||!view||!api.Call(view,"get_panel")||api.Call(view,"get_itemData")!=e.item.get()||api.Value<bool>(api.Call(e.item.get(),"get_disabled"))||!Present(e.menu.get(),e.item.get()))return true;
            Ref root(Root(e.menu.get()));if(!root.get()||!api.Call(root.get(),"get_panel"))return true;Close(e);if(e.visible())e.toggle();
        }catch(const std::exception& error){Log(std::string("TOOL_PANEL_HIDE_ERROR ")+error.what());}return true;
    }
    return false;
}
}
void Register(const char* key,const char* title,int icon,bool(*visible)(),bool(*toggle)()){
    if(!visible||!toggle)throw std::runtime_error("Missing tool panel callbacks");
    if(std::any_of(entries.begin(),entries.end(),[&](const Entry& e){return e.key==key;}))return;
    Entry e;e.key=key;e.title=title;e.icon=icon;e.visible=visible;e.toggle=toggle;entries.push_back(std::move(e));
}
std::vector<Shortcut> Shortcuts(){
    std::vector<Shortcut> result;
    for(const auto& e:entries)result.push_back({e.key,e.title,e.icon});
    return result;
}
bool Visible(const char* key){return Find(key).visible();}
bool Toggle(const char* key){return Ready()&&Find(key).toggle();}
void Install(){if(installed)return;
    auto scroll=api.Class("UnityEngine.UIElementsModule.dll","UnityEngine.UIElements","ScrollView");
    api.FindVerified(scroll,".ctor",0,0x5742BD0);api.FindVerified(scroll,"get_contentContainer",0,0x5742BE0);
    auto klass=api.Class("Core.dll",Menus,"DofusContextualMenu");
    appendTitle=api.FindVerified(klass,"AppendTitle",1,0x16D7F70);appendSeparator=api.FindVerified(klass,"AppendSeparator",0,0x16D76E0);appendItem=api.FindVerified(klass,"AppendDefaultItem",13,0x16D6C60);
    auto extensions=api.Class("UnityEngine.UIElementsModule.dll","UnityEngine.UIElements","VisualElementExtensions");add=api.FindVerified(extensions,"AddManipulator",2,0x56755F0);remove=api.FindVerified(extensions,"RemoveManipulator",2,0x5676A90);
    // The native widget list ("…" in the top bar) receives no owned entries:
    // the toolbar buttons beside Capture d’âme are the only shortcuts, and each
    // open panel keeps its own header menu above.
    soul::SetAuxiliaryPanelMenus(Populate,Callback);installed=true;
}
managed::Obj ScrollBody(managed::Obj window,managed::Obj root){
    Ref host(api.Call(window,"get_contentContainer")),scroll(api.New(api.Class("UnityEngine.UIElementsModule.dll","UnityEngine.UIElements","ScrollView")));
    Ref resolved(api.Call(root,"get_resolvedStyle"));float height=api.Value<float>(api.Call(resolved.get(),"get_height"));
    float width=api.Value<float>(api.Call(resolved.get(),"get_width"));
    if(!std::isfinite(height)||height<280)height=480;
    if(!std::isfinite(width)||width<300)width=720;
    const auto pixels=[](Obj element,const char* property,float value){
        int unit=0;Ref style(api.Call(element,"get_style"));
        Ref length(api.New(api.Class("UnityEngine.UIElementsModule.dll","UnityEngine.UIElements","Length"),{&value,&unit}));
        Ref box(api.Call(style.get(),(std::string("UnityEngine.UIElements.IStyle.get_")+property).c_str()));
        api.Call(box.get(),"set_value",{api.object_unbox(length.get())});
        api.Call(style.get(),(std::string("UnityEngine.UIElements.IStyle.set_")+property).c_str(),{api.object_unbox(box.get())});
    };
    pixels(scroll.get(),"maxHeight",std::clamp(height-120.f,160.f,560.f));
    pixels(window,"maxWidth",width-32.f);pixels(window,"left",16);pixels(window,"top",48);
    api.Call(host.get(),"Add",{scroll.get()});return api.Call(scroll.get(),"get_contentContainer");
}
void AttachHeader(const char* key,Obj window){auto& e=Find(key);if(e.window.get())DetachHeader(key);e.window.reset(window);e.header.reset(api.Call(window,"get_headerWidget"));e.cached.reset(module->cached.get());e.compositor.reset(module->compositor.get());e.service.reset(module->FindService("fpz"));if(!e.service.get())throw std::runtime_error("Panel menu service unavailable");int more=202,left=0;bool yes=true,no=false;api.Call(window,"set_headerIconRight",{&more});api.Call(e.header.get(),"set_showRightButton",{&yes});e.button.reset(api.Field(e.header.get(),"rightButton"));if(!e.button.get())throw std::runtime_error("Panel header button unavailable");e.manipulator.reset(api.New(api.Class("Core.dll",Menus,"DofusContextualMenuManipulator"),{e.service.get(),nullptr,&left,&no}));api.Invoke(add,nullptr,{e.button.get(),e.manipulator.get()});}
void DetachHeader(const char* key){auto& e=Find(key);Close(e);if(e.manipulator.get()){api.Invoke(remove,nullptr,{e.button.get(),e.manipulator.get()});e.manipulator.reset();}e.button.reset();e.header.reset();e.window.reset();e.cached.reset();e.compositor.reset();e.service.reset();}
void Tick(){try{
    if(!Ready()){Restore();return;}
}catch(const std::exception& error){Log(std::string("TOOL_PANEL_REFRESH_ERROR ")+error.what());}}
void Restore(){
    for(auto& e:entries)if(e.window.get())try{DetachHeader(e.key.c_str());}catch(const std::exception& error){Log(std::string("TOOL_HEADER_CLEANUP_ERROR ")+error.what());}
}
bool CleanupPending(){return std::any_of(entries.begin(),entries.end(),[](const Entry& e){return e.window.get()!=nullptr;});}
}
