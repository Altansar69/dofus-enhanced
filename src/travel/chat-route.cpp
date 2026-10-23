#include "travel/chat-route.hpp"
#include "core/module.hpp"
#include <algorithm>
#include <chrono>
#include <queue>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace chat_route {
namespace {
using namespace managed;
constexpr const char* Space="Core.PathFinding.WorldPathfinding";
struct Node { int distance; size_t source; uint64_t uid; Ref vertex; };
struct Later { bool operator()(const Node& a,const Node& b)const {
    return std::tie(a.distance,a.source,a.uid)>std::tie(b.distance,b.source,b.uid);
} };
std::priority_queue<Node,std::vector<Node>,Later> queue;
std::unordered_set<uint64_t> visited;
std::unordered_map<uint64_t,std::pair<int,size_t>> best;
std::vector<chat_travel::Destination> choices;
Ref graph, vertices, outgoing, filter, loader, pathService, dependencies;
Method getGraph{},init{},edgeAllowed{},areaAllowed{},initFilter{};
Klass graphClass{},vertexClass{},edgeClass{},filterClass{},loaderClass{},serviceClass{};
int64_t target=0,current=0;
int currentZone=-1;
bool templeAllowed=true;
bool seeded=false,loading=false;
size_t inspected=0;
size_t slices=0;
std::chrono::steady_clock::time_point searchStarted;
// Keep a high-degree vertex resumable within the per-frame time budget. The
// installed graph contains a legitimate 296-edge hub (map 81789954).
Ref pendingEdges;
int edgeIndex=0,edgeCount=0,pendingDistance=0;
size_t pendingSource=0;
int64_t pendingMap=0;
// The Roublard temple under the Amakna cemetery: its interior maps (world -1,
// subarea 6, all at [10,16]) and the cemetery map of its door, as the game's own
// graph names them. Its statue teleports to the Temple des Douze and the Domaine
// des dieux; the game's criteria (ens.wvs) decide who may use them.
constexpr int64_t TempleMaps[]={67108864,67109888,67110912,67111936,67112960,67113984,67110147};
constexpr int64_t TempleDoorMap=82314240;
bool InTemple(int64_t map) {return std::find(std::begin(TempleMaps),std::end(TempleMaps),map)!=std::end(TempleMaps);}
// With « Passer par le temple Roublard » off: entering the temple (its cemetery
// door, the statue's return from the Domaine des dieux) and leaving it by the
// statue's teleports. Walking inside it and out through the cemetery door stays
// allowed, so a character already inside can still leave.
bool TempleShortcut(int64_t from,int64_t to) {return InTemple(to)?!InTemple(from):InTemple(from)&&to!=TempleDoorMap;}
int Count(Obj list,int limit,const char* context) {
    if(!list)return 0;
    const int n=api.Value<int>(api.Call(list,"get_Count"));
    if(n<0||n>limit)throw std::runtime_error(std::string("Route ")+context+" count="+std::to_string(n)+" limit="+std::to_string(limit));
    return n;
}
template<class Key> Obj Lookup(Obj dictionary,Key key) {
    if(!dictionary)return nullptr;
    Obj value=nullptr;
    return api.Value<bool>(api.Call(dictionary,"TryGetValue",{&key,&value}))?value:nullptr;
}
bool Exact(Obj object,Klass type) {return object&&api.object_get_class(object)==type;}
bool Session() {
    return module&&module->servicesReady&&!module->stopped&&!module->stopRequested&&
        module->FindService("fnf")==pathService.get()&&module->FindService("fkq")==dependencies.get();
}
bool Improves(uint64_t uid,int distance,size_t source) {
    if(visited.contains(uid))return false;
    const auto found=best.find(uid);
    return found==best.end()||std::pair{distance,source}<found->second;
}
void Enqueue(Obj vertex,uint64_t uid,int distance,size_t source) {
    best[uid]={distance,source};
    queue.push({distance,source,uid,Ref(vertex)});
}
// Start distances, in map changes; a teleport adds 1. The current map starts at
// 0: only the player's own zone when the game's cell-to-zone link gave it, else
// every zone of the map (the game's autopilot then walks from the real cell). A
// zaap starts at 1, but on a map split into several zones the arrival side of
// the zaap is unknown: every zone then starts at 2. A closer zaap is never
// hidden, and an unambiguous arrival still wins an otherwise equal route.
constexpr int HereStart=0, ZaapStart=1, SplitZaapStart=2;
void Seed() {
    vertices.reset(api.Field(graph.get(),"m_vertices"));outgoing.reset(api.Field(graph.get(),"m_outgoingEdges"));
    if(!vertices.get()||!outgoing.get())throw std::runtime_error("World route graph incomplete");
    filter.reset(api.New(filterClass));api.Invoke(initFilter,filter.get());
    int hereStarts=0,splitZaaps=0;
    for(size_t i=0;i<choices.size();++i) {
        const auto& choice=choices[i];
        Ref zones(Lookup(vertices.get(),choice.map));
        const int count=Count(zones.get(),64,"zones");
        if(!count)continue;
        Ref values(api.Call(zones.get(),"get_Values"));Ref iterator(api.Call(values.get(),"GetEnumerator"));
        std::vector<Ref> starts;
        while(static_cast<int>(starts.size())<count&&api.Value<bool>(api.Call(iterator.get(),"MoveNext"))) {
            Ref vertex(api.Call(iterator.get(),"get_Current"));
            if(!Exact(vertex.get(),vertexClass))throw std::runtime_error("World route vertex mismatch");
            starts.push_back(std::move(vertex));
        }
        const bool here=choice.map==current;
        if(here&&currentZone>=0) {
            const auto own=std::find_if(starts.begin(),starts.end(),[](const Ref& vertex) {
                return *static_cast<int*>(api.FieldAddress(vertex.get(),"m_zoneId"))==currentZone;
            });
            if(own!=starts.end()) {Ref vertex(std::move(*own));starts.clear();starts.push_back(std::move(vertex));}
        }
        const int distance=here?HereStart:starts.size()==1?ZaapStart:SplitZaapStart;
        if(!here&&starts.size()>1)++splitZaaps;
        for(const auto& vertex:starts) {
            const auto uid=*static_cast<uint64_t*>(api.FieldAddress(vertex.get(),"m_uid"));
            if(!Improves(uid,distance,i))continue;
            Enqueue(vertex.get(),uid,distance,i);
            if(here)++hereStarts;
        }
    }
    seeded=true;
    Log("CHAT_ROUTE_SEEDED target="+std::to_string(target)+" candidates="+std::to_string(choices.size())+" starts="+std::to_string(queue.size())+
        " hereStarts="+std::to_string(hereStarts)+" splitZaaps="+std::to_string(splitZaaps)+" temple="+std::to_string(templeAllowed));
}
}
void Install() {
    loaderClass=api.Class("Core.dll","","ezc");filterClass=api.Class("Core.dll","","ens");serviceClass=api.Class("Core.dll","","eza");
    graphClass=api.Class("Core.dll",Space,"PathFindingData");vertexClass=api.Class("Core.dll",Space,"Vertex");edgeClass=api.Class("Core.dll",Space,"Edge");
    getGraph=api.FindVerified(loaderClass,"bbno",0,0x19B7A00);
    init=api.FindVerified(loaderClass,"bbnn",0,0x19B7970);
    api.FindVerified(loaderClass,".ctor",0,0x19B7910);
    api.FindVerified(filterClass,".ctor",0,0x17E38E0);
    initFilter=api.FindVerified(filterClass,"wvo",0,0x17E5FB0);
    edgeAllowed=api.FindVerified(filterClass,"wvs",1,0x17E6C60);
    areaAllowed=api.FindVerified(filterClass,"wvt",1,0x17E6E70);
}
void Clear() {
    queue={};visited.clear();best.clear();choices.clear();graph.reset();vertices.reset();outgoing.reset();filter.reset();loader.reset();
    pathService.reset();dependencies.reset();target=current=0;currentZone=-1;templeAllowed=true;seeded=loading=false;inspected=0;
    pendingEdges.reset();edgeIndex=edgeCount=pendingDistance=0;pendingSource=0;pendingMap=0;slices=0;
}
void Begin(std::vector<chat_travel::Destination> candidates,int64_t destination,int64_t source,int sourceZone,bool roublardTemple) {
    Clear();target=destination;current=source;currentZone=sourceZone;templeAllowed=roublardTemple;searchStarted=std::chrono::steady_clock::now();
    pathService.reset(module->FindService("fnf"));dependencies.reset(module->FindService("fkq"));
    if(!Exact(pathService.get(),serviceClass)||!dependencies.get())throw std::runtime_error("Native route conditions unavailable");
    for(auto value:candidates)if(value.map>0&&value.known&&value.type==0&&value.cost>=0)choices.push_back(value);
    // An equal route from the current map beats a teleport: no kamas, no loading.
    std::sort(choices.begin(),choices.end(),[](const auto& a,const auto& b){
        return std::tuple(a.map!=current,a.cost,a.map)<std::tuple(b.map!=current,b.cost,b.map);
    });
}
Result Tick() {
    if(!target||!Session())return {Status::Unreachable};
    if(!graph.get()) {
        graph.reset(api.Invoke(getGraph,nullptr));
        if(!graph.get()) {
            if(!loading) {loader.reset(api.New(loaderClass));api.Invoke(init,loader.get());loading=true;}
            return {};
        }
        if(!Exact(graph.get(),graphClass))throw std::runtime_error("World route graph mismatch");
    } else if(api.Invoke(getGraph,nullptr)!=graph.get())return {Status::Unreachable};
    if(!seeded)Seed();
    // GetTickCount64 has a coarse Windows tick and cannot enforce a 2ms slice.
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(2);
    ++slices;
    for(int budget=0;budget<256&&std::chrono::steady_clock::now()<deadline;++budget) {
        if(!pendingEdges.get()) {
            if(queue.empty())return {Status::Unreachable};
            const auto& top=queue.top();
            Node node{top.distance,top.source,top.uid,Ref(top.vertex.get())};queue.pop();
            if(best.at(node.uid)!=std::pair{node.distance,node.source})continue;
            if(!visited.insert(node.uid).second)continue;
            if(visited.size()>65536||node.distance>4096)throw std::runtime_error("World route search bounds");
            const auto map=*static_cast<int64_t*>(api.FieldAddress(node.vertex.get(),"m_mapId"));
            if(map==target) {
                const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-searchStarted).count();
                Log("CHAT_ROUTE_FOUND elapsedMs="+std::to_string(elapsed)+" slices="+std::to_string(slices)+
                    " vertices="+std::to_string(visited.size())+" edges="+std::to_string(inspected));
                return {Status::Found,choices[node.source],node.distance};
            }
            Ref wrapper(Lookup(outgoing.get(),node.uid));if(!wrapper.get())continue;
            pendingEdges.reset(api.Field(wrapper.get(),"m_edgeList"));
            edgeCount=Count(pendingEdges.get(),1024,"outgoing edges");edgeIndex=0;
            pendingDistance=node.distance;pendingSource=node.source;pendingMap=map;
        }
        if(edgeIndex<edgeCount) {
            if(++inspected>262144)throw std::runtime_error("World route edge bounds");
            int index=edgeIndex++;
            Ref edge(api.Call(pendingEdges.get(),"get_Item",{&index}));
            if(!Exact(edge.get(),edgeClass))throw std::runtime_error("World route edge mismatch");
            Ref to(api.Field(edge.get(),"m_to"));
            if(!Exact(to.get(),vertexClass))throw std::runtime_error("World route destination mismatch");
            const auto uid=*static_cast<uint64_t*>(api.FieldAddress(to.get(),"m_uid"));
            // Only skip when an already accepted path is as good or better.
            // A rejected edge never marks its destination reached.
            if(!Improves(uid,pendingDistance+1,pendingSource))continue;
            if(!templeAllowed&&TempleShortcut(pendingMap,*static_cast<int64_t*>(api.FieldAddress(to.get(),"m_mapId"))))continue;
            Ref transitions(api.Field(edge.get(),"m_transitions"));
            if(Count(transitions.get(),128,"transitions")==0)continue;
            // The exact filters used by ens.wvq: character criteria, then
            // native areas excluded from automatic travel. No UI/network action.
            if(!api.Value<bool>(api.Invoke(edgeAllowed,nullptr,{edge.get()}))||
                !api.Value<bool>(api.Invoke(areaAllowed,filter.get(),{edge.get()})))continue;
            Enqueue(to.get(),uid,pendingDistance+1,pendingSource);
        }
        if(edgeIndex>=edgeCount)pendingEdges.reset();
    }
    return queue.empty()&&!pendingEdges.get()?Result{Status::Unreachable}:Result{};
}
}
