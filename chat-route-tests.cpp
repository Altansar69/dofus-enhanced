#include "chat-route.hpp"
#include "module.hpp"
#include "features.hpp"
#include "options-ui.hpp"
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
void Check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
Object& Get(Obj o){Check(o!=nullptr,"Null fake object");return *static_cast<Object*>(o);}
Obj Make(std::string t){auto o=std::make_unique<Object>();o->type=t;auto p=o.get();objects.push_back(std::move(o));return p;}
template<class T> Obj Box(T v){auto o=Make("Box");std::memcpy(Get(o).data.data(),&v,sizeof(v));return o;}
Obj Vertex(int64_t map,int zone=1){
    auto v=Make("Vertex");Get(v).numbers={{"m_uid",map*10+zone},{"m_mapId",map}};
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
    Fixture(){chat_route::Clear();module=&owner;owner.servicesReady=true;loaded=true;loads=criteria=areaChecks=0;
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
void Log(const std::string&){}
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
    {Fixture f;auto a=Vertex(1),dest=Vertex(2);Vertex(1,2);Edge(a,dest);Begin({Choice(1)},2,99);Check(f.Finish().status==Status::Unreachable,"Ambiguous arrival zone must not select an arbitrary path");}
    {Fixture f;auto a=Vertex(1),dest=Vertex(2);Edge(a,dest);auto bad=Choice(1);bad.known=false;Begin({bad},2,99);Check(f.Finish().status==Status::Unreachable,"Unknown zaap cannot enter search");}
    {Fixture f;auto a=Vertex(1),dest=Vertex(2);Edge(a,dest);loaded=false;Begin({Choice(1)},2,99);Check(Tick().status==Status::Pending&&loads==1,"Lazy graph loading is read-only and deferred");Tick();Check(loads==1,"Graph load is requested only once");loaded=true;Check(f.Finish().status==Status::Found,"Loaded graph resumes pending planning");}
    {Fixture f;auto a=Vertex(1),dest=Vertex(2);Edge(a,dest);Begin({Choice(1)},2,99);services["fkq"]=Make("Replacement");Check(Tick().status==Status::Unreachable,"Changed character criteria invalidates planning");}
    Check(handles.empty(),"Route roots leaked");std::cout<<"PASS: real route comparison, barriers, conditions, interactive edges, cycles, zones, loading and session changes\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
