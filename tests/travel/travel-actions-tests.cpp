#include "travel/chat-travel.hpp"
#include "travel/map-travel.hpp"
#include "travel/chat-travel-policy.hpp"
#include "travel/chat-route.hpp"
#include "travel/roublard-passage.hpp"
#include "notebook/notebook-native.hpp"
#include "notebook/notebook-panel.hpp"
#include "souls/soul-group-menu.hpp"
#include "souls/soul-spell.hpp"
#include "souls/soul-combat.hpp"
#include "souls/soul-inventory.hpp"
#include "core/module.hpp"
#include "appearance/features.hpp"
#include "ui/options-ui.hpp"
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
void (*mapMouseDownHook)(Obj,Obj,Method)=nullptr;
int nativeMenus=0,nativeWalks=0,nativeAutos=0,nativeMouseDowns=0,stoppedEvents=0; float gameTime=100;
Point nativeCoordinates{},nativeMouse{}; int nativeWorld=-1; bool nativeImmediate=false;
bool sceneReady=true, loadingMap=false, acceptRoute=true, routeRunning=false, routePendingNative=false, followingMember=false;
int loadedMapOverride=-1;
int64_t plannedDeparture=104;
bool routePending=false;
int routeTicks=0;
std::vector<chat_travel::Destination> plannedChoices;
std::vector<std::string> notices,logs;
bool failSpellEligibility=false;
int allowedChecks=0;
int64_t teleportMap=0,walkedMap=0,plannedTarget=0,plannedSource=0; ChatTpTarget resolvedFor{};
int plannedZone=-2; Obj mapCells{}; bool plannedTemple=false;
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
void OriginalMapMouseDown(Obj,Obj,Method){++nativeMouseDowns;}
Obj MouseDown(int button,Point mouse,bool ctrl=false){auto e=Make("MouseDownEvent");Get(e).numbers["button"]=button;Get(e).numbers["ctrl"]=ctrl;Get(e).fields["mouse"]=Box(mouse);return e;}
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
    if (name=="get_time") return Box(gameTime);
    if (name=="bepv") {auto p=*static_cast<Point*>(a[0]);return Box(Point{p.x/10,p.y/10});}
    auto& n=Get(self);
    if (name=="get_button") return Box(int(n.numbers["button"]));
    if (name=="get_ctrlKey") return Box(bool(n.numbers["ctrl"]));
    if (name=="get_mousePosition") return n.fields["mouse"];
    if (name=="StopPropagation") {++stoppedEvents;return nullptr;}
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
    if (name=="xkd") {auto d=Make("ClientMapData");Get(d).fields["id"]=Box<int>(loadedMapOverride<0?int(Get(currentMap).numbers["m_mapId"]):loadedMapOverride);Get(d).fields["cellsData"]=mapCells;return d;}
    if (name=="bcdi") return Box(routeRunning);
    if (name=="zcl") return Box(followingMember);
    if (name=="zhx") return Box(eligible);
    if (name=="zid") {resolvedFor={*static_cast<int*>(a[0]),*static_cast<int*>(a[1]),*static_cast<int*>(a[2])};auto result=List();if(mapValid)for(auto map:resolvedMaps)Get(result).items.push_back(Box<int64_t>(map));return result;}
    if (name=="zdc") return n.fields["money"];
    // zch/0x11EDC80 reads +0xF8, not the known-zaap list at +0x120.
    if (name=="zch") return n.fields["otherMaps"];
    if (name=="zhr") return Box(directKnown);
    if (name=="zcx") return Box<int64_t>(localId);
    if (name=="bajk") return Box<int>(static_cast<int>(n.numbers["cell"]));
    if (name=="GetLinkedRpZone") return Box<int>(static_cast<int>(n.numbers["zone"]));
    if (name=="bepd") {++quotes;requestedQuote=Get(a[0]).numbers["map"];if(synchronousQuote){auto message=Make("hgu");Raw<int64_t>(message,0x28,requestedQuote);Raw<int>(message,0x30,25);quoteHook(self,message,nullptr);}return Box(true);}
    if (name=="yhr") return n.fields["event"];
    if (name=="bjfd") {++entered;return nullptr;}
    if (name=="qyb") return interaction;
    if (name=="OnSkillClicked") {Check(a[1]==Get(interaction).items[0]&&*static_cast<unsigned*>(a[2])==888&&*static_cast<int*>(a[3])==-1,"Native enabled interactive ABI");++interacted;if(synchronousList){auto msg=Make("hgq");Raw<int>(msg,0x28,3);receiveHook(services["fpf"],msg,nullptr);}return nullptr;}
    if (name=="get_subArea") return n.fields["area"];
    if (name=="get_worldmapId") return Box(static_cast<int>(n.numbers["world"]));
    if (name=="bcla") {++teleported;teleportMap=Get(a[0]).numbers["map"];teleportSource=static_cast<int>(Get(a[0]).numbers["source"]);teleportCost=static_cast<int>(Get(a[0]).numbers["cost"]);return Box(true);}
    // faw.bcdq: the game's own autopilot request handler, toward an exact map.
    if (name=="bcdq") {Check(self==services["fjs"]&&*static_cast<bool*>(a[1]),"Explicit TP must resume native autopilot without confirmation");++walked;
        walkedMap=*static_cast<int64_t*>(a[0]);
        if(acceptRoute){auto d=Make("MapInformationData");Get(d).fields["id"]=Box<uint32_t>(static_cast<uint32_t>(walkedMap));Get(services["fjs"]).fields["dsjs"]=d;routeRunning=true;}
        return nullptr;}
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
    api.class_get_field_from_name=[](Klass,const char* name)->void*{
        static size_t dugt=0x80,cellsData=0x80,cellNumber=0x10;const std::string field(name);
        return field=="dugt"?&dugt:field=="cellsData"?&cellsData:field=="cellNumber"?static_cast<void*>(&cellNumber):nullptr;};
    api.field_get_offset=[](void* field)->size_t{return *static_cast<size_t*>(field);};
}
struct Fixture {
    Module owner{"offline"};
    Fixture() {
        services.clear();mapInfos.clear();module=&owner;owner.ready=true;owner.servicesReady=true;owner.settings.soulReminder=true;owner.cached.reset(Make("Cached"));owner.compositor.reset(Make("Compositor"));owner.fight.reset(Make("Fight"));
        monsterInfos.clear();inventoryReads=0;auto monster=Make("MonsterData");Get(monster).numbers["boss"]=1;Get(monster).fields["souls"]=List({Make("MonsterSoulData")});monsterInfos[1]=monster;
        combat=false;ready=true;allowed=true;bag=false;eligible=true;mapValid=true;controlled=localId=7;fightState={soul::CombatState::Fighting,17,51};inventory={true,0,{{55,9687,3,true}}};
        entered=interacted=teleported=walked=cast=equipped=quotes=0;teleportSource=teleportCost=-1;requestedQuote=0;directKnown=true;synchronousQuote=false;
        synchronousList=failAppend=false;resolvedMaps={123};nativeMenus=nativeWalks=nativeAutos=nativeMouseDowns=stoppedEvents=0;gameTime=100;
        plannedDeparture=104;plannedChoices.clear();notices.clear();logs.clear();routePending=false;routeTicks=0;
        walkedMap=plannedTarget=plannedSource=0;resolvedFor={};plannedZone=-2;mapCells=nullptr;plannedTemple=false;
        for(auto name:{"fng","fpf","fkc","frq","fro","frv","fsm","fmz","fnm","fnk","fnr","fjx","frs","fsf","fvq","fns","fsr","fmo"}) services[name]=Make(name);
        services["ham"]=Make("hal");services["fjs"]=Make("faw");services["fpm"]=Make("epf");
        sceneReady=acceptRoute=true;loadingMap=routeRunning=routePendingNative=followingMember=false;loadedMapOverride=-1;
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
    Obj MapOwner(int world=1,const char* type="CartographyBannerView") {auto cartographer=Make(type),root=Make("MapRoot"),data=Make("WorldMapData");Get(root).fields["panel"]=root;Get(data).fields["id"]=Box(world);Get(cartographer).fields={{"dugy",localCharacter},{"dugv",services["fkc"]},{"dugq",root},{"world",data},{"dugt",Box<float>(-1000.f)}};return cartographer;}
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
    else if(rva==0x1BF88F0){fake::mapMouseDownHook=reinterpret_cast<decltype(fake::mapMouseDownHook)>(replacement);*original=reinterpret_cast<void*>(&fake::OriginalMapMouseDown);}
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
// The shortcuts chat-travel offers (the Roublards' secret passage), and the one
// this fake comparison reports as taken (-1: none).
std::vector<Shortcut> passed;int chosen=-1;
void SetShortcuts(std::vector<Shortcut> shortcuts) {passed=std::move(shortcuts);}
void Install() {}
void Clear() {result={Status::Unreachable};}
void Begin(std::vector<chat_travel::Destination> choices,int64_t target,int64_t source,int zone,bool temple) {
    fake::Check(target>0,"Planner requires the resolved native destination map");
    fake::plannedChoices=choices;fake::plannedTarget=target;fake::plannedSource=source;fake::plannedZone=zone;fake::plannedTemple=temple;
    Clear();for(auto choice:choices)if(choice.map==fake::plannedDeparture){result={Status::Found,choice,3};result.shortcut=chosen;return;}
}
Result Tick(){++fake::routeTicks;return fake::routePending?Result{}:result;}
}
// « Ajouter aux positions »: the notebook's side (notebook-native), recorded.
namespace fake {
struct AddedPosition {int x=0,y=0,world=0;int64_t map=0;};
std::vector<AddedPosition> addedPositions;bool duplicatePosition=false;std::string positionError,positionTitle;int panelChecks=0;
void PositionReset(const std::string& title){addedPositions.clear();duplicatePosition=false;positionError.clear();positionTitle=title;panelChecks=0;}
}
void CheckNotebookPanel(){++fake::panelChecks;}
namespace notebook {
Added AddMapPosition(int x,int y,int world,int64_t map){
    if(!fake::positionError.empty())throw std::runtime_error(fake::positionError);
    Entry entry;entry.kind=Kind::Position;entry.x=x;entry.y=y;entry.world=world;entry.map=map;entry.title=fake::positionTitle;
    if(fake::duplicatePosition)return {entry,false};
    fake::addedPositions.push_back({x,y,world,map});entry.id=fake::addedPositions.size();return {entry,true};
}
}
void MapTravelTests() {
    using namespace fake;
    const auto run=[](auto scenario) {const auto before=handles.size();{Fixture f;scenario(f);}Check(handles.size()==before,"Map travel must release managed references");};
    // Map double-click: the game's mouse-down handler carries the travel itself.
    run([](Fixture& f){auto owner=f.MapOwner();
        mapMouseDownHook(owner,MouseDown(0,{100,100}),nullptr);
        Check(nativeMouseDowns==1&&!stoppedEvents&&!quotes&&!interacted&&!walked,"A single left click stays the game's own (drag start, selection)");
        Get(owner).fields["dugt"]=Box<float>(gameTime-0.3f);
        mapMouseDownHook(owner,MouseDown(0,{100,100}),nullptr);
        Check(nativeMouseDowns==1&&stoppedEvents==1&&quotes==1&&requestedQuote==104&&!nativeAutos&&!nativeWalks&&!walked,"A double-click within the game's window starts the TP planner, never the native handler");
        Check(std::any_of(logs.begin(),logs.end(),[](const std::string& l){return l.find("MAP_TRAVEL_ROUTE entry=double-click x=10 y=10 world=1")!=std::string::npos;}),"The map coordinates come from the game's own pixel conversion");});
    run([](Fixture& f){auto owner=f.MapOwner();Get(owner).fields["dugt"]=Box<float>(gameTime-0.3f);
        mapMouseDownHook(owner,MouseDown(0,{100,100},true),nullptr);Check(nativeMouseDowns==1&&!stoppedEvents&&!quotes,"Ctrl + double-click keeps the game's own direct autopilot");
        mapMouseDownHook(owner,MouseDown(1,{100,100}),nullptr);Check(nativeMouseDowns==2&&!quotes,"Right clicks (context menu) are untouched");
        Get(owner).fields["dugt"]=Box<float>(gameTime-0.5f);mapMouseDownHook(owner,MouseDown(0,{100,100}),nullptr);Check(nativeMouseDowns==3&&!quotes,"A second click after the window is a plain click");
        combat=true;Get(owner).fields["dugt"]=Box<float>(gameTime-0.3f);mapMouseDownHook(owner,MouseDown(0,{100,100}),nullptr);Check(nativeMouseDowns==4&&!quotes,"In combat the handler stays native");combat=false;
        services["fkc"]=Make("OtherCartography");mapMouseDownHook(owner,MouseDown(0,{100,100}),nullptr);Check(nativeMouseDowns==5&&!quotes,"A stale map owner stays native");});
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
        Check(walked==1&&resolvedFor==ChatTpTarget{-10,11,14}&&walkedMap==123&&!nativeWalks&&!nativeAutos,
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
    run([](Fixture& f){bag=true;f.owner.settings.mapAddPosition=false;resolvedMaps={104};auto owner=f.MapOwner();auto menu=f.MapMenu(owner);
        auto& rows=Get(Get(menu).fields["rows"]).items;Check(rows.size()==3&&!nativeMenus&&!interacted&&!teleported&&!quotes,"Opening menu appends one row without travel or quote");
        auto callback=Get(rows.back()).fields["callback"];auto view=lastView;Check(Get(rows.back()).numbers["icon"]==304,"Native teleport icon");
        Check(HandleMapTravelCallback(callback,view)&&interacted==1&&!teleported,"Explicit row click interacts with enabled Havenbag zaap");
        HandleMapTravelCallback(callback,view);TickChatTravel();Check(interacted==1&&!teleported,"Repeated click and cached list cannot authorize departure");
        f.Receive();Check(!teleported,"Receiving fresh server list cannot dispatch from receive hook");TickChatTravel();
        Check(teleported==1&&teleportMap==104&&teleportSource==3&&teleportCost==20&&!walked&&!quotes&&routeTicks==0,"Exact selected zaap uses fresh native list source and cost without planning or direct quote");
        Get(currentMap).numbers["m_mapId"]=104;bag=false;TickChatTravel();TickChatTravel();Check(!walked,"Exact zaap click never starts autopilot");TickMapTravel();Check(rows.size()==2,"Only owned row removed after leaving Havre-Sac");});
    run([](Fixture& f){bag=true;eligible=false;services["fjs"]=nullptr;StartHavenbagZaap(104);f.Receive();TickChatTravel();Check(teleported==1&&!walked,"Exact native zaap does not depend on automatic route eligibility");});
    run([](Fixture& f){bag=true;f.owner.settings.mapAddPosition=false;resolvedMaps={104,104,104};auto m=f.MapMenu(f.MapOwner());Check(Get(Get(m).fields["rows"]).items.size()==3,"Duplicate native map candidates yield one owned row");});
    for(int invalid=0;invalid<7;++invalid)run([invalid](Fixture& f){bag=true;f.owner.settings.mapAddPosition=false;resolvedMaps={104};auto owner=f.MapOwner();auto m=f.MapMenu(owner);auto row=Get(Get(m).fields["rows"]).items.back();auto cb=Get(row).fields["callback"];auto view=lastView;
        if(invalid==0)bag=false;
        if(invalid==1)combat=true;
        if(invalid==2)services["fng"]=Make("AnotherCharacter");
        if(invalid==3)Get(currentMap).numbers["m_mapId"]=22;
        if(invalid==4)Get(owner).fields["world"]=Make("OtherWorld");
        if(invalid==5)Get(view).fields["panel"]=nullptr;
        if(invalid==6)directKnown=false;
        Check(HandleMapTravelCallback(cb,view)&&!interacted&&!teleported,"Stale/closed/unknown/changed map callback is consumed without action");});
    run([](Fixture& f){bag=true;f.owner.settings.mapAddPosition=false;resolvedMaps={104};auto owner=f.MapOwner();auto first=f.MapMenu(owner);auto cb=Get(Get(Get(first).fields["rows"]).items.back()).fields["callback"];auto view=lastView;
        f.MapMenu(owner);Check(!HandleMapTravelCallback(cb,view)&&!interacted,"Replaced owned callbacks stay inert with null native actions");
        Check(Get(Get(first).fields["rows"]).items.size()==2,"Old menu keeps its native rows");});
    run([](Fixture& f){bag=true;resolvedMaps={104};failAppend=true;auto menu=f.MapMenu(f.MapOwner());Check(Get(Get(menu).fields["rows"]).items.size()==2&&!interacted,"Failed native append cleans its partial row without travel");});
    run([](Fixture& f){bag=true;f.owner.settings.mapAddPosition=false;directKnown=false;auto menu=f.MapMenu(f.MapOwner());Check(Get(Get(menu).fields["rows"]).items.size()==2&&!interacted,"No entry for unknown zaap");});
    for(int action=0;action<2;++action)run([action](Fixture& f){bag=true;auto owner=f.MapOwner();
        if(action)mapAutoHook(owner,{10,10},nullptr);else mapWalkHook(owner,{10,10},1,true,nullptr);
        Check(interacted==1&&!nativeAutos&&!nativeWalks&&!quotes,"Both native map-travel entrypoints use Havenbag interactive");f.Receive();TickChatTravel();
        Check(teleported==1&&teleportMap==104&&teleportSource==3&&!walked,"Route chooses accessible zaap first");
        Get(currentMap).numbers["m_mapId"]=104;TickChatTravel();Check(!walked,"No walking while still in Havenbag context");bag=false;TickChatTravel();TickChatTravel();TickChatTravel();
        Check(walked==1&&resolvedFor==ChatTpTarget{10,10,1}&&walkedMap==123,"One native no-confirm onward route after confirmed arrival");});
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
    // « Remplacer l’autopilote du jeu »: on by default, so every scenario above is the
    // option's default behaviour. Off, the three hooked travel entries are the game's
    // own again on both map views, in or out of the Havre-Sac: original handler with
    // its own arguments, no planner, quote, zaap or route. Switching it back on
    // applies to the very next action, without a restart.
    run([](Fixture& f){Check(f.owner.settings.mapTravelPlanner,"The mod's travel replaces the game's autopilot by default");});
    for(const char* type:{"CartographyBannerView","CartographyMapView"}) for(int bagged=0;bagged<2;++bagged)run([type,bagged](Fixture& f){
        bag=bagged==1;auto owner=f.MapOwner(14,type);f.owner.settings.mapTravelPlanner=false;
        mapWalkHook(owner,{-10.8f,11.9f},14,true,nullptr);
        Check(nativeWalks==1&&nativeCoordinates.x==-10.8f&&nativeCoordinates.y==11.9f&&nativeWorld==14&&nativeImmediate,
            "Option off: the right-click travel entry reaches the game's handler with all its arguments");
        mapAutoHook(owner,{7.5f,8.5f},nullptr);
        Check(nativeAutos==1&&nativeCoordinates.x==7.5f&&nativeCoordinates.y==8.5f,"Option off: the native auto-travel call reaches the game's handler");
        Get(owner).fields["dugt"]=Box<float>(gameTime-0.3f);mapMouseDownHook(owner,MouseDown(0,{100,100}),nullptr);
        Check(nativeMouseDowns==1&&!stoppedEvents,"Option off: the double-click reaches the game's own handler and autopilot, event untouched");
        TickChatTravel();TickChatTravel();
        Check(!quotes&&!interacted&&!teleported&&!walked&&!entered&&!routeTicks&&notices.empty(),"Option off never starts the mod's travel");
        Check(std::none_of(logs.begin(),logs.end(),[](const std::string& l){return l.find("MAP_TRAVEL_ROUTE")!=std::string::npos;}),"Option off: no mod route is attempted");
        f.owner.settings.mapTravelPlanner=true;mapMouseDownHook(owner,MouseDown(0,{100,100}),nullptr);
        Check(nativeMouseDowns==1&&stoppedEvents==1&&(bagged?interacted==1&&!quotes:quotes==1&&requestedQuote==104),
            "Switched back on, the very next double-click is the mod's travel again, without a restart");});
    for(int action=0;action<2;++action)run([action](Fixture& f){auto owner=f.MapOwner();f.owner.settings.mapTravelPlanner=false;
        if(action)mapAutoHook(owner,{10,10},nullptr);else mapWalkHook(owner,{10,10},1,true,nullptr);
        Check(nativeWalks==1-action&&nativeAutos==action&&!quotes&&!interacted,"Option off: each travel entry is the game's");
        f.owner.settings.mapTravelPlanner=true;
        if(action)mapAutoHook(owner,{10,10},nullptr);else mapWalkHook(owner,{10,10},1,true,nullptr);
        Check(nativeWalks==1-action&&nativeAutos==action&&quotes==1&&requestedQuote==104,"Switched back on, the same entry is the mod's travel again at once");});
    run([](Fixture& f){bag=true;resolvedMaps={104};f.owner.settings.mapTravelPlanner=false;f.owner.settings.mapAddPosition=false;auto menu=f.MapMenu(f.MapOwner());
        auto& rows=Get(Get(menu).fields["rows"]).items;
        Check(rows.size()==3&&!nativeMenus&&Get(rows.back()).text=="Se téléporter à ce zaap (Havre-Sac)"&&Get(rows.back()).numbers["icon"]==304,
            "Option off keeps the Havre-Sac zaap row of the map menu");
        Check(HandleMapTravelCallback(Get(rows.back()).fields["callback"],lastView)&&interacted==1&&!teleported,"Option off keeps the Havre-Sac zaap click");
        f.Receive();TickChatTravel();Check(teleported==1&&teleportMap==104&&!walked&&!quotes,"Option off: the chosen Havre-Sac zaap still departs on its fresh list");});
    // « Ajouter aux positions » (Settings::mapAddPosition, on by default): the
    // right-clicked cell into the notebook, on both map views, in or out of the
    // Havre-Sac, after the teleport rows; told in the local chat. The notebook's
    // side (notebook::AddMapPosition) is recorded above.
    run([](Fixture& f){Check(f.owner.settings.mapAddPosition,"« Ajouter aux positions » is on by default");});
    for(const char* type:{"CartographyBannerView","CartographyMapView"})run([type](Fixture& f){
        PositionReset(std::string("Zone ")+type);resolvedMaps={83104256,82313729};
        auto owner=f.MapOwner(1,type);auto menu=f.MapMenu(owner);auto& rows=Get(Get(menu).fields["rows"]).items;
        Check(nativeMenus==1&&rows.size()==3&&Get(rows.back()).text=="Ajouter aux positions"&&Get(rows.back()).numbers["icon"]==225&&!Get(rows.back()).numbers["disabled"],
            "Outside the Havre-Sac: the game's own fragment, then « Ajouter aux positions »");
        Check(addedPositions.empty()&&!quotes&&!interacted&&notices.empty(),"Opening the menu adds and starts nothing");
        auto callback=Get(rows.back()).fields["callback"];auto view=lastView;
        Check(HandleMapTravelCallback(callback,view)&&addedPositions.size()==1&&addedPositions[0].x==9&&addedPositions[0].y==10&&addedPositions[0].world==1&&addedPositions[0].map==82313729,
            "The click adds the right-clicked cell, in the displayed world, with the game's own map for it");
        Check(resolvedFor==ChatTpTarget{9,10,1}&&!quotes&&!interacted&&!teleported&&!walked,"Adding never travels");
        Check(!notices.empty()&&notices.back()==std::string("Carnet : [9,10] « Zone ")+type+" » ajoutée aux positions."&&panelChecks==1,"Told in the local chat; an open notebook lists it at once");
        Check(HandleMapTravelCallback(callback,view)&&addedPositions.size()==1,"A second click on the same row adds nothing more");});
    run([](Fixture& f){PositionReset("Zone du Havre-Sac");bag=true;resolvedMaps={104};
        auto menu=f.MapMenu(f.MapOwner());auto& rows=Get(Get(menu).fields["rows"]).items;
        Check(!nativeMenus&&rows.size()==4&&Get(rows[2]).text=="Se téléporter à ce zaap (Havre-Sac)"&&Get(rows[3]).text=="Ajouter aux positions","In the Havre-Sac: its zaap rows, then « Ajouter aux positions »");
        Check(HandleMapTravelCallback(Get(rows[3]).fields["callback"],lastView)&&addedPositions.size()==1&&addedPositions[0].map==104&&!interacted&&!teleported,"The position row adds the cell, no zaap trip");
        Get(currentMap).numbers["m_mapId"]=104;bag=false;TickMapTravel();
        Check(rows.size()==3&&Get(rows.back()).text=="Ajouter aux positions","Leaving the Havre-Sac retires the zaap rows only");});
    for(int bagged=0;bagged<2;++bagged)run([bagged](Fixture& f){PositionReset("Zone");bag=bagged==1;resolvedMaps={104};f.owner.settings.mapAddPosition=false;
        auto menu=f.MapMenu(f.MapOwner());const auto rows=Get(Get(menu).fields["rows"]).items;
        Check(rows.size()==(bagged?3u:2u)&&std::none_of(rows.begin(),rows.end(),[](Obj row){return Get(row).text=="Ajouter aux positions";}),"Option off: no « Ajouter aux positions » row");
        f.owner.settings.mapAddPosition=true;menu=f.MapMenu(f.MapOwner());
        Check(Get(Get(menu).fields["rows"]).items.size()==(bagged?4u:3u),"Switched back on, the very next right-click has it again");});
    run([](Fixture& f){PositionReset("Zone connue");duplicatePosition=true;auto menu=f.MapMenu(f.MapOwner());auto rows=Get(Get(menu).fields["rows"]).items;
        Check(HandleMapTravelCallback(Get(rows.back()).fields["callback"],lastView)&&!notices.empty()&&notices.back()=="Carnet : [9,10] est déjà dans tes positions (« Zone connue »).",
            "A cell already in the notebook is told, not added twice");Check(!panelChecks,"Nothing new for the notebook to list");});
    run([](Fixture& f){PositionReset("Zone");positionError="Le carnet est plein (256 entrées).";auto menu=f.MapMenu(f.MapOwner());auto rows=Get(Get(menu).fields["rows"]).items;
        Check(HandleMapTravelCallback(Get(rows.back()).fields["callback"],lastView)&&addedPositions.empty()&&!notices.empty()&&notices.back()=="Carnet : Le carnet est plein (256 entrées).",
            "A refused save is told in the local chat");});
    run([](Fixture& f){PositionReset("Hors carte");mapValid=false;auto menu=f.MapMenu(f.MapOwner());auto rows=Get(Get(menu).fields["rows"]).items;
        Check(HandleMapTravelCallback(Get(rows.back()).fields["callback"],lastView)&&addedPositions.size()==1&&addedPositions[0].map==0,"A cell without a map of the game is still added, by its coordinates");});
    for(int invalid=0;invalid<6;++invalid)run([invalid](Fixture& f){PositionReset("Zone");auto owner=f.MapOwner();auto menu=f.MapMenu(owner);
        auto cb=Get(Get(Get(menu).fields["rows"]).items.back()).fields["callback"];auto view=lastView;
        if(invalid==0)combat=true;
        if(invalid==1)services["fng"]=Make("AnotherCharacter");
        if(invalid==2)Get(owner).fields["world"]=Make("OtherWorld");
        if(invalid==3)Get(view).fields["panel"]=nullptr;
        if(invalid==4)Get(Get(owner).fields["dugq"]).fields["panel"]=nullptr;
        if(invalid==5)f.owner.cached.reset(Make("OtherCache"));
        Check(HandleMapTravelCallback(cb,view)&&addedPositions.empty()&&notices.empty(),"A stale, closed or changed map menu row is consumed without adding");});
    run([](Fixture& f){PositionReset("Zone");combat=true;auto menu=f.MapMenu(f.MapOwner());
        Check(nativeMenus==1&&Get(Get(menu).fields["rows"]).items.size()==2,"In combat the menu stays the game's");});
    run([](Fixture& f){PositionReset("Zone");failAppend=true;auto menu=f.MapMenu(f.MapOwner());
        Check(nativeMenus==1&&Get(Get(menu).fields["rows"]).items.size()==2,"A failed append leaves the game's rows only");});
    run([](Fixture& f){PositionReset("Zone");auto menu=f.MapMenu(f.MapOwner());auto& rows=Get(Get(menu).fields["rows"]).items;
        TickMapTravel();Check(rows.size()==3,"The shown menu keeps its row");
        Get(Get(menu).fields["dofusContextualMenuRoot"]).fields["panel"]=nullptr;TickMapTravel();Check(rows.size()==2,"Closed, the menu loses its owned row");});
}
// r110: already there, the player's own zone, the Havre-Sac start, and the trip
// toward the game's own destination map (the last map esg.zid gives).
void TravelFixTests() {
    using namespace fake;
    const auto run=[](auto scenario) {const auto before=handles.size();{Fixture f;scenario(f);}Check(handles.size()==before,"Travel must release managed references");};
    const auto logged=[](const std::string& text) {return std::any_of(logs.begin(),logs.end(),[&](const std::string& l){return l.find(text)!=std::string::npos;});};
    const auto noticed=[](const std::string& text) {return std::any_of(notices.begin(),notices.end(),[&](const std::string& n){return n.find(text)!=std::string::npos;});};
    const auto idle=[] {return !quotes&&!teleported&&!walked&&!interacted&&!entered&&!routeTicks&&!plannedTarget;};
    // The user's report: on [9,17] (82313729), a TP to [9,17] took the zaap of
    // [10,22] and walked back. esg.zid gives both maps there, the game's choice last.
    run([&](Fixture&){Get(currentMap).numbers["m_mapId"]=82313729;resolvedMaps={83104256,82313729};
        StartChatTravel({9,17,1});
        Check(idle(),"Already on the destination map: no quote, zaap, route comparison or trip");
        Check(!notices.empty()&&notices.back()=="TP : tu es déjà sur [9,17].","Already there is told in the local chat");
        Check(logged("CHAT_TRAVEL_RESOLVED source=82313729 target=82313729 world=1 maps=83104256,82313729")&&
            logged("CHAT_TRAVEL_ALREADY_THERE map=82313729 x=9 y=17 world=1"),"Already there is logged with every map of the coordinates");
        TickChatTravel();TickChatTravel();Check(idle(),"Nothing stays pending after an already-there click");
        resolvedMaps={123};Get(currentMap).numbers["m_mapId"]=10;StartChatTravel({10,10,1});
        Check(quotes==1&&requestedQuote==104,"The next TP plans normally");});
    // Another map at the same coordinates (a zaap's own layer, an interior) is not
    // the destination: a trip to the game's choice is planned.
    run([&](Fixture&){resolvedMaps={10,123};StartChatTravel({9,10,1});
        Check(quotes==1&&requestedQuote==104&&plannedTarget==123&&!logged("CHAT_TRAVEL_ALREADY_THERE")&&!noticed("déjà sur"),
            "Standing on another map of the coordinates plans a trip to the game's choice");});
    // Map double-click, « Voyager jusqu’à cette position » and the auto entry.
    for(int action=0;action<3;++action)run([&,action](Fixture& f){
        Get(currentMap).numbers["m_mapId"]=82313729;resolvedMaps={83104256,82313729};auto owner=f.MapOwner();
        const float x=30.f+action;const auto expected="TP : tu es déjà sur ["+std::to_string(30+action)+",17].";
        if(action==0){Get(owner).fields["dugt"]=Box<float>(gameTime-0.3f);mapMouseDownHook(owner,MouseDown(0,{x*10,170}),nullptr);}
        else if(action==1)mapWalkHook(owner,{x+0.4f,17.6f},1,true,nullptr);
        else mapAutoHook(owner,{x,17},nullptr);
        Check(idle()&&!nativeWalks&&!nativeAutos&&!nativeMouseDowns,"Map entries on the current position neither travel nor fall back to the game's autopilot");
        Check(!notices.empty()&&notices.back()==expected,"Map entries tell already there");
        Check(resolvedFor==ChatTpTarget{30+action,17,1}&&logged("CHAT_TRAVEL_ALREADY_THERE map=82313729 x="+std::to_string(30+action)+" y=17 world=1"),
            "Map entries resolve the clicked position and log already there");
        Check(action!=0||stoppedEvents==1,"The double-click is consumed");});
    // The user's double-click on [10,17] from 82313729: with the current map as a
    // departure, the trip walks there, with no zaap request.
    run([&](Fixture&){Get(currentMap).numbers["m_mapId"]=82313729;Info(82313729,9,17,1);plannedDeparture=82313729;resolvedMaps={83101186,82314241};
        StartChatTravel({10,17,1});TickChatTravel();TickChatTravel();
        Check(plannedSource==82313729&&plannedTarget==82314241&&
            std::any_of(plannedChoices.begin(),plannedChoices.end(),[](const auto& c){return c.map==82313729;}),"The current map is a departure toward the game's map of [10,17]");
        Check(!quotes&&!teleported&&!interacted&&walked==1&&walkedMap==82314241,"Walking from the current map to [10,17], no zaap");});
    // Several maps at the coordinates: all logged; the route search, the onward
    // trip and its confirmation use only the game's choice (the last).
    run([&](Fixture& f){resolvedMaps={555,123};StartChatTravel({10,10,1});
        Check(plannedTarget==123&&logged("CHAT_TRAVEL_RESOLVED source=10 target=123 world=1 maps=555,123"),"The route search targets only the game's choice");
        f.Price();TickChatTravel();Get(currentMap).numbers["m_mapId"]=104;TickChatTravel();TickChatTravel();
        Check(walked==1&&walkedMap==123&&logged("CHAT_TRAVEL_ONWARD_REQUEST attempt=1 target=123"),"The onward native trip targets the game's choice");
        TickChatTravel();Check(logged("CHAT_TRAVEL_ONWARD_ACTIVE target=123 following=0"),"The native trip toward it is confirmed");});
    // Review case (Otomaï): a zaap on another layer of the destination coordinates
    // is a departure like any other; arriving there does not end the trip.
    run([&](Fixture& f){resolvedMaps={104,123};StartChatTravel({10,10,1});f.Price();TickChatTravel();
        Check(plannedTarget==123&&teleported==1&&teleportMap==104,"The zaap layer is only a departure");
        Get(currentMap).numbers["m_mapId"]=104;TickChatTravel();TickChatTravel();
        Check(walked==1&&walkedMap==123&&!noticed("destination atteinte"),"Arriving on the zaap layer continues to the game's choice");});
    // Inside the Havre-Sac, its own coordinates ([0,0] world 1 for most bags) are
    // not « déjà sur »: the trip always departs through the Havre-Sac zaap.
    for(int order=0;order<2;++order)run([&,order](Fixture& f){bag=true;resolvedMaps=order?std::vector<int64_t>{123,10}:std::vector<int64_t>{10,123};
        StartChatTravel({0,0,1});
        Check(interacted==1&&!quotes&&!logged("CHAT_TRAVEL_ALREADY_THERE")&&!noticed("déjà sur"),"A Havre-Sac start uses its zaap even at the bag's own coordinates");
        f.Receive();TickChatTravel();Check(teleported==1&&teleportSource==3,"The Havre-Sac trip departs on the fresh zaap list");});
    // The player's own zone, read like the game's world path service: the local
    // entity's cell in the loaded map's cells, then that cell's roleplay zone.
    const auto zones=[] {
        Info(10,9,10,1);Get(localCharacter).fields["dpzj"]=Make("z");services["fso"]=Make("evv");Get(entity).numbers["cell"]=2;mapCells=List();
        for(int i=0;i<4;++i){auto cell=Make("ClientCellData");Get(cell).fields["cellNumber"]=Box<uint32_t>(i);Get(cell).numbers["zone"]=i==2?3:1;Get(mapCells).items.push_back(cell);}
    };
    run([&](Fixture&){zones();StartChatTravel({10,10,1});
        Check(plannedZone==3&&plannedSource==10&&logged("CHAT_TRAVEL_HERE_ZONE map=10 cell=2 zone=3")&&quotes==1,"The player's own zone reaches the route search");});
    for(int invalid=0;invalid<7;++invalid)run([&,invalid](Fixture&){zones();
        if(invalid==0)Get(localCharacter).fields["dpzj"]=nullptr;
        if(invalid==1)services["fso"]=Make("OtherRegistry");
        if(invalid==2)entity=nullptr;
        if(invalid==3)sceneReady=false;
        if(invalid==4)loadedMapOverride=11;
        if(invalid==5)Get(entity).numbers["cell"]=9;
        if(invalid==6)Get(Get(mapCells).items[2]).fields["cellNumber"]=Box<uint32_t>(7);
        StartChatTravel({10,10,1});
        const char* reasons[]={"character","registry","entity","scene","map","cell","cell"};
        Check(plannedZone==-1&&quotes==1,"An unknown zone starts every zone of the current map; planning goes on");
        Check(logged(std::string("CHAT_TRAVEL_HERE_ZONE map=10 unknown=")+reasons[invalid]),"The unknown zone is logged with its reason");});
    run([&](Fixture& f){zones();bag=true;StartChatTravel({10,10,1});f.Receive();TickChatTravel();
        Check(plannedZone==-1&&plannedTarget==123&&!logged("CHAT_TRAVEL_HERE_ZONE"),"From the Havre-Sac there is no current-map departure, hence no zone");});
    // « Passer par le temple Roublard » (on by default) is read at every route
    // comparison: switching it applies to the next trip.
    run([&](Fixture& f){StartChatTravel({10,10,1});Check(plannedTemple,"The temple option is on by default");
        RestoreChatTravel();f.owner.settings.travelRoublardTemple=false;StartChatTravel({11,10,1});
        Check(!plannedTemple&&quotes==2,"Unchecked, the route comparison avoids the temple");
        RestoreChatTravel();f.owner.settings.travelRoublardTemple=true;StartChatTravel({12,10,1});
        Check(plannedTemple,"Checked again, the next trip may use it");});
    // A changed cell layout disables only the zone reading: travel still starts
    // from every zone of the current map.
    const auto layout=api.class_get_field_from_name;
    api.class_get_field_from_name=[](Klass,const char* name)->void*{static size_t dugt=0x80;return std::string(name)=="dugt"?&dugt:nullptr;};
    logs.clear();InstallChatTravel();api.class_get_field_from_name=layout;
    Check(logged("CHAT_TRAVEL_HERE_ZONE_UNAVAILABLE Travel field mismatch: cellsData"),"The zone contracts are optional and their absence is logged");
    run([&](Fixture&){zones();StartChatTravel({10,10,1});
        Check(plannedZone==-1&&quotes==1&&logged("CHAT_TRAVEL_HERE_ZONE map=10 unknown=contracts"),"Without them every zone of the current map starts the search");});
    logs.clear();InstallChatTravel();Check(!logged("CHAT_TRAVEL_HERE_ZONE_UNAVAILABLE"),"The zone contracts install again");
}
// r112: the Roublards' secret passage inside the journey (roublard_passage is
// the production code, on a scripted Bad Aboum; its native side has its own
// tests). The comparison offers his trips, the autopilot walks to him, the mod
// talks and answers, then plans again from the arrival without shortcuts.
namespace fake {
struct PassageGame final : roublard_passage::Game {
    int64_t entity=-12,teleportTo=0,closed=0;int npcs=1,generation=0,marked=0,talks=0,replies=0,closes=0,finds=0,message=8722;
    std::vector<int> offer{8758,8779,-1};roublard_passage::Dialog dialog;std::unordered_map<int64_t,std::pair<int,int>> coords;
    int64_t Character() override {return 77;}
    bool Locate(int64_t map,int& x,int& y) override {auto at=coords.find(map);if(at==coords.end())return false;x=at->second.first;y=at->second.second;return true;}
    int FindNpc(int,int64_t& found) override {++finds;found=npcs>0?entity:0;return npcs;}
    void Mark() override {marked=generation;}
    // Shown as the native layer reads it: an NPC entity and a non-empty button.
    roublard_passage::Dialog Read() override {auto read=dialog;
        read.shown=dialog.shown&&dialog.entity!=-1&&std::any_of(dialog.replies.begin(),dialog.replies.end(),[](int id){return id!=0;});
        read.fresh=dialog.shown&&generation!=marked;return read;}
    bool Talk(int64_t) override {++talks;dialog={};dialog.shown=true;dialog.npc=1422;dialog.entity=entity;dialog.message=message;dialog.replies=offer;++generation;return true;}
    bool Reply(int button,int reply) override {Check(button>=0&&button<static_cast<int>(dialog.replies.size())&&dialog.replies[static_cast<size_t>(button)]==reply,"The reply read on that button");
        ++replies;dialog.locked=true;if(teleportTo){dialog.shown=false;Get(currentMap).numbers["m_mapId"]=teleportTo;}return true;}
    bool Close(int64_t target) override {++closes;closed=target;return true;}
    // The player opens another NPC's dialog (the chapel mercenary).
    void Other() {dialog={};dialog.shown=true;dialog.npc=4351;dialog.entity=-40;dialog.message=30706;dialog.replies={37075,37073};++generation;}
};
}
void RoublardPassageJourneyTests() {
    using namespace fake;
    static constexpr int64_t Street=10,Crypt=67372032,Door=67108864,Cemetery=82314240,Sewer=101716489;
    const auto logged=[](const std::string& text) {return std::any_of(logs.begin(),logs.end(),[&](const std::string& l){return l.find(text)!=std::string::npos;});};
    const auto noticed=[](const std::string& text) {return std::any_of(notices.begin(),notices.end(),[&](const std::string& n){return n.find(text)!=std::string::npos;});};
    const auto offered=[](int id,int64_t from,int64_t to) {return std::any_of(chat_route::passed.begin(),chat_route::passed.end(),[&](const chat_route::Shortcut& s){return s.id==id&&s.from==from&&s.to==to&&s.cost==2;});};
    const auto run=[](auto scenario) {const auto before=handles.size();
        {Fixture f;PassageGame game;game.coords={{Street,{5,-14}},{Crypt,{5,-15}},{Door,{10,16}},{Cemetery,{10,16}}};
         roublard_passage::UseGame(&game);roublard_passage::ResetKnowledge();chat_route::passed.clear();chat_route::chosen=-1;
         Info(Street,5,-14,1);Info(Crypt,5,-15,1);Info(Door,10,16,-1);resolvedMaps={Cemetery};plannedDeparture=Street;
         scenario(f,game);
         RestoreChatTravel();roublard_passage::UseGame(nullptr);chat_route::chosen=-1;}
        Check(handles.size()==before,"The passage journey releases managed references");};
    // Walks to Bad Aboum: the comparison took the passage, the autopilot reached the crypt.
    const auto reach=[](PassageGame& game) {
        StartChatTravel({10,16,1});TickChatTravel();TickChatTravel();TickChatTravel();
        Check(walked==1&&walkedMap==Crypt&&game.talks==0,"The game's autopilot walks to Bad Aboum first");
        Get(currentMap).numbers["m_mapId"]=Crypt;routeRunning=false;Get(services["fjs"]).fields["dsjs"]=nullptr;
        TickChatTravel();TickChatTravel();};
    run([&](Fixture&,PassageGame& game){chat_route::chosen=101;game.teleportTo=Door;
        StartChatTravel({10,16,1});
        Check(plannedTarget==Cemetery&&plannedSource==Street&&offered(101,Crypt,Door)&&offered(102,Crypt,67373056)&&!offered(110,Door,Crypt),
            "Bad Aboum's trips are offered to the comparison (the temple side is not known yet)");
        Check(logged("ROUBLARD_PASSAGE_ROUTE link=101 entrance=67372032 departure=10 target=82314240")&&
            noticed("TP : le trajet passe par le passage secret des Roublards, d’Astrub au temple Roublard."),"The chosen passage is logged and told");
        TickChatTravel();TickChatTravel();
        Check(walked==1&&walkedMap==Crypt,"The game's autopilot walks to Bad Aboum's map, not to the destination");
        TickChatTravel();TickChatTravel();
        Check(logged("ROUBLARD_PASSAGE_WALK target=67372032")&&game.talks==0&&!noticed("autopilote lancé"),"The journey waits for the walk");
        Get(currentMap).numbers["m_mapId"]=Crypt;TickChatTravel();TickChatTravel();TickChatTravel();
        Check(game.talks==0,"On his map, the talk waits for the game's autopilot to finish");
        routeRunning=false;Get(services["fjs"]).fields["dsjs"]=nullptr;
        TickChatTravel();Check(game.talks==0,"One settled sample on his map is not enough");
        TickChatTravel();TickChatTravel();Check(game.talks==1&&game.replies==0,"Spoken to on his map");
        plannedDeparture=Door;
        TickChatTravel();Check(game.replies==1&&Get(currentMap).numbers["m_mapId"]==Door,"The temple reply, then his teleport");
        TickChatTravel();TickChatTravel();TickChatTravel();
        Check(noticed("passage secret des Roublards emprunté ; suite du trajet.")&&plannedSource==Door&&chat_route::passed.empty(),
            "Planned again from the arrival, without shortcuts");
        Check(roublard_passage::Known().arrival[0][1]==Door&&roublard_passage::Known().npc[1]==Door,"The arrival, and Bad Aboum waiting there, are learned");
        TickChatTravel();TickChatTravel();
        Check(walked==2&&walkedMap==Cemetery,"Then the game's autopilot to the destination");
        TickChatTravel();Check(noticed("autopilote lancé."),"And the trip ends as any other");});
    // His trip is not offered to this character: nothing clicked, the trip goes on
    // from the crypt without the passage, and the next trip does not plan it.
    run([&](Fixture&,PassageGame& game){chat_route::chosen=101;game.offer={8779,-1};plannedDeparture=Street;
        reach(game);plannedDeparture=Crypt;
        TickChatTravel();TickChatTravel();
        Check(game.talks==1&&game.replies==0&&game.closes==1,"Not offered: the dialog is closed, nothing clicked");
        Check(noticed("Bad Aboum ne propose pas ce trajet à ce personnage ; le trajet continue sans le passage secret.")&&plannedSource==Crypt&&
            chat_route::passed.empty()&&logged("CHAT_TRAVEL_REPLAN ROUBLARD_PASSAGE failed: reply absent"),"The trip goes on from the crypt");
        TickChatTravel();TickChatTravel();Check(walked==2&&walkedMap==Cemetery,"Toward the destination by the game's own route");
        RestoreChatTravel();plannedDeparture=Street;Get(currentMap).numbers["m_mapId"]=Street;
        StartChatTravel({10,16,1});Check(!offered(101,Crypt,Door)&&offered(102,Crypt,67373056),"No more temple trips for this character");});
    // The player walks away during the dialog: the trip stops, no click.
    run([&](Fixture&,PassageGame& game){chat_route::chosen=101;game.teleportTo=Door;game.offer={};reach(game);
        TickChatTravel();Check(game.talks==1,"Spoken to");
        Get(currentMap).numbers["m_mapId"]=Street;TickChatTravel();
        Check(game.replies==0&&noticed("trajet interrompu pendant le passage secret des Roublards.")&&logged("passage aborted: left his map during the dialog"),
            "Leaving his map stops the trip");
        TickChatTravel();Check(walked==1&&game.talks==1,"Nothing more is started");});
    // Combat during the passage: the journey's own guard stops everything.
    run([&](Fixture&,PassageGame& game){chat_route::chosen=101;game.offer={};reach(game);TickChatTravel();
        combat=true;TickChatTravel();combat=false;TickChatTravel();
        Check(game.replies==0&&noticed("trajet annulé")&&logged("ROUBLARD_PASSAGE_STOPPED trip=astrub>temple phase=dialog")&&!roublard_passage::Running(),
            "Combat cancels the trip and the passage, without a click");});
    // While his teleport is awaited, the player's own actions stop the trip: the
    // mod never closes the player's dialog nor starts a new autopilot.
    run([&](Fixture&,PassageGame& game){chat_route::chosen=101;reach(game);TickChatTravel();TickChatTravel();
        Check(game.replies==1,"Replied, no teleport yet");
        game.Other();TickChatTravel();
        Check(game.closes==0&&noticed("trajet interrompu pendant le passage secret des Roublards.")&&logged("passage aborted: another dialog npc=4351"),
            "Another NPC's dialog: the trip stops, that dialog stays");
        TickChatTravel();TickChatTravel();Check(walked==1,"No new autopilot");});
    run([&](Fixture&,PassageGame& game){chat_route::chosen=101;reach(game);TickChatTravel();TickChatTravel();
        game.dialog.shown=false;Get(currentMap).numbers["m_mapId"]=Sewer;
        for(int i=0;i<4;++i)TickChatTravel();
        Check(noticed("arrivée inattendue après le passage secret : trajet arrêté.")&&walked==1&&game.closes==0,"Walking into the sewers stops the trip");});
    run([&](Fixture&,PassageGame& game){chat_route::chosen=101;reach(game);TickChatTravel();
        auto other=Make("MapInformationData");Get(other).fields["id"]=Box<uint32_t>(456);Get(services["fjs"]).fields["dsjs"]=other;routeRunning=true;
        TickChatTravel();
        Check(game.replies==0&&noticed("trajet interrompu pendant le passage secret des Roublards.")&&logged("passage aborted: the game's autopilot started"),
            "The game's autopilot started meanwhile: stop, no click");});
    run([&](Fixture&,PassageGame& game){chat_route::chosen=101;reach(game);TickChatTravel();
        routePendingNative=true;TickChatTravel();
        Check(game.replies==0&&logged("passage aborted: the game's autopilot started"),"The game's autopilot requested meanwhile: stop, no click");});
    // The arrival's scene is awaited before the trip goes on.
    run([&](Fixture&,PassageGame& game){chat_route::chosen=101;game.teleportTo=Door;reach(game);TickChatTravel();
        sceneReady=false;TickChatTravel();
        Check(game.replies==1&&Get(currentMap).numbers["m_mapId"]==Door,"Teleported");
        for(int i=0;i<4;++i)TickChatTravel();
        Check(!noticed("emprunté")&&roublard_passage::Running(),"The arrival waits for its scene");
        sceneReady=true;for(int i=0;i<3;++i)TickChatTravel();
        Check(noticed("passage secret des Roublards emprunté ; suite du trajet."),"Then the trip goes on");});
    run([&](Fixture&,PassageGame& game){chat_route::chosen=101;reach(game);TickChatTravel();
        bag=true;TickChatTravel();bag=false;
        Check(game.replies==0&&logged("passage aborted: Havre-Sac"),"Into the Havre-Sac: stop");});
    // « Passage secret et temple des Roublards » unticked during the walk or the passage.
    run([&](Fixture& f,PassageGame& game){chat_route::chosen=101;StartChatTravel({10,16,1});for(int i=0;i<4;++i)TickChatTravel();
        f.owner.settings.travelRoublardTemple=false;TickChatTravel();
        Check(noticed("option du passage secret des Roublards décochée : trajet arrêté.")&&game.talks==0,"Unticked during the walk: stop");});
    run([&](Fixture& f,PassageGame& game){chat_route::chosen=101;reach(game);TickChatTravel();
        f.owner.settings.travelRoublardTemple=false;TickChatTravel();
        Check(noticed("option du passage secret des Roublards décochée")&&game.replies==0,"Unticked during the passage: stop, no click");});
    // The walk to him stops before his map, or the game travels elsewhere.
    run([&](Fixture&,PassageGame& game){chat_route::chosen=101;StartChatTravel({10,16,1});
        for(int i=0;i<4;++i)TickChatTravel();
        routeRunning=false;Get(services["fjs"]).fields["dsjs"]=nullptr;TickChatTravel();TickChatTravel();
        Check(!noticed("trajet interrompu avant"),"A short pause is tolerated");
        TickChatTravel();
        Check(noticed("trajet interrompu avant le passage secret des Roublards.")&&game.talks==0,"An interrupted walk ends the trip");});
    run([&](Fixture&,PassageGame& game){chat_route::chosen=101;StartChatTravel({10,16,1});
        for(int i=0;i<4;++i)TickChatTravel();
        auto other=Make("MapInformationData");Get(other).fields["id"]=Box<uint32_t>(456);Get(services["fjs"]).fields["dsjs"]=other;TickChatTravel();
        Check(noticed("un autre trajet est actif")&&game.talks==0,"Another native destination replaces the walk");});
    // The passage no longer available when the walk ends: the trip goes on without it.
    run([&](Fixture&,PassageGame& game){chat_route::chosen=101;StartChatTravel({10,16,1});TickChatTravel();TickChatTravel();TickChatTravel();
        Get(currentMap).numbers["m_mapId"]=Crypt;routeRunning=false;Get(services["fjs"]).fields["dsjs"]=nullptr;
        roublard_passage::UseGame(nullptr);plannedDeparture=Crypt;TickChatTravel();TickChatTravel();
        Check(noticed("le passage secret des Roublards n’est pas disponible ; le trajet continue sans lui.")&&plannedSource==Crypt&&
            logged("CHAT_TRAVEL_REPLAN ROUBLARD_PASSAGE start refused")&&game.talks==0,"Planned again from his map, without it");});
    // Already on his map: no autopilot before the passage.
    run([&](Fixture&,PassageGame& game){chat_route::chosen=101;game.offer={};Get(currentMap).numbers["m_mapId"]=Crypt;plannedDeparture=Crypt;
        StartChatTravel({10,16,1});for(int i=0;i<5;++i)TickChatTravel();
        Check(walked==0&&game.talks==1,"Departing from the crypt itself, Bad Aboum is spoken to directly");});
    // From the Havre-Sac: its zaap, then the walk to him; the Havre-Sac guard
    // does not take the walk for a departure abandoned.
    run([&](Fixture& f,PassageGame& game){chat_route::chosen=101;game.offer={};bag=true;plannedDeparture=104;
        StartChatTravel({10,16,1});f.Receive();TickChatTravel();
        Check(teleported==1&&teleportMap==104&&offered(101,Crypt,Door),"The Havre-Sac zaap first");
        Get(currentMap).numbers["m_mapId"]=104;bag=false;TickChatTravel();TickChatTravel();TickChatTravel();TickChatTravel();
        Check(walked==1&&walkedMap==Crypt&&logged("ROUBLARD_PASSAGE_WALK")&&!logged("left Havenbag"),"Then the walk to Bad Aboum");
        Get(currentMap).numbers["m_mapId"]=Crypt;routeRunning=false;Get(services["fjs"]).fields["dsjs"]=nullptr;
        for(int i=0;i<4;++i)TickChatTravel();
        Check(game.talks==1,"Spoken to after a Havre-Sac departure");});
    // Option off: no shortcut is offered, a reported one is ignored, the temple flag is off.
    run([&](Fixture& f,PassageGame& game){chat_route::chosen=101;f.owner.settings.travelRoublardTemple=false;
        StartChatTravel({10,16,1});
        Check(chat_route::passed.empty()&&!plannedTemple&&logged("ROUBLARD_PASSAGE_ROUTE_IGNORED link=101"),"Option off: no passage planned");
        TickChatTravel();TickChatTravel();Check(walked==1&&walkedMap==Cemetery&&game.talks==0,"Straight to the destination");});
    // Unavailable passage (contracts refused): nothing offered either.
    run([&](Fixture&,PassageGame& game){roublard_passage::UseGame(nullptr);chat_route::chosen=101;
        StartChatTravel({10,16,1});
        Check(chat_route::passed.empty()&&logged("ROUBLARD_PASSAGE_ROUTE_IGNORED link=101")&&game.talks==0,"Unavailable: no passage");});
    // The passive look for Bad Aboum (learning from the player's own passages)
    // follows the option too: off, the maps are never looked at.
    run([&](Fixture& f,PassageGame& game){
        for(int on=0;on<2;++on){f.owner.settings.travelRoublardTemple=on==1;game.finds=0;
            TickChatTravel();Sleep(250);TickChatTravel();Sleep(1300);TickChatTravel();Sleep(250);TickChatTravel();
            Check(on?game.finds>0:game.finds==0,"The passive look follows the option");}});
}
int main(){using namespace fake;try{
    Initialize();InstallChatTravel();InstallMapTravel();InstallSoulGroupMenu();soul::InstallSpell();
    Check(std::none_of(logs.begin(),logs.end(),[](const std::string& l){return l.find("CHAT_TRAVEL_HERE_ZONE_UNAVAILABLE")!=std::string::npos;}),"The player's zone contracts install");
    MapTravelTests();
    TravelFixTests();
    RoublardPassageJourneyTests();
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
    {Fixture f;StartChatTravel({10,10,1});Check(entered==0&&interacted==0&&quotes==1&&requestedQuote==104&&teleported==0,"Direct native quote for closest known same-world zaap without Havenbag");TickChatTravel();f.Receive();f.Price(25,101);TickChatTravel();Check(quotes==1&&teleported==0,"Cached lists and another map's quote cannot authorize direct TP; no polling requests");f.Price();Check(teleported==0,"Price receiver must not dispatch reentrantly");TickChatTravel();Check(teleported==1&&teleportMap==104&&teleportSource==5&&teleportCost==25&&walked==0,"Native direct source and fresh cost");TickChatTravel();Check(walked==0,"Wait for actual arrival");Get(currentMap).numbers["m_mapId"]=104;TickChatTravel();TickChatTravel();Check(walked==1&&resolvedFor==ChatTpTarget{10,10,1}&&walkedMap==123,"Exactly one onward route");}
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
    {Fixture f;StartChatTravel({1,2,1});StartChatTravel({7,8,14});f.Price();TickChatTravel();Get(currentMap).numbers["m_mapId"]=104;TickChatTravel();TickChatTravel();Check(walked==1&&resolvedFor==ChatTpTarget{7,8,14}&&walkedMap==123,"A different link replaces the target and preserves Srambad's world ID");}
    {Fixture f;plannedDeparture=103;StartChatTravel({7,8,14});Check(quotes==1&&requestedQuote==103,"A known zaap on another world layer must reach the native graph comparison");}
    {Fixture f;Info(10,0,0,1);plannedDeparture=10;StartChatTravel({7,8,14});TickChatTravel();TickChatTravel();Check(quotes==0&&teleported==0&&walked==1&&resolvedFor.world==14,"Current map participates across connected world layers without a needless teleport");}
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
    // Party follow: the onward confirmation explains the game's own restarts, and a
    // passive watch logs the native trip state after the mod's single request.
    {Fixture f;followingMember=true;StartChatTravel({10,10,1});f.Price();TickChatTravel();
      Get(currentMap).numbers["m_mapId"]=104;TickChatTravel();TickChatTravel();TickChatTravel();
      Check(walked==1&&notices.back().find("suit un membre du groupe")!=std::string::npos,"A following character is told the game will restart its own trip");
      Check(std::any_of(logs.begin(),logs.end(),[](const std::string& l){return l.find("CHAT_TRAVEL_ONWARD_ACTIVE target=123 following=1")!=std::string::npos;}),"Onward log records the follow state");
      logs.clear();routeRunning=false;Get(services["fjs"]).fields["dsjs"]=nullptr;TickChatTravel();
      Check(logs.size()==1&&logs[0].find("AUTOTRIP_WATCH active=0 pending=0 target=0 following=1")!=std::string::npos,"The watch logs a stopped trip");
      auto other=Make("MapInformationData");Get(other).fields["id"]=Box<uint32_t>(456);Get(services["fjs"]).fields["dsjs"]=other;routeRunning=true;TickChatTravel();TickChatTravel();
      Check(logs.size()==2&&logs[1].find("AUTOTRIP_WATCH active=1 pending=0 target=456 following=1")!=std::string::npos,"The watch logs a restart toward another map once");
      Check(walked==1,"The mod never sends a second request");
      RestoreChatTravel();logs.clear();routeRunning=false;TickChatTravel();Check(logs.empty(),"Restore releases the watch");}
    {Fixture f;StartChatTravel({10,10,1});f.Price();TickChatTravel();Get(currentMap).numbers["m_mapId"]=104;TickChatTravel();TickChatTravel();TickChatTravel();
      Check(notices.back()=="TP : autopilote lancé."||notices.back().find("autopilote lancé.")!=std::string::npos,"Without party follow the confirmation stays short");
      Check(notices.back().find("membre du groupe")==std::string::npos,"No follow notice when the character follows nobody");}
    {Fixture f;acceptRoute=false;StartChatTravel({10,10,1});f.Price();TickChatTravel();
      Get(currentMap).numbers["m_mapId"]=104;TickChatTravel();TickChatTravel();Check(walked==1,"First onward request");
      TickChatTravel();Check(walked==1,"No repeated frame requests");Sleep(1050);
      TickChatTravel();acceptRoute=true;TickChatTravel();TickChatTravel();Check(walked==2,"One retry after unaccepted request, then native confirmation");}
    {Fixture f;acceptRoute=false;StartChatTravel({10,10,1});f.Price();TickChatTravel();
      Get(currentMap).numbers["m_mapId"]=104;TickChatTravel();TickChatTravel();
      auto d=Make("MapInformationData");Get(d).fields["id"]=Box<uint32_t>(123);Get(services["fjs"]).fields["dsjs"]=d;
      routePendingNative=true;TickChatTravel();routePendingNative=false;Sleep(1050);TickChatTravel();TickChatTravel();
      Check(walked==2,"A route the game accepted then dropped right after the arrival is retried once (pressing TP again was what it took in play)");
      Sleep(1050);TickChatTravel();TickChatTravel();Check(walked==2,"No third attempt");}
    {Fixture f;acceptRoute=false;StartChatTravel({10,10,1});f.Price();TickChatTravel();
      Get(currentMap).numbers["m_mapId"]=104;TickChatTravel();TickChatTravel();
      auto d=Make("MapInformationData");Get(d).fields["id"]=Box<uint32_t>(123);Get(services["fjs"]).fields["dsjs"]=d;
      routePendingNative=true;TickChatTravel();routePendingNative=false;Sleep(3100);TickChatTravel();TickChatTravel();
      Check(walked==1,"A route accepted then interrupted later on purpose is never restarted");}
    {Fixture f;acceptRoute=false;StartChatTravel({10,10,1});f.Price();TickChatTravel();
      Get(currentMap).numbers["m_mapId"]=104;TickChatTravel();TickChatTravel();
      auto d=Make("MapInformationData");Get(d).fields["id"]=Box<uint32_t>(999);Get(services["fjs"]).fields["dsjs"]=d;
      routePendingNative=true;TickChatTravel();Sleep(1050);TickChatTravel();Check(walked==1,"Never replace a different native destination");}
    std::cout<<"PASS: production TP orchestration, optional replacement of the game's map autopilot, already there on the game's destination map, the Havre-Sac start, the player's own zone, the Roublard temple option, confirmed arrival then native onward travel to the game's map, stale/cancel guards, native group equip and selfcast click validation\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
