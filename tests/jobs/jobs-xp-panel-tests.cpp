#include "core/module.hpp"
#include "appearance/features.hpp"
#include "ui/options-ui.hpp"
#include "jobs/jobs-xp-data.hpp"
#include <algorithm>
#include <array>
#include <climits>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <thread>

namespace fixture {
using namespace managed;
struct Class {std::string name;};
struct MethodData {std::string type,name;int argc=0;};
struct Rect {float x,y,width,height;};
struct Vector3 {float x,y,z;};
struct Object {
    Class* klass=nullptr;
    std::map<std::string,Obj> fields;
    std::map<std::string,std::array<char,16>> styles;
    std::vector<Obj> children;
    std::array<char,16> bytes{};
    std::string text,property;
    bool on=false,enabled=true,activeClass=false,allowSwitchOff=false,ownedTile=false;
    Rect layout{0,9,100,43};
    Vector3 translation{};
    int translationWrites=0;
    float resolvedHeight=0;
    std::map<std::string,float> resolvedMetrics;
    int itemId=0;
    int64_t quantity=0;
    int fontCase=0;
};
std::map<std::string,std::unique_ptr<Class>> classes;
std::vector<std::unique_ptr<Object>> objects;
std::vector<std::unique_ptr<MethodData>> methods;
std::map<GCHandle,Obj> handles;
std::map<std::string,size_t> fields{{"m_binding",0x68},{"m_jobId",0x60},{"m_jobCraftAndSkillsHandler",0x150},
    {"m_jobWindowRecipesUI",0x50},{"m_currentRecipesFilter",0x208},{"onRightClickCallback",0x3F8},{"id",0x14}};
GCHandle handleId=100;
Obj service=nullptr,job=nullptr,owner=nullptr,handler=nullptr,binding=nullptr,failDetach=nullptr;
Obj recipes=nullptr,resources=nullptr,recipeTab=nullptr,resourceTab=nullptr,tabGroup=nullptr;
Obj nativeRecipes=nullptr;
std::set<Obj> imageTargets,tooltipTargets,contextTargets;
std::vector<int> bindingItems;
int itemBindings=0,bindFailureCountdown=-1,imageCleanupFailures=0,eventCleanupFailures=0;
int unavailableItemId=0,nativeTabStyleWrites=0,customTabTopWrites=0;
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
void CheckNativeItem(Obj tile,int item,int64_t quantity){
    Check(tile&&Get(tile).ownedTile&&Get(tile).itemId==item,"Native image must represent the exact recipe or ingredient item");
    Check(Get(tile).quantity==std::min<int64_t>(quantity,INT_MAX),"Native item binder receives the complete bounded quantity");
    Check(imageTargets.contains(tile)&&tooltipTargets.contains(tile)&&contextTargets.contains(tile)&&Get(tile).fields["onRightClickCallback"],
        "Every visible native item needs its hover tooltip and native actions");
    Check(Get(Get(tile).fields["onRightClickCallback"]).itemId==item,"Native item actions must reference that exact displayed item");
}
void CheckRetiredItem(Obj tile){
    Check(!imageTargets.contains(tile)&&!tooltipTargets.contains(tile)&&!contextTargets.contains(tile)&&!Get(tile).fields["onRightClickCallback"],
        "Retired native item must have no tooltip, click or context action");
    Check(!Get(tile).fields["parent"],"Retired native item must detach from the panel tree");
}
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
    api.gc_wbarrier_set_field=[](Obj self,Obj* field,Obj value){
        Check(Get(self).ownedTile&&field==&Get(self).fields["onRightClickCallback"]&&!value,"Only owned native Tile context delegate may be cleared");
        *field=value;
    };
}
}
namespace managed {
Api api{};uintptr_t gameBase=0;
Klass Api::Class(const char*,const char*,const char* name)const{return fixture::Type(name);}
Method Api::Find(Klass klass,const char* name,int argc)const{
    auto m=std::make_unique<fixture::MethodData>();m->type=static_cast<fixture::Class*>(klass)->name;m->name=name;m->argc=argc;
    auto ptr=m.get();fixture::methods.push_back(std::move(m));return ptr;
}
Method Api::FindVerified(Klass klass,const char* name,int argc,uintptr_t)const{return Find(klass,name,argc);}
Obj Api::Field(Obj obj,const char* name)const{return fixture::Get(obj).fields.at(name);}
void* Api::FieldAddress(Obj obj,const char* name)const{
    fixture::Check(fixture::Get(obj).ownedTile&&std::string(name)=="onRightClickCallback","Only the native Tile action field may be addressed");
    return &fixture::Get(obj).fields["onRightClickCallback"];
}
std::string Api::String(Obj obj)const{return fixture::Get(obj).text;}
Obj Api::New(Klass klass,std::initializer_list<void*> args)const{
    using namespace fixture;
    auto o=Make(static_cast<fixture::Class*>(klass)->name.c_str());
    if(Get(o).klass->name=="Color"){
        size_t i=0;for(auto arg:args){std::memcpy(Get(o).bytes.data()+i*4,arg,4);++i;}
    }else if(Get(o).klass->name=="Length"){
        size_t i=0;for(auto arg:args){std::memcpy(Get(o).bytes.data()+i*4,arg,4);++i;}
    }else if(Get(o).klass->name=="ListElementBasic")Get(o).fields["m_textLabel"]=Make("Label");
    else if(Get(o).klass->name=="Tile")Get(o).ownedTile=true;
    return o;
}
Obj Api::Invoke(Method method,Obj self,std::initializer_list<void*> args)const{
    const auto& info=*static_cast<const fixture::MethodData*>(method);
    fixture::Check(info.argc==static_cast<int>(args.size()),"Managed invocation argument count");
    using namespace fixture;
    const std::vector<void*> values(args);
    if(info.type=="mc"&&info.name=="ohc"){
        Check(!self&&values.size()==5&&*static_cast<int16_t*>(values[1])==63&&
            *static_cast<uint32_t*>(values[2])==0&&*static_cast<uint32_t*>(values[3])==1&&!values[4],
            "Native item factory ABI uses preview position, no inventory UID, one item and no fabricated effects");
        auto item=Make("mc");Get(item).itemId=*static_cast<int*>(values[0]);Get(item).quantity=1;
        auto data=Make("ItemData");Get(data).fields["id"]=Box(Get(item).itemId==unavailableItemId?0:Get(item).itemId);
        Get(item).fields["myx"]=data;return item;
    }
    if(info.type=="pq"&&(info.name=="paq"||info.name=="pbo")){
        Check(!self&&values.size()==1&&Get(values[0]).ownedTile,"Only owned item views may release native callbacks");
        if(info.name=="paq"){
            if(imageCleanupFailures>0){--imageCleanupFailures;throw std::runtime_error("Temporary image cleanup failure");}
            imageTargets.erase(values[0]);
        }else{
            if(eventCleanupFailures>0){--eventCleanupFailures;throw std::runtime_error("Temporary item event cleanup failure");}
            tooltipTargets.erase(values[0]);contextTargets.erase(values[0]);
        }
        return nullptr;
    }
    return Call(self,info.name.c_str(),args);
}
Obj Api::Call(Obj self,const char* name,std::initializer_list<void*> arguments)const{
    using namespace fixture;
    auto& o=Get(self);std::string method=name;std::vector<void*> args(arguments);
    if(method=="BindTileData"){
        Check(self==nativeRecipes&&args.size()==3&&Get(args[0]).klass->name=="mc"&&Get(args[1]).ownedTile,
            "Native job recipe binder receives only the owned preview Tile and item wrapper");
        auto& tile=Get(args[1]);tile.itemId=Get(args[0]).itemId;tile.quantity=*static_cast<int*>(args[2]);
        ++itemBindings;bindingItems.push_back(tile.itemId);imageTargets.insert(args[1]);tooltipTargets.insert(args[1]);tile.fields["onRightClickCallback"]=args[0];
        if(bindFailureCountdown==0){bindFailureCountdown=-1;throw std::runtime_error("Partial native item bind failure");}
        if(bindFailureCountdown>0)--bindFailureCountdown;
        contextTargets.insert(args[1]);return nullptr;
    }
    if(method=="get_layout")return Box(o.layout);
    if(method=="get_transform")return self;
    if(method=="UnityEngine.UIElements.ITransform.set_position"){
        Check(self!=recipeTab&&self!=resourceTab,"Native tabs must never be moved");
        o.translation=*static_cast<Vector3*>(args[0]);++o.translationWrites;return nullptr;
    }
    if(method=="get_style"){auto style=Make("Style");Get(style).fields["owner"]=self;return style;}
    if(o.klass->name=="Style"){
        const auto property=method.substr(method.find_last_of('_')+1);auto& target=Get(o.fields["owner"]);
        if(method.find(".get_")!=std::string::npos){auto box=Box(0);Get(box).bytes=target.styles[property];Get(box).property=property;return box;}
        if(o.fields["owner"]==recipeTab||o.fields["owner"]==resourceTab)++nativeTabStyleWrites;
        else if(target.klass->name=="DofusTab"&&property=="top")++customTabTopWrites;
        std::memcpy(target.styles[property].data(),args[0],Size(property));return nullptr;
    }
    if(method=="set_value"){std::memcpy(o.bytes.data(),args[0],Size(o.property));return nullptr;}
    if(method=="set_text"||method=="set_rightText"||method=="set_counterValue"){o.text=Get(args[0]).text;return nullptr;}
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
    if(method=="get_resolvedStyle"){
        auto resolved=Make("ResolvedStyleAccess");Get(resolved).fields["owner"]=self;return resolved;
    }
    if(o.klass->name=="ResolvedStyleAccess"&&method.starts_with("get_")){
        const auto& element=Get(o.fields.at("owner"));const auto property=method.substr(4);
        if(property=="alignSelf")return Box(static_cast<int>(element.resolvedMetrics.at(property)));
        return Box(property=="height"?element.resolvedHeight:element.resolvedMetrics.at(property));
    }
    if(method=="get_size")return Box(1);
    if(method=="set_tileSize"){Check(o.ownedTile&&*static_cast<int*>(args[0])==2,"Native item preview uses medium Tile size");return nullptr;}
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
    if(method=="myx"){Check(o.klass->name=="mc","Native item metadata belongs to the wrapper");return o.fields.at("myx");}
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
    Page page{12,{{query.job*100+query.skip,query.level,1,.05,"Poisson pané frit",{{1,2,"Ingrédient"},{2,3,"Autre ingrédient"}}}}};
    ++fixture::completed;return page;
}
}
#include "jobs/jobs-xp-panel.cpp"

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
        nativeRecipes=Make("JobWindowRecipesUI");Get(nativeRecipes).fields["m_currentRecipesFilter"]=Make("NativeRecipesFilter");
        Get(handler).fields["m_jobWindowRecipesUI"]=nativeRecipes;
        auto foreignTile=Make("Tile"),foreignCallback=Make("NativeItemAction");Get(foreignTile).fields["onRightClickCallback"]=foreignCallback;
        imageTargets.insert(foreignTile);tooltipTargets.insert(foreignTile);contextTargets.insert(foreignTile);
        auto root=Make("Root"),tabs=Make("Tabs"),content=Make("Content");Get(root).fields["panel"]=Make("Panel");
        fixture::Add(root,tabs);fixture::Add(root,content);Get(binding).fields["root"]=root;Get(binding).fields["ctr_tabs"]=tabs;Get(binding).fields["content"]=content;
        recipes=Make("Recipes");resources=Make("Resources");recipeTab=Make("DofusTab");resourceTab=Make("DofusTab");
        fixture::Add(content,recipes);fixture::Add(content,resources);fixture::Add(tabs,recipeTab);fixture::Add(tabs,resourceTab);
        Get(binding).fields["recipesUI"]=recipes;Get(binding).fields["jobSkillsUI"]=resources;
        Get(binding).fields["tab_recipe"]=recipeTab;Get(binding).fields["tab_ressources"]=resourceTab;
        tabGroup=Make("SelectableGroup");Get(tabGroup).children={recipeTab,resourceTab};
        Get(recipeTab).fields["group"]=tabGroup;Get(resourceTab).fields["group"]=tabGroup;
        Get(resourceTab).layout={100,3,100,43};
        Get(recipeTab).fontCase=Get(resourceTab).fontCase=1;Get(resourceTab).resolvedHeight=43;
        Get(resourceTab).resolvedMetrics={{"top",0.f},{"bottom",-1.f},{"marginTop",2.f},{"marginBottom",3.f},
            {"paddingTop",4.f},{"paddingBottom",5.f},{"marginLeft",7.f},{"marginRight",1.f},{"alignSelf",2.f}};
        Get(recipeTab).resolvedMetrics={{"marginRight",3.f}};
        float baseline=-1;std::memcpy(Get(resourceTab).styles["bottom"].data(),&baseline,4);
        Select(recipeTab,true);NativeSwitch(handler,0,nullptr);
        Select(recipeTab,false);Check(Get(recipeTab).on&&Get(recipeTab).activeClass,"Fixture models native refusal to clear its final selected tab");
        const auto initialFilter=Get(nativeRecipes).fields["m_currentRecipesFilter"];
        Get(handler).fields["m_jobWindowRecipesUI"]=nullptr;
        InstallJobsXp();EnableHook(owner,nullptr);TickJobsXp();
        Check(!state.pane.get()&&pending.get()==owner,"Native recipe UI initialization must defer panel creation without dropping its pending owner");
        Get(handler).fields["m_jobWindowRecipesUI"]=nativeRecipes;Get(nativeRecipes).fields["m_currentRecipesFilter"]=nullptr;TickJobsXp();
        Check(!state.pane.get()&&pending.get()==owner,"Native recipe filter initialization must remain retryable on the next tick");
        Get(nativeRecipes).fields["m_currentRecipesFilter"]=initialFilter;TickJobsXp();
        Check(state.pane.get()&&Get(tabs).children.size()==3,"Third real native tab attached");
        Check(Get(state.tab.get()).klass->name=="DofusTab","Native DofusTab class");
        Check(Get(state.tab.get()).text=="XP","Custom tab caption must be exactly XP");
        Check(Get(state.tab.get()).fontCase==1&&Metric(state.tab.get(),"bottom")==-1,"Native typography and baseline copied");
        Check(customTabTopWrites==1&&Get(resourceTab).resolvedMetrics.at("top")==0,
            "Copy native top once, then preserve auto instead of replacing it with resolved zero that masks bottom alignment");
        Check(Metric(state.tab.get(),"height")==43&&Metric(state.tab.get(),"minHeight")==43&&Metric(state.tab.get(),"maxHeight")==43,"XP inherits actual native theme tab height");
        Check(Metric(state.tab.get(),"marginTop")==2&&Metric(state.tab.get(),"marginBottom")==3&&
            Metric(state.tab.get(),"paddingTop")==4&&Metric(state.tab.get(),"paddingBottom")==5,
            "XP baseline must inherit resolved native margins and padding even when inline values are absent");
        Check(Metric(state.tab.get(),"marginLeft")+Get(resourceTab).resolvedMetrics.at("marginRight")==
            Get(resourceTab).resolvedMetrics.at("marginLeft")+Get(recipeTab).resolvedMetrics.at("marginRight"),
            "The Resources-to-XP gap must equal the native Recipes-to-Resources gap");
        auto& xp=Get(state.tab.get());
        Check(xp.layout.y+xp.translation.y==Get(resourceTab).layout.y,"XP actual border rectangle aligns even when copied style metrics do not");
        const int writes=xp.translationWrites;TickJobsXp();TickJobsXp();
        Check(xp.translationWrites==writes,"Position correction is stable across unchanged layout ticks");
        Get(resourceTab).layout.y=12;TickJobsXp();
        Check(xp.layout.y+xp.translation.y==12,"XP follows relayout in either vertical direction without accumulating correction");
        xp.layout.height=0;Get(resourceTab).layout.y=30;TickJobsXp();
        Check(xp.translationWrites==writes+1,"Unresolved layout must not move the tab");
        xp.layout.height=43;TickJobsXp();
        Check(xp.layout.y+xp.translation.y==30,"Deferred layout correction retries after geometry becomes available");
        Get(resourceTab).resolvedHeight=51;TickJobsXp();Check(Metric(state.tab.get(),"height")==51,"Changed native theme geometry follows on next pump");
        Check(customTabTopWrites==1&&Metric(state.tab.get(),"bottom")==-1,"Theme relayout must preserve automatic top and native bottom alignment");
        Select(state.tab.get(),true);TickJobsXp();Wait();
        Check(Display(recipes)==1&&Display(resources)==1&&Display(state.pane.get())==0,"XP pane owns content only while selected");
        Check(!Get(recipeTab).on&&!Get(resourceTab).on&&!Get(recipeTab).activeClass&&!Get(resourceTab).activeClass&&Get(state.tab.get()).activeClass,"Only XP is green; native group has no stale selection");
        Check(forcedOff==1&&!Get(tabGroup).allowSwitchOff,"Native force-off restores original required-selection policy");
        Check(state.query.job==28&&state.xp==24500,"Native selected job and total XP");
        CheckNativeItem(state.recipeItems[0].tile.get(),2800,1);
        Check(action(state.rows[0].get()),"Owned recipe action");
        const auto plan=jobsxp::Simulate(state.page.recipes[0],state.xp,state.lastTarget,state.lastCoefficient,state.lastEpic);
        Check(plan.count>0&&state.ingredients.size()==2,"Selected recipe must create each ingredient as an individual native item");
        for(size_t i=0;i<state.ingredients.size();++i){
            const auto& ingredient=state.page.recipes[0].ingredients[i];
            CheckNativeItem(state.ingredients[i].tile.get(),ingredient.id,int64_t(ingredient.quantity)*plan.count);
        }
        const auto regularIngredient=state.ingredients[0].tile.get();
        state.page.recipes[0].ingredients[0].quantity=INT_MAX;Render();
        const auto largeTotal=int64_t(INT_MAX)*plan.count;
        Check(largeTotal>INT_MAX,"Large-quantity fixture exercises totals beyond the native Int32 counter");
        CheckRetiredItem(regularIngredient);CheckNativeItem(state.ingredients[0].tile.get(),1,largeTotal);
        Check(Get(state.ingredients[0].tile.get()).text==std::to_string(largeTotal)&&
            Get(state.ingredients[0].label.get()).text.starts_with(std::to_string(largeTotal)+" × "),
            "Large preparation totals must remain complete in the native counter and ingredient label");
        state.page.recipes[0].ingredients[0].quantity=2;Render();
        const auto previousIngredient=state.ingredients[1].tile.get();
        unavailableItemId=9999;state.page.recipes[0].ingredients[1].id=unavailableItemId;Render();
        CheckRetiredItem(previousIngredient);
        Check(!state.ingredients[1].tile.get()&&Get(state.ingredients[1].label.get()).text.find("indisponible")!=std::string::npos,
            "An unknown native item must keep its quantity/name without binding the native placeholder's actions");
        state.page.recipes[0].ingredients[1].id=2;unavailableItemId=0;Render();
        const auto stableRecipe=state.recipeItems[0].tile.get();const auto stableBindings=itemBindings;
        Render();Check(state.recipeItems[0].tile.get()==stableRecipe&&itemBindings==stableBindings,
            "Unchanged render must reuse native bindings without duplicate tooltips or callbacks");
        state.nextRequest=0;Check(action(state.next.get()),"Next page action");
        CheckRetiredItem(stableRecipe);Wait();Check(state.query.skip==jobsxp::PageSize,"Pagination changes the native recipe page");
        CheckNativeItem(state.recipeItems[0].tile.get(),2800+jobsxp::PageSize,1);
        const auto nextPageTile=state.recipeItems[0].tile.get();
        state.nextRequest=0;action(state.previous.get());CheckRetiredItem(nextPageTile);Wait();
        CheckNativeItem(state.recipeItems[0].tile.get(),2800,1);action(state.rows[0].get());
        std::vector<Obj> leavingTiles{state.recipeItems[0].tile.get()};
        for(const auto& ingredient:state.ingredients)leavingTiles.push_back(ingredient.tile.get());
        Select(recipeTab,true);SwitchHook(handler,0,nullptr);Check(!state.active&&Display(recipes)==0&&Get(recipeTab).on&&Get(recipeTab).activeClass&&!Get(state.tab.get()).activeClass,"Return to same original recipe index");
        for(auto tile:leavingTiles)CheckRetiredItem(tile);
        Select(state.tab.get(),true);TickJobsXp();Select(resourceTab,true);SwitchHook(handler,1,nullptr);
        Check(!state.active&&Display(resources)==0&&Get(resourceTab).on&&Get(resourceTab).activeClass&&!Get(recipeTab).activeClass&&!Get(state.tab.get()).activeClass,"Return to resource tab does not restore former recipe selection");
        const auto bindingsBeforeJobChange=bindingItems.size();hold=true;Get(handler).fields["m_jobId"]=Box(24);
        Select(state.tab.get(),true);TickJobsXp();Check(state.active,"XP can reopen");
        Check(state.query.job==24&&bindingItems.size()==bindingsBeforeJobChange&&!state.recipeItems[0].tile.get(),
            "Reopening XP after changing the native job must not bind an old recipe against the new recipe filter");
        hold=false;state.nextRequest=0;Wait();CheckNativeItem(state.recipeItems[0].tile.get(),2400,1);
        const auto oldCalls=calls.load();hold=true;state.nextRequest=0;action(state.refresh.get());
        for(int i=0;i<100&&calls==oldCalls;++i)Sleep(1);
        Get(handler).fields["m_jobId"]=Box(26);state.nextPoll=0;TickJobsXp();
        hold=false;state.nextRequest=0;Wait();
        Check(state.query.job==26&&state.page.recipes[0].id==2600,"Changed native job cancels and discards stale HTTP");
        const auto oldFilter=Get(nativeRecipes).fields["m_currentRecipesFilter"],filterItem=state.recipeItems[0].tile.get();
        Get(nativeRecipes).fields["m_currentRecipesFilter"]=nullptr;TickJobsXp();
        Check(!state.pane.get(),"Unavailable native recipe filter must retire the XP panel before binding more items");CheckRetiredItem(filterItem);
        Get(nativeRecipes).fields["m_currentRecipesFilter"]=oldFilter;
        EnableHook(owner,nullptr);TickJobsXp();Select(state.tab.get(),true);TickJobsXp();Wait();
        auto oldItem=state.recipeItems[0].tile.get();state.nextRequest=0;action(state.refresh.get());CheckRetiredItem(oldItem);
        bindFailureCountdown=0;imageCleanupFailures=1;
        for(int i=0;i<500&&!JobsXpCleanupPending();++i){TickJobsXp();Sleep(1);}
        Check(JobsXpCleanupPending()&&state.recipeItems[0].tile.get(),"Partial native item bind must retain ownership when immediate cleanup fails");
        auto partiallyBound=state.recipeItems[0].tile.get();
        Check(imageTargets.contains(partiallyBound)&&!tooltipTargets.contains(partiallyBound)&&
            !contextTargets.contains(partiallyBound)&&!Get(partiallyBound).fields["onRightClickCallback"]&&Display(state.pane.get())==1,
            "Partial cleanup must hide the pane and clear stale actions while retaining unfinished native image cleanup");
        TickJobsXp();Check(!JobsXpCleanupPending()&&!state.pane.get(),"Next tick must finish native item cleanup after a partial binding failure");
        CheckRetiredItem(partiallyBound);
        EnableHook(owner,nullptr);TickJobsXp();Select(state.tab.get(),true);TickJobsXp();Wait();
        auto staleAction=state.rows[0].get(),staleNativeItem=state.recipeItems[0].tile.get();mod.cached.reset(Make("OtherCache"));
        Check(action(staleAction),"Stale owned action consumed without native fallback");
        TickJobsXp();Check(!state.pane.get(),"Character session replacement detaches");
        CheckRetiredItem(staleNativeItem);
        EnableHook(owner,nullptr);TickJobsXp();Select(state.tab.get(),true);TickJobsXp();Wait();
        auto pane=state.pane.get(),closingTile=state.recipeItems[0].tile.get();failDetach=pane;eventCleanupFailures=1;RestoreJobsXp();
        Check(JobsXpCleanupPending()&&!Get(closingTile).fields["onRightClickCallback"]&&Display(pane)==1,
            "Native event cleanup failure must retain ownership with item action disabled and pane hidden");
        RestoreJobsXp();
        Check(JobsXpCleanupPending(),"Transient detach remains retryable");
        RestoreJobsXp();Check(!JobsXpCleanupPending()&&!Panel(pane),"Retry releases owned controls");
        CheckRetiredItem(closingTile);
        Check(Get(resourceTab).on&&Display(resources)==0,"Cleanup restores original selected native tab");
        Check(nativeTabStyleWrites==0,"XP geometry matching must never modify either native tab's styles");
        Check(imageTargets==std::set<Obj>{foreignTile}&&tooltipTargets==std::set<Obj>{foreignTile}&&contextTargets==std::set<Obj>{foreignTile}&&
            Get(foreignTile).fields["onRightClickCallback"]==foreignCallback,"Cleanup must preserve all foreign native item callbacks");
        if(worker){worker->cancelled=true;for(int i=0;i<500&&!worker->done;++i)Sleep(1);worker.reset();}
        mod.cached.reset();mod.compositor.reset();module=nullptr;
        Check(handles.empty(),"No retained native references after cleanup");
        std::cout<<"PASS: native third tab, original tab returns, auto job XP, stale requests/actions and retryable cleanup\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"FAIL: "<<e.what()<<'\n';for(const auto& line:logs)std::cerr<<line<<'\n';return 1;
    }
}
