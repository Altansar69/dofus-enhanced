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
struct Choice {std::string key;Ref menu,item,callback,cached,compositor,owner,service;};
std::vector<Entry> entries;
std::vector<Choice> choices;
bool retiring=false;
Method add{},remove{},appendTitle{},appendSeparator{},appendItem{},appendCheckbox{};
bool installed=false;
using BuildFn=void(*)(Obj,Obj,Method);
BuildFn originalBuild=nullptr;
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
void CloseChoices(){
    retiring=true;
    for(auto& c:choices){Ref root(Root(c.menu.get()));if(root.get()&&api.Call(root.get(),"get_panel")){bool delayed=false;api.Call(root.get(),"RemoveMenu",{&delayed});}}
    choices.clear();retiring=false;
}
bool Callback(Obj callback,Obj view){
    for(auto& e:entries)if(callback&&callback==e.callback.get()){
        try{if(!e.armed||!Current(e)||!view||!api.Call(view,"get_panel")||api.Call(view,"get_itemData")!=e.item.get()||api.Value<bool>(api.Call(e.item.get(),"get_disabled"))||!Present(e.menu.get(),e.item.get()))return true;
            Ref root(Root(e.menu.get()));if(!root.get()||!api.Call(root.get(),"get_panel"))return true;Close(e);if(e.visible())e.toggle();
        }catch(const std::exception& error){Log(std::string("TOOL_PANEL_HIDE_ERROR ")+error.what());}return true;
    }
    for(auto& c:choices)if(callback&&callback==c.callback.get()){
        try{if(retiring||!Ready()||c.cached.get()!=module->cached.get()||c.compositor.get()!=module->compositor.get()||c.service.get()!=module->FindService("frm")||api.Field(c.service.get(),"<dzqi>k__BackingField")!=c.owner.get()||!view||!api.Call(view,"get_panel")||api.Call(view,"get_itemData")!=c.item.get()||api.Value<bool>(api.Call(c.item.get(),"get_disabled"))||!Present(c.menu.get(),c.item.get()))return true;
            Ref root(Root(c.menu.get()));if(!root.get()||!api.Call(root.get(),"get_panel"))return true;
            auto& e=Find(c.key.c_str());e.toggle();bool on=e.visible();api.Call(c.item.get(),"set_isOn",{&on});
        }catch(const std::exception& error){Log(std::string("TOOL_MODULE_TOGGLE_ERROR ")+error.what());}return true;
    }
    return false;
}
void BuildHook(Obj owner,Obj menu,Method method){
    originalBuild(owner,menu,method);
    try{
        if(!Ready()||!owner||!menu)return;Ref service(module->FindService("frm"));if(!service.get()||api.Field(service.get(),"<dzqi>k__BackingField")!=owner)return;
        if(!retiring&&!choices.empty()&&std::all_of(choices.begin(),choices.end(),[&](const Choice& c){
            return c.menu.get()==menu&&c.owner.get()==owner&&c.cached.get()==module->cached.get()&&c.compositor.get()==module->compositor.get()&&Present(menu,c.item.get());
        }))return;
        CloseChoices();api.Invoke(appendSeparator,menu);
        for(const auto& e:entries){
            Ref rows(api.Call(menu,"get_menuItems"));int index=api.Value<int>(api.Call(rows.get(),"get_Count"));if(index<0||index>256)throw std::runtime_error("Module menu bounds");
            Ref title(api.string_new(e.title.c_str())),help(api.string_new("Afficher ou masquer ce panneau.")),empty(api.string_new(""));bool on=e.visible(),disabled=false,close=false;unsigned delay=500;int icon=e.icon;
            api.Invoke(appendCheckbox,menu,{title.get(),nullptr,&on,&disabled,&close,help.get(),&delay,&icon,empty.get()});
            Ref item(api.Call(rows.get(),"get_Item",{&index}));Ref callback(api.Call(item.get(),"get_m_callback"));
            choices.push_back({e.key,Ref(menu),std::move(item),std::move(callback),Ref(module->cached.get()),Ref(module->compositor.get()),Ref(owner),Ref(service.get())});
        }
    }catch(const std::exception& error){Log(std::string("TOOL_MODULE_MENU_ERROR ")+error.what());}
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
    appendTitle=api.FindVerified(klass,"AppendTitle",1,0x16D7F70);appendSeparator=api.FindVerified(klass,"AppendSeparator",0,0x16D76E0);appendItem=api.FindVerified(klass,"AppendDefaultItem",13,0x16D6C60);appendCheckbox=api.FindVerified(klass,"AppendCheckboxItem",9,0x16D63D0);
    auto extensions=api.Class("UnityEngine.UIElementsModule.dll","UnityEngine.UIElements","VisualElementExtensions");add=api.FindVerified(extensions,"AddManipulator",2,0x56755F0);remove=api.FindVerified(extensions,"RemoveManipulator",2,0x5676A90);
    auto manager=api.Class("Core.dll","Core.UILogic.Common","WidgetManager");
    AddHook(api.FindVerified(manager,"AddPresetContextualMenuItems",1,0x17D83F0),0x17D83F0,reinterpret_cast<void*>(&BuildHook),reinterpret_cast<void**>(&originalBuild));
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
    if(retiring){CloseChoices();return;}
    for(auto& c:choices){
        Ref root(Root(c.menu.get()));
        if(c.cached.get()!=module->cached.get()||c.compositor.get()!=module->compositor.get()||!root.get()||!api.Call(root.get(),"get_panel")){CloseChoices();return;}
        bool on=Find(c.key.c_str()).visible();api.Call(c.item.get(),"set_isOn",{&on});
    }
}catch(const std::exception& error){Log(std::string("TOOL_MODULE_REFRESH_ERROR ")+error.what());}}
void Restore(){
    try{CloseChoices();}catch(const std::exception& error){Log(std::string("TOOL_MODULE_MENU_CLEANUP_ERROR ")+error.what());}
    for(auto& e:entries)if(e.window.get())try{DetachHeader(e.key.c_str());}catch(const std::exception& error){Log(std::string("TOOL_HEADER_CLEANUP_ERROR ")+error.what());}
}
bool CleanupPending(){return !choices.empty()||std::any_of(entries.begin(),entries.end(),[](const Entry& e){return e.window.get()!=nullptr;});}
}
