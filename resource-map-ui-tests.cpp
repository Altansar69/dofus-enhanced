#include "resource-map-ui.hpp"
#include "resource-map-data.hpp"
#include "module.hpp"
#include "features.hpp"
#include "options-ui.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <unordered_map>
#include <vector>

// Production UI orchestration against an offline managed tree. This fixture
// never opens a client, drives input, changes preferences or loads game code.
namespace fake {
using namespace managed;
struct Rect {float x,y,width,height;};
struct Type {std::string name;};
struct MethodData {Type* type;std::string name;int argc;};
struct Object {
    Type* type{};std::string text;
    std::map<std::string,Obj> fields;
    std::map<std::string,float> styles;
    std::vector<Obj> children;
    std::array<unsigned char,64> data{};
    Rect bound{0,0,1000,800};
    bool owned=false,value=false,enabled=true,open=false;
    int size=0,notifications=0;
};
std::map<std::string,std::unique_ptr<Type>> types;
std::vector<std::unique_ptr<Object>> objects;
std::vector<std::unique_ptr<MethodData>> methods;
std::unordered_map<GCHandle,Obj> handles;
std::map<std::string,uintptr_t> guards;
std::vector<std::string> logs;
GCHandle nextHandle=0xABCD00000000ULL;
Obj character{},cartography{};
int nativeEnable=0,nativeDisable=0,changes=0,nativeStyleWrites=0,moveFailureCountdown=-1,removeFailures=0;
bool rejectChange=false;
using LifeFn=void(*)(Obj,Method);
LifeFn enableHook{},disableHook{};
void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
Type* Class(const std::string& name){auto& type=types[name];if(!type)type=std::make_unique<Type>(Type{name});return type.get();}
Object& Get(Obj value){Check(value!=nullptr,"Null fixture object");return *static_cast<Object*>(value);}
Obj Make(const std::string& name,bool owned=false){auto value=std::make_unique<Object>();value->type=Class(name);value->owned=owned;auto result=value.get();objects.push_back(std::move(value));return result;}
template<class T>Obj Box(T value,const char* name="Box"){auto result=Make(name);static_assert(sizeof(T)<=64);std::memcpy(Get(result).data.data(),&value,sizeof(value));return result;}
Obj String(const char* text){auto result=Make("String");Get(result).text=text;return result;}
void Detach(Obj child){auto parent=Get(child).fields["parent"];if(parent){auto& children=Get(parent).children;children.erase(std::remove(children.begin(),children.end(),child),children.end());}Get(child).fields["parent"]=nullptr;}
void Insert(Obj parent,int index,Obj child){
    if(Get(child).type->name=="NativeRow"&&Get(parent).owned){
        if(moveFailureCountdown==0){moveFailureCountdown=-1;throw std::runtime_error("injected move failure");}
        if(moveFailureCountdown>0)--moveFailureCountdown;
    }
    Detach(child);auto& children=Get(parent).children;Check(index>=0&&index<=static_cast<int>(children.size()),"Insert bounds");
    children.insert(children.begin()+index,child);Get(child).fields["parent"]=parent;
}
void Add(Obj parent,Obj child){Detach(child);Insert(parent,static_cast<int>(Get(parent).children.size()),child);}
Obj Panel(Obj value){while(value){if(Get(value).fields["panel"])return Get(value).fields["panel"];value=Get(value).fields["parent"];}return nullptr;}
void OriginalEnable(Obj owner,Method){++nativeEnable;Get(owner).enabled=true;}
void OriginalDisable(Obj owner,Method){++nativeDisable;Get(owner).enabled=false;}
void Init(){
    api.object_get_class=[](Obj value)->Klass{return Get(value).type;};
    api.class_is_valuetype=[](Klass type){const auto& name=static_cast<Type*>(type)->name;return name=="Box"||name=="Length"||name=="StyleValue"||name=="Hierarchy";};
    api.object_unbox=[](Obj value)->void*{return Get(value).data.data();};
    api.object_new=[](Klass type)->Obj{return Make(static_cast<Type*>(type)->name,true);};
    api.string_new=[](const char* text)->Obj{return String(text);};
    api.gchandle_new=[](Obj value,bool)->GCHandle{const auto handle=++nextHandle;handles.emplace(handle,value);return handle;};
    api.gchandle_get_target=[](GCHandle handle)->Obj{return handles.at(handle);};
    api.gchandle_free=[](GCHandle handle){Check(handles.erase(handle)==1,"GC reference double free");};
}
Obj Invoke(Method method,Obj self,std::initializer_list<void*> arguments){
    const auto& m=*static_cast<const MethodData*>(method);const std::vector<void*> args(arguments);
    Check(static_cast<int>(args.size())==m.argc,"Managed argument count mismatch");
    auto& object=Get(self);
    if(m.name=="get_ctr_filters")return object.fields["filters"];
    if(m.name==".ctor"){
        if(m.type->name=="CheckboxCustom"||m.type->name=="DofusLabel"){Check(args.empty(),"Checkbox/label constructor ABI");return nullptr;}
        if(m.type->name=="SectionHeader"){
            Check(args.size()==5&&*static_cast<bool*>(args[0])&&*static_cast<bool*>(args[1]),"Native header must have content and folder button");
            for(int i=2;i<5;++i)Check(!*static_cast<bool*>(args[i]),"No foreign icon/data/action buttons");
            auto content=Make("VisualElement",true);Add(self,content);object.fields["content"]=content;return nullptr;
        }
        if(m.type->name=="ScrollView"){
            Check(args.size()==1&&*static_cast<int*>(args[0])==0,"Vertical ScrollView constructor");
            auto content=Make("VisualElement",true);Add(self,content);object.fields["content"]=content;return nullptr;
        }
        throw std::runtime_error("Unexpected native constructor");
    }
    if(m.name=="get_contentContainer")return object.fields["content"];
    if(m.name=="get_panel")return Panel(self);
    if(m.name=="get_parent")return object.fields["parent"];
    if(m.name=="get_worldBound")return Box(object.bound);
    if(m.name=="get_value")return Box(object.value);
    if(m.name=="SetValueWithoutNotify"){object.value=*static_cast<bool*>(args[0]);return nullptr;}
    if(m.name=="set_rightText"||m.name=="set_title"||m.name=="set_text"){object.text=Get(args[0]).text;return nullptr;}
    if(m.name=="set_isSingleLine"){Check(!*static_cast<bool*>(args[0]),"Source note can wrap");return nullptr;}
    if(m.name=="set_tooltip"){object.fields["tooltip"]=args[0];return nullptr;}
    if(m.name=="set_farRightText"){object.fields["level"]=args[0];return nullptr;}
    if(m.name=="set_size"){object.size=*static_cast<int*>(args[0]);return nullptr;}
    if(m.name=="set_canBeOpened"){object.enabled=*static_cast<bool*>(args[0]);return nullptr;}
    if(m.name=="set_isOpen"){object.open=*static_cast<bool*>(args[0]);return nullptr;}
    if(m.name=="set_horizontalScrollerVisibility"){Check(*static_cast<int*>(args[0])==2,"Horizontal scroller hidden");return nullptr;}
    if(m.name=="set_verticalScrollerVisibility"){Check(*static_cast<int*>(args[0])==0,"Vertical scroller automatic");return nullptr;}
    if(m.name=="AddSubContent"){Add(object.fields["content"],args[0]);return nullptr;}
    if(m.name=="Add"){Add(self,args[0]);return nullptr;}
    if(m.name=="Insert"){Insert(self,*static_cast<int*>(args[0]),args[1]);return nullptr;}
    if(m.name=="RemoveFromHierarchy"){
        Check(object.owned,"Only wrapper removed");if(removeFailures>0){--removeFailures;throw std::runtime_error("injected remove failure");}Detach(self);return nullptr;
    }
    if(m.name=="get_hierarchy"){auto value=Make("Hierarchy");Get(value).fields["owner"]=self;return value;}
    if(m.name=="get_childCount")return Box(static_cast<int>(Get(object.fields["owner"]).children.size()));
    if(m.name=="get_Item")return Get(object.fields["owner"]).children.at(*static_cast<int*>(args[0]));
    if(m.name=="get_style")return self;
    if(m.name.starts_with("UnityEngine.UIElements.IStyle.get_"))return Make("StyleValue");
    if(m.name=="set_value"){std::memcpy(object.data.data(),args[0],8);return nullptr;}
    if(m.name.starts_with("UnityEngine.UIElements.IStyle.set_")){
        if(!object.owned)++nativeStyleWrites;
        const auto name=m.name.substr(std::string("UnityEngine.UIElements.IStyle.set_").size());
        object.styles[name]=*static_cast<float*>(args[0]);return nullptr;
    }
    throw std::runtime_error("Unexpected managed operation "+m.type->name+"."+m.name);
}
}
namespace managed {
Api api{};uintptr_t gameBase=0;
Klass Api::Class(const char*,const char*,const char* name)const{return fake::Class(name);}
Method Api::Find(Klass type,const char* name,int argc)const{auto method=std::make_unique<fake::MethodData>(fake::MethodData{static_cast<fake::Type*>(type),name,argc});auto result=method.get();fake::methods.push_back(std::move(method));return result;}
Method Api::FindVerified(Klass type,const char* name,int argc,uintptr_t rva)const{fake::guards[static_cast<fake::Type*>(type)->name+"."+name]=rva;return Find(type,name,argc);}
Obj Api::Invoke(Method method,Obj self,std::initializer_list<void*> args)const{return fake::Invoke(method,self,args);}
Obj Api::Call(Obj self,const char* name,std::initializer_list<void*> args)const{return Invoke(Find(object_get_class(self),name,static_cast<int>(args.size())),self,args);}
Obj Api::Field(Obj self,const char* name)const{return fake::Get(self).fields[name];}
void* Api::FieldAddress(Obj self,const char* name)const{if(std::string(name)=="dtzx")return &fake::Get(self).enabled;if(std::string(name)=="dzyi")return fake::Get(self).fields.at(name);throw std::runtime_error("Unexpected field address");}
Obj Api::New(Klass type,std::initializer_list<void*> args)const{
    fake::Check(static_cast<fake::Type*>(type)->name=="Length"&&args.size()==2,"Only native Length constructor");
    const std::vector<void*> values(args);struct Length{float value;int unit;};return fake::Box(Length{*static_cast<float*>(values[0]),*static_cast<int*>(values[1])},"Length");
}
}
namespace resourceMap {
constexpr Resource TestCatalog[]{{311,1,1,1,"Eau"},{303,2,1,2,"Frêne"},{441,24,1,3,"Fer"},
    {421,26,1,4,"Ortie"},{289,28,1,5,"Blé"},{1782,36,1,6,"Goujon"}};
std::span<const Resource> Catalog(){return TestCatalog;}
}
Module* module=nullptr;
Module::Module(std::filesystem::path):features(nullptr),ui(nullptr){}
managed::Obj Module::FindService(const char* name)const{if(std::string(name)=="fng")return fake::character;if(std::string(name)=="fkc")return fake::cartography;throw std::runtime_error("Unexpected resource service");}
void Module::Change(Settings value,bool){++fake::changes;if(fake::rejectChange)throw std::runtime_error("injected preference failure");settings=std::move(value);}
void Log(const std::string& text){fake::logs.push_back(text);}
void AddHook(managed::Method method,uintptr_t rva,void* replacement,void** original){
    const auto& info=*static_cast<const fake::MethodData*>(method);fake::guards["fhi."+info.name]=rva;
    if(info.name=="OnEnable"){fake::Check(rva==0x1BAEA20,"Enable RVA");fake::enableHook=reinterpret_cast<fake::LifeFn>(replacement);*original=reinterpret_cast<void*>(fake::OriginalEnable);}
    else if(info.name=="OnDisable"){fake::Check(rva==0x1BAE560,"Disable RVA");fake::disableHook=reinterpret_cast<fake::LifeFn>(replacement);*original=reinterpret_cast<void*>(fake::OriginalDisable);}
    else throw std::runtime_error("Unexpected hook");
}
namespace fake {
struct Fixture {
    Module state{std::filesystem::path{}};
    Obj owner{},root{},panel{},filters{};
    std::vector<Obj> originals;
    Fixture(){
        module=&state;state.ready=true;state.servicesReady=true;state.compositor.reset(Make("Compositor"));
        character=Make("Character");cartography=Make("Cartography");
        owner=Make("fhi");root=Make("VisualElement");panel=Make("Panel");filters=Make("VisualElement");
        Get(root).fields["panel"]=panel;Get(filters).bound={0,272,140,468};Add(root,filters);
        Get(owner).fields["dtzy"]=character;Get(owner).fields["dtzz"]=cartography;Get(owner).fields["dzyj"]=root;
        const auto binding=Make("Binding");Get(binding).fields["filters"]=filters;Get(owner).fields["dzyi"]=binding;
        for(int i=0;i<14;++i){auto child=Make("NativeRow");Get(child).notifications=i+10;originals.push_back(child);Add(filters,child);}
        nativeEnable=nativeDisable=changes=nativeStyleWrites=removeFailures=0;moveFailureCountdown=-1;rejectChange=false;logs.clear();
        resourceMap::InstallUi();
    }
    ~Fixture(){moveFailureCountdown=-1;removeFailures=0;resourceMap::RestoreUi();module=nullptr;}
    void Open(){enableHook(owner,nullptr);resourceMap::TickUi();}
    Obj Scroll(){for(auto child:Get(filters).children)if(Get(child).type->name=="ScrollView")return child;return nullptr;}
    Obj Content(){return Get(Scroll()).fields.at("content");}
    Obj Resources(){return Get(Content()).children.front();}
    Obj Master(){return Get(Get(Resources()).fields.at("content")).children.front();}
    Obj Row(const char* text){for(const auto& object:objects)if(object->owned&&object->type->name=="CheckboxCustom"&&object->text==text&&Panel(object.get())==panel)return object.get();throw std::runtime_error("Missing resource checkbox");}
    void Restored(){Check(Get(filters).children==originals,"Original native children and order must be restored");for(int i=0;i<14;++i)Check(Get(originals[i]).notifications==i+10,"Native handlers/state changed");Check(nativeStyleWrites==0,"Native filter styles were modified");}
};
void NativeTreeAndDuplicateOpen(){
    Fixture f;f.Open();auto scroll=f.Scroll();Check(scroll,"Native scroll wrapper missing");
    Check(Get(f.Content()).children.size()==15,"Original14 native rows plus owned resource section");
    Check(std::equal(f.originals.begin(),f.originals.end(),Get(f.Content()).children.begin()+1),"Native controls must keep identity/order");
    Check(Get(scroll).styles["height"]==516&&Get(scroll).styles["width"]==280,"Bounded sidebar dimensions");
    const auto& categories=Get(Get(f.Resources()).fields.at("content")).children;Check(categories.size()==8,"Master, source note and six harvest job categories");
    Check(Get(categories[1]).text=="Relevés statiques · couverture partielle\nDisponibilité en jeu inconnue"&&Get(Get(categories[1]).fields.at("tooltip")).text.find("Dofus Map")!=std::string::npos,"Static provenance, partial coverage and unknown live availability are visible");
    Check(Get(categories[2]).text=="Base"&&Get(categories[3]).text=="Bûcheron"&&Get(categories[7]).text=="Pêcheur","Exact job labels");
    for(int i=2;i<8;++i)Check(!Get(categories[i]).open&&Get(categories[i]).enabled,"Native accordion initially collapsed");
    Check(Get(f.Row("Eau")).size==2,"Rows match large native checkbox size");
    f.Open();Check(f.Scroll()==scroll&&nativeEnable==2,"Duplicate native OnEnable must not duplicate controls");
    Check(guards.at("SectionHeader..ctor")==0x1745F00&&guards.at("ScrollView..ctor")==0x5741C70&&guards.at("CheckboxCustom.SetValueWithoutNotify")==0x17044F0,"Verified exact constructor/value contracts");
    disableHook(f.owner,nullptr);Check(nativeDisable==1,"Native OnDisable preserved");f.Restored();
}
void SelectionAndExternalChanges(){
    Fixture f;f.state.settings.resourceMapItems={303};f.Open();Check(Get(f.Row("Frêne")).value,"Persisted selection restored");
    Get(f.Row("Eau")).value=true;Get(f.Row("Fer")).value=true;resourceMap::TickUi();
    Check(changes==1&&f.state.settings.resourceMapItems==std::vector<int>({303,311,441}),"Multiple checked resources saved once sorted");
    Get(f.Master()).value=false;resourceMap::TickUi();Check(changes==2&&!f.state.settings.resourceMapEnabled&&f.state.settings.resourceMapItems.size()==3,"Disable preserves selection");
    f.state.settings.resourceMapItems={289};f.state.settings.resourceMapEnabled=true;resourceMap::TickUi();
    Check(changes==2&&Get(f.Row("Blé")).value&&!Get(f.Row("Eau")).value&&Get(f.Master()).value,"External reset wins without feedback save");
    Get(f.Row("Fer")).value=true;rejectChange=true;resourceMap::TickUi();Check(f.state.settings.resourceMapItems==std::vector<int>{289},"Failed save must not mutate preferences");f.Restored();
}
void UnknownIdsArePreserved(){
    Fixture f;f.state.settings.resourceMapItems={999999};f.Open();Check(changes==0,"Opening must not remove unknown persisted resource IDs");
    Get(f.Row("Eau")).value=true;resourceMap::TickUi();Check(f.state.settings.resourceMapItems==std::vector<int>({311,999999}),"Selection retains IDs from another compatible catalog");
}
void OwnerAndPanelLifetime(){
    Fixture f;f.Open();Get(f.Row("Eau")).value=true;f.state.compositor.reset(Make("NewCompositor"));resourceMap::TickUi();
    Check(changes==0,"Stale session UI must never commit");f.Restored();
    f.Open();Get(f.root).fields["panel"]=Make("ReplacementPanel");resourceMap::TickUi();f.Restored();
    Get(f.root).fields["panel"]=f.panel;f.Open();f.state.stopRequested=true;resourceMap::TickUi();f.Restored();
    Check(changes==0,"Stop must not apply pending UI changes");
}
void PartialMoveAndRetryableCleanup(){
    Fixture f;
    // Fail during the fourth native reparent, after Add already removed it.
    enableHook(f.owner,nullptr);moveFailureCountdown=3;resourceMap::TickUi();moveFailureCountdown=-1;
    if(f.Scroll())resourceMap::RestoreUi();f.Restored();
    f.Open();removeFailures=1;disableHook(f.owner,nullptr);Check(nativeDisable==1,"Cleanup failure cannot swallow native lifecycle");
    Check(resourceMap::UiCleanupPending(),"Transient cleanup keeps references for stop drain");
    resourceMap::TickUi();Check(!resourceMap::UiCleanupPending(),"Successful retry releases pending cleanup");f.Restored();
}
void ForeignChildrenAndNativeReparent(){
    Fixture f;f.Open();auto outside=Make("VisualElement");Add(f.root,outside);Add(outside,f.originals[3]);
    auto foreign=Make("ForeignRow");Add(f.Content(),foreign);auto sibling=Make("OtherExtension");Add(f.filters,sibling);
    resourceMap::RestoreUi();Check(Get(f.originals[3]).fields["parent"]==outside,"Restore must not steal independently reparented native row");
    Check(Get(foreign).fields["parent"]==f.filters&&Get(sibling).fields["parent"]==f.filters,"Foreign additions retained");
    Check(Get(f.filters).children.size()==15,"Only owned wrapper/resources removed");
}
void DeferredLayoutAndResize(){
    Fixture f;Get(f.root).bound.height=0;f.Open();Check(!f.Scroll(),"No wrapper before finite layout");
    Get(f.root).bound.height=900;resourceMap::TickUi();Check(f.Scroll()&&Get(f.Scroll()).styles["height"]==616,"Pending map attaches after layout");
    Get(f.root).bound.height=700;resourceMap::TickUi();Check(Get(f.Scroll()).styles["height"]==416,"Resize respects actual remaining map height");
    Get(f.owner).fields["dtzz"]=Make("StaleService");resourceMap::TickUi();f.Restored();
}
}
int main(){
    try{
        fake::Init();
        fake::NativeTreeAndDuplicateOpen();std::cout<<"native lifecycle passed\n";
        fake::SelectionAndExternalChanges();std::cout<<"selection passed\n";
        fake::UnknownIdsArePreserved();std::cout<<"catalog migration passed\n";
        fake::OwnerAndPanelLifetime();std::cout<<"owner lifetime passed\n";
        fake::PartialMoveAndRetryableCleanup();std::cout<<"partial mutation recovery passed\n";
        fake::ForeignChildrenAndNativeReparent();std::cout<<"foreign ownership passed\n";
        fake::DeferredLayoutAndResize();std::cout<<"deferred layout passed\n";
        fake::Check(fake::handles.empty(),"Managed references leaked after fixture destruction");
        std::cout<<"Resource map UI production orchestration tests passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
