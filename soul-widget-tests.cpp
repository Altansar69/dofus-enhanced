#include "soul-widget.hpp"
#include "module.hpp"
#include "features.hpp"
#include "options-ui.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <map>
#include <memory>
namespace fake {
using namespace managed;
struct Object{std::string type,text;std::map<std::string,Obj> fields;std::map<std::string,int> ints;std::map<std::string,float> dimensions;std::vector<Obj> children;int scalar=0;float floatValue=1;std::array<Obj,19> bindingValue{};};
struct MethodInfo{std::string name;};
std::map<std::string,std::unique_ptr<std::string>> types;std::vector<std::unique_ptr<Object>> objects;std::vector<std::unique_ptr<MethodInfo>> methods;
std::map<GCHandle,Obj> handles;GCHandle next=1;Obj service{},nativeToggle{},menuToggle{},nativeItem{},tipService{},root{},bindingBox{},lastTooltip{};int changes=0,foreign=0,tooltipAdds=0,tooltipRemoves=0;
bool failRemoveTooltip=false;std::vector<std::string> logs;
using ToggleFn=void(*)(Obj,Method);ToggleFn toggle=nullptr;
void Check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
Object& Get(Obj o){Check(o!=nullptr,"Null fake widget");return *static_cast<Object*>(o);}
Obj Make(std::string t){auto o=std::make_unique<Object>();o->type=t;auto p=o.get();objects.push_back(std::move(o));return p;}
Obj Box(int v){auto o=Make("Box");Get(o).scalar=v;return o;}
void Native(Obj,Method){++foreign;}
void Init(){api.class_is_assignable_from=[](Klass base,Klass actual)->bool{return base==actual;};
    api.class_get_name=[](Klass k)->const char*{return static_cast<std::string*>(k)->c_str();};api.object_get_class=[](Obj o)->Klass{return api.Class("","",Get(o).type.c_str());};api.object_unbox=[](Obj o)->void*{return Get(o).type=="WidgetManagerBinding"?static_cast<void*>(Get(o).bindingValue.data()):(Get(o).type=="StyleBox"||Get(o).type=="Length")?static_cast<void*>(&Get(o).floatValue):&Get(o).scalar;};
    api.object_new=[](Klass k)->Obj{return Make(*static_cast<std::string*>(k));};
    api.string_new=[](const char* s)->Obj{auto o=Make("String");Get(o).text=s;return o;};
    api.gchandle_new=[](Obj o,bool)->GCHandle{handles[++next]=o;return next;};api.gchandle_get_target=[](GCHandle h)->Obj{return handles.at(h);};api.gchandle_free=[](GCHandle h){Check(handles.erase(h)==1,"GC release");};}
Obj Call(Obj self,const std::string& name,std::initializer_list<void*> arguments){std::vector<void*> a(arguments);auto& o=Get(self);
    if(name=="get_parent")return o.fields["parent"];
    if(name=="get_panel")return o.fields["parent"]?root:nullptr;
    if(name=="get_enabledInHierarchy")return Box(!o.ints["disabled"]);
    if(name=="get_toggleSize")return Box(o.ints["set_toggleSize"]);
    if(name=="get_style"||name=="get_resolvedStyle")return self;
    if(name=="get_width"||name=="get_height"){auto v=Make("StyleBox");Get(v).floatValue=o.dimensions.at(name.substr(4));return v;}
    const std::string styleGet="UnityEngine.UIElements.IStyle.get_",styleSet="UnityEngine.UIElements.IStyle.set_";
    if(name.starts_with(styleGet)&&!name.ends_with("flexShrink")){auto v=Make("StyleBox");Get(v).floatValue=o.dimensions[name.substr(styleGet.size())];return v;}
    if(name.starts_with(styleSet)&&!name.ends_with("flexShrink")){o.dimensions[name.substr(styleSet.size())]=*static_cast<float*>(a[0]);return nullptr;}
    if(name=="UnityEngine.UIElements.IStyle.get_flexShrink"){auto box=Make("StyleBox");Get(box).floatValue=o.floatValue;return box;}
    if(name=="UnityEngine.UIElements.IStyle.set_flexShrink"){o.floatValue=*static_cast<float*>(a[0]);return nullptr;}
    if(name=="set_value"){o.floatValue=*static_cast<float*>(a[0]);return nullptr;}
    if(name=="SetValueWithoutNotify"){o.ints["isOn"]=*static_cast<bool*>(a[0]);return nullptr;}
    if(name=="set_name"||name=="set_tooltip"){o.fields[name]=a[0];return nullptr;}
    if(name==".ctor"){
        Check(o.type=="TooltipManipulator"&&a.size()==6&&a[0]==tipService,"Real native tooltip constructor with live tooltip service");
        Check(*static_cast<int*>(a[2])==1&&*static_cast<int*>(a[3])==7&&*static_cast<int*>(a[4])==1&&!*static_cast<bool*>(a[5]),"Same hover and placement arguments as native widget controls");
        o.fields["service"]=a[0];o.text=Get(a[1]).text;lastTooltip=self;return nullptr;
    }
    if(name=="HideTooltip"){o.ints["hidden"]=1;return nullptr;}
    if(name.starts_with("set_")){o.ints[name]=*static_cast<int*>(a[0]);return nullptr;}
    if(name=="Add"){o.children.push_back(a[0]);Get(a[0]).fields["parent"]=self;return nullptr;}
    if(name=="IndexOf"){auto it=std::find(o.children.begin(),o.children.end(),a[0]);return Box(it==o.children.end()?-1:static_cast<int>(it-o.children.begin()));}
    if(name=="Insert"){auto index=*static_cast<int*>(a[0]);Check(index>=0&&size_t(index)<=o.children.size(),"Valid menu anchor insertion");o.children.insert(o.children.begin()+index,a[1]);Get(a[1]).fields["parent"]=self;return nullptr;}
    if(name=="PlaceBehind"){auto p=o.fields["parent"];Check(p&&p==Get(a[0]).fields["parent"],"Reorder only siblings");auto& children=Get(p).children;std::erase(children,self);children.insert(std::find(children.begin(),children.end(),a[0]),self);return nullptr;}
    if(name=="RemoveFromHierarchy"){auto p=o.fields["parent"];if(p)std::erase(Get(p).children,self);o.fields["parent"]=nullptr;return nullptr;}
    throw std::runtime_error("Unexpected widget call "+name);
}
void Setup(){service=Make("gml");root=Make("WidgetControls");auto manager=Make("WidgetManager"),binding=Make("WidgetManagerBinding");nativeToggle=Make("DofusToggleButtonCustom");
    menuToggle=Make("DofusToggleButtonCustom");nativeItem=Make("NativeItem");tipService=Make("TooltipService");
    bindingBox=binding;Get(binding).bindingValue[9]=nativeToggle;Get(binding).bindingValue[14]=menuToggle;
    Get(nativeToggle).ints["set_toggleSize"]=0;Get(menuToggle).ints["set_toggleSize"]=2;Get(menuToggle).dimensions={{"width",36.f},{"height",36.f}};
    Get(service).fields["<dzqi>k__BackingField"]=manager;Get(manager).fields["dzyi"]=binding;
    for(auto o:{nativeToggle,menuToggle,nativeItem})Get(o).fields["parent"]=root;
    Get(root).children={nativeToggle,menuToggle,nativeItem};}
Obj Owned(){Check(Get(root).children.size()==4,"Exactly one additional native module toggle");auto b=Get(root).children[1];Check(Get(root).children[0]==nativeToggle&&Get(root).children[2]==menuToggle&&Get(root).children[3]==nativeItem,"Soul button before Menu, native siblings preserved");return b;}
}
namespace managed {
Api api{};uintptr_t gameBase=0;
Klass Api::Class(const char*,const char*,const char* n)const{auto& t=fake::types[n];if(!t)t=std::make_unique<std::string>(n);return t.get();}
Method Api::Find(Klass,const char* n,int)const{auto m=std::make_unique<fake::MethodInfo>(fake::MethodInfo{n});auto p=m.get();fake::methods.push_back(std::move(m));return p;}
Method Api::FindVerified(Klass k,const char* n,int c,uintptr_t)const{return Find(k,n,c);}
Obj Api::Invoke(Method m,Obj o,std::initializer_list<void*> a)const{
    auto name=static_cast<const fake::MethodInfo*>(m)->name;
    if(name=="get_tg_map"||name=="get_tg_modules"){
        fake::Check(o==fake::Get(fake::bindingBox).bindingValue.data(),"Widget getter requires the unboxed 152-byte binding, not its object header");
        return static_cast<Obj*>(o)[name=="get_tg_map"?9:14];
    }
    if(name=="AddManipulator"||name=="RemoveManipulator"){
        std::vector<void*> args(a);fake::Check(!o&&args.size()==2,"Static UI manipulator ABI");
        if(name=="RemoveManipulator"&&fake::failRemoveTooltip)throw std::runtime_error("Temporary tooltip cleanup failure");
        auto& tip=fake::Get(args[1]);tip.fields["target"]=name=="AddManipulator"?args[0]:nullptr;
        if(name=="AddManipulator")++fake::tooltipAdds;else ++fake::tooltipRemoves;
        return nullptr;
    }
    return fake::Call(o,name,a);
}
Obj Api::Call(Obj o,const char* n,std::initializer_list<void*> a)const{return fake::Call(o,n,a);}
Obj Api::New(Klass k,std::initializer_list<void*> args)const{
    auto o=fake::Make(*static_cast<std::string*>(k));
    if(fake::Get(o).type=="Length"){fake::Check(args.size()==2,"Native Length arguments");fake::Get(o).floatValue=*static_cast<float*>(*args.begin());}return o;
}
Obj Api::Field(Obj o,const char* n)const{return fake::Get(o).fields[n];}
}
Module* module=nullptr;
bool HandleSpousePartyWidgetToggle(managed::Obj) { return false; }
Module::Module(std::filesystem::path p):folder(p){}
managed::Obj Module::FindService(const char* n)const{if(std::string(n)=="fou")return fake::tipService;fake::Check(std::string(n)=="frm","Only widget or tooltip service expected");return fake::service;}
void Module::Change(Settings s,bool save){fake::Check(save,"Widget choice must persist");settings=s;++fake::changes;}
void Log(const std::string& m){fake::logs.push_back(m);}
void AddHook(managed::Method,uintptr_t rva,void* h,void** original){fake::Check(rva==0x1715030,"Toggle ABI");fake::toggle=reinterpret_cast<fake::ToggleFn>(h);*original=reinterpret_cast<void*>(&fake::Native);}
int main(){using namespace fake;try{Init();InstallSoulWidget();{
    Module owner{"offline"};module=&owner;owner.ready=true;owner.servicesReady=true;owner.compositor.reset(Make("Compositor"));Setup();
    TickSoulWidget();auto b=Owned();TickSoulWidget();Check(Owned()==b,"No duplicated toggle per tick");
    Check(Get(b).ints["set_mainStyle"]==2&&Get(b).ints["set_toggleSize"]==Get(menuToggle).ints["set_toggleSize"]&&Get(b).ints["isOn"]==int(owner.settings.soulReminder),"Native module size, style and persisted setting");
    Check(Get(b).dimensions["width"]==36&&Get(b).dimensions["height"]==36,"Match Menu actual36px face, not only its enum size");
    Check(Get(b).floatValue==0,"Full toolbar cannot shrink soul button into a tiny square");
    Check(tooltipAdds==1&&Get(lastTooltip).fields["target"]==b&&Get(lastTooltip).text.find("Capture d’âme")!=std::string::npos&&Get(lastTooltip).text.find("Afficher ou masquer")!=std::string::npos,"Native hover tooltip attached once with meaningful caption");
    Check(!Get(b).fields.contains("set_tooltip"),"Do not rely on unused Unity tooltip property");
    toggle(nativeToggle,nullptr);Check(foreign==1&&changes==0,"Native controls pass through");
    const bool previous=owner.settings.soulReminder;toggle(b,nullptr);Check(changes==1&&owner.settings.soulReminder!=previous&&Get(b).ints["isOn"]==int(owner.settings.soulReminder),"Only explicit owned toggle persists setting");
    owner.settings.soulReminder=previous;TickSoulWidget();Check(Get(b).ints["isOn"]==int(previous)&&changes==1,"Mod settings synchronize without event loop");
    Get(b).ints["disabled"]=1;toggle(b,nullptr);Check(changes==1,"Disabled control inert");
    auto oldRoot=root,oldTooltip=lastTooltip;Setup();Get(menuToggle).ints["set_toggleSize"]=3;Get(menuToggle).dimensions={{"width",44.f},{"height",44.f}};
    Get(bindingBox).bindingValue[9]=nullptr; // The map toggle is optional/hidden independently.
    toggle(b,nullptr);Check(changes==1,"Stale manager cannot toggle preference");TickSoulWidget();auto newer=Owned();
    Check(Get(oldRoot).children.size()==3&&newer!=b&&Get(newer).ints["set_toggleSize"]==3&&Get(newer).dimensions["width"]==44,"Replacement survives absent Map toggle, adopts actual Menu size and preserves foreign controls");
    Check(Get(oldTooltip).ints["hidden"]&&!Get(oldTooltip).fields["target"]&&tooltipRemoves==1,"Old tooltip hidden and callbacks detached");
    failRemoveTooltip=true;RestoreSoulWidget();Check(SoulWidgetCleanupPending(),"Retain owned references on tooltip cleanup failure");
    toggle(newer,nullptr);Check(changes==1,"Partially removed widget remains inert");
    failRemoveTooltip=false;RestoreSoulWidget();Check(!SoulWidgetCleanupPending()&&Get(root).children.size()==3,"Cleanup retries remove only owned widget");
    TickSoulWidget();Owned();owner.stopped=true;TickSoulWidget();Check(Get(root).children.size()==3,"Stop removes mod entry only");RestoreSoulWidget();module=nullptr;
    }Check(handles.empty(),"Widget GC refs leaked");std::cout<<"PASS: native display toggle, settings sync, callback isolation, replaced UI and cleanup\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
