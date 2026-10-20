#include "chat-travel.hpp"
#include "map-travel.hpp"
#include "chat-travel-policy.hpp"
#include "chat-route.hpp"
#include "soul-group-menu.hpp"
#include "soul-spell.hpp"
#include "soul-combat.hpp"
#include "soul-inventory.hpp"
#include "module.hpp"
#include "features.hpp"
#include "options-ui.hpp"
#include <array>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <memory>
#include <limits>
#include <unordered_map>

namespace fake {
using namespace managed;
struct Node { std::array<unsigned char,256> raw{}; std::string type,text; std::unordered_map<std::string,Obj> fields; std::unordered_map<std::string,int64_t> numbers; std::vector<Obj> items; };
struct MethodData { std::string type,name; uintptr_t rva=0; };
std::vector<std::unique_ptr<Node>> nodes;
std::vector<std::unique_ptr<MethodData>> methods;
std::unordered_map<Obj,Node*> objects;
std::unordered_map<std::string,std::unique_ptr<std::string>> classes;
std::unordered_map<GCHandle,Obj> handles;
std::unordered_map<std::string,Obj> services;
std::unordered_map<int64_t,Obj> mapInfos;
std::unordered_map<int64_t,Obj> monsterInfos;
int inventoryReads=0;
GCHandle serial=1;
Obj currentMap{}, interaction{}, destinations{}, group{}, entity{}, collection{}, groupStore{}, variants{}, spell{}, spellList{};
Obj lastMenu{}, lastView{}, localCharacter{}, banner{};
int64_t localId=7,controlled=7;
bool combat=false,ready=true,allowed=true,bag=false,eligible=true,mapValid=true;
int entered=0,interacted=0,teleported=0,walked=0,cast=0,equipped=0,quotes=0,teleportSource=-1,teleportCost=-1;
int64_t requestedQuote=0;
bool directKnown=true,synchronousQuote=false;
bool synchronousList=false,failAppend=false;
std::vector<int64_t> resolvedMaps{123};
struct Point { float x,y; };
void (*mapMenuHook)(Obj,Obj,Point,Point,Method)=nullptr;
void (*mapWalkHook)(Obj,Point,int,bool,Method)=nullptr;
void (*mapAutoHook)(Obj,Point,Method)=nullptr;
int nativeMenus=0,nativeWalks=0,nativeAutos=0;
Point nativeCoordinates{},nativeMouse{}; int nativeWorld=-1; bool nativeImmediate=false;
bool sceneReady=true, loadingMap=false, acceptRoute=true, routeRunning=false, routePendingNative=false;
int loadedMapOverride=-1;
int64_t plannedDeparture=104;
bool routePending=false;
int routeTicks=0;
std::vector<chat_travel::Destination> plannedChoices;
std::vector<std::string> notices,logs;
bool failSpellEligibility=false;
int allowedChecks=0;
int64_t teleportMap=0; ChatTpTarget walkedTo{};
soul::CombatSnapshot fightState{soul::CombatState::Fighting,17,51};
soul::InventorySnapshot inventory{true,0,{{55,9687,3,true}}};
bool (*receiveHook)(Obj,Obj,Method)=nullptr;
bool (*quoteHook)(Obj,Obj,Method)=nullptr;
Obj (*createHook)(Obj,Method)=nullptr;
void (*spellClick)(Obj)=nullptr;
void Check(bool condition,const char* message) { if (!condition) throw std::runtime_error(message); }
Obj Make(const std::string& type) { auto node=std::make_unique<Node>();node->type=type;auto ptr=node->raw.data();objects[ptr]=node.get();nodes.push_back(std::move(node));return ptr; }
Node& Get(Obj obj) { if (!objects.contains(obj)) throw std::runtime_error("Unknown fake object");return *objects.at(obj); }
template<class T> Obj Box(T value) { auto result=Make("Box");std::memcpy(result,&value,sizeof(value));return result; }
template<class T> void Raw(Obj obj,size_t offset,T value) { std::memcpy(static_cast<unsigned char*>(obj)+offset,&value,sizeof(value)); }
Obj List(std::initializer_list<Obj> values={}) { auto list=Make("List");Get(list).items=values;return list; }
Obj Info(int64_t id,int x,int y,int world) { auto info=Make("MapInformationData"),area=Make("SubAreaData");Raw<int16_t>(info,0x18,static_cast<int16_t>(x));Raw<int16_t>(info,0x1A,static_cast<int16_t>(y));Get(info).fields["area"]=area;Get(area).numbers["world"]=world;mapInfos[id]=info;return info; }
Obj Destination(int64_t id,int x,int y,int world,int cost,bool known=true) { auto d=Make("fbn");Get(d).numbers={{"dsoi",id},{"dsok",0},{"dsol",cost},{"dsoo",known}};Info(id,x,y,world);return d; }
bool OriginalList(Obj,Obj,Method) { return true; }
Obj OriginalCreate(Obj,Method) { lastMenu=Make("DofusContextualMenu");auto rows=List({Make("NativeBestiary"),Make("NativeShare")});Get(lastMenu).fields["rows"]=rows;auto root=Make("Root");Get(root).fields["data"]=lastMenu;Get(root).fields["panel"]=root;Get(lastMenu).fields["dofusContextualMenuRoot"]=root;return lastMenu; }
void OriginalMapMenu(Obj,Obj,Point point,Point mouse,Method){++nativeMenus;nativeCoordinates=point;nativeMouse=mouse;}
void OriginalMapWalk(Obj,Point point,int world,bool immediate,Method){++nativeWalks;nativeCoordinates=point;nativeWorld=world;nativeImmediate=immediate;}
void OriginalMapAuto(Obj,Point point,Method){++nativeAutos;nativeCoordinates=point;}
Obj Invoke(Method method,Obj self,std::initializer_list<void*> args) {
    const auto& m=*static_cast<const MethodData*>(method);std::vector<void*> a(args);const auto& name=m.name;
    if (name==".ctor") {
        if(m.type=="cwo"){
            Check(a.size()==12&&*static_cast<unsigned*>(a[0])==0x200&&*static_cast<unsigned*>(a[11])==4,"Native local error/status channel and display flags");
            Check(*static_cast<int64_t*>(a[2])==0&&*static_cast<int64_t*>(a[3])==0&&*static_cast<int64_t*>(a[4])==0&&!a[5]&&!a[6]&&!a[10],"Local notice has no sender or recipient");
            Get(self).text=Get(a[1]).text;
        }
        if (m.type=="dhc") { Get(self).numbers["source"]=*static_cast<int*>(a[0]);Get(self).numbers["map"]=*static_cast<int64_t*>(a[2]);Get(self).numbers["cost"]=*static_cast<unsigned*>(a[3]); }
        if (m.type=="dhw") Get(self).numbers["map"]=*static_cast<int64_t*>(a[0]);
        return nullptr;
    }
    if(name=="bncy"){auto msg=Make("ChatMessage");Get(msg).text=Get(a[0]).text;return msg;}
    if(name=="bmyc"){Check(Get(self).type=="hal","Only the local chat history receives feedback");notices.push_back(Get(a[0]).text);return nullptr;}
    if (name=="GetMapInformationById") return mapInfos.contains(*static_cast<int64_t*>(a[0]))?mapInfos[*static_cast<int64_t*>(a[0])]:nullptr;
    if (name=="GetById"&&m.type=="MonsterData") {auto id=*static_cast<int64_t*>(a[0]);return monsterInfos.contains(id)?monsterInfos[id]:nullptr;}
    auto& n=Get(self);
    if (name=="get_isBoss") return Box(bool(n.numbers["boss"]));
    if (name=="get_isMiniBoss") return Box(bool(n.numbers["arch"]));
    if (name=="get_soulCaptureForbidden") return Box(bool(n.numbers["forbidden"]));
    if (name=="get_Count") return Box(static_cast<int>(n.items.size()));
    if (name=="get_Item") return n.items.at(*static_cast<int*>(a[0]));
    if (name=="get_Target") return n.fields["target"];
    if (name=="get_panel") return n.fields["panel"];
    if (name=="get_menuData"||name=="get_itemData") return n.fields["data"];
    if (name=="get_menuItems") return n.fields["rows"];
    if (name=="get_m_callback") return n.fields["callback"];
    if (name=="get_disabled") return Box(bool(n.numbers["disabled"]));
    if (name=="RemoveItemAt") { Get(n.fields["rows"]).items.erase(Get(n.fields["rows"]).items.begin()+*static_cast<int*>(a[0]));return nullptr; }
    if (name=="RemoveMenu") { n.fields["panel"]=nullptr;return nullptr; }
    if (name=="AppendDefaultItem") { Check(a.size()==13&&!a[1]&&*static_cast<bool*>(a[3]),"Owned native row uses inert managed action and native close");auto item=Make("ContextMenuItem");Get(item).text=Get(a[0]).text;Get(item).numbers["disabled"]=*static_cast<bool*>(a[2]);Get(item).numbers["icon"]=*static_cast<int*>(a[9]);Get(item).fields["callback"]=Make("ClickCallbackHandler");Get(n.fields["rows"]).items.push_back(item);lastView=Make("ContextMenuItemView");Get(lastView).fields["data"]=item;Get(lastView).fields["panel"]=lastView;if(failAppend)throw std::runtime_error("Append failed after adding row");return nullptr; }
    if (name=="beqx") return n.fields["world"];
    if (name=="zeo") return currentMap;
    if (name=="zfr") return Box(bag);
    if (name=="xkj") return Box(sceneReady);
    if (name=="xkd") {auto d=Make("ClientMapData");Get(d).fields["id"]=Box<int>(loadedMapOverride<0?int(Get(currentMap).numbers["m_mapId"]):loadedMapOverride);return d;}
    if (name=="bcdi") return Box(routeRunning);
    if (name=="zhx") return Box(eligible);
    if (name=="zid") {auto result=List();if(mapValid)for(auto map:resolvedMaps)Get(result).items.push_back(Box<int64_t>(map));return result;}
    if (name=="zdc") return n.fields["money"];
    // zch/0x11EDC80 reads +0xF8, not the known-zaap list at +0x120.
    if (name=="zch") return n.fields["otherMaps"];
    if (name=="zhr") return Box(directKnown);
    if (name=="bepd") {++quotes;requestedQuote=Get(a[0]).numbers["map"];if(synchronousQuote){auto message=Make("hgu");Raw<int64_t>(message,0x28,requestedQuote);Raw<int>(message,0x30,25);quoteHook(self,message,nullptr);}return Box(true);}
    if (name=="yhr") return n.fields["event"];
    if (name=="bjfd") {++entered;return nullptr;}
    if (name=="qyb") return interaction;
    if (name=="OnSkillClicked") {Check(a[1]==Get(interaction).items[0]&&*static_cast<unsigned*>(a[2])==888&&*static_cast<int*>(a[3])==-1,"Native enabled interactive ABI");++interacted;if(synchronousList){auto msg=Make("hgq");Raw<int>(msg,0x28,3);receiveHook(services["fpf"],msg,nullptr);}return nullptr;}
    if (name=="get_subArea") return n.fields["area"];
    if (name=="get_worldmapId") return Box(static_cast<int>(n.numbers["world"]));
    if (name=="bcla") {++teleported;teleportMap=Get(a[0]).numbers["map"];teleportSource=static_cast<int>(Get(a[0]).numbers["source"]);teleportCost=static_cast<int>(Get(a[0]).numbers["cost"]);return Box(true);}
    if (name=="zhy") {Check(*static_cast<bool*>(a[3]),"Explicit TP must resume native autopilot without confirmation");++walked;
        if(acceptRoute){auto d=Make("MapInformationData");Get(d).fields["id"]=Box<uint32_t>(123);Get(services["fjs"]).fields["dsjs"]=d;routeRunning=true;}
        walkedTo={*static_cast<int*>(a[0]),*static_cast<int*>(a[1]),*static_cast<int*>(a[2])};return nullptr;}
    if (name=="bajh") return Box<int64_t>(99);
    if (name=="bapo") return groupStore;
    if (name=="baqa") {*static_cast<Obj*>(a[1])=group;return Box(group!=nullptr);}
    if (name=="xmg") return collection;
    if (name=="xmp") return services["fns"];
    if (name=="xmi") return services["fsr"];
    if (name=="bamm") {*static_cast<Obj*>(a[1])=entity;return Box(entity!=nullptr);}
    if (name=="bafy") return variants;
    if (name=="bfty") return Box(controlled);
    if (name=="bhlm") {Check(a.size()==2&&*static_cast<int64_t*>(a[0])==localId&&*static_cast<int*>(a[1])==2,"Read only the local participant's native preferred spell list");return spellList;}
    if (name=="nhp") return Box(static_cast<unsigned>(n.numbers["id"]));
    if (name=="nhs") return Box(n.numbers["owner"]);
    if (name=="nhm") return n.fields["level"];
    if (name=="nim") return Box(0);
    if (name=="ngl") return Box<unsigned>(1);
    if (name=="bfua") {++allowedChecks;if(failSpellEligibility)throw std::runtime_error("Cast state not ready");Check(*static_cast<unsigned*>(a[0])==413&&*static_cast<int64_t*>(a[2])==localId&&a[3]==nullptr,"Native selfcast validation ABI");return Box(allowed);}
    if (name=="bgaf") {Check(a[0]==spell&&*static_cast<int64_t*>(a[1])==localId,"Native selfcast ABI");++cast;return nullptr;}
    throw std::runtime_error("Unexpected call "+m.type+"."+name);
}
void Initialize() {
    api.object_get_class=[](Obj obj)->Klass{return api.Class("","",Get(obj).type.c_str());};
    api.class_get_name=[](Klass c){return static_cast<std::string*>(c)->c_str();};
    api.object_new=[](Klass c)->Obj{return Make(*static_cast<std::string*>(c));};
    api.object_unbox=[](Obj obj)->void*{return obj;};
    api.string_new=[](const char* value)->Obj{auto s=Make("String");Get(s).text=value;return s;};
    api.gchandle_new=[](Obj obj,bool)->GCHandle{auto id=serial++;handles[id]=obj;return id;};
    api.gchandle_get_target=[](GCHandle id)->Obj{return handles.at(id);};
    api.gchandle_free=[](GCHandle id){handles.erase(id);};
    api.method_get_param=[](Method,uint32_t)->const void*{return api.Class("","","ValueStruct");};
    api.class_from_type=[](const void* type)->Klass{return const_cast<void*>(type);};
}
struct Fixture {
    Module owner{"offline"};
    Fixture() {
        services.clear();mapInfos.clear();module=&owner;owner.ready=true;owner.servicesReady=true;owner.settings.soulReminder=true;owner.cached.reset(Make("Cached"));owner.compositor.reset(Make("Compositor"));owner.fight.reset(Make("Fight"));
        monsterInfos.clear();inventoryReads=0;auto monster=Make("MonsterData");Get(monster).numbers["boss"]=1;Get(monster).fields["souls"]=List({Make("MonsterSoulData")});monsterInfos[1]=monster;
        combat=false;ready=true;allowed=true;bag=false;eligible=true;mapValid=true;controlled=localId=7;fightState={soul::CombatState::Fighting,17,51};inventory={true,0,{{55,9687,3,true}}};
        entered=interacted=teleported=walked=cast=equipped=quotes=0;teleportSource=teleportCost=-1;requestedQuote=0;directKnown=true;synchronousQuote=false;
        synchronousList=failAppend=false;resolvedMaps={123};nativeMenus=nativeWalks=nativeAutos=0;
        plannedDeparture=104;plannedChoices.clear();notices.clear();logs.clear();routePending=false;routeTicks=0;
        for(auto name:{"fng","fpf","fkc","frq","fro","frv","fsm","fmz","fnm","fnk","fnr","fjx","frs","fsf","fvq","fns","fsr","fmo"}) services[name]=Make(name);
        services["ham"]=Make("hal");services["fjs"]=Make("faw");services["fpm"]=Make("epf");
        sceneReady=acceptRoute=true;loadingMap=routeRunning=routePendingNative=false;loadedMapOverride=-1;
        localCharacter=services["fng"];currentMap=Make("tk");Get(currentMap).numbers["m_mapId"]=10;
        auto money=Make("gb");Get(money).numbers["dcjl"]=100;Get(localCharacter).fields["money"]=money;
        banner=Make("BannerMenu");auto callback=Make("Action");Get(callback).fields["target"]=banner;auto event=Make("InputCustomEvent");Get(event).fields["m_input"]=List({callback});Get(services["frq"]).fields["event"]=event;
        auto wrapper=Make("wm"),data=Make("dq"),skill=Make("dp");Get(wrapper).fields["dhav"]=data;Get(data).numbers["dcao"]=16;Get(data).fields["dcap"]=List({skill});Get(skill).numbers={{"dcak",114},{"dcal",888}};interaction=List({wrapper});
        destinations=List({Destination(101,8,10,1,15),Destination(102,10,10,1,12,false),Destination(103,10,10,2,10),Destination(104,9,10,1,20)});Get(services["fpf"]).fields["dspc"]=destinations;
        Get(localCharacter).fields["otherMaps"]=List({Box<int64_t>(999)});
        Get(localCharacter).fields["dpzi"]=List({Box<int64_t>(101),Box<int64_t>(103),Box<int64_t>(104)});
        entity=Make("EntityInfo");collection=Make("evv");groupStore=Make("ewi");group=Make("ewd");auto leader=Make("fh");Get(leader).numbers={{"dcgq",1},{"dcgs",51}};Get(group).fields["dqyq"]=leader;Get(group).fields["dqyr"]=List();Get(group).fields["dqys"]=List();variants=nullptr;
        auto context=Make("epk");Get(services["fns"]).fields["dqst"]=context;
        spell=Make("ht");Get(spell).numbers={{"id",413},{"owner",localId}};Get(spell).fields["level"]=Make("hr");spellList=List({spell});
        failSpellEligibility=false;allowedChecks=0;
    }
    Obj Maker() { auto maker=Make("MonsterGroupMenuMaker");Get(maker).fields={{"m_data",entity},{"m_rolePlayEntitiesService",services["fns"]},{"m_staticInfoService",services["fsr"]}};return maker; }
    Obj MapOwner(int world=1,const char* type="CartographyBannerView") {auto cartographer=Make(type),root=Make("MapRoot"),data=Make("WorldMapData");Get(root).fields["panel"]=root;Get(data).fields["id"]=Box(world);Get(cartographer).fields={{"dugy",localCharacter},{"dugv",services["fkc"]},{"dugq",root},{"world",data}};return cartographer;}
    Obj MapMenu(Obj cartographer) {auto menu=OriginalCreate(nullptr,nullptr);mapMenuHook(cartographer,menu,{9,10},{30,40},nullptr);return menu;}
    void Receive() { auto message=Make("hgq");Raw<int>(message,0x28,3);Check(receiveHook(services["fpf"],message,nullptr),"Preserve native list result"); }
    void Price(int cost=25,int64_t map=104) {auto message=Make("hgu");Raw<int64_t>(message,0x28,map);Raw<int>(message,0x30,cost);Check(quoteHook(services["fkc"],message,nullptr),"Preserve native price result");}
    ~Fixture(){RestoreMapTravel();RestoreChatTravel();RestoreSoulGroupMenu();owner.cached.reset();owner.compositor.reset();owner.fight.reset();module=nullptr;}
};
}
namespace managed {
Api api{};uintptr_t gameBase=0;
Klass Api::Class(const char*,const char*,const char* name)const{auto& value=fake::classes[name];if(!value)value=std::make_unique<std::string>(name);return value.get();}
Method Api::Find(Klass c,const char* name,int)const{auto m=std::make_unique<fake::MethodData>();m->type=*static_cast<std::string*>(c);m->name=name;auto ptr=m.get();fake::methods.push_back(std::move(m));return ptr;}
Method Api::FindVerified(Klass c,const char* name,int argc,uintptr_t rva)const{auto method=Find(c,name,argc);const_cast<fake::MethodData*>(static_cast<const fake::MethodData*>(method))->rva=rva;return method;}
void* Api::Native(Method method,uintptr_t rva)const{const_cast<fake::MethodData*>(static_cast<const fake::MethodData*>(method))->rva=rva;return reinterpret_cast<void*>(rva);}
Obj Api::Invoke(Method method,Obj self,std::initializer_list<void*> args)const{return fake::Invoke(method,self,args);}
Obj Api::Call(Obj self,const char* name,std::initializer_list<void*> args)const{return Invoke(Find(object_get_class(self),name,static_cast<int>(args.size())),self,args);}
Obj Api::Field(Obj self,const char* name)const{
    if(std::string(name)=="dsjd") return fake::Box(fake::routePendingNative);
    return fake::Get(self).fields[name];
}
void* Api::FieldAddress(Obj self,const char* name)const{return &fake::Get(self).numbers[name];}
}
Module* module=nullptr;
bool MapTransitionLoading(){return fake::loadingMap;}
Module::Module(std::filesystem::path p):folder(p){}
bool Module::InCombat()const{return fake::combat;}
managed::Obj Module::FindService(const char* name)const{return fake::services[name];}
void Log(const std::string& text){fake::logs.push_back(text);}
void AddHook(managed::Method,uintptr_t rva,void* replacement,void** original){
    if(rva==0x1A4AA10){fake::receiveHook=reinterpret_cast<decltype(fake::receiveHook)>(replacement);*original=reinterpret_cast<void*>(&fake::OriginalList);}
    else if(rva==0x1C4A5D0){fake::quoteHook=reinterpret_cast<decltype(fake::quoteHook)>(replacement);*original=reinterpret_cast<void*>(&fake::OriginalList);}
    else if(rva==0x16E72F0){fake::createHook=reinterpret_cast<decltype(fake::createHook)>(replacement);*original=reinterpret_cast<void*>(&fake::OriginalCreate);}
    else if(rva==0x1BF78A0){fake::mapMenuHook=reinterpret_cast<decltype(fake::mapMenuHook)>(replacement);*original=reinterpret_cast<void*>(&fake::OriginalMapMenu);}
    else if(rva==0x1BF5E50){fake::mapWalkHook=reinterpret_cast<decltype(fake::mapWalkHook)>(replacement);*original=reinterpret_cast<void*>(&fake::OriginalMapWalk);}
    else if(rva==0x1BF8F40){fake::mapAutoHook=reinterpret_cast<decltype(fake::mapAutoHook)>(replacement);*original=reinterpret_cast<void*>(&fake::OriginalMapAuto);}
    else throw std::runtime_error("Unexpected hook");
}
namespace soul {
CombatSnapshot ReadCombat(managed::Obj,managed::Obj){return fake::fightState;}
int64_t CaptureParticipant(managed::Obj,managed::Obj){return fake::ready?fake::localId:0;}
InventorySnapshot ReadInventory(managed::Obj){++fake::inventoryReads;return fake::inventory;}
bool EquipExactStone(managed::Obj,uint64_t uid,int id){fake::Check(uid==55&&id==9687,"Exact tier/UID");++fake::equipped;return true;}
void SetSpellClick(void(*click)(managed::Obj)){fake::spellClick=click;}
}
// ChatRouteTests exercises the production graph bridge; isolate action gating here.
namespace chat_route {
Result result;
void Install() {}
void Clear() {result={Status::Unreachable};}
void Begin(std::vector<chat_travel::Destination> choices,int64_t target,int64_t) {
    fake::Check(target>0,"Planner requires the resolved native destination map");
    fake::plannedChoices=choices;
    Clear();for(auto choice:choices)if(choice.map==fake::plannedDeparture){result={Status::Found,choice,3};return;}
}
Result Tick(){++fake::routeTicks;return fake::routePending?Result{}:result;}
}
void MapTravelTests() {
    using namespace fake;
    const auto run=[](auto scenario) {const auto before=handles.size();{Fixture f;scenario(f);}Check(handles.size()==before,"Map travel must release managed references");};
    run([](Fixture& f){f.MapMenu(f.MapOwner());
        Check(nativeMenus==1&&nativeMouse.x==30&&nativeCoordinates.x==9,"Outside Havenbag direct-zaap menu fragment remains native");
        Check(!interacted&&!teleported&&!quotes&&!walked,"Opening map menu never starts travel");});
    for(const char* type:{"CartographyBannerView","CartographyMapView"}) for(int action=0;action<3;++action)run([type,action](Fixture& f){
        auto owner=f.MapOwner(14,type);
        if(action==2)mapAutoHook(owner,{-10.8f,11.9f},nullptr);else mapWalkHook(owner,{-10.8f,11.9f},14,action==1,nullptr);
        Check(quotes==1&&requestedQuote==104&&!nativeWalks&&!nativeAutos&&!interacted&&!teleported&&!walked,
            "Mini/full-map destination and double click use TP planner and fresh quote, never native walking first");
        mapAutoHook(owner,{-10.8f,11.9f},nullptr);mapWalkHook(owner,{-10.8f,11.9f},14,true,nullptr);
        Check(quotes==1&&routeTicks==1,"Same destination through either UI entrypoint preserves pending request");
        f.Price();TickChatTravel();Check(teleported==1&&teleportMap==104&&teleportSource==5&&!walked,"Fresh affordable quote permits selected direct zaap");
        Get(currentMap).numbers["m_mapId"]=104;TickChatTravel();TickChatTravel();TickChatTravel();
        Check(walked==1&&walkedTo==ChatTpTarget{-10,11,14}&&!nativeWalks&&!nativeAutos,
            "Confirmed arrival starts one no-confirm native route with displayed world and native coordinate truncation");});
    for(int action=0;action<2;++action)run([action](Fixture& f){
        plannedDeparture=10;Info(10,9,10,1);auto owner=f.MapOwner();
        if(action)mapAutoHook(owner,{10,10},nullptr);else mapWalkHook(owner,{10,10},1,true,nullptr);
        TickChatTravel();TickChatTravel();TickChatTravel();
        Check(walked==1&&!quotes&&!teleported&&!interacted&&!nativeWalks&&!nativeAutos,
            "Selecting current position as best departure walks directly without any zaap request");});
    for(int action=0;action<2;++action) for(int invalid=0;invalid<9;++invalid)run([action,invalid](Fixture& f){
        auto owner=f.MapOwner();Point point{10,10};int world=1;
        if(invalid==0)Get(Get(owner).fields["dugq"]).fields["panel"]=nullptr;
        if(invalid==1)services["fng"]=Make("DifferentCharacter");
        if(invalid==2)services["fkc"]=Make("DifferentCartography");
        if(invalid==3)Get(owner).fields["world"]=nullptr;
        if(invalid==4)point.x=std::numeric_limits<float>::quiet_NaN();
        if(invalid==5)point.y=std::numeric_limits<float>::infinity();
        if(invalid==6)point.x=100001;
        if(invalid==7){world=-1;Get(Get(owner).fields["world"]).fields["id"]=Box(-1);}
        if(invalid==8)owner=nullptr;
        if(action)mapAutoHook(owner,point,nullptr);else mapWalkHook(owner,point,world,true,nullptr);
        Check(!quotes&&!interacted&&!teleported&&!walked&&!nativeWalks&&!nativeAutos,
            "Invalid or stale cartography actions are consumed without movement or native fallback");});
    run([](Fixture& f){mapWalkHook(f.MapOwner(14),{10,10},1,true,nullptr);
        Check(!quotes&&!teleported&&!walked&&!nativeWalks,"Stale destination world cannot target another map layer");});
    for(int invalid=0;invalid<5;++invalid)run([invalid](Fixture& f){auto owner=f.MapOwner();
        if(invalid==0)f.owner.ready=false;
        if(invalid==1)f.owner.servicesReady=false;
        if(invalid==2)f.owner.stopped=true;
        if(invalid==3)f.owner.stopRequested=true;
        if(invalid==4)combat=true;
        mapWalkHook(owner,{9,10},14,false,nullptr);
        Check(nativeWalks==1&&nativeCoordinates.x==9&&nativeWorld==14&&!nativeImmediate,"Inactive/combat native destination call retains all arguments");
        mapAutoHook(owner,{7,8},nullptr);
        Check(nativeAutos==1&&nativeCoordinates.x==7&&!quotes&&!interacted&&!teleported&&!walked,"Inactive/combat native double-click retained with no mod travel");});
    for(int invalid=0;invalid<5;++invalid)run([invalid](Fixture& f){
        if(invalid==0)eligible=false;
        if(invalid==1)mapValid=false;
        if(invalid==2)plannedDeparture=99999;
        if(invalid==3)services["fjs"]=nullptr;
        if(invalid==4)services["fpf"]=nullptr;
        mapAutoHook(f.MapOwner(),{10,10},nullptr);
        Check(!quotes&&!interacted&&!teleported&&!walked&&!nativeWalks&&!nativeAutos,
            "Missing eligibility/services/destination/path cannot fall back to unintended native movement");
        const char* expected=invalid==1?"no native map at destination coordinates":
            invalid==2?"no verified native route":"native automatic travel unavailable";
        // Local chat deliberately throttles identical messages across repeated requests.
        Check(std::any_of(logs.begin(),logs.end(),[expected](const auto& line){return line.find(expected)!=std::string::npos;}),
            "Rejected map travel records the actual TP failure, including throttled repeat notices");});
    run([](Fixture& f){bag=true;resolvedMaps={104};auto owner=f.MapOwner();auto menu=f.MapMenu(owner);
        auto& rows=Get(Get(menu).fields["rows"]).items;Check(rows.size()==3&&!nativeMenus&&!interacted&&!teleported&&!quotes,"Opening menu appends one row without travel or quote");
        auto callback=Get(rows.back()).fields["callback"];auto view=lastView;Check(Get(rows.back()).numbers["icon"]==304,"Native teleport icon");
        Check(HandleMapTravelCallback(callback,view)&&interacted==1&&!teleported,"Explicit row click interacts with enabled Havenbag zaap");
        HandleMapTravelCallback(callback,view);TickChatTravel();Check(interacted==1&&!teleported,"Repeated click and cached list cannot authorize departure");
        f.Receive();Check(!teleported,"Receiving fresh server list cannot dispatch from receive hook");TickChatTravel();
        Check(teleported==1&&teleportMap==104&&teleportSource==3&&teleportCost==20&&!walked&&!quotes&&routeTicks==0,"Exact selected zaap uses fresh native list source and cost without planning or direct quote");
        Get(currentMap).numbers["m_mapId"]=104;bag=false;TickChatTravel();TickChatTravel();Check(!walked,"Exact zaap click never starts autopilot");TickMapTravel();Check(rows.size()==2,"Only owned row removed after leaving Havre-Sac");});
    run([](Fixture& f){bag=true;eligible=false;services["fjs"]=nullptr;StartHavenbagZaap(104);f.Receive();TickChatTravel();Check(teleported==1&&!walked,"Exact native zaap does not depend on automatic route eligibility");});
    run([](Fixture& f){bag=true;resolvedMaps={104,104,104};auto m=f.MapMenu(f.MapOwner());Check(Get(Get(m).fields["rows"]).items.size()==3,"Duplicate native map candidates yield one owned row");});
    for(int invalid=0;invalid<7;++invalid)run([invalid](Fixture& f){bag=true;resolvedMaps={104};auto owner=f.MapOwner();auto m=f.MapMenu(owner);auto row=Get(Get(m).fields["rows"]).items.back();auto cb=Get(row).fields["callback"];auto view=lastView;
        if(invalid==0)bag=false;
        if(invalid==1)combat=true;
        if(invalid==2)services["fng"]=Make("AnotherCharacter");
        if(invalid==3)Get(currentMap).numbers["m_mapId"]=22;
        if(invalid==4)Get(owner).fields["world"]=Make("OtherWorld");
        if(invalid==5)Get(view).fields["panel"]=nullptr;
        if(invalid==6)directKnown=false;
        Check(HandleMapTravelCallback(cb,view)&&!interacted&&!teleported,"Stale/closed/unknown/changed map callback is consumed without action");});
    run([](Fixture& f){bag=true;resolvedMaps={104};auto owner=f.MapOwner();auto first=f.MapMenu(owner);auto cb=Get(Get(Get(first).fields["rows"]).items.back()).fields["callback"];auto view=lastView;
        f.MapMenu(owner);Check(!HandleMapTravelCallback(cb,view)&&!interacted,"Replaced owned callbacks stay inert with null native actions");
        Check(Get(Get(first).fields["rows"]).items.size()==2,"Old menu keeps its native rows");});
    run([](Fixture& f){bag=true;resolvedMaps={104};failAppend=true;auto menu=f.MapMenu(f.MapOwner());Check(Get(Get(menu).fields["rows"]).items.size()==2&&!interacted,"Failed native append cleans its partial row without travel");});
    run([](Fixture& f){bag=true;directKnown=false;auto menu=f.MapMenu(f.MapOwner());Check(Get(Get(menu).fields["rows"]).items.size()==2&&!interacted,"No entry for unknown zaap");});
    for(int action=0;action<2;++action)run([action](Fixture& f){bag=true;auto owner=f.MapOwner();
        if(action)mapAutoHook(owner,{10,10},nullptr);else mapWalkHook(owner,{10,10},1,true,nullptr);
        Check(interacted==1&&!nativeAutos&&!nativeWalks&&!quotes,"Both native map-travel entrypoints use Havenbag interactive");f.Receive();TickChatTravel();
        Check(teleported==1&&teleportMap==104&&teleportSource==3&&!walked,"Route chooses accessible zaap first");
        Get(currentMap).numbers["m_mapId"]=104;TickChatTravel();Check(!walked,"No walking while still in Havenbag context");bag=false;TickChatTravel();TickChatTravel();TickChatTravel();
        Check(walked==1&&walkedTo==ChatTpTarget{10,10,1},"One native no-confirm onward route after confirmed arrival");});
    run([](Fixture& f){bag=true;eligible=false;mapAutoHook(f.MapOwner(),{10,10},nullptr);Check(!interacted&&!teleported&&!walked,"Native route eligibility failure cannot initiate movement");});
    run([](Fixture& f){bag=true;auto owner=f.MapOwner();mapAutoHook(owner,{std::numeric_limits<float>::quiet_NaN(),10},nullptr);mapWalkHook(owner,{9,10},-1,true,nullptr);Check(!interacted&&!teleported,"Invalid map coordinates/world cannot initiate a trip");});
    run([](Fixture& f){bag=true;auto owner=f.MapOwner();Get(Get(owner).fields["dugq"]).fields["panel"]=nullptr;mapAutoHook(owner,{10,10},nullptr);Check(!interacted,"Detached cartography owner is inert");});
    for(int invalid=0;invalid<7;++invalid)run([invalid](Fixture& f){bag=true;StartHavenbagZaap(104);
        if(invalid==0)Get(Get(localCharacter).fields["money"]).numbers["dcjl"]=0;
        if(invalid==1)Get(destinations).items.erase(Get(destinations).items.end()-1);
        if(invalid==2)Get(Get(destinations).items.back()).numbers["dsoo"]=false;
        if(invalid==3)Get(Get(destinations).items.back()).numbers["dsok"]=1;
        if(invalid==4)bag=false;
        if(invalid==5)directKnown=false;
        if(invalid==6)Get(destinations).items.push_back(Get(destinations).items.back());
        f.Receive();TickChatTravel();Check(!teleported&&!walked,"Recheck funds, exact destination, type, known status, Havenbag and ambiguity; never use alternative");});
    run([](Fixture&){bag=true;synchronousList=true;StartHavenbagZaap(104);StartHavenbagZaap(104);Check(interacted==1&&!teleported,"Synchronous list and repeated selection stay single-action");TickChatTravel();Check(teleported==1,"Synchronous response dispatches from regular pump only");});
    run([](Fixture& f){bag=true;StartHavenbagZaap(104);f.Receive();Get(services["fpf"]).fields["dspc"]=List();TickChatTravel();Check(!teleported,"Replaced server list cannot authorize selected zaap");});
    run([](Fixture& f){bag=true;StartHavenbagZaap(104);RestoreChatTravel();f.Receive();TickChatTravel();Check(!teleported,"Late response after cancellation is inert");});
    run([](Fixture& f){bag=true;Get(Get(interaction).items[0]).fields["dhav"]=nullptr;StartHavenbagZaap(104);TickChatTravel();Check(!interacted&&!teleported,"No enabled Havenbag zaap means no interaction or departure");f.Receive();TickChatTravel();Check(!teleported,"Unsolicited list cannot replace a missing interaction");});
    run([](Fixture& f){bag=true;Get(Get(destinations).items.back()).numbers["dsol"]=0;Get(Get(localCharacter).fields["money"]).numbers["dcjl"]=0;StartHavenbagZaap(104);f.Receive();TickChatTravel();Check(teleported==1&&teleportCost==0,"A fresh free native destination is allowed");});
}
int main(){using namespace fake;try{
    Initialize();InstallChatTravel();InstallMapTravel();InstallSoulGroupMenu();soul::InstallSpell();
    MapTravelTests();
    {Fixture f;routePending=true;StartChatTravel({10,10,1});Check(!quotes,"Pending route must not request a price");
      TickChatTravelPlanning();auto slices=routeTicks;TickChatTravel();TickChatTravelPlanning();
      Check(routeTicks==slices,"Frame and normal pumps share a single planning-slice gate");
      Sleep(25);routePending=false;TickChatTravelPlanning();
      Check(quotes==1,"Planning advances without waiting for the 200ms mod pump");
      f.Price();TickChatTravelPlanning();Check(!teleported,"Fast planning pump does not advance network phases");
      TickChatTravel();Check(teleported==1,"Normal pump still dispatches the verified teleport");}
    {Fixture f;Get(monsterInfos.at(1)).numbers["boss"]=0;Get(monsterInfos.at(1)).fields["souls"]=List();createHook(f.Maker(),nullptr);Check(Get(Get(lastMenu).fields["rows"]).items.size()==2&&inventoryReads==0&&equipped==0,"Ordinary Piou group must retain only native menu rows without inventory access");}
    {Fixture f;Get(monsterInfos.at(1)).fields["souls"]=List();createHook(f.Maker(),nullptr);Check(Get(Get(lastMenu).fields["rows"]).items.size()==2,"Boss without capture definition must not offer equip");}
    {Fixture f;Get(monsterInfos.at(1)).numbers["forbidden"]=1;createHook(f.Maker(),nullptr);Check(Get(Get(lastMenu).fields["rows"]).items.size()==2,"Forbidden boss must not offer equip");}
    {Fixture f;monsterInfos.clear();createHook(f.Maker(),nullptr);Check(Get(Get(lastMenu).fields["rows"]).items.size()==2,"Unknown monster metadata must not offer equip");}
    {Fixture f;auto data=monsterInfos.at(1);Get(data).numbers["boss"]=0;Get(data).numbers["arch"]=1;createHook(f.Maker(),nullptr);Check(Get(Get(lastMenu).fields["rows"]).items.size()==3,"Archimonster retains exact-tier menu action");auto callback=Get(Get(lastView).fields["data"]).fields["callback"];Get(data).numbers["arch"]=0;HandleSoulGroupMenuCallback(callback,lastView);Check(equipped==0,"Stale action must recheck capture eligibility before equipping");}
    {Fixture f;StartChatTravel({10,10,1});Check(entered==0&&interacted==0&&quotes==1&&requestedQuote==104&&teleported==0,"Direct native quote for closest known same-world zaap without Havenbag");TickChatTravel();f.Receive();f.Price(25,101);TickChatTravel();Check(quotes==1&&teleported==0,"Cached lists and another map's quote cannot authorize direct TP; no polling requests");f.Price();Check(teleported==0,"Price receiver must not dispatch reentrantly");TickChatTravel();Check(teleported==1&&teleportMap==104&&teleportSource==5&&teleportCost==25&&walked==0,"Native direct source and fresh cost");TickChatTravel();Check(walked==0,"Wait for actual arrival");Get(currentMap).numbers["m_mapId"]=104;TickChatTravel();TickChatTravel();Check(walked==1&&walkedTo==ChatTpTarget{10,10,1},"Exactly one onward route");}
    {Fixture f;bag=true;StartChatTravel({10,10,1});Check(interacted==1&&quotes==0&&entered==0,"Already in Havenbag retains enabled native zaap");TickChatTravel();Check(teleported==0,"Cached list cannot authorize teleport");f.Receive();Check(teleported==0,"Receiver hook must not dispatch travel reentrantly");TickChatTravel();Check(teleported==1&&teleportMap==104&&teleportSource==3,"Havenbag list source preserved");Get(currentMap).numbers["m_mapId"]=104;TickChatTravel();Check(walked==0,"Wait for exit from Havenbag");bag=false;TickChatTravel();TickChatTravel();Check(walked==1,"Onward route after Havenbag arrival");}
    {Fixture f;synchronousQuote=true;StartChatTravel({10,10,1});Check(quotes==1&&teleported==0,"Synchronous quote records only");TickChatTravel();TickChatTravel();Check(teleported==1,"Synchronous response dispatched once");}
    {Fixture f;StartChatTravel({10,10,1});f.Price(0);TickChatTravel();Check(teleported==0&&entered==0,"Unavailable direct price never forces departure");}
    {Fixture f;StartChatTravel({10,10,1});f.Price(101);TickChatTravel();Check(teleported==0&&entered==0,"Insufficient fresh funds never falls back to another movement");}
    {Fixture f;StartChatTravel({10,10,1});f.Price();directKnown=false;TickChatTravel();Check(teleported==0,"Recheck native known destination at dispatch");}
    {Fixture f;StartChatTravel({10,10,1});f.Price();eligible=false;TickChatTravel();Check(teleported==0,"Recheck native travel eligibility");}
    {Fixture f;StartChatTravel({10,10,1});f.Price();Get(currentMap).numbers["m_mapId"]=11;TickChatTravel();Check(teleported==0,"Price invalid after departure map changes");}
    {Fixture f;StartChatTravel({10,10,1});f.Price();services["fkc"]=Make("ReplacementCartography");TickChatTravel();Check(teleported==0,"Replaced native cartography cancels");}
    {Fixture f;StartChatTravel({10,10,1});RestoreChatTravel();f.Price();TickChatTravel();Check(teleported==0,"Late quote after explicit cancellation is inert");}
    {Fixture f;Get(localCharacter).fields["dpzi"]=List();Get(localCharacter).fields["otherMaps"]=List({Box<int64_t>(101)});StartChatTravel({10,10,1});Check(quotes==0&&teleported==0&&entered==0,"Unrelated map list must never authorize a zaap departure");}
    {Fixture f;Get(currentMap).numbers["m_mapId"]=104;StartChatTravel({10,10,1});TickChatTravel();TickChatTravel();Check(quotes==0&&teleported==0&&walked==1,"Already at closest zaap: onward route only");}
    {Fixture f;eligible=false;StartChatTravel({1,2,1});Check(entered==0,"Native eligibility gates departure");eligible=true;mapValid=false;StartChatTravel({1,2,1});Check(entered==0,"Unknown coordinates do not depart");}
    {Fixture f;StartChatTravel({1,2,1});StartChatTravel({1,2,1});Check(quotes==1,"Repeated click must not cancel or duplicate the requested trip");f.Price();TickChatTravel();Check(teleported==1,"Trip survives an impatient repeated click");}
    {Fixture f;StartChatTravel({1,2,1});StartChatTravel({7,8,14});f.Price();TickChatTravel();Get(currentMap).numbers["m_mapId"]=104;TickChatTravel();TickChatTravel();Check(walked==1&&walkedTo==ChatTpTarget{7,8,14},"A different link replaces the target and preserves Srambad's world ID");}
    {Fixture f;plannedDeparture=103;StartChatTravel({7,8,14});Check(quotes==1&&requestedQuote==103,"A known zaap on another world layer must reach the native graph comparison");}
    {Fixture f;Info(10,0,0,1);plannedDeparture=10;StartChatTravel({7,8,14});TickChatTravel();TickChatTravel();Check(quotes==0&&teleported==0&&walked==1&&walkedTo.world==14,"Current map participates across connected world layers without a needless teleport");}
    {Fixture f;plannedDeparture=0;StartChatTravel({7,8,14});Check(quotes==0&&teleported==0&&walked==0&&!notices.empty()&&notices.back().find("Srambad")!=std::string::npos,"An inaccessible dimension gets a local explanation, never a guessed world1 teleport");}
    {Fixture f;StartChatTravel({1,2,1});combat=true;bag=true;TickChatTravel();Check(interacted==0,"Combat cancels pending trip");}
    {Fixture f;StartChatTravel({1,2,1});services["fng"]=Make("Replacement");bag=true;TickChatTravel();Check(interacted==0,"Session replacement cancels");}
    {Fixture f;createHook(f.Maker(),nullptr);Check(equipped==0&&Get(Get(lastMenu).fields["rows"]).items.size()==3,"Menu preserves native entries and has no equip side effect");auto item=Get(lastView).fields["data"];Check(Get(item).numbers["icon"]==128,"Native equip icon");auto callback=Get(item).fields["callback"];Check(HandleSoulGroupMenuCallback(callback,lastView)&&equipped==1,"Explicit exact-tier action");HandleSoulGroupMenuCallback(callback,lastView);Check(equipped==1,"Duplicate equip is gated");}
    {Fixture f;createHook(f.Maker(),nullptr);auto callback=Get(Get(lastView).fields["data"]).fields["callback"];entity=Make("ReplacementEntity");HandleSoulGroupMenuCallback(callback,lastView);Check(equipped==0,"Same ID with replaced entity is stale");}
    {Fixture f;inventory.equippedGenericId=9687;createHook(f.Maker(),nullptr);auto item=Get(lastView).fields["data"];Check(Get(item).numbers["disabled"]&&Get(item).numbers["icon"]==91,"Already equipped native state");HandleSoulGroupMenuCallback(Get(item).fields["callback"],lastView);Check(equipped==0,"Disabled row never equips");}
    {Fixture f;createHook(f.Maker(),nullptr);auto callback=Get(Get(lastView).fields["data"]).fields["callback"];auto adjusted=Make("fh");Get(adjusted).numbers={{"dcgq",1},{"dcgs",40}};variants=List({adjusted});Get(group).fields["dqys"]=List({Make("ewe")});HandleSoulGroupMenuCallback(callback,lastView);Check(equipped==0&&Get(variants).items.size()==1,"Party-size tier change rechecked without mutating native variants");createHook(f.Maker(),nullptr);auto item=Get(lastView).fields["data"];Check(Get(item).text.find("50")!=std::string::npos&&Get(item).numbers["disabled"],"Native variant uses exact required tier, not a larger bag stone");}
    {Fixture f;bag=true;Get(Get(localCharacter).fields["money"]).numbers["dcjl"]=0;StartChatTravel({10,10,1});f.Receive();TickChatTravel();Check(teleported==0&&walked==0,"No funds: no teleport and no onward request");}
    {Fixture f;combat=true;bool enabled=false;
        Get(spell).numbers["owner"]=0;
        Check(soul::CaptureSpell(enabled)==spell&&enabled&&cast==0,"Local learned wrapper with default owner0 must be visible before its first cast");
        controlled=8;
        Check(soul::CaptureSpell(enabled)==spell&&!enabled,"Keep the local icon visible when another fighter is controlled");
        spellClick(spell);Check(cast==0,"Controlled ally cannot cast local spell");
        controlled=7;failSpellEligibility=true;
        Check(soul::CaptureSpell(enabled)==spell&&!enabled,"Unavailable cast state must not hide the learned icon");
        spellClick(spell);Check(cast==0,"Unavailable cast state cannot dispatch");failSpellEligibility=false;
        auto casting=services["fsf"];services.erase("fsf");
        Check(soul::CaptureSpell(enabled)==spell&&!enabled,"Late casting service only disables the displayed action");services["fsf"]=casting;
        Get(spell).numbers["owner"]=8;Check(!soul::CaptureSpell(enabled)&&!enabled,"Foreign explicit owner cannot supply the capture icon");
        Get(spell).numbers["owner"]=0;Get(spellList).items.push_back(spell);
        Check(!soul::CaptureSpell(enabled),"Ambiguous list cannot authorize a spell");Get(spellList).items.pop_back();
        Get(spellList).items.clear();Check(!soul::CaptureSpell(enabled),"Unknown/unlearned spell is not synthesized");Get(spellList).items.push_back(spell);
        Get(spell).numbers["owner"]=localId;
        Check(soul::CaptureSpell(enabled)==spell&&enabled&&cast==0,"Owned wrapper remains visible without a cast side effect");
        allowed=false;Check(soul::CaptureSpell(enabled)==spell&&!enabled,"Native PA/cooldowns disable but do not hide");spellClick(spell);Check(cast==0,"Recheck native PA/cooldowns on click");
        allowed=true;fightState.fightIdentity=18;spellClick(spell);Check(cast==0,"New fight rejects stale displayed slot");
        Get(spell).numbers["owner"]=0;
        soul::CaptureSpell(enabled);spellClick(Make("ForeignSpell"));Check(cast==0,"Wrong slot binding cannot cast");
        spellClick(spell);spellClick(spell);Check(cast==1,"Explicit validated selfcast, reentry/duplicate gate");
        Get(spell).numbers["owner"]=localId;
        Check(soul::CaptureSpell(enabled)==spell&&!enabled,"Successful click leaves the icon visible during cooldown");
        fightState.state=soul::CombatState::Ready;Check(!soul::CaptureSpell(enabled),"Placement cannot display a cast action");
    }
    Check(handles.empty(),"All managed roots released");
    chat_travel::Journey trip;trip.Begin({0,0,1},1,100);Check(!trip.Expired(30099)&&trip.Expired(30100),"Bounded phase timeout");
    {Fixture f;bag=true;Get(Get(localCharacter).fields["money"]).numbers["dcjl"]=0;StartChatTravel({10,10,1});f.Receive();TickChatTravel();Check(teleported==0,"Unaffordable choices must not reach route planner");}
    {Fixture f;StartChatTravel({10,10,1});f.Price();TickChatTravel();
      Get(currentMap).numbers["m_mapId"]=104;sceneReady=false;
      TickChatTravel();TickChatTravel();Check(!walked,"Map ID alone is not scene readiness");
      sceneReady=true;loadedMapOverride=10;TickChatTravel();TickChatTravel();Check(!walked,"Reject still-loaded previous map");
      loadedMapOverride=-1;loadingMap=true;TickChatTravel();TickChatTravel();Check(!walked,"Wait for native fade completion");
      loadingMap=false;TickChatTravel();Check(!walked,"One ready sample cannot prematurely dispatch");
      sceneReady=false;TickChatTravel();sceneReady=true;TickChatTravel();Check(!walked,"Readiness loss resets stabilization");
      TickChatTravel();Check(walked==1,"Settled arrival dispatches onward exactly once");
      TickChatTravel();TickChatTravel();Check(walked==1 && notices.back().find("autopilote lancé")!=std::string::npos,"Only actual native route activation confirms success");}
    {Fixture f;acceptRoute=false;StartChatTravel({10,10,1});f.Price();TickChatTravel();
      Get(currentMap).numbers["m_mapId"]=104;TickChatTravel();TickChatTravel();Check(walked==1,"First onward request");
      TickChatTravel();Check(walked==1,"No repeated frame requests");Sleep(1050);
      TickChatTravel();acceptRoute=true;TickChatTravel();TickChatTravel();Check(walked==2,"One retry after unaccepted request, then native confirmation");}
    {Fixture f;acceptRoute=false;StartChatTravel({10,10,1});f.Price();TickChatTravel();
      Get(currentMap).numbers["m_mapId"]=104;TickChatTravel();TickChatTravel();
      auto d=Make("MapInformationData");Get(d).fields["id"]=Box<uint32_t>(123);Get(services["fjs"]).fields["dsjs"]=d;
      routePendingNative=true;TickChatTravel();routePendingNative=false;Sleep(1050);TickChatTravel();TickChatTravel();
      Check(walked==1,"Accepted then cancelled native route must not be retried");}
    {Fixture f;acceptRoute=false;StartChatTravel({10,10,1});f.Price();TickChatTravel();
      Get(currentMap).numbers["m_mapId"]=104;TickChatTravel();TickChatTravel();
      auto d=Make("MapInformationData");Get(d).fields["id"]=Box<uint32_t>(999);Get(services["fjs"]).fields["dsjs"]=d;
      routePendingNative=true;TickChatTravel();Sleep(1050);TickChatTravel();Check(walked==1,"Never replace a different native destination");}
    std::cout<<"PASS: production TP orchestration, confirmed arrival then native onward travel, stale/cancel guards, native group equip and selfcast click validation\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
