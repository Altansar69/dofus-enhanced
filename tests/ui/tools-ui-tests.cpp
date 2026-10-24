#include "ui/tools-ui.hpp"
#include "souls/soul-panel.hpp"
#include "core/module.hpp"
#include "appearance/features.hpp"
#include "ui/options-ui.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <vector>

// Run production menu orchestration against a synthetic managed tree.
// No client process, game code, network or saved user preferences are accessed.
namespace fake {
using namespace managed;
struct Type { std::string name; };
struct MethodData { Type* type; std::string name; int argc; };
struct Object {
    Type* type{};
    std::string text;
    std::map<std::string,Obj> fields;
    std::vector<Obj> items;
    std::array<unsigned char,32> bytes{};
    bool disabled=false,on=false,close=false,enabled=true,small=false;
    int icon=0,headerSizes=0;
};
std::map<std::string,std::unique_ptr<Type>> types;
std::vector<std::unique_ptr<Object>> objects;
std::vector<std::unique_ptr<MethodData>> methods;
std::map<GCHandle,Obj> handles;
std::map<std::string,uintptr_t> guards;
std::map<std::string,Obj> services;
std::vector<std::string> logs;
GCHandle nextHandle=0xAABB00000000ULL;
bool(*populate)(Obj,Obj)=nullptr;
bool(*callback)(Obj,Obj)=nullptr;
using SubMenuFn=Obj(*)(Obj,Method);
using AllFn=void(*)(Obj,bool,Method);
SubMenuFn subMenuHook=nullptr;AllFn allHeadersHook=nullptr;
int installs=0,menuRegistrations=0,closed=0,closeFailures=0,removeFailures=0,nativeSubMenus=0,nativeAllHeaders=0,changes=0;
bool notebook=false,treasure=false,refuseNotebook=false;
bool perf=false,perfButton=false; // a tool whose top-bar button follows an option
int notebookToggles=0,treasureToggles=0;
Obj soulWindow=nullptr,currentPanel=nullptr;

void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
Object& Get(Obj value){Check(value!=nullptr,"Null menu fixture object");return *static_cast<Object*>(value);}
Type* Class(const std::string& name){auto& type=types[name];if(!type)type=std::make_unique<Type>(Type{name});return type.get();}
Obj Make(const std::string& name){auto object=std::make_unique<Object>();object->type=Class(name);auto result=object.get();objects.push_back(std::move(object));return result;}
Obj String(const std::string& text){auto result=Make("String");Get(result).text=text;return result;}
template<class T>Obj Box(T value){auto result=Make("Box");std::memcpy(Get(result).bytes.data(),&value,sizeof(value));return result;}
Obj Row(Obj menu,const std::string& kind,const std::string& title){
    auto row=Make(kind);Get(row).text=title;
    Get(Get(menu).fields.at("rows")).items.push_back(row);return row;
}
Obj NewMenu(){
    auto menu=Make("DofusContextualMenu"),root=Make("MenuRoot");
    Get(menu).fields["rows"]=Make("MenuRows");Get(menu).fields["dofusContextualMenuRoot"]=root;
    Get(root).fields["menu"]=menu;Get(root).fields["panel"]=currentPanel;Get(root).fields["views"]=Make("Views");return menu;
}
Obj OriginalSubMenu(Obj,Method){++nativeSubMenus;return NewMenu();}
void OriginalAllHeaders(Obj,bool,Method){++nativeAllHeaders;}
bool NotebookVisible(){return notebook;}
bool TreasureVisible(){return treasure;}
bool ToggleNotebook(){if(refuseNotebook)return false;++notebookToggles;notebook=!notebook;return true;}
bool ToggleTreasure(){++treasureToggles;treasure=!treasure;return true;}
bool PerfVisible(){return perf;}
bool TogglePerf(){perf=!perf;return true;}
bool PerfButton(){return perfButton;}
int compactToggles=0;bool compact=false;
bool failCompact=false;
int CompactLayout(){return compact?1:0;}
bool ChooseCompact(int layout){if(failCompact)throw std::runtime_error("Injected layout failure");++compactToggles;compact=layout==1;return true;}

void Initialize(){
    api.object_get_class=[](Obj value)->Klass{return Get(value).type;};
    api.object_new=[](Klass type)->Obj{return Make(static_cast<Type*>(type)->name);};
    api.object_unbox=[](Obj value)->void*{return Get(value).bytes.data();};
    api.string_new=[](const char* text)->Obj{return String(text);};
    api.class_from_type=[](const void* type)->Klass{return const_cast<void*>(type);};
    api.method_get_param=[](Method method,uint32_t index)->const void*{
        const auto& name=static_cast<const MethodData*>(method)->name;
        Check((name=="AppendDefaultItem"&&(index==7||index==10))||(name=="AppendRadioButtonItem"&&index==4),"Only asset/color parameter metadata needed");
        return Class(index==10?"Color":"AddressableEntry");
    };
    api.gchandle_new=[](Obj value,bool)->GCHandle{handles[++nextHandle]=value;return nextHandle;};
    api.gchandle_get_target=[](GCHandle value)->Obj{return handles.at(value);};
    api.gchandle_free=[](GCHandle value){Check(handles.erase(value)==1,"Double-freed menu GC handle");};
}
Obj Invoke(Method method,Obj self,std::initializer_list<void*> arguments){
    const auto& m=*static_cast<const MethodData*>(method);std::vector<void*> args(arguments);
    Check(m.argc==static_cast<int>(args.size()),"Menu ABI argument count");
    if(m.name=="AddManipulator"){
        Check(!self&&args[0]&&args[1],"Static AddManipulator ABI");Get(args[0]).items.push_back(args[1]);return nullptr;
    }
    if(m.name=="RemoveManipulator"){
        if(removeFailures>0){--removeFailures;throw std::runtime_error("Injected remove failure");}
        auto& list=Get(args[0]).items;auto found=std::find(list.begin(),list.end(),args[1]);
        Check(found!=list.end(),"Only attached manipulator can be removed");list.erase(found);return nullptr;
    }
    auto& object=Get(self);
    if(m.name=="get_menuData")return object.fields["menu"];
    if(m.name=="get_panel")return object.fields["panel"];
    if(m.name=="get_menu")return object.fields["menu"];
    if(m.name=="get_menuItems")return object.fields.at("rows");
    if(m.name=="GetChildItemsInMenu")return object.fields.at("views");
    if(m.name=="get_Count")return Box(int(object.items.size()));
    if(m.name=="get_Item")return object.items.at(*static_cast<int*>(args[0]));
    if(m.name=="get_m_callback")return object.fields["callback"];
    if(m.name=="get_itemData")return object.fields["item"];
    if(m.name=="get_disabled")return Box(object.disabled);
    if(m.name=="get_enabledInHierarchy")return Box(object.enabled);
    if(m.name=="get_headerWidget")return object.fields.at("header");
    if(m.name=="set_headerIconRight"){Check(*static_cast<int*>(args[0])==202,"Native ellipsis icon");return nullptr;}
    if(m.name=="set_showRightButton"){Check(*static_cast<bool*>(args[0]),"Header button shown");return nullptr;}
    if(m.name=="SetHeaderSize"){Check(object.type->name=="Window"||object.type->name.starts_with("SoulWindow"),"Header size goes to a panel window");object.small=*static_cast<bool*>(args[0]);++object.headerSizes;return nullptr;}
    if(m.name=="set_isOn"){object.on=*static_cast<bool*>(args[0]);return nullptr;}
    if(m.name=="AppendTitle"){Row(self,"Title",Get(args[0]).text);return nullptr;}
    if(m.name=="AppendSeparator"){Row(self,"Separator","");return nullptr;}
    if(m.name=="AppendCheckboxItem"){
        Check(args.size()==9&&!args[1]&&!Get(args[5]).text.empty(),"Owned checkbox rows use the null native callback and explain themselves");
        auto item=Row(self,"CheckboxItem",Get(args[0]).text);auto& row=Get(item);
        row.on=*static_cast<bool*>(args[2]);row.disabled=*static_cast<bool*>(args[3]);row.close=*static_cast<bool*>(args[4]);
        row.icon=*static_cast<int*>(args[7]);row.fields["callback"]=Make("Callback");return nullptr;
    }
    if(m.name=="AppendRadioButtonItem"){
        Check(args.size()==12&&!args[1]&&*static_cast<int*>(args[3])==0&&!*static_cast<bool*>(args[5])&&!*static_cast<bool*>(args[11])&&!Get(args[8]).text.empty(),
            "Layout rows are one native radio group with the null callback, no switch-off and an explanation");
        auto item=Row(self,"RadioItem",Get(args[0]).text);auto& row=Get(item);
        row.on=*static_cast<bool*>(args[2]);row.disabled=*static_cast<bool*>(args[6]);row.close=*static_cast<bool*>(args[7]);
        row.fields["callback"]=Make("Callback");return nullptr;
    }
    if(m.name=="AppendDefaultItem"){
        Check(args.size()==13&&!args[1],"Native hide row uses owned callback identity");
        auto item=Row(self,"DefaultItem",Get(args[0]).text);auto& row=Get(item);
        row.disabled=*static_cast<bool*>(args[2]);row.close=*static_cast<bool*>(args[3]);
        row.icon=*static_cast<int*>(args[9]);row.fields["callback"]=Make("Callback");return nullptr;
    }
    if(m.name=="RemoveMenu"){
        Check(!*static_cast<bool*>(args[0]),"Owned menu closes immediately");
        if(closeFailures>0){--closeFailures;throw std::runtime_error("Injected close failure");}
        object.fields["panel"]=nullptr;++closed;return nullptr;
    }
    if(m.name=="get_contentContainer")return self;
    if(m.name=="get_resolvedStyle")return self;
    if(m.name=="get_height")return Box(800.f);
    if(m.name=="get_style")return self;
    if(m.name=="UnityEngine.UIElements.IStyle.get_maxHeight")return Box(0.f);
    if(m.name=="set_value"){std::memcpy(object.bytes.data(),args[0],8);return nullptr;}
    if(m.name=="UnityEngine.UIElements.IStyle.set_maxHeight")return nullptr;
    if(m.name=="Add"){object.items.push_back(args[0]);return nullptr;}
    throw std::runtime_error("Unexpected menu call: "+m.name);
}
}
namespace managed {
Api api{};uintptr_t gameBase=0;
Klass Api::Class(const char*,const char*,const char* name)const{return fake::Class(name);}
Method Api::Find(Klass type,const char* name,int argc)const{
    auto m=std::make_unique<fake::MethodData>(fake::MethodData{static_cast<fake::Type*>(type),name,argc});
    auto result=m.get();fake::methods.push_back(std::move(m));return result;
}
Method Api::FindVerified(Klass type,const char* name,int argc,uintptr_t rva)const{
    fake::guards[static_cast<fake::Type*>(type)->name+"."+name]=rva;return Find(type,name,argc);
}
Obj Api::Invoke(Method method,Obj self,std::initializer_list<void*> args)const{return fake::Invoke(method,self,args);}
Obj Api::Call(Obj self,const char* name,std::initializer_list<void*> args)const{return Invoke(Find(object_get_class(self),name,int(args.size())),self,args);}
Obj Api::Field(Obj self,const char* name)const{return fake::Get(self).fields[name];}
Obj Api::New(Klass type,std::initializer_list<void*> arguments)const{
    using namespace fake;std::vector<void*> args(arguments);const auto& name=static_cast<Type*>(type)->name;
    auto object=Make(name);
    if(name=="DofusContextualMenuManipulator"){
        Check(args.size()==4&&args[0]==services.at("fpz")&&!args[1]&&*static_cast<int*>(args[2])==0&&!(*static_cast<bool*>(args[3])),"Native header manipulator ABI");
    }else if(name=="Length"){
        Check(args.size()==2,"Length ABI");std::memcpy(Get(object).bytes.data(),args[0],4);std::memcpy(Get(object).bytes.data()+4,args[1],4);
    }else Check(name=="ScrollView"&&args.empty(),"Only expected native controls constructed");
    return object;
}
}
Module* module=nullptr;
Module::Module(std::filesystem::path):features(nullptr),ui(nullptr){}
managed::Obj Module::FindService(const char* name)const{return fake::services.at(name);}
void Module::Change(Settings value,bool){settings=std::move(value);++fake::changes;}
void Log(const std::string& message){fake::logs.push_back(message);}
void AddHook(managed::Method method,uintptr_t rva,void* replacement,void** original){
    const auto& m=*static_cast<const fake::MethodData*>(method);++fake::installs;
    if(rva==0x17D9B00){fake::Check(m.name=="CreateHeaderSubMenu"&&m.argc==0,"Header sub-menu hook ABI");fake::subMenuHook=reinterpret_cast<fake::SubMenuFn>(replacement);*original=reinterpret_cast<void*>(&fake::OriginalSubMenu);return;}
    if(rva==0x17DDDD0){fake::Check(m.name=="SetAllHeaderState"&&m.argc==1,"All-headers hook ABI");fake::allHeadersHook=reinterpret_cast<fake::AllFn>(replacement);*original=reinterpret_cast<void*>(&fake::OriginalAllHeaders);return;}
    throw std::runtime_error("The tool menu hooks only the header sub-menu and the all-headers toggle");
}
namespace soul {
void SetAuxiliaryPanelMenus(bool(*populate)(managed::Obj,managed::Obj),bool(*callback)(managed::Obj,managed::Obj)){
    ++fake::menuRegistrations;fake::populate=populate;fake::callback=callback;
}
managed::Obj PanelWindow(){return fake::soulWindow;}
}
namespace fake {
struct Fixture {
    Module state{std::filesystem::path{}};
    Obj owner{},panel{},window{},button{},manipulator{};
    Fixture(){
        module=&state;state.ready=true;state.servicesReady=true;
        state.cached.reset(Make("Cached"));state.compositor.reset(Make("Compositor"));
        owner=Make("WidgetManager");panel=Make("Panel");currentPanel=panel;
        services["frm"]=Make("WidgetService");Get(services["frm"]).fields["<dzqi>k__BackingField"]=owner;
        services["fpz"]=Make("ContextMenuService");
        notebook=treasure=false;notebookToggles=treasureToggles=closed=nativeSubMenus=nativeAllHeaders=changes=0;compactToggles=0;compact=false;failCompact=false;
        closeFailures=removeFailures=0;soulWindow=nullptr;logs.clear();
    }
    ~Fixture(){closeFailures=removeFailures=0;toolsUi::Restore();soulWindow=nullptr;module=nullptr;}
    Obj Menu(){return NewMenu();}
    Obj MenuRoot(Obj menu){return Get(menu).fields["dofusContextualMenuRoot"];}
    std::vector<Obj>& Rows(Obj menu){return Get(Get(menu).fields["rows"]).items;}
    Obj View(Obj item){auto view=Make("ItemView");Get(view).fields["item"]=item;Get(view).fields["panel"]=panel;return view;}
    Obj Callback(Obj item){return Get(item).fields["callback"];}
    void Header(){
        window=Make("Window");auto header=Make("Header");button=Make("Button");
        Get(window).fields["panel"]=Get(button).fields["panel"]=panel;
        Get(window).fields["header"]=header;Get(header).fields["rightButton"]=button;
        toolsUi::AttachHeader("notebook",window);Check(Get(button).items.size()==1,"Exactly one header manipulator");
        manipulator=Get(button).items.front();
    }
    Obj HeaderMenu(){
        auto menu=Menu(),event=Make("MenuEvent");Get(event).fields["menu"]=menu;
        Check(populate(manipulator,event),"Owned header population handled");return menu;
    }
    Obj SubMenu(){auto menu=subMenuHook(owner,nullptr);Check(menu!=nullptr,"The game's sub-menu is returned");return menu;}
};
void ToolButtonFollowsItsOption(){
    Fixture f;perfButton=false;perf=false;
    auto shortcuts=toolsUi::Shortcuts();
    Check(shortcuts.size()==2&&shortcuts[0].key=="notebook"&&shortcuts[1].key=="treasure","A tool whose button is off has no top-bar button");
    Check(toolsUi::Toggle("perf")&&toolsUi::Visible("perf")&&perf,"Without its button, the tool still opens");
    perfButton=true;shortcuts=toolsUi::Shortcuts();
    Check(shortcuts.size()==3&&shortcuts[2].key=="perf"&&shortcuts[2].title=="Performances"&&shortcuts[2].icon==77,"Its button on: listed after the others");
    perf=false;perfButton=false;
}
void ToolbarUsesRegisteredActions(){
    Fixture f;auto shortcuts=toolsUi::Shortcuts();
    Check(shortcuts.size()==2&&shortcuts[0].key=="notebook"&&shortcuts[0].title=="Carnet"&&shortcuts[0].icon==225,
        "Toolbar descriptors share the registered menu identity and native icon");
    Check(toolsUi::Toggle("notebook")&&toolsUi::Visible("notebook")&&notebookToggles==1,"Toolbar opens the registered panel");
    refuseNotebook=true;Check(!toolsUi::Toggle("notebook")&&toolsUi::Visible("notebook"),"Toolbar honors dirty-note close refusal");refuseNotebook=false;
    Check(toolsUi::Toggle("notebook")&&!toolsUi::Visible("notebook"),"Toolbar closes the registered panel");
    toolsUi::Tick();Check(!toolsUi::CleanupPending()&&logs.empty(),"Idle ticks own nothing and log nothing");
    f.state.stopRequested=true;Check(!toolsUi::Toggle("treasure")&&!treasure,"Stopped module rejects shortcut actions");
}
void NativeWidgetListKeepsNativeRows(){
    Fixture f;
    // The top bar "…" list itself is built only by the game; the registered
    // panels stay reachable through their toolbar buttons and header menus.
    Check(installs==2&&menuRegistrations==1,"Only the header sub-menu and the all-headers toggle are hooked");
    auto menu=f.Menu();toolsUi::Tick();
    Check(f.Rows(menu).empty()&&Get(f.MenuRoot(menu)).fields["panel"]==f.panel,"Native menus receive no owned entries and stay open");
}
void HeaderTitleAndHide(){
    Fixture f;notebook=true;f.Header();auto menu=f.HeaderMenu();auto rows=f.Rows(menu);
    Check(rows.size()==6&&Get(rows[0]).type->name=="Title"&&Get(rows[0]).text=="Carnet"&&Get(rows[1]).type->name=="Separator","Header menu includes title and separator");
    Check(Get(rows[3]).type->name=="Separator"&&Get(rows[4]).type->name=="RadioItem"&&Get(rows[5]).type->name=="RadioItem"&&Get(rows[4]).text=="Version complète"&&Get(rows[5]).text=="Version compacte",
        "The notebook menu lists its two layouts as radio rows after the hide row");
    Check(Get(rows[4]).on&&!Get(rows[5]).on&&Get(rows[4]).close&&Get(rows[5]).close&&!Get(rows[5]).disabled,"The current layout is marked and a choice closes the menu");
    Check(Get(rows[2]).text=="Masquer le module"&&Get(rows[2]).icon==137&&Get(rows[2]).close,"Native hide row includes eye-slash icon and closes");
    auto cb=f.Callback(rows[2]),view=f.View(rows[2]);
    auto wrong=f.View(Make("ForeignItem"));
    Check(callback(cb,wrong)&&notebook&&notebookToggles==0,"Wrong view must not hide module");
    Check(callback(cb,view)&&!notebook&&notebookToggles==1&&closed==1,"Header hide closes owned menu then hides once");
    callback(cb,view);Check(notebookToggles==1,"Old header callback cannot toggle again");
    notebook=true;menu=f.HeaderMenu();rows=f.Rows(menu);
    Check(callback(f.Callback(rows[5]),f.View(rows[4]))&&compactToggles==0&&closed==1,"A layout row's callback with another row's view does nothing");
    Check(callback(f.Callback(rows[5]),f.View(rows[5]))&&compactToggles==1&&compact&&notebookToggles==1&&closed==2,"The compact row closes the menu and applies its layout, not the hide action");
    menu=f.HeaderMenu();rows=f.Rows(menu);Check(rows.size()==6&&!Get(rows[4]).on&&Get(rows[5]).on,"The mark follows the current layout");
    Check(callback(f.Callback(rows[5]),f.View(rows[5]))&&compactToggles==1&&compact&&closed==3,"The current layout's row only closes the menu");
    failCompact=true;menu=f.HeaderMenu();rows=f.Rows(menu);
    Check(callback(f.Callback(rows[4]),f.View(rows[4]))&&compact&&compactToggles==1&&closed==4&&
        std::any_of(logs.begin(),logs.end(),[](const std::string& line){return line.starts_with("TOOL_PANEL_LAYOUT_ERROR");}),"A failing layout choice is logged and the menu still closes");
    failCompact=false;
    Check(toolsUi::ChooseLayout("notebook",0)&&compactToggles==2&&!compact,"A layout is also reachable directly");
    Check(!toolsUi::ChooseLayout("notebook",2)&&!toolsUi::ChooseLayout("notebook",-1)&&!toolsUi::ChooseLayout("treasure",0)&&compactToggles==2,"Unknown layouts are refused");
    notebook=false;menu=f.HeaderMenu();Check(f.Rows(menu).size()==3,"A hidden panel's menu has no layout rows");
    Check(!populate(Make("ForeignManipulator"),Make("Event")),"Foreign header menu preserved");
    Check(!callback(Make("ForeignCallback"),view),"Foreign callbacks remain native");
}
void StaleHeaderCallbacks(){
    for(int reason=0;reason<5;++reason){
        Fixture f;notebook=true;f.Header();auto menu=f.HeaderMenu(),item=f.Rows(menu)[2],view=f.View(item);
        if(reason==0)services["fpz"]=Make("NewContextService");
        if(reason==1)Get(f.button).enabled=false;
        if(reason==2)Get(f.window).fields["panel"]=nullptr;
        if(reason==3)Get(f.button).fields["panel"]=Make("OtherPanel");
        if(reason==4)f.state.stopRequested=true;
        Check(callback(f.Callback(item),view)&&notebookToggles==0,"Invalid header context must not hide");
    }
}
void RestoreAndFailures(){
    Fixture f;notebook=true;f.Header();auto headerMenu=f.HeaderMenu(),headerItem=f.Rows(headerMenu)[2];
    closeFailures=1;removeFailures=1;toolsUi::Restore();
    Check(toolsUi::CleanupPending()&&Get(f.button).items.size()==1,"Failed menu close retains the header for retry");
    callback(f.Callback(headerItem),f.View(headerItem));Check(notebookToggles==0,"Header menu already closed during failed detach stays disarmed");
    toolsUi::Restore();Check(toolsUi::CleanupPending()&&Get(f.button).items.size()==1,"Failed manipulator removal retains the header for retry");
    toolsUi::Restore();Check(!toolsUi::CleanupPending()&&Get(f.button).items.empty(),"Retry removes owned manipulator and GC roots");
    Check(!Get(f.MenuRoot(headerMenu)).fields["panel"],"Owned menus closed on cleanup");
}
void HeaderCloseFailure(){
    Fixture f;notebook=true;f.Header();auto menu=f.HeaderMenu(),item=f.Rows(menu)[2],cb=f.Callback(item);
    closeFailures=1;toolsUi::Restore();
    Check(toolsUi::CleanupPending(),"Failed header close retains retry ownership");
    Check(callback(cb,f.View(item))&&notebookToggles==0,"Header is disarmed even when native close fails");
    toolsUi::Restore();Check(!toolsUi::CleanupPending()&&Get(f.button).items.empty(),"Header close retry releases manipulator");
}
void HeaderMenuReplacement(){
    Fixture f;notebook=true;f.Header();auto old=f.HeaderMenu(),oldItem=f.Rows(old)[2],oldCallback=f.Callback(oldItem),oldLayout=f.Rows(old)[5];
    auto current=f.HeaderMenu();
    Check(!Get(f.MenuRoot(old)).fields["panel"]&&Get(f.MenuRoot(current)).fields["panel"],"A new header menu retires only the old owned menu");
    callback(oldCallback,f.View(oldItem));Check(notebookToggles==0,"Replaced header callbacks cannot toggle");
    callback(f.Callback(oldLayout),f.View(oldLayout));Check(compactToggles==0&&!compact,"Replaced layout rows cannot apply a layout");
    auto event=Make("MenuEvent");Get(event).fields["menu"]=current;
    Check(populate(f.manipulator,event)&&f.Rows(current).size()==6,"Repopulating the same menu must not duplicate rows (hide row and layout rows once each)");
}
void StopClosesHeaderMenus(){
    Fixture f;notebook=true;f.Header();auto menu=f.HeaderMenu(),item=f.Rows(menu)[2];
    f.state.stopRequested=true;toolsUi::Tick();
    callback(f.Callback(item),f.View(item));
    Check(!toolsUi::CleanupPending()&&!Get(f.MenuRoot(menu)).fields["panel"]&&Get(f.button).items.empty()&&notebookToggles==0,"Stop closes owned menus, releases the header and discards stale actions");
}
// "Réduire les titres des modules" sub-menu of the native widget manager.
void HeaderSubMenuListsPanels(){
    Fixture f;auto menu=f.SubMenu();auto rows=f.Rows(menu);
    Check(nativeSubMenus==1&&rows.size()==5&&Get(rows[0]).type->name=="Separator","The game's rows come first, then a separator and the mod panels");
    const char* titles[]={"Pierre d\xE2\x80\x99\xC3\xA2me","Chasse au tr\xC3\xA9sor","Carnet personnel","Statistiques"};const int icons[]={128,317,225,77};
    for(int i=0;i<4;++i){
        const auto& row=Get(rows[i+1]);
        Check(row.type->name=="CheckboxItem"&&row.text==titles[i]&&row.icon==icons[i]&&!row.on&&!row.close&&!row.disabled,"Each mod panel has an unchecked native checkbox row that keeps the menu open");
    }
    Check(toolsUi::CleanupPending(),"Owned rows are tracked until the menu goes away");
    auto foreign=subMenuHook(Make("OtherManager"),nullptr);Check(f.Rows(foreign).empty()&&nativeSubMenus==2,"A foreign widget manager gets no rows");
    f.state.stopRequested=true;auto stopped=subMenuHook(f.owner,nullptr);Check(f.Rows(stopped).empty()&&nativeSubMenus==3,"A stopping module adds nothing but the game's menu still opens");
}
void HeaderToggleAppliesAndPersists(){
    Fixture f;notebook=true;f.Header();
    Check(Get(f.window).headerSizes==1&&!Get(f.window).small,"Attaching a header applies the saved (full) size once");
    auto menu=f.SubMenu();auto row=f.Rows(menu)[3],view=f.View(row);
    Check(callback(f.Callback(row),view)&&f.state.settings.smallHeaderNotebook&&changes==1,"Clicking the notebook row reduces its header in the settings");
    Check(Get(f.window).small&&Get(f.window).headerSizes==2&&Get(row).on&&Get(view).on,"The open window shrinks and the row shows the new state");
    Check(callback(f.Callback(row),view)&&!f.state.settings.smallHeaderNotebook&&!Get(f.window).small&&!Get(row).on&&changes==2,"A second click restores the header");
    auto soulRow=f.Rows(menu)[1];soulWindow=Make("SoulWindow");toolsUi::Tick();
    Check(Get(soulWindow).headerSizes==1&&!Get(soulWindow).small,"The soul window receives its size the frame it appears");
    Check(callback(f.Callback(soulRow),f.View(soulRow))&&f.state.settings.smallHeaderSoul&&Get(soulWindow).small,"The soul row toggles the soul panel's header");
    toolsUi::Tick();Check(Get(soulWindow).headerSizes==2,"Idle ticks do not reapply the size");
    soulWindow=nullptr;toolsUi::Tick();soulWindow=Make("SoulWindow2");toolsUi::Tick();
    Check(Get(soulWindow).small&&Get(soulWindow).headerSizes==1,"A new soul window applies the saved reduced size");
    auto treasureRow=f.Rows(menu)[2];
    Check(callback(f.Callback(treasureRow),f.View(treasureRow))&&f.state.settings.smallHeaderTreasure&&changes==4,"The treasure row persists even while its panel is closed");
    Check(!callback(Make("ForeignCallback"),view),"Foreign callbacks remain native");
}
void SavedSizeAppliesOnAttach(){
    Fixture f;notebook=true;f.state.settings.smallHeaderNotebook=true;f.Header();
    Check(Get(f.window).small&&Get(f.window).headerSizes==1,"A saved reduced header is applied when the panel window attaches");
}
void AllHeadersCoverPanels(){
    Fixture f;notebook=true;f.Header();soulWindow=Make("SoulWindow");toolsUi::Tick();
    auto menu=f.SubMenu();auto rows=f.Rows(menu);
    auto views=Get(f.MenuRoot(menu)).fields["views"];for(int i=1;i<5;++i)Get(views).items.push_back(f.View(rows[i]));
    allHeadersHook(f.owner,true,nullptr);
    Check(nativeAllHeaders==1&&f.state.settings.smallHeaderSoul&&f.state.settings.smallHeaderTreasure&&f.state.settings.smallHeaderNotebook&&f.state.settings.smallHeaderStats&&changes==1,"Reducing all native headers also reduces the mod panels and saves once");
    Check(Get(f.window).small&&Get(soulWindow).small,"Open mod windows shrink with the native ones");
    for(int i=1;i<5;++i)Check(Get(rows[i]).on&&Get(Get(views).items[i-1]).on,"Owned rows and their rendered views refresh");
    allHeadersHook(f.owner,true,nullptr);Check(changes==1&&nativeAllHeaders==2,"An unchanged state saves nothing");
    allHeadersHook(f.owner,false,nullptr);Check(!f.state.settings.smallHeaderNotebook&&!f.state.settings.smallHeaderStats&&!Get(f.window).small&&!Get(rows[3]).on&&changes==2,"Restoring all headers restores the mod panels");
    allHeadersHook(Make("OtherManager"),true,nullptr);Check(!f.state.settings.smallHeaderNotebook&&nativeAllHeaders==4,"A foreign manager only runs the game's own code");
}
void StaleHeaderChoices(){
    for(int reason=0;reason<5;++reason){
        Fixture f;notebook=true;f.Header();auto menu=f.SubMenu();auto row=f.Rows(menu)[3],view=f.View(row);
        if(reason==0)f.state.cached.reset(Make("OtherCache"));
        if(reason==1)Get(services["frm"]).fields["<dzqi>k__BackingField"]=Make("OtherManager");
        if(reason==2)Get(row).disabled=true;
        if(reason==3){auto& rows=f.Rows(menu);rows.erase(rows.begin()+3);}
        if(reason==4)f.state.stopRequested=true;
        Check(callback(f.Callback(row),view)&&!f.state.settings.smallHeaderNotebook&&changes==0&&Get(f.window).headerSizes==1,"Invalid sub-menu context must not change the setting");
    }
}
void RestoreClosesSubMenu(){
    Fixture f;auto menu=f.SubMenu();
    Check(toolsUi::CleanupPending(),"Sub-menu rows keep cleanup pending");
    toolsUi::Restore();Check(!Get(f.MenuRoot(menu)).fields["panel"]&&closed==1&&!toolsUi::CleanupPending(),"Restore closes the owned sub-menu and releases its rows");
    auto next=f.SubMenu();Get(f.MenuRoot(next)).fields["panel"]=nullptr;
    auto third=f.SubMenu();Check(closed==1&&f.Rows(third).size()==5&&f.Rows(next).size()==5,"Rebuilding for a newer menu drops rows of a menu the game already closed");
    f.state.stopRequested=true;toolsUi::Tick();Check(closed==2&&!toolsUi::CleanupPending(),"Stop closes the open sub-menu");
}
}
int main(){
    try{
        fake::Initialize();
        toolsUi::Register("notebook","Carnet",225,fake::NotebookVisible,fake::ToggleNotebook,
            {{{"Version complète","Grande fenêtre."},{"Version compacte","Fenêtre étroite."}},fake::CompactLayout,fake::ChooseCompact});
        toolsUi::Register("treasure","Chasse au trésor",317,fake::TreasureVisible,fake::ToggleTreasure);
        toolsUi::Register("notebook","Ignored duplicate",999,fake::NotebookVisible,fake::ToggleNotebook);
        // A tool whose top-bar button follows an option: no button while it is off,
        // the panel itself still opens (options, menus).
        toolsUi::Register("perf","Performances",77,fake::PerfVisible,fake::TogglePerf,{},fake::PerfButton);
        bool refused=false;
        try{toolsUi::Register("broken","Sans choix",1,fake::NotebookVisible,fake::ToggleNotebook,{{{"Seule","Une disposition."}}});}catch(const std::runtime_error&){refused=true;}
        fake::Check(refused,"Layouts need their current and choose callbacks");
        toolsUi::Install();toolsUi::Install();
        fake::Check(fake::installs==2&&fake::menuRegistrations==1&&fake::subMenuHook&&fake::allHeadersHook,"Install registers header menus once and hooks the native header sub-menu");
        fake::Check(fake::guards.at("MagneticDragWindow.SetHeaderSize")==0x15B27B0&&fake::guards.at("WidgetManager.CreateHeaderSubMenu")==0x17D9B00&&fake::guards.at("WidgetManager.SetAllHeaderState")==0x17DDDD0&&fake::guards.at("DofusContextualMenu.AppendCheckboxItem")==0x16D63D0&&fake::guards.at("DofusContextualMenu.AppendRadioButtonItem")==0x16D72F0,"Native header entry points are pinned");
        for(auto test:{fake::ToolbarUsesRegisteredActions,fake::ToolButtonFollowsItsOption,fake::NativeWidgetListKeepsNativeRows,fake::HeaderTitleAndHide,
                fake::StaleHeaderCallbacks,fake::RestoreAndFailures,fake::HeaderCloseFailure,
                fake::HeaderMenuReplacement,fake::StopClosesHeaderMenus,fake::HeaderSubMenuListsPanels,fake::HeaderToggleAppliesAndPersists,
                fake::SavedSizeAppliesOnAttach,fake::AllHeadersCoverPanels,fake::StaleHeaderChoices,fake::RestoreClosesSubMenu})test();
        fake::Check(fake::handles.empty(),"All menu handles released");
        std::cout<<"ToolsUiTests: native panel header menus and header-size sub-menu passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';for(const auto& l:fake::logs)std::cerr<<l<<'\n';return 1;}
}
