#include "ui/tools-ui.hpp"
#include "souls/soul-panel.hpp"
#include "core/module.hpp"
#include <algorithm>
#include <cmath>
#include <vector>
namespace toolsUi {
namespace {
using namespace managed;
constexpr const char* Menus="Core.UILogic.Components.ContextMenu";
struct Entry {
    std::string key,title; int icon=0; bool (*visible)()=nullptr; bool (*toggle)()=nullptr;
    bool (*barButton)()=nullptr; // whether the top-bar button is shown (always without)
    Layouts layouts; // optional radio rows of the panel's layouts
    Ref window,header,button,manipulator,cached,compositor,service;
    Ref menu,item,callback;
    struct LayoutRow {Ref item,callback;};
    std::vector<LayoutRow> layoutRows; // in layout order
    bool armed=false;
};
// "Réduire les titres des modules": the game's header sub-menu lists its own
// widgets; the mod's panels are appended there, remembered in the settings
// and applied to each window through the native header size.
struct HeaderPanel { const char* key; const char* title; int icon; bool Settings::*small; };
constexpr HeaderPanel HeaderPanels[]={
    {"soul","Pierre d\xE2\x80\x99\xC3\xA2me",128,&Settings::smallHeaderSoul},
    {"treasure","Chasse au tr\xC3\xA9sor",317,&Settings::smallHeaderTreasure},
    {"notebook","Carnet personnel",225,&Settings::smallHeaderNotebook},
    {"stats","Statistiques",77,&Settings::smallHeaderStats}};
struct Choice { size_t panel=0; Ref menu,item,callback,cached,compositor,owner,service; };
std::vector<Entry> entries;
std::vector<Choice> choices;
Ref soulWindow;
Method add{},remove{},appendTitle{},appendSeparator{},appendItem{},appendCheckbox{},appendRadio{},setHeaderSize{};
using SubMenuFn=Obj(*)(Obj,Method);
using AllFn=void(*)(Obj,bool,Method);
SubMenuFn originalSubMenu=nullptr;
AllFn originalAll=nullptr;
bool installed=false;
bool Ready(){return module&&module->ready&&module->servicesReady&&!module->stopped&&!module->stopRequested;}
Entry& Find(const char* key){auto i=std::find_if(entries.begin(),entries.end(),[&](const Entry& e){return e.key==key;});if(i==entries.end())throw std::runtime_error("Unknown native tool panel");return *i;}
Obj Root(Obj menu){if(!menu)return nullptr;Obj root=api.Field(menu,"dofusContextualMenuRoot");return root&&api.Call(root,"get_menuData")==menu?root:nullptr;}
bool Present(Obj menu,Obj item){Ref rows(api.Call(menu,"get_menuItems"));int n=api.Value<int>(api.Call(rows.get(),"get_Count"));if(n<0||n>256)return false;for(int i=0;i<n;++i)if(api.Call(rows.get(),"get_Item",{&i})==item)return true;return false;}
void Close(Entry& e){e.armed=false;Ref root(Root(e.menu.get()));if(root.get()&&api.Call(root.get(),"get_panel")){bool delayed=false;api.Call(root.get(),"RemoveMenu",{&delayed});}e.callback.reset();e.item.reset();e.layoutRows.clear();e.menu.reset();}
bool Current(const Entry& e){return Ready()&&e.window.get()&&e.manipulator.get()&&e.cached.get()==module->cached.get()&&e.compositor.get()==module->compositor.get()&&e.service.get()==module->FindService("fpz")&&api.Call(e.window.get(),"get_panel")&&api.Call(e.button.get(),"get_panel")==api.Call(e.window.get(),"get_panel")&&api.Value<bool>(api.Call(e.button.get(),"get_enabledInHierarchy"));}
bool Populate(Obj owner,Obj event){
    auto found=std::find_if(entries.begin(),entries.end(),[&](const Entry& e){return owner&&owner==e.manipulator.get();});if(found==entries.end())return false;
    auto& e=*found;
    try{
        if(!Current(e)||!event)return true;Ref menu(api.Call(event,"get_menu"));if(!menu.get()||menu.get()==e.menu.get())return true;
        Close(e);e.menu.reset(menu.get());Ref title(api.string_new(e.title.c_str())),text(api.string_new("Masquer le module")),empty(api.string_new(""));
        // A panel whose top-bar button its option removed comes back from the options;
        // its button sits next to Capture d’âme's unless soulButton hides that one (1.16.1).
        const bool bar=!e.barButton||e.barButton(),soul=module->settings.soulButton;
        Ref help(api.string_new(!bar?"Réactiver ce panneau depuis les options du mod.":soul?"Réactiver ce panneau avec son bouton à côté de Capture d’âme, en haut à droite.":"Réactiver ce panneau avec son bouton en haut à droite de l’écran."));
        api.Invoke(appendTitle,e.menu.get(),{title.get()});api.Invoke(appendSeparator,e.menu.get());
        Ref rows(api.Call(e.menu.get(),"get_menuItems"));int index=api.Value<int>(api.Call(rows.get(),"get_Count"));if(index<0||index>128)throw std::runtime_error("Tool menu bounds");
        Ref asset(api.object_new(api.class_from_type(api.method_get_param(appendItem,7)))),color(api.object_new(api.class_from_type(api.method_get_param(appendItem,10))));
        bool disabled=false,close=true;unsigned delay=500;int style=1,icon=137;
        api.Invoke(appendItem,e.menu.get(),{text.get(),nullptr,&disabled,&close,help.get(),&delay,empty.get(),api.object_unbox(asset.get()),&style,&icon,api.object_unbox(color.get()),empty.get(),empty.get()});
        e.item.reset(api.Call(rows.get(),"get_Item",{&index}));e.callback.reset(api.Call(e.item.get(),"get_m_callback"));e.armed=true;
        if(!e.layouts.choices.empty()&&e.visible()){
            // The panel's layouts as the game's radio rows (one group, the
            // current one marked); a click on another one applies it.
            api.Invoke(appendSeparator,e.menu.get());
            Ref radioAsset(api.object_new(api.class_from_type(api.method_get_param(appendRadio,4))));
            const int current=e.layouts.current();int group=0;bool switchOff=false,refreshParent=false;
            for(size_t i=0;i<e.layouts.choices.size();++i){
                int layoutIndex=api.Value<int>(api.Call(rows.get(),"get_Count"));
                if(layoutIndex<0||layoutIndex>128)throw std::runtime_error("Tool menu bounds");
                Ref label(api.string_new(e.layouts.choices[i].label.c_str())),layoutHelp(api.string_new(e.layouts.choices[i].help.c_str()));
                bool on=static_cast<int>(i)==current;
                api.Invoke(appendRadio,e.menu.get(),{label.get(),nullptr,&on,&group,api.object_unbox(radioAsset.get()),&switchOff,&disabled,&close,layoutHelp.get(),empty.get(),&delay,&refreshParent});
                Entry::LayoutRow row;row.item.reset(api.Call(rows.get(),"get_Item",{&layoutIndex}));row.callback.reset(api.Call(row.item.get(),"get_m_callback"));
                e.layoutRows.push_back(std::move(row));
            }
        }
    }catch(const std::exception& error){e.armed=false;Log(std::string("TOOL_PANEL_MENU_ERROR ")+error.what());}
    return true;
}
// Header size persistence and application.
size_t PanelIndex(const char* key){for(size_t i=0;i<std::size(HeaderPanels);++i)if(std::string(HeaderPanels[i].key)==key)return i;return std::size(HeaderPanels);}
bool Small(size_t panel){return module->settings.*HeaderPanels[panel].small;}
Obj PanelWindow(size_t panel){
    if(panel==0)return soul::PanelWindow();
    auto i=std::find_if(entries.begin(),entries.end(),[&](const Entry& e){return e.key==HeaderPanels[panel].key;});
    return i==entries.end()?nullptr:i->window.get();
}
void ApplySize(Obj window,bool small){if(window)api.Invoke(setHeaderSize,window,{&small});}
void DropChoices(){
    for(auto& c:choices){
        try{Ref root(Root(c.menu.get()));if(root.get()&&api.Call(root.get(),"get_panel")){bool delayed=false;api.Call(root.get(),"RemoveMenu",{&delayed});}}
        catch(const std::exception& error){Log(std::string("TOOL_HEADER_MENU_CLEANUP_ERROR ")+error.what());}
    }
    choices.clear();
}
bool CurrentChoice(const Choice& c){
    if(!Ready()||c.cached.get()!=module->cached.get()||c.compositor.get()!=module->compositor.get())return false;
    Ref service(module->FindService("frm"));
    return service.get()&&service.get()==c.service.get()&&api.Field(service.get(),"<dzqi>k__BackingField")==c.owner.get();
}
// Checked state of the owned rows: the item data, and the rendered views
// while the sub-menu is open (a native "all headers" toggle changes them).
void RefreshChoices(){
    for(auto& c:choices){
        if(!CurrentChoice(c))continue;
        bool on=Small(c.panel);api.Call(c.item.get(),"set_isOn",{&on});
        Ref root(Root(c.menu.get()));if(!root.get()||!api.Call(root.get(),"get_panel"))continue;
        Ref views(api.Call(root.get(),"GetChildItemsInMenu"));if(!views.get())continue;
        int n=api.Value<int>(api.Call(views.get(),"get_Count"));if(n<0||n>256)continue;
        for(int i=0;i<n;++i){Ref view(api.Call(views.get(),"get_Item",{&i}));if(view.get()&&api.Call(view.get(),"get_itemData")==c.item.get())api.Call(view.get(),"set_isOn",{&on});}
    }
}
Obj SubMenuHook(Obj self,Method method){
    Obj menu=originalSubMenu(self,method);
    try{
        if(!Ready()||!self||!menu)return menu;
        Ref service(module->FindService("frm"));if(!service.get()||api.Field(service.get(),"<dzqi>k__BackingField")!=self)return menu;
        DropChoices();api.Invoke(appendSeparator,menu);
        for(size_t i=0;i<std::size(HeaderPanels);++i){
            Ref rows(api.Call(menu,"get_menuItems"));int index=api.Value<int>(api.Call(rows.get(),"get_Count"));if(index<0||index>256)throw std::runtime_error("Header menu bounds");
            Ref title(api.string_new(HeaderPanels[i].title)),help(api.string_new("Réduire ou agrandir le titre de ce panneau du mod.")),empty(api.string_new(""));
            bool on=Small(i),disabled=false,close=false;unsigned delay=500;int icon=HeaderPanels[i].icon;
            api.Invoke(appendCheckbox,menu,{title.get(),nullptr,&on,&disabled,&close,help.get(),&delay,&icon,empty.get()});
            Ref item(api.Call(rows.get(),"get_Item",{&index}));Ref callback(api.Call(item.get(),"get_m_callback"));
            Choice c;c.panel=i;c.menu.reset(menu);c.item=std::move(item);c.callback=std::move(callback);c.cached.reset(module->cached.get());c.compositor.reset(module->compositor.get());c.owner.reset(self);c.service.reset(service.get());
            choices.push_back(std::move(c));
        }
    }catch(const std::exception& error){Log(std::string("TOOL_HEADER_MENU_ERROR ")+error.what());}
    return menu;
}
void AllHeadersHook(Obj self,bool small,Method method){
    originalAll(self,small,method);
    try{
        if(!Ready())return;
        Ref service(module->FindService("frm"));if(!service.get()||api.Field(service.get(),"<dzqi>k__BackingField")!=self)return;
        Settings next=module->settings;bool changed=false;
        for(const auto& panel:HeaderPanels)if(next.*panel.small!=small){next.*panel.small=small;changed=true;}
        if(changed)module->Change(next);
        for(size_t i=0;i<std::size(HeaderPanels);++i)ApplySize(PanelWindow(i),small);
        RefreshChoices();
    }catch(const std::exception& error){Log(std::string("TOOL_HEADER_ALL_ERROR ")+error.what());}
}
bool Callback(Obj callback,Obj view){
    for(auto& e:entries)if(callback&&callback==e.callback.get()){
        try{if(!e.armed||!Current(e)||!view||!api.Call(view,"get_panel")||api.Call(view,"get_itemData")!=e.item.get()||api.Value<bool>(api.Call(e.item.get(),"get_disabled"))||!Present(e.menu.get(),e.item.get()))return true;
            Ref root(Root(e.menu.get()));if(!root.get()||!api.Call(root.get(),"get_panel"))return true;Close(e);if(e.visible())e.toggle();
        }catch(const std::exception& error){Log(std::string("TOOL_PANEL_HIDE_ERROR ")+error.what());}return true;
    }
    for(auto& e:entries)for(size_t layout=0;layout<e.layoutRows.size();++layout)if(callback&&callback==e.layoutRows[layout].callback.get()){
        try{const auto& row=e.layoutRows[layout];
            if(!e.armed||!Current(e)||!view||!api.Call(view,"get_panel")||api.Call(view,"get_itemData")!=row.item.get()||api.Value<bool>(api.Call(row.item.get(),"get_disabled"))||!Present(e.menu.get(),row.item.get()))return true;
            Ref root(Root(e.menu.get()));if(!root.get()||!api.Call(root.get(),"get_panel"))return true;Close(e);
            // The current layout's row only keeps its mark (native radio): nothing to apply.
            if(static_cast<int>(layout)!=e.layouts.current())e.layouts.choose(static_cast<int>(layout));
        }catch(const std::exception& error){Log(std::string("TOOL_PANEL_LAYOUT_ERROR ")+error.what());}return true;
    }
    for(auto& c:choices)if(callback&&callback==c.callback.get()){
        try{
            if(!CurrentChoice(c)||!view||!api.Call(view,"get_panel")||api.Call(view,"get_itemData")!=c.item.get()||api.Value<bool>(api.Call(c.item.get(),"get_disabled"))||!Present(c.menu.get(),c.item.get()))return true;
            Settings next=module->settings;bool small=!Small(c.panel);next.*HeaderPanels[c.panel].small=small;module->Change(next);
            ApplySize(PanelWindow(c.panel),small);
            api.Call(c.item.get(),"set_isOn",{&small});api.Call(view,"set_isOn",{&small});
        }catch(const std::exception& error){Log(std::string("TOOL_HEADER_TOGGLE_ERROR ")+error.what());}return true;
    }
    return false;
}
}
void Register(const char* key,const char* title,int icon,bool(*visible)(),bool(*toggle)(),Layouts layouts,bool(*button)()){
    if(!visible||!toggle)throw std::runtime_error("Missing tool panel callbacks");
    if(!layouts.choices.empty()&&(!layouts.current||!layouts.choose))throw std::runtime_error("Missing tool panel layout callbacks");
    if(std::any_of(entries.begin(),entries.end(),[&](const Entry& e){return e.key==key;}))return;
    Entry e;e.key=key;e.title=title;e.icon=icon;e.visible=visible;e.toggle=toggle;e.layouts=std::move(layouts);e.barButton=button;entries.push_back(std::move(e));
}
bool ChooseLayout(const char* key,int layout){auto& e=Find(key);return Ready()&&layout>=0&&layout<static_cast<int>(e.layouts.choices.size())&&e.layouts.choose(layout);}
std::vector<Shortcut> Shortcuts(){
    std::vector<Shortcut> result;
    for(const auto& e:entries)if(!e.barButton||e.barButton())result.push_back({e.key,e.title,e.icon});
    return result;
}
bool Visible(const char* key){return Find(key).visible();}
bool Toggle(const char* key){return Ready()&&Find(key).toggle();}
void Install(){if(installed)return;
    auto scroll=api.Class("UnityEngine.UIElementsModule.dll","UnityEngine.UIElements","ScrollView");
    api.FindVerified(scroll,".ctor",0,0x5742BD0);api.FindVerified(scroll,"get_contentContainer",0,0x5742BE0);
    auto klass=api.Class("Core.dll",Menus,"DofusContextualMenu");
    appendTitle=api.FindVerified(klass,"AppendTitle",1,0x16D7F70);appendSeparator=api.FindVerified(klass,"AppendSeparator",0,0x16D76E0);appendItem=api.FindVerified(klass,"AppendDefaultItem",13,0x16D6C60);
    appendCheckbox=api.FindVerified(klass,"AppendCheckboxItem",9,0x16D63D0);
    appendRadio=api.FindVerified(klass,"AppendRadioButtonItem",12,0x16D72F0);
    auto extensions=api.Class("UnityEngine.UIElementsModule.dll","UnityEngine.UIElements","VisualElementExtensions");add=api.FindVerified(extensions,"AddManipulator",2,0x56755F0);remove=api.FindVerified(extensions,"RemoveManipulator",2,0x5676A90);
    auto window=api.Class("Core.dll","Core.UILogic.Components","MagneticDragWindow");
    setHeaderSize=api.FindVerified(window,"SetHeaderSize",1,0x15B27B0);
    // The native widget list ("…" in the top bar) receives no owned entries;
    // only its "Réduire les titres des modules" sub-menu lists the mod panels.
    auto manager=api.Class("Core.dll","Core.UILogic.Common","WidgetManager");
    AddHook(api.FindVerified(manager,"CreateHeaderSubMenu",0,0x17D9B00),0x17D9B00,reinterpret_cast<void*>(&SubMenuHook),reinterpret_cast<void**>(&originalSubMenu));
    AddHook(api.FindVerified(manager,"SetAllHeaderState",1,0x17DDDD0),0x17DDDD0,reinterpret_cast<void*>(&AllHeadersHook),reinterpret_cast<void**>(&originalAll));
    soul::SetAuxiliaryPanelMenus(Populate,Callback);installed=true;
}
managed::Obj ScrollBody(managed::Obj window,managed::Obj root,bool resizable){
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
    if(resizable){
        // The window's resizers set its size; the scroll view fills it.
        float grow=1;Ref style(api.Call(scroll.get(),"get_style"));
        Ref box(api.Call(style.get(),"UnityEngine.UIElements.IStyle.get_flexGrow"));api.Call(box.get(),"set_value",{&grow});
        api.Call(style.get(),"UnityEngine.UIElements.IStyle.set_flexGrow",{api.object_unbox(box.get())});
        pixels(scroll.get(),"maxHeight",height-96.f);
    } else {
        pixels(scroll.get(),"maxHeight",std::clamp(height-120.f,160.f,560.f));
        pixels(window,"maxWidth",width-32.f);
    }
    pixels(window,"left",16);pixels(window,"top",48);
    api.Call(host.get(),"Add",{scroll.get()});return api.Call(scroll.get(),"get_contentContainer");
}
void AttachHeader(const char* key,Obj window){auto& e=Find(key);if(e.window.get())DetachHeader(key);e.window.reset(window);e.header.reset(api.Call(window,"get_headerWidget"));e.cached.reset(module->cached.get());e.compositor.reset(module->compositor.get());e.service.reset(module->FindService("fpz"));if(!e.service.get())throw std::runtime_error("Panel menu service unavailable");int more=202,left=0;bool yes=true,no=false;api.Call(window,"set_headerIconRight",{&more});api.Call(e.header.get(),"set_showRightButton",{&yes});e.button.reset(api.Field(e.header.get(),"rightButton"));if(!e.button.get())throw std::runtime_error("Panel header button unavailable");e.manipulator.reset(api.New(api.Class("Core.dll",Menus,"DofusContextualMenuManipulator"),{e.service.get(),nullptr,&left,&no}));api.Invoke(add,nullptr,{e.button.get(),e.manipulator.get()});
    const auto panel=PanelIndex(key);if(panel<std::size(HeaderPanels))ApplySize(window,Small(panel));}
void DetachHeader(const char* key){auto& e=Find(key);Close(e);if(e.manipulator.get()){api.Invoke(remove,nullptr,{e.button.get(),e.manipulator.get()});e.manipulator.reset();}e.button.reset();e.header.reset();e.window.reset();e.cached.reset();e.compositor.reset();e.service.reset();}
void Tick(){try{
    if(!Ready()){Restore();return;}
    // The soul panel builds its own window; its header follows the setting
    // the frame it appears, before the first render.
    Obj soul=soul::PanelWindow();
    if(soul!=soulWindow.get()){soulWindow.reset(soul);if(soul)ApplySize(soul,Small(0));}
}catch(const std::exception& error){Log(std::string("TOOL_PANEL_REFRESH_ERROR ")+error.what());}}
void Restore(){
    DropChoices();soulWindow.reset();
    for(auto& e:entries)if(e.window.get())try{DetachHeader(e.key.c_str());}catch(const std::exception& error){Log(std::string("TOOL_HEADER_CLEANUP_ERROR ")+error.what());}
}
bool CleanupPending(){return !choices.empty()||std::any_of(entries.begin(),entries.end(),[](const Entry& e){return e.window.get()!=nullptr;});}
}
