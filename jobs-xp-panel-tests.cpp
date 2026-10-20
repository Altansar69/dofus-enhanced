#include "module.hpp"
#include "features.hpp"
#include "options-ui.hpp"
#include "jobs-xp-data.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <thread>

namespace fixture {
using namespace managed;
struct Class {std::string name;};
struct MethodData {std::string name;};
struct Object {
    Class* klass=nullptr;
    std::map<std::string,Obj> fields;
    std::map<std::string,std::array<char,16>> styles;
    std::vector<Obj> children;
    std::array<char,16> bytes{};
    std::string text,property;
    bool on=false,enabled=true,activeClass=false,allowSwitchOff=false;
    float resolvedHeight=0;
    int fontCase=0;
};
std::map<std::string,std::unique_ptr<Class>> classes;
std::vector<std::unique_ptr<Object>> objects;
std::vector<std::unique_ptr<MethodData>> methods;
std::map<GCHandle,Obj> handles;
std::map<std::string,size_t> fields{{"m_binding",0x68},{"m_jobId",0x60},{"m_jobCraftAndSkillsHandler",0x150}};
GCHandle handleId=100;
Obj service=nullptr,job=nullptr,owner=nullptr,handler=nullptr,binding=nullptr,failDetach=nullptr;
Obj recipes=nullptr,resources=nullptr,recipeTab=nullptr,resourceTab=nullptr,tabGroup=nullptr;
int forcedOff=0;
bool(*action)(Obj)=nullptr;
int nativeSwitches=0,nativeEnables=0,nativeDisables=0;
std::atomic_int calls=0,completed=0;
std::atomic_bool hold=false;
std::vector<std::string> logs;
void Check(bool v,const char* message){if(!v)throw std::runtime_error(message);}
Object& Get(Obj obj){Check(obj!=nullptr,"Null fake object");return *static_cast<Object*>(obj);}
Class* Type(const char* name){auto& c=classes[name];if(!c)c=std::make_unique<Class>(Class{name});return c.get();}
Obj Make(const char* name){auto o=std::make_unique<Object>();o->klass=Type(name);auto p=o.get();objects.push_back(std::move(o));return p;}
template<class T>Obj Box(T value){auto o=Make("Box");std::memcpy(Get(o).bytes.data(),&value,sizeof(value));return o;}
Obj String(const char* text){auto o=Make("String");Get(o).text=text;return o;}
void Add(Obj parent,Obj child){Check(!Get(child).fields["parent"],"Duplicate attachment");Get(parent).children.push_back(child);Get(child).fields["parent"]=parent;}
void Detach(Obj child){if(auto parent=Get(child).fields["parent"]){auto& v=Get(parent).children;v.erase(std::remove(v.begin(),v.end(),child),v.end());}Get(child).fields["parent"]=nullptr;}
Obj Panel(Obj o){if(Get(o).fields["panel"])return Get(o).fields["panel"];auto p=Get(o).fields["parent"];return p?Panel(p):nullptr;}
int Display(Obj o){int result=0;std::memcpy(&result,Get(o).styles["display"].data(),sizeof(result));return result;}
void Display(Obj o,int d){std::memcpy(Get(o).styles["display"].data(),&d,sizeof(d));}
size_t Size(const std::string& property){
    if(property=="color"||property.ends_with("Color"))return 16;
    if(property=="display"||property=="flexDirection"||property=="whiteSpace"||property=="flexGrow"||property=="alignSelf"||property=="alignItems"||property=="flexWrap"||property=="unityFontStyleAndWeight")return 4;
    return 8;
}
float Metric(Obj object,const char* property){float value=0;std::memcpy(&value,Get(object).styles[property].data(),4);return value;}
void Select(Obj object,bool value){
    auto& item=Get(object);auto group=item.fields["group"];
    if(item.on==value)return;
    if(group&&!value&&!Get(group).allowSwitchOff){
        const auto& peers=Get(group).children;
        const auto count=std::count_if(peers.begin(),peers.end(),[](Obj peer){return Get(peer).on;});
        if(count<=1)return; // Exact native last-selected rule, even without notification.
    }
    item.on=item.activeClass=value;
    if(group&&value)for(auto peer:Get(group).children)if(peer!=object)Select(peer,false);
}
void NativeEnable(Obj,Method){++nativeEnables;}
void NativeDisable(Obj,Method){++nativeDisables;}
void NativeSwitch(Obj,int index,Method){
    Check(index==0||index==1,"Never forward custom tab index");++nativeSwitches;
    // Native SwitchTab changes content only; selection was performed by SelectableGroup before the callback.
    Display(recipes,index==0?0:1);Display(resources,index==1?0:1);
}
void Setup(){
    api.object_get_class=[](Obj o)->Klass{return Get(o).klass;};
    api.class_is_valuetype=[](Klass k){return static_cast<Class*>(k)->name=="Box";};
    api.object_unbox=[](Obj o)->void*{return Get(o).bytes.data();};
    api.string_new=[](const char* s)->Obj{return String(s);};
    api.gchandle_new=[](Obj o,bool)->GCHandle{handles[++handleId]=o;return handleId;};
    api.gchandle_get_target=[](GCHandle h)->Obj{return handles.at(h);};
    api.gchandle_free=[](GCHandle h){Check(handles.erase(h)==1,"Invalid GC release");};
    api.class_get_field_from_name=[](Klass,const char* name)->void*{
        const auto it=fields.find(name);return it==fields.end()?nullptr:&it->second;};
    api.field_get_offset=[](void* f){return *static_cast<size_t*>(f);};
}
}
namespace managed {
Api api{};uintptr_t gameBase=0;
Klass Api::Class(const char*,const char*,const char* name)const{return fixture::Type(name);}
Method Api::Find(Klass,const char* name,int)const{
    auto m=std::make_unique<fixture::MethodData>();m->name=name;auto ptr=m.get();fixture::methods.push_back(std::move(m));return ptr;
}
Method Api::FindVerified(Klass klass,const char* name,int argc,uintptr_t)const{return Find(klass,name,argc);}
Obj Api::Field(Obj obj,const char* name)const{return fixture::Get(obj).fields.at(name);}
std::string Api::String(Obj obj)const{return fixture::Get(obj).text;}
Obj Api::New(Klass klass,std::initializer_list<void*> args)const{
    using namespace fixture;
    auto o=Make(static_cast<fixture::Class*>(klass)->name.c_str());
    if(Get(o).klass->name=="Color"){
        size_t i=0;for(auto arg:args){std::memcpy(Get(o).bytes.data()+i*4,arg,4);++i;}
    }else if(Get(o).klass->name=="Length"){
        size_t i=0;for(auto arg:args){std::memcpy(Get(o).bytes.data()+i*4,arg,4);++i;}
    }else if(Get(o).klass->name=="ListElementBasic")Get(o).fields["m_textLabel"]=Make("Label");
    return o;
}
Obj Api::Invoke(Method method,Obj self,std::initializer_list<void*> args)const{
    return Call(self,static_cast<const fixture::MethodData*>(method)->name.c_str(),args);
}
Obj Api::Call(Obj self,const char* name,std::initializer_list<void*> arguments)const{
    using namespace fixture;
    auto& o=Get(self);std::string method=name;std::vector<void*> args(arguments);
    if(method=="get_style"){auto style=Make("Style");Get(style).fields["owner"]=self;return style;}
    if(o.klass->name=="Style"){
        const auto property=method.substr(method.find_last_of('_')+1);auto& target=Get(o.fields["owner"]);
        if(method.find(".get_")!=std::string::npos){auto box=Box(0);Get(box).bytes=target.styles[property];Get(box).property=property;return box;}
        std::memcpy(target.styles[property].data(),args[0],Size(property));return nullptr;
    }
    if(method=="set_value"){std::memcpy(o.bytes.data(),args[0],Size(o.property));return nullptr;}
    if(method=="set_text"||method=="set_rightText"){o.text=Get(args[0]).text;return nullptr;}
    if(method=="get_text")return fixture::String(o.text.c_str());
    if(method=="get_isOn"||method=="get_value")return Box(o.on);
    if(method=="SetValueWithoutNotify"){Select(self,*static_cast<bool*>(args[0]));return nullptr;}
    if(method=="ForceAllSelectionsOff"){
        ++forcedOff;const bool original=o.allowSwitchOff;o.allowSwitchOff=true;
        for(auto item:o.children)Select(item,false);
        o.allowSwitchOff=original;return nullptr;
    }
    if(method=="get_group")return o.fields["group"];
    if(method=="get_fontCase")return Box(o.fontCase);
    if(method=="set_fontCase"){o.fontCase=*static_cast<int*>(args[0]);return nullptr;}
    if(method=="get_resolvedStyle")return self;
    if(method=="get_height")return Box(o.resolvedHeight);
    if(method=="get_size")return Box(1);
    if(method=="set_size"||method=="set_tooltip"||method=="set_maxLength"||method=="set_listElementStyle"||
        method=="set_isSingleLine"||method=="set_horizontalScrollerVisibility"||method=="set_verticalScrollerVisibility")return nullptr;
    if(method=="SetEnabled"){o.enabled=*static_cast<bool*>(args[0]);return nullptr;}
    if(method=="Add"){Add(self,args[0]);return nullptr;}
    if(method=="get_contentContainer")return self;
    if(method=="get_parent")return o.fields["parent"];
    if(method=="get_panel")return Panel(self);
    if(method=="RemoveFromHierarchy"){
        if(self==failDetach){failDetach=nullptr;throw std::runtime_error("Temporary detach failure");}
        Detach(self);return nullptr;
    }
    if(method=="GetRootElement")return o.fields.at("root");
    if(method.starts_with("get_")&&o.fields.contains(method.substr(4)))return o.fields.at(method.substr(4));
    if(method=="bbxi")return fixture::job;
    if(method=="obv"||method=="obx"||method=="obt")return o.fields.at(method);
    throw std::runtime_error("Unexpected fake method "+method);
}
}
Module* module=nullptr;
Module::Module(std::filesystem::path path):folder(std::move(path)){}
managed::Obj Module::FindService(const char* name)const{
    fixture::Check(std::string(name)=="fmp","Only job service used");return fixture::service;
}
void Log(const std::string& message){fixture::logs.push_back(message);}
void AddHook(managed::Method,uintptr_t rva,void*,void** original){
    if(rva==0x2AF67C0)*original=reinterpret_cast<void*>(fixture::NativeEnable);
    else if(rva==0x2AF6780)*original=reinterpret_cast<void*>(fixture::NativeDisable);
    else if(rva==0x2ABC770)*original=reinterpret_cast<void*>(fixture::NativeSwitch);
    else throw std::runtime_error("Unexpected hook");
}
namespace soul {void SetAuxiliaryPanelAction(bool(*fn)(managed::Obj)){fixture::action=fn;}}
namespace jobsxp {
Page Fetch(const Query& query,const std::atomic_bool& cancel){
    ++fixture::calls;while(fixture::hold&&!cancel.load())Sleep(1);
    Page page{12,{{query.job*100+query.skip,query.level,1,.05,"Recette",{{1,2,"Ingrédient"}}}}};
    ++fixture::completed;return page;
}
}
#include "jobs-xp-panel.cpp"

void Wait(){
    for(int i=0;i<500;++i){TickJobsXp();if(!state.request&&state.havePage)return;Sleep(2);}
    throw std::runtime_error("Results timeout");
}
int main(){
    using namespace fixture;
    try{
        Setup();Module mod({});module=&mod;mod.ready=mod.servicesReady=true;
        mod.cached.reset(Make("Cache"));mod.compositor.reset(Make("Compositor"));
        service=Make("fag");job=Make("ll");Get(job).fields["obv"]=Box(50);Get(job).fields["obx"]=Box<int64_t>(24500);Get(job).fields["obt"]=fixture::String("Paysan");
        owner=Make("JobUI");handler=Make("JobCraftAndSkillsHandler");binding=Make("Binding");
        Get(owner).fields["m_jobCraftAndSkillsHandler"]=handler;Get(handler).fields["m_binding"]=binding;Get(handler).fields["m_jobId"]=Box(28);
        auto root=Make("Root"),tabs=Make("Tabs"),content=Make("Content");Get(root).fields["panel"]=Make("Panel");
        fixture::Add(root,tabs);fixture::Add(root,content);Get(binding).fields["root"]=root;Get(binding).fields["ctr_tabs"]=tabs;Get(binding).fields["content"]=content;
        recipes=Make("Recipes");resources=Make("Resources");recipeTab=Make("DofusTab");resourceTab=Make("DofusTab");
        fixture::Add(content,recipes);fixture::Add(content,resources);fixture::Add(tabs,recipeTab);fixture::Add(tabs,resourceTab);
        Get(binding).fields["recipesUI"]=recipes;Get(binding).fields["jobSkillsUI"]=resources;
        Get(binding).fields["tab_recipe"]=recipeTab;Get(binding).fields["tab_ressources"]=resourceTab;
        tabGroup=Make("SelectableGroup");Get(tabGroup).children={recipeTab,resourceTab};
        Get(recipeTab).fields["group"]=tabGroup;Get(resourceTab).fields["group"]=tabGroup;
        Get(recipeTab).fontCase=Get(resourceTab).fontCase=1;Get(resourceTab).resolvedHeight=43;
        float baseline=-1;std::memcpy(Get(resourceTab).styles["bottom"].data(),&baseline,4);
        Select(recipeTab,true);NativeSwitch(handler,0,nullptr);
        Select(recipeTab,false);Check(Get(recipeTab).on&&Get(recipeTab).activeClass,"Fixture models native refusal to clear its final selected tab");
        InstallJobsXp();EnableHook(owner,nullptr);TickJobsXp();
        Check(state.pane.get()&&Get(tabs).children.size()==3,"Third real native tab attached");
        Check(Get(state.tab.get()).klass->name=="DofusTab","Native DofusTab class");
        Check(Get(state.tab.get()).fontCase==1&&Metric(state.tab.get(),"bottom")==-1,"Native typography and baseline copied");
        Check(Metric(state.tab.get(),"height")==43&&Metric(state.tab.get(),"minHeight")==43&&Metric(state.tab.get(),"maxHeight")==43,"XP inherits actual native theme tab height");
        Get(resourceTab).resolvedHeight=51;TickJobsXp();Check(Metric(state.tab.get(),"height")==51,"Changed native theme geometry follows on next pump");
        Select(state.tab.get(),true);TickJobsXp();Wait();
        Check(Display(recipes)==1&&Display(resources)==1&&Display(state.pane.get())==0,"XP pane owns content only while selected");
        Check(!Get(recipeTab).on&&!Get(resourceTab).on&&!Get(recipeTab).activeClass&&!Get(resourceTab).activeClass&&Get(state.tab.get()).activeClass,"Only XP is green; native group has no stale selection");
        Check(forcedOff==1&&!Get(tabGroup).allowSwitchOff,"Native force-off restores original required-selection policy");
        Check(state.query.job==28&&state.xp==24500,"Native selected job and total XP");
        Check(action(state.rows[0].get()),"Owned recipe action");Check(Get(state.details.get()).text.find("Ingrédient")!=std::string::npos,"Native ingredient simulation details");
        Select(recipeTab,true);SwitchHook(handler,0,nullptr);Check(!state.active&&Display(recipes)==0&&Get(recipeTab).on&&Get(recipeTab).activeClass&&!Get(state.tab.get()).activeClass,"Return to same original recipe index");
        Select(state.tab.get(),true);TickJobsXp();Select(resourceTab,true);SwitchHook(handler,1,nullptr);
        Check(!state.active&&Display(resources)==0&&Get(resourceTab).on&&Get(resourceTab).activeClass&&!Get(recipeTab).activeClass&&!Get(state.tab.get()).activeClass,"Return to resource tab does not restore former recipe selection");
        Select(state.tab.get(),true);TickJobsXp();Check(state.active,"XP can reopen");
        const auto oldCalls=calls.load();hold=true;state.nextRequest=0;action(state.refresh.get());
        for(int i=0;i<100&&calls==oldCalls;++i)Sleep(1);
        Get(handler).fields["m_jobId"]=Box(26);state.nextPoll=0;TickJobsXp();
        hold=false;state.nextRequest=0;Wait();
        Check(state.query.job==26&&state.page.recipes[0].id==2600,"Changed native job cancels and discards stale HTTP");
        auto staleAction=state.rows[0].get();mod.cached.reset(Make("OtherCache"));
        Check(action(staleAction),"Stale owned action consumed without native fallback");
        TickJobsXp();Check(!state.pane.get(),"Character session replacement detaches");
        EnableHook(owner,nullptr);TickJobsXp();Select(state.tab.get(),true);TickJobsXp();
        auto pane=state.pane.get();failDetach=pane;RestoreJobsXp();
        Check(JobsXpCleanupPending(),"Transient detach remains retryable");
        RestoreJobsXp();Check(!JobsXpCleanupPending()&&!Panel(pane),"Retry releases owned controls");
        Check(Get(resourceTab).on&&Display(resources)==0,"Cleanup restores original selected native tab");
        if(worker){worker->cancelled=true;for(int i=0;i<500&&!worker->done;++i)Sleep(1);worker.reset();}
        mod.cached.reset();mod.compositor.reset();module=nullptr;
        Check(handles.empty(),"No retained native references after cleanup");
        std::cout<<"PASS: native third tab, original tab returns, auto job XP, stale requests/actions and retryable cleanup\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"FAIL: "<<e.what()<<'\n';for(const auto& line:logs)std::cerr<<line<<'\n';return 1;
    }
}
