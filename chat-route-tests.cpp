#include "chat-route.hpp"
#include "module.hpp"
#include "features.hpp"
#include "options-ui.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
namespace fake {
using namespace managed;
struct Object { std::string type; std::map<std::string,Obj> fields;std::map<std::string,int64_t> numbers;std::map<int64_t,Obj> entries;std::vector<Obj> list;int cursor=-1;std::array<uint64_t,4> data{}; };
struct MethodInfo {std::string type,name;};
std::map<std::string,std::unique_ptr<std::string>> types;
std::vector<std::unique_ptr<Object>> objects;std::vector<std::unique_ptr<MethodInfo>> methods;
std::map<GCHandle,Obj> handles;GCHandle next=1;std::map<std::string,Obj> services;
Obj graph{},vertices{},outgoing{};bool loaded=true;int loads=0,criteria=0,areaChecks=0;
std::vector<std::string> logs;
bool logged(const std::string& text){return std::any_of(logs.begin(),logs.end(),[&](const std::string& line){return line.find(text)!=std::string::npos;});}
void Check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
Object& Get(Obj o){Check(o!=nullptr,"Null fake object");return *static_cast<Object*>(o);}
Obj Make(std::string t){auto o=std::make_unique<Object>();o->type=t;auto p=o.get();objects.push_back(std::move(o));return p;}
template<class T> Obj Box(T v){auto o=Make("Box");std::memcpy(Get(o).data.data(),&v,sizeof(v));return o;}
Obj Vertex(int64_t map,int zone=1){
    auto v=Make("Vertex");Get(v).numbers={{"m_uid",map*10+zone},{"m_mapId",map},{"m_zoneId",zone}};
    auto& zones=Get(vertices).entries[map];if(!zones)zones=Make("Dictionary");Get(zones).entries[zone]=v;return v;
}
Obj Edge(Obj from,Obj to,bool allowed=true,bool area=true,int type=1){
    auto e=Make("Edge"),ts=Make("List"),t=Make("Transition");Get(ts).list.push_back(t);Get(t).numbers["type"]=type;
    Get(e).fields={{"m_to",to},{"m_transitions",ts}};Get(e).numbers={{"allowed",allowed},{"area",area}};
    auto uid=Get(from).numbers["m_uid"];auto& wrapper=Get(outgoing).entries[uid];
    if(!wrapper){wrapper=Make("EdgeListWrapper");Get(wrapper).fields["m_edgeList"]=Make("List");}
    Get(Get(wrapper).fields["m_edgeList"]).list.push_back(e);return e;
}
chat_travel::Destination Choice(int64_t map,int x=0,int y=0){return {map,x,y,1,0,true,0};}
void InitApi(){
    api.object_get_class=[](Obj o)->Klass{return api.Class("","",Get(o).type.c_str());};
    api.object_unbox=[](Obj o)->void*{return Get(o).data.data();};
    api.gchandle_new=[](Obj o,bool)->GCHandle{handles[++next]=o;return next;};
    api.gchandle_get_target=[](GCHandle h)->Obj{return handles.at(h);};
    api.gchandle_free=[](GCHandle h){Check(handles.erase(h)==1,"Lost GC root");};
}
Obj Invoke(Method m,Obj self,std::initializer_list<void*> args){
    auto n=static_cast<const MethodInfo*>(m)->name;std::vector<void*> a(args);
    if(n=="bbno")return loaded?graph:nullptr;
    if(n=="bbnn"){Check(Get(self).type=="ezc","Loader must be privately allocated");++loads;return nullptr;}
    if(n=="wvo"){Check(Get(self).type=="ens","Private route filter");return nullptr;}
    if(n=="wvs"){++criteria;return Box(bool(Get(a[0]).numbers["allowed"]));}
    if(n=="wvt"){++areaChecks;return Box(bool(Get(a[0]).numbers["area"]));}
    auto& o=Get(self);
    if(n=="get_Count")return Box(int(o.type=="Dictionary"?o.entries.size():o.list.size()));
    if(n=="TryGetValue"){auto key=*static_cast<int64_t*>(a[0]);auto it=o.entries.find(key);*static_cast<Obj*>(a[1])=it==o.entries.end()?nullptr:it->second;return Box(it!=o.entries.end());}
    if(n=="get_Values"){auto list=Make("List");for(auto [k,v]:o.entries)Get(list).list.push_back(v);return list;}
    if(n=="GetEnumerator"){auto it=Make("Iterator");Get(it).list=o.list;return it;}
    if(n=="MoveNext")return Box(++o.cursor<int(o.list.size()));
    if(n=="get_Current")return o.list.at(o.cursor);
    if(n=="get_Item")return o.list.at(*static_cast<int*>(a[0]));
    throw std::runtime_error("Unexpected native route call "+n);
}
struct Fixture {
    Module owner{"offline"};
    Fixture(){chat_route::Clear();module=&owner;owner.servicesReady=true;loaded=true;loads=criteria=areaChecks=0;logs.clear();
        services={{"fnf",Make("eza")},{"fkq",Make("Criteria")}};graph=Make("PathFindingData");vertices=Make("Dictionary");outgoing=Make("Dictionary");Get(graph).fields={{"m_vertices",vertices},{"m_outgoingEdges",outgoing}};}
    ~Fixture(){chat_route::Clear();module=nullptr;}
    chat_route::Result Finish(){for(int i=0;i<10000;++i){auto r=chat_route::Tick();if(r.status!=chat_route::Status::Pending)return r;}throw std::runtime_error("Planner did not finish");}
};
}
namespace managed {
Api api{};uintptr_t gameBase=0;
Klass Api::Class(const char*,const char*,const char* name)const{auto& t=fake::types[name];if(!t)t=std::make_unique<std::string>(name);return t.get();}
Method Api::FindVerified(Klass k,const char* name,int,uintptr_t)const{auto m=std::make_unique<fake::MethodInfo>(fake::MethodInfo{*static_cast<std::string*>(k),name});auto p=m.get();fake::methods.push_back(std::move(m));return p;}
Obj Api::Invoke(Method m,Obj o,std::initializer_list<void*> a)const{return fake::Invoke(m,o,a);}
Obj Api::Call(Obj o,const char* n,std::initializer_list<void*> a)const{fake::MethodInfo m{fake::Get(o).type,n};return Invoke(&m,o,a);}
Obj Api::New(Klass k,std::initializer_list<void*>)const{return fake::Make(*static_cast<std::string*>(k));}
Obj Api::Field(Obj o,const char* n)const{return fake::Get(o).fields[n];}
void* Api::FieldAddress(Obj o,const char* n)const{return &fake::Get(o).numbers[n];}
}
Module* module=nullptr;
void Log(const std::string& text){fake::logs.push_back(text);}
Module::Module(std::filesystem::path p):folder(p){}
managed::Obj Module::FindService(const char* n)const{return fake::services[n];}
int main(){using namespace fake;using namespace chat_route;try{InitApi();Install();
    {Fixture f;auto hub=Vertex(81789954),dest=Vertex(999999);
        for(int i=0;i<295;++i)Edge(hub,Vertex(100000+i));Edge(hub,dest);
        Begin({Choice(81789954)},999999,99);
        Check(Tick().status==Status::Pending&&criteria<296,"A real 296-edge hub must yield without losing its remaining edges");
        auto r=f.Finish();Check(r.status==Status::Found&&r.steps==2,"The final edge of a 296-edge hub remains reachable");}
    {Fixture f;auto hub=Vertex(1);Vertex(2);for(int i=0;i<1025;++i)Edge(hub,Vertex(10000+i));Begin({Choice(1)},2,99);
        bool rejected=false;try{f.Finish();}catch(const std::exception& e){rejected=std::string(e.what()).find("outgoing edges count=1025")!=std::string::npos;}
        Check(rejected,"Malformed collection still fails with its name and actual count");}
    {Fixture f;auto closeStart=Vertex(1),distantStart=Vertex(2),a=Vertex(3),b=Vertex(4),dest=Vertex(5);Edge(closeStart,a);Edge(a,b);Edge(b,dest);Edge(distantStart,dest);
        Begin({Choice(1,0,0),Choice(2,50,50)},5,99);auto r=f.Finish();Check(r.status==Status::Found&&r.departure.map==2&&r.steps==2,"Actual shorter path must beat coordinate-nearest zaap");}
    {Fixture f;auto here=Vertex(1),zaap=Vertex(2),dest=Vertex(3);Edge(here,dest);Edge(zaap,dest);Begin({Choice(2),Choice(1)},3,1);Check(f.Finish().departure.map==1,"Walking directly avoids needless teleport");}
    {Fixture f;auto a=Vertex(1),b=Vertex(2),dest=Vertex(3);Edge(a,dest,false);Edge(b,dest);Begin({Choice(1),Choice(2)},3,99);Check(f.Finish().departure.map==2&&criteria>0,"Native criteria must reject locked shortcut");}
    {Fixture f;auto a=Vertex(1),dest=Vertex(2);for(int i=0;i<200;++i)Edge(a,dest);Begin({Choice(1)},2,99);
        auto r=f.Finish();Check(r.status==Status::Found&&criteria==1&&areaChecks==1,"Equivalent accepted paths must not repeatedly evaluate native criteria");}
    {Fixture f;auto a=Vertex(1),dest=Vertex(2);Edge(a,dest,false);Edge(a,dest,true);Begin({Choice(1)},2,99);
        Check(f.Finish().status==Status::Found&&criteria==2,"Rejected first edge must not hide an accessible parallel edge");}
    {Fixture f;auto a=Vertex(1),b=Vertex(2),dest=Vertex(3);Edge(a,dest);Edge(b,dest);auto cheap=Choice(2);cheap.cost=1;auto expensive=Choice(1);expensive.cost=10;
        Begin({expensive,cheap},3,99);Check(f.Finish().departure.map==2,"Equal route length keeps the lower-cost departure tie-break");}
    {Fixture f;auto a=Vertex(1),dest=Vertex(2);Edge(a,dest,true,false);Begin({Choice(1)},2,99);Check(f.Finish().status==Status::Unreachable&&areaChecks>0,"Native forbidden area cannot authorize a teleport");}
    {Fixture f;auto a=Vertex(1),b=Vertex(2);Vertex(3);Edge(a,b);Edge(b,a);Begin({Choice(1)},3,99);Check(f.Finish().status==Status::Unreachable,"Cycles and disconnected destination terminate without fallback");}
    {Fixture f;auto a=Vertex(1),dest=Vertex(2);Edge(a,dest,true,true,32);Begin({Choice(1)},2,99);Check(f.Finish().status==Status::Found,"Native interactive transport edges participate in routing");}
    {Fixture f;auto a=Vertex(1),dest=Vertex(2);Vertex(1,2);Edge(a,dest);Begin({Choice(1)},2,99);auto r=f.Finish();
        Check(r.status==Status::Found&&r.steps==3,"A zaap map split into zones starts from every zone, one step behind an unambiguous arrival");}
    {Fixture f;auto a=Vertex(1),dest=Vertex(2);Edge(a,dest);auto bad=Choice(1);bad.known=false;Begin({bad},2,99);Check(f.Finish().status==Status::Unreachable,"Unknown zaap cannot enter search");}
    {Fixture f;auto a=Vertex(1),dest=Vertex(2);Edge(a,dest);loaded=false;Begin({Choice(1)},2,99);Check(Tick().status==Status::Pending&&loads==1,"Lazy graph loading is read-only and deferred");Tick();Check(loads==1,"Graph load is requested only once");loaded=true;Check(f.Finish().status==Status::Found,"Loaded graph resumes pending planning");}
    {Fixture f;auto a=Vertex(1),dest=Vertex(2);Edge(a,dest);Begin({Choice(1)},2,99);services["fkq"]=Make("Replacement");Check(Tick().status==Status::Unreachable,"Changed character criteria invalidates planning");}
    // r110. The current map split into zones: every zone starts at 0 while the
    // player's zone is unknown, and walking beats a zaap for an adjacent target.
    {Fixture f;auto hereA=Vertex(1,1),hereB=Vertex(1,2),zaap=Vertex(2),dest=Vertex(3);(void)hereA;Edge(hereB,dest);Edge(zaap,dest);
        Begin({Choice(2),Choice(1)},3,1);auto r=f.Finish();
        Check(r.status==Status::Found&&r.departure.map==1&&r.steps==1,"A current map split into zones starts at 0 and beats a zaap for an adjacent target");
        Check(logged("CHAT_ROUTE_SEEDED target=3 candidates=2 starts=3 hereStarts=2 splitZaaps=0"),"Every zone of the current map starts the search");}
    // The player's own zone, when the game's cell-to-zone link gave it.
    {Fixture f;auto own=Vertex(1,1),other=Vertex(1,2),zaap=Vertex(2),a=Vertex(4),b=Vertex(5),dest=Vertex(3);
        Edge(other,dest);Edge(own,a);Edge(a,b);Edge(b,dest);Edge(zaap,dest);
        Begin({Choice(1),Choice(2)},3,1,1);auto r=f.Finish();
        Check(r.departure.map==2&&r.steps==2,"Known player zone: the far side of the current map cannot win");
        Begin({Choice(1),Choice(2)},3,1,2);r=f.Finish();
        Check(r.departure.map==1&&r.steps==1,"Known player zone on the near side walks");
        Begin({Choice(1),Choice(2)},3,1,7);r=f.Finish();
        Check(r.departure.map==1&&r.steps==1,"A zone missing from the graph starts every zone of the current map");}
    // A zaap map split into zones: every zone starts at 2, the arrival side being unknown.
    {Fixture f;auto close1=Vertex(5,1),close2=Vertex(5,2),distant=Vertex(6),a=Vertex(7),b=Vertex(8),c=Vertex(9),dest=Vertex(3);(void)close2;
        Edge(close1,dest);Edge(distant,a);Edge(a,b);Edge(b,c);Edge(c,dest);
        Begin({Choice(6),Choice(5)},3,99);auto r=f.Finish();
        Check(r.status==Status::Found&&r.departure.map==5&&r.steps==3,"A closer zaap on a split map beats a farther single-zone zaap");}
    {Fixture f;auto split1=Vertex(5,1),split2=Vertex(5,2),single=Vertex(6),dest=Vertex(3);(void)split2;Edge(split1,dest);Edge(single,dest);
        Begin({Choice(5),Choice(6)},3,99);auto r=f.Finish();
        Check(r.departure.map==6&&r.steps==2,"For an equal route the unambiguous single-zone arrival wins");}
    // Only the game's own destination ends the search. Review case (Otomaï): on
    // 63965954 [-51,18], knowing the zaap 28050436, a TP to [-53,18] where zid
    // also lists that zaap's own layer before the game's choice 63964930. The
    // zaap layer is a departure like any other, never an arrival: walk 2 maps.
    {Fixture f;auto here=Vertex(63965954),mid=Vertex(63965442),chosen=Vertex(63964930),zaapLayer=Vertex(28050436);
        Edge(here,mid);Edge(mid,chosen);Obj from=zaapLayer;for(int i=0;i<4;++i){auto step=Vertex(800000+i);Edge(from,step);from=step;}Edge(from,chosen);
        Begin({Choice(28050436,-53,18),Choice(63965954,-51,18)},63964930,63965954);auto r=f.Finish();
        Check(r.status==Status::Found&&r.departure.map==63965954&&r.steps==2,"A zaap on another layer of the coordinates does not end the search: walk 2 maps to the game's choice");}
    // An equal route from the current map beats a teleport (no kamas, no loading).
    {Fixture f;auto zaap=Vertex(1),here=Vertex(2),mid=Vertex(4),dest=Vertex(3);Edge(zaap,dest);Edge(here,mid);Edge(mid,dest);
        Begin({Choice(1),Choice(2)},3,2);auto r=f.Finish();
        Check(r.departure.map==2&&r.steps==2,"Equal route: walking from the current map wins over the zaap");}
    // The user's r109 log: on [9,17] (82313729, split into zones), a TP to [9,17]
    // and a double-click on [10,17] took the zaap of [10,22] (88085249), then
    // walked 14 to 15 maps back: the current map was never a departure. The
    // game's choices are the priority maps 82313729 and 82314241.
    {Fixture f;auto here1=Vertex(82313729,1),here2=Vertex(82313729,2),crypt=Vertex(83104256),east=Vertex(82314241),eastCrypt=Vertex(83101186),zaap=Vertex(88085249);
        Edge(here1,east);Edge(east,here1);Edge(here2,crypt);Edge(crypt,eastCrypt);
        Obj from=zaap;for(int i=0;i<12;++i){auto step=Vertex(700000+i);Edge(from,step);from=step;}Edge(from,east);
        Begin({Choice(88085249,10,22)},82313729,82313729);auto r=f.Finish();
        Check(r.departure.map==88085249&&r.steps==15,"Without the current map, the r109 choice is reproduced: zaap of [10,22], routeScore=15");
        Begin({Choice(88085249,10,22)},82314241,82313729);r=f.Finish();
        Check(r.departure.map==88085249&&r.steps==14,"Without the current map, the r109 choice is reproduced: zaap of [10,22], routeScore=14");
        Begin({Choice(88085249,10,22),Choice(82313729,9,17)},82313729,82313729);r=f.Finish();
        Check(r.departure.map==82313729&&r.steps==0,"TP to the current map: reached at once from here (chat travel stops before: already there)");
        Begin({Choice(88085249,10,22),Choice(82313729,9,17)},82314241,82313729);r=f.Finish();
        Check(r.departure.map==82313729&&r.steps==1,"Double-click on the adjacent [10,17] walks one map, not the zaap of [10,22]");}
    // The user's r109 log: from the Havre-Sac (162791424, no current-map departure)
    // to [-2,-10] (190841348), the zaap of [-5,-8] (185860609, a map split into
    // zones) was skipped and the one of [3,-5] (68552706) used, routeScore=12.
    {Fixture f;auto west1=Vertex(185860609,1),west2=Vertex(185860609,2),east=Vertex(68552706),dest=Vertex(190841348);(void)west2;
        const auto chain=[&](Obj from,int length,int64_t first){for(int i=0;i<length-1;++i){auto step=Vertex(first+i);Edge(from,step);from=step;}Edge(from,dest);};
        chain(west1,5,500000);chain(east,11,600000);
        Begin({Choice(68552706,3,-5),Choice(185860609,-5,-8)},190841348,162791424);auto r=f.Finish();
        Check(r.status==Status::Found&&r.departure.map==185860609&&r.steps==7,"The closer split-map zaap of [-5,-8] wins (2+5 < 1+11)");
        Check(logged("CHAT_ROUTE_SEEDED target=190841348 candidates=2 starts=3 hereStarts=0 splitZaaps=1"),"Every zone of the split zaap starts the search");}
    // r111. The Roublard temple under the Amakna cemetery, with the game's own ids:
    // door map 82314240 -> hall 67108864 -> 67110912 -> 67111936 -> statue room
    // 67113984, whose statue teleports to the Domaine des dieux 188219392 and the
    // Temple des Douze 192416776; the hall leads back out to 82314240. The statue's
    // edges carry the game's criteria (PG=13...): `statue` is their native result.
    const auto temple=[](bool statue){
        auto cemetery=Vertex(82313729),door=Vertex(82314240),hall=Vertex(67108864),west=Vertex(67110912),inner=Vertex(67111936),room=Vertex(67113984);
        auto domaine=Vertex(188219392),douze=Vertex(192416776);
        Edge(cemetery,door);Edge(door,cemetery);Edge(door,hall,true,true,32);Edge(hall,door,true,true,8);
        Edge(hall,west,true,true,8);Edge(west,hall,true,true,32);Edge(west,inner,true,true,32);Edge(inner,west,true,true,32);
        Edge(inner,room,true,true,32);Edge(room,inner,true,true,32);
        Edge(room,domaine,statue,true,32);Edge(room,douze,statue,true,32);Edge(domaine,room,statue,true,32);
        Obj from=door;for(int i=0;i<8;++i){auto step=Vertex(900000+i);Edge(from,step);from=step;}Edge(from,douze); // the long way round (Astrub)
    };
    {Fixture f;temple(true);
        Begin({Choice(82313729)},188219392,82313729);auto r=f.Finish();
        Check(r.status==Status::Found&&r.departure.map==82313729&&r.steps==6&&logged("temple=1"),"Temple option on: its door and statue reach the Domaine des dieux");
        Begin({Choice(82313729)},192416776,82313729);r=f.Finish();
        Check(r.status==Status::Found&&r.steps==6,"Temple option on: the statue shortens the trip to the Temple des Douze");
        Begin({Choice(82313729)},188219392,82313729,-1,false);r=f.Finish();
        Check(r.status==Status::Unreachable&&logged("temple=0"),"Temple option off: no way into the temple, no statue teleport");
        Begin({Choice(82313729)},192416776,82313729,-1,false);r=f.Finish();
        Check(r.status==Status::Found&&r.steps==10,"Temple option off: the Temple des Douze by the long way round");}
    {Fixture f;temple(true);
        Begin({Choice(67113984)},82313729,67113984,-1,false);auto r=f.Finish();
        Check(r.status==Status::Found&&r.steps==5,"Temple option off: a character inside the temple still walks out through its door");
        Begin({Choice(67113984)},192416776,67113984,-1,false);r=f.Finish();
        Check(r.status==Status::Found&&r.steps==13,"Temple option off: no statue teleport, even from the statue room");
        Begin({Choice(67113984)},192416776,67113984);r=f.Finish();
        Check(r.status==Status::Found&&r.steps==1,"Temple option on: one teleport from the statue room");
        Begin({Choice(188219392)},82313729,188219392);r=f.Finish();
        Check(r.status==Status::Found&&r.steps==6,"Temple option on: the statue's return from the Domaine des dieux");
        Begin({Choice(188219392)},82313729,188219392,-1,false);r=f.Finish();
        Check(r.status==Status::Unreachable,"Temple option off: the statue's return is not used either");}
    // A character the game's criteria refuse (not a Roublard, or the quest not
    // done): the statue's edges fail ens.wvs, the doors stay open to everyone.
    {Fixture f;temple(false);
        Begin({Choice(82313729)},188219392,82313729);auto r=f.Finish();
        Check(r.status==Status::Unreachable&&criteria>0,"The game's criteria refuse the statue: no temple shortcut for that character");
        Begin({Choice(82313729)},192416776,82313729);r=f.Finish();
        Check(r.status==Status::Found&&r.steps==10,"Without the statue, the long way round");
        Begin({Choice(82313729)},67113984,82313729);r=f.Finish();
        Check(r.status==Status::Found&&r.steps==5,"The temple's doors stay open to every character");}
    Check(handles.empty(),"Route roots leaked");std::cout<<"PASS: real route comparison, barriers, conditions, interactive edges, cycles, current and zaap maps split into zones, the player's own zone, only the game's destination map, the Roublard temple option, loading and session changes\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
