#include "breeding-crossing-panel.hpp"
#include "breeding-crossing-data.hpp"
#include "soul-panel.hpp"
#include "module.hpp"
#include "features.hpp"
#include "options-ui.hpp"
#include <array>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <algorithm>

namespace fake {
using namespace managed;
struct Type{std::string name;};
struct MethodData{std::string name;int argc;uintptr_t rva=0;};
struct Object{
    Type* type=nullptr;std::map<std::string,Obj> fields;
    std::vector<Obj> children;std::array<std::byte,32> bytes{};
    std::string text;bool enabled=true;
};
std::map<std::string,std::unique_ptr<Type>> types;
std::vector<std::unique_ptr<Object>> objects;
std::vector<std::unique_ptr<MethodData>> methods;
std::map<GCHandle,Obj> handles;
GCHandle next=0x2340000000ULL;
using Life=void(*)(Obj,Method);
Life enable=nullptr,disable=nullptr;
bool(*action)(Obj)=nullptr;
int enables=0,disables=0;
Obj failing=nullptr;
void Check(bool yes,const char* text){if(!yes)throw std::runtime_error(text);}
Object& Get(Obj o){Check(o!=nullptr,"Unexpected null fake object");return *static_cast<Object*>(o);}
Type* Class(const std::string& name){auto& t=types[name];if(!t)t=std::make_unique<Type>(name);return t.get();}
Obj Make(const std::string& name){auto o=std::make_unique<Object>();o->type=Class(name);auto p=o.get();objects.push_back(std::move(o));return p;}
Obj String(const std::string& text){auto o=Make("String");Get(o).text=text;return o;}
template<class T>Obj Box(T value){auto o=Make("Box");std::memcpy(Get(o).bytes.data(),&value,sizeof(value));return o;}
Obj Panel(Obj o){for(int i=0;o&&i<100;++i){if(Get(o).fields["panel"])return Get(o).fields["panel"];o=Get(o).fields["parent"];}return nullptr;}
void Detach(Obj o){if(auto p=Get(o).fields["parent"]){auto& c=Get(p).children;c.erase(std::remove(c.begin(),c.end(),o),c.end());Get(o).fields["parent"]=nullptr;}}
Obj Find(Obj o,const std::string& text){if(Get(o).text==text)return o;for(auto c:Get(o).children)if(auto r=Find(c,text))return r;return nullptr;}
Obj Starts(Obj o,const std::string& text){if(Get(o).text.starts_with(text))return o;for(auto c:Get(o).children)if(auto r=Starts(c,text))return r;return nullptr;}
void OriginalEnable(Obj,Method){++enables;}void OriginalDisable(Obj,Method){++disables;}
void Initialize(){
    api.object_get_class=[](Obj o)->Klass{return Get(o).type;};
    api.class_is_valuetype=[](Klass k){auto n=static_cast<Type*>(k)->name;return n=="Box"||n=="Length"||n=="Color";};
    api.object_unbox=[](Obj o)->void*{return Get(o).type->name=="BreedingUiBinding"?o:Get(o).bytes.data();};
    api.string_new=[](const char* s)->Obj{return String(s);};
    api.gchandle_new=[](Obj o,bool)->GCHandle{auto h=++next;handles[h]=o;return h;};
    api.gchandle_get_target=[](GCHandle h)->Obj{return handles.at(h);};
    api.gchandle_free=[](GCHandle h){Check(handles.erase(h)==1,"Double handle release");};
}
}
namespace managed {
Api api{};uintptr_t gameBase=0;
Klass Api::Class(const char*,const char*,const char* n)const{return fake::Class(n);}
Method Api::Find(Klass,const char* n,int count)const{auto m=std::make_unique<fake::MethodData>(n,count);auto p=m.get();fake::methods.push_back(std::move(m));return p;}
Method Api::FindVerified(Klass k,const char* n,int count,uintptr_t rva)const{
    const std::map<std::string,std::pair<int,uintptr_t>> expected={
        {"blqu",{0,0x36BE9D0}},{".ctor",{1,0x151B3E0}},
        {"get_ctr_tabs",{0,0x2E78AA0}},{"get_ctr_window",{0,0x2E78C20}}};
    fake::Check(expected.contains(n)&&expected.at(n)==std::pair{count,rva},"Verified native breeding ABI");
    auto method=Find(k,n,count);const_cast<fake::MethodData*>(static_cast<const fake::MethodData*>(method))->rva=rva;return method;
}
Obj Api::Call(Obj o,const char* n,std::initializer_list<void*> a)const{return Invoke(Find(object_get_class(o),n,static_cast<int>(a.size())),o,a);}
Obj Api::Invoke(Method method,Obj o,std::initializer_list<void*> args)const{
    using namespace fake;
    auto& m=*static_cast<const MethodData*>(method);std::vector<void*> a(args);auto& value=Get(o);
    Check(a.size()==static_cast<size_t>(m.argc),"Call argument count");
    if(m.name=="blqu"){Check(m.rva==0x36BE9D0,"Root getter must be verified");return value.fields["root"];}
    if(m.name=="get_ctr_tabs"||m.name=="get_ctr_window")return Get(value.fields["root"]).fields[m.name=="get_ctr_tabs"?"tabs":"content"];
    if(m.name=="get_panel")return Panel(o);
    if(m.name=="get_enabledInHierarchy")return Box(value.enabled);
    if(m.name=="SetEnabled"){value.enabled=*static_cast<bool*>(a[0]);return nullptr;}
    if(m.name=="set_text"){value.text=Get(a[0]).text;return nullptr;}
    if(m.name=="set_tooltip"||m.name=="set_name"){value.fields[m.name]=a[0];return nullptr;}
    if(m.name=="set_listElementStyle"){Check(*static_cast<int*>(a[0])==1,"Native list style");return nullptr;}
    if(m.name=="Add"){Detach(a[0]);value.children.push_back(a[0]);Get(a[0]).fields["parent"]=o;return nullptr;}
    if(m.name=="RemoveFromHierarchy"){
        if(failing==o){failing=nullptr;throw std::runtime_error("Detach transient failure");}
        Detach(o);return nullptr;
    }
    if(m.name=="get_style"){auto s=Make("Style");Get(s).fields["owner"]=o;return s;}
    if(value.type->name=="Style"){
        const std::string prefix="UnityEngine.UIElements.IStyle.";
        Check(m.name.starts_with(prefix),"Explicit style interface");
        auto op=m.name.substr(prefix.size());
        if(op.starts_with("get_"))return Make("Box");
        Check(op.starts_with("set_"),"Style setter");return nullptr;
    }
    if(value.type->name=="Box"&&m.name=="set_value")return nullptr;
    throw std::runtime_error("Unexpected method "+m.name);
}
Obj Api::New(Klass k,std::initializer_list<void*> args)const{
    auto name=static_cast<fake::Type*>(k)->name;auto o=fake::Make(name);
    if(name=="Length"){fake::Check(args.size()==2,"Length ABI");std::memcpy(fake::Get(o).bytes.data(),*args.begin(),4);}
    else if(name=="Color")fake::Check(args.size()==4,"Color ABI");
    else if(name=="BreedingUiBinding"){fake::Check(args.size()==1,"Binding ctor ABI");fake::Get(o).fields["root"]=*args.begin();}
    else fake::Check((name=="VisualElement"||name=="Label"||name=="ListElementBasic")&&args.size()==0,"Native UI constructor");
    return o;
}
}
Module* module=nullptr;
Module::Module(std::filesystem::path path):folder(std::move(path)){}
void Log(const std::string&){}
namespace soul{void SetAuxiliaryPanelAction(bool(*handler)(managed::Obj)){fake::action=handler;}}
void AddHook(managed::Method method,uintptr_t rva,void* replacement,void** original){
    auto& m=*static_cast<const fake::MethodData*>(method);
    fake::Check(m.argc==0,"Lifecycle ABI");
    if(rva==0x2DF50C0){fake::Check(m.name=="OnEnable","Enable method");fake::enable=reinterpret_cast<fake::Life>(replacement);*original=reinterpret_cast<void*>(&fake::OriginalEnable);}
    else {fake::Check(rva==0x2DF4F70&&m.name=="OnDisable","Disable method");fake::disable=reinterpret_cast<fake::Life>(replacement);*original=reinterpret_cast<void*>(&fake::OriginalDisable);}
}
int main(){
    using namespace fake;
    auto folder=std::filesystem::temp_directory_path()/(L"dofus-crossing-ui-tests-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try{
        Initialize();std::filesystem::create_directory(folder);
        Module owner{folder};module=&owner;owner.settingsPath=folder/"settings.ini";
        owner.ready=owner.servicesReady=true;owner.cached.reset(Make("Cache"));owner.compositor.reset(Make("Compositor"));
        InstallBreedingCrossing();Check(enable&&disable&&action,"Installed native lifecycle and actions");
        auto breeding=Make("BreedingUi"),root=Make("VisualElement");Get(breeding).fields["root"]=root;
        enable(breeding,nullptr);TickBreedingCrossing();Check(Get(root).children.empty(),"Wait for actual native panel attachment");
        auto tabs=Make("VisualElement"),content=Make("VisualElement");
        Get(root).fields["tabs"]=tabs;Get(root).fields["content"]=content;
        Get(tabs).fields["parent"]=root;Get(content).fields["parent"]=root;Get(root).children={tabs,content};
        Get(root).fields["panel"]=Make("Panel");TickBreedingCrossing();
        auto entry=Find(root,"Croisements");Check(entry!=nullptr,"Entry belongs to actual breeding root");
        TickBreedingCrossing();Check(Get(tabs).children.size()==1&&Get(entry).fields["parent"]==tabs,"Entry in native tabs, no duplicate");
        Check(!action(Make("ForeignAction")),"Foreign action forwarded");
        Check(action(entry),"Entry handled");
        auto first=Find(root,"☐ Dorée · G1");Check(first!=nullptr,"Native first crossing row");
        action(first);Check(breedingCrossing::Read(breedingCrossing::StoragePath(owner.settingsPath))[0],"Click persists manual checkbox");
        auto ten=Find(root,"10");Check(ten!=nullptr,"Generation filter");action(ten);
        Check(Find(root,"☐ Dorée et Émeraude · G10")!=nullptr,"Generation ten includes correct hybrid");
        auto nextButton=Find(root,"Suivants");action(nextButton);Check(!Find(root,"☐ Dorée et Émeraude · G10"),"Pagination advances filtered data");
        Check(Get(content).children.size()==1,"Crossings panel belongs to actual native content container");
        auto stale=first;
        disable(breeding,nullptr);Check(Get(tabs).children.empty()&&Get(content).children.empty()&&!BreedingCrossingCleanupPending(),"Native close removes only owned controls");
        Check(!action(stale),"Stale row never writes data after closing");
        enable(breeding,nullptr);TickBreedingCrossing();action(Find(root,"Croisements"));
        Check(Find(root,"☑ Dorée · G1")!=nullptr,"Reopen restores saved collection");
        owner.cached.reset(Make("NewCache"));TickBreedingCrossing();Check(Get(tabs).children.empty()&&Get(content).children.empty(),"Character/session replacement detaches old controls");
        enable(breeding,nullptr);TickBreedingCrossing();failing=Find(root,"Croisements");
        RestoreBreedingCrossing();Check(BreedingCrossingCleanupPending(),"Transient detach retained for retry");
        RestoreBreedingCrossing();Check(!BreedingCrossingCleanupPending(),"Retry releases native roots");
        Check(enables==3&&disables==1,"Original lifecycle preserved exactly once");
        owner.cached.reset();owner.compositor.reset();module=nullptr;Check(handles.empty(),"No native GC roots leaked");
        auto resolved=std::filesystem::weakly_canonical(folder);
        Check(resolved.parent_path()==std::filesystem::weakly_canonical(std::filesystem::temp_directory_path()),"Test cleanup boundary");
        std::filesystem::remove_all(resolved);
        std::cout<<"PASS: native breeding child, lifecycle, filters, persistence, stale actions and cleanup retry\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
