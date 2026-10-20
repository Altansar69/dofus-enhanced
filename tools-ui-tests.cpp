#include "tools-ui.hpp"
#include "soul-panel.hpp"
#include "module.hpp"
#include "features.hpp"
#include "options-ui.hpp"
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
    bool disabled=false,on=false,close=false,enabled=true;
    int icon=0;
};
std::map<std::string,std::unique_ptr<Type>> types;
std::vector<std::unique_ptr<Object>> objects;
std::vector<std::unique_ptr<MethodData>> methods;
std::map<GCHandle,Obj> handles;
std::map<std::string,uintptr_t> guards;
std::map<std::string,Obj> services;
std::vector<std::string> logs;
GCHandle nextHandle=0xAABB00000000ULL;
using BuildFn=void(*)(Obj,Obj,Method);
BuildFn build=nullptr;
bool(*populate)(Obj,Obj)=nullptr;
bool(*callback)(Obj,Obj)=nullptr;
int installs=0,menuRegistrations=0,nativeBuilds=0,closed=0,closeFailures=0,removeFailures=0;
bool notebook=false,treasure=false;
int notebookToggles=0,treasureToggles=0;

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
void OriginalBuild(Obj,Obj menu,Method){++nativeBuilds;Row(menu,"NativeItem","Interface native");}
bool NotebookVisible(){return notebook;}
bool TreasureVisible(){return treasure;}
bool ToggleNotebook(){++notebookToggles;notebook=!notebook;return true;}
bool ToggleTreasure(){++treasureToggles;treasure=!treasure;return true;}

void Initialize(){
    api.object_get_class=[](Obj value)->Klass{return Get(value).type;};
    api.object_new=[](Klass type)->Obj{return Make(static_cast<Type*>(type)->name);};
    api.object_unbox=[](Obj value)->void*{return Get(value).bytes.data();};
    api.string_new=[](const char* text)->Obj{return String(text);};
    api.class_from_type=[](const void* type)->Klass{return const_cast<void*>(type);};
    api.method_get_param=[](Method method,uint32_t index)->const void*{
        Check(static_cast<const MethodData*>(method)->name=="AppendDefaultItem"&&(index==7||index==10),"Only asset/color parameter metadata needed");
        return Class(index==7?"AddressableEntry":"Color");
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
    if(m.name=="get_Count")return Box(int(object.items.size()));
    if(m.name=="get_Item")return object.items.at(*static_cast<int*>(args[0]));
    if(m.name=="get_m_callback")return object.fields["callback"];
    if(m.name=="get_itemData")return object.fields["item"];
    if(m.name=="get_disabled")return Box(object.disabled);
    if(m.name=="get_enabledInHierarchy")return Box(object.enabled);
    if(m.name=="set_isOn"){object.on=*static_cast<bool*>(args[0]);return nullptr;}
    if(m.name=="get_headerWidget")return object.fields.at("header");
    if(m.name=="set_headerIconRight"){Check(*static_cast<int*>(args[0])==202,"Native ellipsis icon");return nullptr;}
    if(m.name=="set_showRightButton"){Check(*static_cast<bool*>(args[0]),"Header button shown");return nullptr;}
    if(m.name=="AppendTitle"){Row(self,"Title",Get(args[0]).text);return nullptr;}
    if(m.name=="AppendSeparator"){Row(self,"Separator","");return nullptr;}
    if(m.name=="AppendCheckboxItem"){
        Check(args.size()==9&&!args[1],"Native checkbox uses owned callback identity");
        auto item=Row(self,"CheckboxItem",Get(args[0]).text);auto& row=Get(item);
        row.on=*static_cast<bool*>(args[2]);row.disabled=*static_cast<bool*>(args[3]);
        row.close=*static_cast<bool*>(args[4]);row.icon=*static_cast<int*>(args[7]);
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
void Log(const std::string& message){fake::logs.push_back(message);}
void AddHook(managed::Method method,uintptr_t rva,void* replacement,void** original){
    fake::Check(static_cast<const fake::MethodData*>(method)->name=="AddPresetContextualMenuItems"&&rva==0x17D83F0,"Exact native widget menu hook");
    ++fake::installs;fake::build=reinterpret_cast<fake::BuildFn>(replacement);*original=reinterpret_cast<void*>(fake::OriginalBuild);
}
namespace soul {
void SetAuxiliaryPanelMenus(bool(*populate)(managed::Obj,managed::Obj),bool(*callback)(managed::Obj,managed::Obj)){
    ++fake::menuRegistrations;fake::populate=populate;fake::callback=callback;
}
}
namespace fake {
struct Fixture {
    Module state{std::filesystem::path{}};
    Obj owner{},panel{},window{},button{},manipulator{};
    Fixture(){
        module=&state;state.ready=true;state.servicesReady=true;
        state.cached.reset(Make("Cached"));state.compositor.reset(Make("Compositor"));
        owner=Make("WidgetManager");panel=Make("Panel");
        services["frm"]=Make("WidgetService");Get(services["frm"]).fields["<dzqi>k__BackingField"]=owner;
        services["fpz"]=Make("ContextMenuService");
        notebook=treasure=false;notebookToggles=treasureToggles=nativeBuilds=closed=0;
        closeFailures=removeFailures=0;logs.clear();
    }
    ~Fixture(){closeFailures=removeFailures=0;toolsUi::Restore();module=nullptr;}
    Obj Menu(){
        auto menu=Make("DofusContextualMenu"),root=Make("MenuRoot");
        Get(menu).fields["rows"]=Make("MenuRows");Get(menu).fields["dofusContextualMenuRoot"]=root;
        Get(root).fields["menu"]=menu;Get(root).fields["panel"]=panel;return menu;
    }
    Obj MenuRoot(Obj menu){return Get(menu).fields["dofusContextualMenuRoot"];}
    std::vector<Obj>& Rows(Obj menu){return Get(Get(menu).fields["rows"]).items;}
    Obj View(Obj item){auto view=Make("ItemView");Get(view).fields["item"]=item;Get(view).fields["panel"]=panel;return view;}
    Obj Callback(Obj item){return Get(item).fields["callback"];}
    Obj ShowModules(){auto menu=Menu();build(owner,menu,nullptr);return menu;}
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
};
void ModulesPreserveNativeAndStayOpen(){
    Fixture f;auto menu=f.ShowModules();auto rows=f.Rows(menu);
    Check(nativeBuilds==1&&rows.size()==4&&Get(rows[0]).type->name=="NativeItem"&&Get(rows[1]).type->name=="Separator","Native module choices preserved before owned additions");
    Check(Get(rows[2]).text=="Carnet"&&Get(rows[3]).text=="Chasse au trésor"&&Get(rows[2]).icon==225&&Get(rows[3]).icon==317,"Named native module choices with correct icons");
    for(size_t i=2;i<4;++i)Check(!Get(rows[i]).disabled&&!Get(rows[i]).close,"Display checkboxes stay open");
    auto cb=f.Callback(rows[2]),view=f.View(rows[2]);
    Check(callback(cb,view)&&notebook&&Get(rows[2]).on&&closed==0,"First checkbox click shows panel without closing menu");
    Check(callback(cb,view)&&!notebook&&!Get(rows[2]).on&&closed==0,"Second click hides panel without closing menu");
    treasure=true;toolsUi::Tick();Check(Get(rows[3]).on,"External visibility changes synchronize menu checkbox");
    Check(!callback(Make("ForeignCallback"),view),"Foreign callbacks remain native");
}
void HeaderTitleAndHide(){
    Fixture f;notebook=true;f.Header();auto menu=f.HeaderMenu();auto rows=f.Rows(menu);
    Check(rows.size()==3&&Get(rows[0]).type->name=="Title"&&Get(rows[0]).text=="Carnet"&&Get(rows[1]).type->name=="Separator","Header menu includes title and separator");
    Check(Get(rows[2]).text=="Masquer le module"&&Get(rows[2]).icon==137&&Get(rows[2]).close,"Native hide row includes eye-slash icon and closes");
    auto cb=f.Callback(rows[2]),view=f.View(rows[2]);
    auto wrong=f.View(Make("ForeignItem"));
    Check(callback(cb,wrong)&&notebook&&notebookToggles==0,"Wrong view must not hide module");
    Check(callback(cb,view)&&!notebook&&notebookToggles==1&&closed==1,"Header hide closes owned menu then hides once");
    callback(cb,view);Check(notebookToggles==1,"Old header callback cannot toggle again");
    Check(!populate(Make("ForeignManipulator"),Make("Event")),"Foreign header menu preserved");
}
void StaleModuleCallbacks(){
    for(int reason=0;reason<7;++reason){
        Fixture f;auto menu=f.ShowModules(),item=f.Rows(menu)[2],cb=f.Callback(item),view=f.View(item);
        if(reason==0)f.state.cached.reset(Make("NewCached"));
        if(reason==1)f.state.compositor.reset(Make("NewCompositor"));
        if(reason==2)Get(services["frm"]).fields["<dzqi>k__BackingField"]=Make("OtherManager");
        if(reason==3)Get(view).fields["item"]=Make("OtherItem");
        if(reason==4)Get(item).disabled=true;
        if(reason==5)f.Rows(menu).erase(f.Rows(menu).begin()+2);
        if(reason==6)Get(f.MenuRoot(menu)).fields["menu"]=Make("ReusedMenu");
        Check(callback(cb,view)&&notebookToggles==0,"Stale or invalid module row must not toggle");
    }
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
    auto menu=f.ShowModules(),item=f.Rows(menu)[2],view=f.View(item),cb=f.Callback(item);
    closeFailures=1;removeFailures=1;toolsUi::Restore();
    Check(toolsUi::CleanupPending(),"Failed menu close or manipulator removal retains state for retry");
    Check(callback(cb,view)&&notebookToggles==0,"Retiring menu callback is disarmed before close");
    callback(f.Callback(headerItem),f.View(headerItem));Check(notebookToggles==0,"Header menu already closed during failed detach stays disarmed");
    toolsUi::Restore();Check(!toolsUi::CleanupPending()&&Get(f.button).items.empty(),"Retry removes owned manipulator and GC roots");
    Check(!Get(f.MenuRoot(menu)).fields["panel"]&&!Get(f.MenuRoot(headerMenu)).fields["panel"],"Owned menus closed on cleanup");
}
void HeaderCloseFailure(){
    Fixture f;notebook=true;f.Header();auto menu=f.HeaderMenu(),item=f.Rows(menu)[2],cb=f.Callback(item);
    closeFailures=1;toolsUi::Restore();
    Check(toolsUi::CleanupPending(),"Failed header close retains retry ownership");
    Check(callback(cb,f.View(item))&&notebookToggles==0,"Header is disarmed even when native close fails");
    toolsUi::Restore();Check(!toolsUi::CleanupPending()&&Get(f.button).items.empty(),"Header close retry releases manipulator");
}
void MenuReplacementAndReentry(){
    Fixture f;auto old=f.ShowModules(),oldItem=f.Rows(old)[2],oldCallback=f.Callback(oldItem);
    auto current=f.ShowModules();
    Check(!Get(f.MenuRoot(old)).fields["panel"]&&Get(f.MenuRoot(current)).fields["panel"],"New native menu retires only old owned menu");
    callback(oldCallback,f.View(oldItem));Check(notebookToggles==0,"Replaced menu callbacks cannot toggle");
    build(f.owner,current,nullptr);
    const auto& rows=f.Rows(current);
    Check(Get(f.MenuRoot(current)).fields["panel"],"Rebuilding the same native menu must not close it");
    const auto owned=std::count_if(rows.begin(),rows.end(),[](Obj item){return Get(item).type->name=="CheckboxItem";});
    Check(owned==2,"Repeated native build must not duplicate owned module choices");
    Check(nativeBuilds==3,"Original native builder always runs");
}
void StopAndWrongOwner(){
    Fixture f;auto foreign=f.Menu();build(Make("ForeignManager"),foreign,nullptr);
    Check(f.Rows(foreign).size()==1,"Unrelated native widget menu preserved");
    auto menu=f.ShowModules(),item=f.Rows(menu)[2];f.state.stopRequested=true;toolsUi::Tick();
    callback(f.Callback(item),f.View(item));
    Check(!toolsUi::CleanupPending()&&!Get(f.MenuRoot(menu)).fields["panel"]&&notebookToggles==0,"Stop closes owned menus and discards stale actions");
}
}
int main(){
    try{
        fake::Initialize();
        toolsUi::Register("notebook","Carnet",225,fake::NotebookVisible,fake::ToggleNotebook);
        toolsUi::Register("treasure","Chasse au trésor",317,fake::TreasureVisible,fake::ToggleTreasure);
        toolsUi::Register("notebook","Ignored duplicate",999,fake::NotebookVisible,fake::ToggleNotebook);
        toolsUi::Install();toolsUi::Install();
        fake::Check(fake::installs==1&&fake::menuRegistrations==1,"Install and registration are idempotent");
        for(auto test:{fake::ModulesPreserveNativeAndStayOpen,fake::HeaderTitleAndHide,fake::StaleModuleCallbacks,
                fake::StaleHeaderCallbacks,fake::RestoreAndFailures,fake::HeaderCloseFailure,
                fake::MenuReplacementAndReentry,fake::StopAndWrongOwner})test();
        fake::Check(fake::handles.empty(),"All menu handles released");
        std::cout<<"ToolsUiTests: native module menu and header lifecycle passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
