#include "travel/chat-route.hpp"
#include "core/module.hpp"
#include "travel/route-graph.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <tuple>

namespace chat_route {
namespace {
using namespace managed;
using route_graph::Clock;
constexpr const char* Space="Core.PathFinding.WorldPathfinding";
// Main-thread time per Tick; the planner ticks every frame while it plans. The
// first search reads the game's graph (about 11k vertices and 31k edges) in a
// few ticks at most; the next ones reuse that copy and usually end in their
// first tick.
std::chrono::microseconds tickBudget{6000};

// IL2CPP layouts of the supported client (3.7.12.12). Fields are checked against
// the metadata (Install, and the first List`1 of each kind); dictionary entries,
// a generic layout the metadata does not give, against the game's own lookups
// before any value is used. Objects: fields from 0x10. Arrays: length at 0x18,
// items from 0x20. Strings: length at 0x10.
constexpr size_t ArrayLength=0x18,ArrayItems=0x20,StringLength=0x10;
// Dictionary`2: _entries, _count, _freeCount, _version; Entry {int hashCode,
// next, key, value} is 24 bytes for the 4- and 8-byte keys and the reference
// values read here. A removed entry has hashCode < 0 (the game's enumerator
// skips it) and Count is _count - _freeCount.
constexpr size_t DictionaryEntries=0x18,DictionaryCount=0x20,DictionaryFree=0x28,DictionaryVersion=0x2C;
constexpr size_t EntrySize=24,EntryHash=0,EntryKey=8,EntryValue=16;
constexpr size_t ListItems=0x10,ListSize=0x18;
constexpr size_t GraphVertices=0x18,GraphOutgoing=0x28,WrapperEdges=0x10;
constexpr size_t VertexMap=0x10,VertexZone=0x18,VertexUid=0x20;
constexpr size_t EdgeDestination=0x18,EdgeTransitions=0x20,TransitionCriterion=0x20;

Method getGraph{},init{},edgeAllowed{},areaAllowed{},initFilter{};
Klass graphClass{},vertexClass{},edgeClass{},wrapperClass{},transitionClass{},verticesClass{},zonesClass{},outgoingClass{};
Klass stringClass{},filterClass{},loaderClass{},serviceClass{};

template<class T> T At(const void* base,size_t offset) {T value;std::memcpy(&value,static_cast<const char*>(base)+offset,sizeof value);return value;}
bool Exact(Obj object,Klass type) {return object&&api.object_get_class(object)==type;}
bool HasField(Klass type,const char* name,size_t offset) {
    for(auto owner=type;owner;owner=api.class_get_parent(owner))
        if(auto field=api.class_get_field_from_name(owner,name))return api.field_get_offset(field)==offset;
    return false;
}
void RequireField(Klass type,const char* name,size_t offset) {
    if(!HasField(type,name,offset))throw std::runtime_error(std::string("Route field mismatch: ")+name);
}
template<class Key> Obj Lookup(Obj dictionary,Key key) {
    if(!dictionary)return nullptr;
    Obj value=nullptr;
    return api.Value<bool>(api.Call(dictionary,"TryGetValue",{&key,&value}))?value:nullptr;
}
std::string Ms(Clock::duration time) {
    char text[32];
    std::snprintf(text,sizeof text,"%.1f",std::chrono::duration<double,std::milli>(time).count());
    return text;
}

// A Dictionary`2's entries, readable when its counts and array agree.
struct Entries { const char* first=nullptr; int count=0,live=0; bool readable=false; };
bool Live(const Entries& entries,int index) {return At<int32_t>(entries.first+static_cast<size_t>(index)*EntrySize,EntryHash)>=0;}
template<class Key> Key KeyAt(const Entries& entries,int index) {return At<Key>(entries.first+static_cast<size_t>(index)*EntrySize,EntryKey);}
Obj ValueAt(const Entries& entries,int index) {return At<Obj>(entries.first+static_cast<size_t>(index)*EntrySize,EntryValue);}
Entries EntriesOf(Obj dictionary) {
    Entries entries;
    entries.count=At<int>(dictionary,DictionaryCount);
    entries.live=entries.count-At<int>(dictionary,DictionaryFree);
    if(entries.count<0||entries.live<0||entries.live>entries.count)return entries;
    if(entries.count) {
        const auto array=At<Obj>(dictionary,DictionaryEntries);
        if(!array||At<uint64_t>(array,ArrayLength)<static_cast<uint64_t>(entries.count))return entries;
        entries.first=static_cast<const char*>(array)+ArrayItems;
        int live=0;
        for(int index=0;index<entries.count;++index)live+=Live(entries,index);
        if(live!=entries.live)return entries;
    }
    entries.readable=true;
    return entries;
}
// The graph's own dictionaries: before any value is used, the game's lookup
// must find the values read for the first, middle and last live keys.
template<class Key> Entries GraphEntries(Obj dictionary,const char* context) {
    const auto entries=EntriesOf(dictionary);
    if(!entries.readable)throw std::runtime_error(std::string("World route ")+context+" dictionary layout");
    std::vector<int> live;
    for(int index=0;index<entries.count;++index)if(Live(entries,index))live.push_back(index);
    if(!live.empty())
        for(const auto index:{live.front(),live[live.size()/2],live.back()})
            if(Lookup(dictionary,KeyAt<Key>(entries,index))!=ValueAt(entries,index))
                throw std::runtime_error(std::string("World route ")+context+" layout mismatch");
    return entries;
}
// The zones of a map, enumerated as r111 enumerated them: the same values.
void CheckZones(Obj zones,const Entries& entries) {
    Ref values(api.Call(zones,"get_Values"));Ref iterator(api.Call(values.get(),"GetEnumerator"));
    for(int index=0;index<entries.count;++index) {
        if(!Live(entries,index))continue;
        if(!api.Value<bool>(api.Call(iterator.get(),"MoveNext"))||api.Call(iterator.get(),"get_Current")!=ValueAt(entries,index))
            throw std::runtime_error("World route zones layout mismatch");
    }
}
// A List`1 of a checked class: its count, and its items when that count is
// within [1, limit] and its array holds them.
struct Items { const char* first=nullptr; int count=0; bool readable=false; };
// The class of a list held by a field the game types List<Edge> or
// List<Transition> (IL2CPP enforces it), with the layout read here.
bool ListClass(Obj list) {
    const auto type=api.object_get_class(list);
    return HasField(type,"_items",ListItems)&&HasField(type,"_size",ListSize);
}
Items ItemsOf(Obj list,Klass listClass,int limit) {
    Items items;
    if(!listClass||api.object_get_class(list)!=listClass)return items;
    items.count=At<int>(list,ListSize);
    if(items.count<1||items.count>limit) {items.readable=true;return items;}
    const auto array=At<Obj>(list,ListItems);
    if(!array||At<uint64_t>(array,ArrayLength)<static_cast<uint64_t>(items.count))return items;
    items.first=static_cast<const char*>(array)+ArrayItems;
    items.readable=true;
    return items;
}
Obj ItemAt(const Items& items,int index) {return At<Obj>(items.first+static_cast<size_t>(index)*sizeof(Obj),0);}

// The game's graph as plain data, kept while ezc.bbno returns the same object
// and its dictionaries stay the same, unchanged.
struct Snapshot {
    Ref graph;  // keeps the copied graph alive: its address names it, its edges stay valid
    route_graph::Graph data;
    std::vector<Obj> edges;     // the managed Edge of each data edge
    std::vector<Ref> held;      // what only the game's own calls gave (kept alive)
    std::vector<uint8_t> areas; // ens.wvt per edge, for the graph's lifetime (see GameAllows)
    Ref filter;                 // the private ens answering ens.wvt
    bool ready=false;
    // Reading, a slice at a time.
    route_graph::Builder builder;
    Obj vertices=nullptr,outgoing=nullptr;
    int verticesVersion=0,outgoingVersion=0,mapCursor=0,listCursor=0;
    Klass edgeLists=nullptr,transitionLists=nullptr;  // List<Edge> and List<Transition>, from the first of each
    bool zonesChecked=false;
    size_t managed=0;  // elements read through the game's own calls (another class, a damaged array)
    Clock::duration spent{};
    size_t slices=0;
};
Snapshot snapshot;
enum Answer : uint8_t { Unasked, Accepted, Refused };

// The current search.
std::vector<chat_travel::Destination> choices;
std::vector<Shortcut> shortcuts,searchShortcuts;
route_graph::Search search;
std::vector<uint8_t> criteria;  // ens.wvs per edge, for this search only
Ref graph,loader,pathService,dependencies;
int64_t target=0,current=0;
int currentZone=-1;
bool templeAllowed=true;
bool seeded=false,loading=false,readHere=false;
size_t slices=0,criteriaCalls=0,areaCalls=0;
Clock::time_point searchStarted;
Clock::duration searchSpent{};

bool Session() {
    return module&&module->servicesReady&&!module->stopped&&!module->stopRequested&&
        module->FindService("fnf")==pathService.get()&&module->FindService("fkq")==dependencies.get();
}
// Anything the direct reading does not take (another class, damaged counts) is
// read through the calls r111 made, and its failure kept for when the search
// reaches it: a malformed element fails where r111 failed, never the whole graph.
void ReadMapManaged(int64_t map,Obj zones) {
    auto& builder=snapshot.builder;
    ++snapshot.managed;
    int count=0;
    try {count=api.Value<int>(api.Call(zones,"get_Count"));}
    catch(const std::exception& e) {builder.Map(map,0);builder.MapCountError(e.what());return;}
    builder.Map(map,count);
    if(count<=0||count>route_graph::ZonesLimit)return;  // r111 stopped before the values
    try {
        Ref values(api.Call(zones,"get_Values"));Ref iterator(api.Call(values.get(),"GetEnumerator"));
        for(int read=0;read<count&&api.Value<bool>(api.Call(iterator.get(),"MoveNext"));++read) {
            Ref vertex(api.Call(iterator.get(),"get_Current"));
            if(!Exact(vertex.get(),vertexClass)) {builder.MapValuesError("World route vertex mismatch");return;}
            builder.ZoneVertex(At<uint64_t>(vertex.get(),VertexUid),At<int64_t>(vertex.get(),VertexMap),At<int32_t>(vertex.get(),VertexZone));
        }
    } catch(const std::exception& e) {builder.MapValuesError(e.what());}
}
void ReadMap(int64_t map,Obj zones) {
    auto& builder=snapshot.builder;
    // r111 read the zones through TryGetValue: a missing value had no zone.
    if(!zones) {builder.Map(map,0);return;}
    if(!Exact(zones,zonesClass)) {ReadMapManaged(map,zones);return;}
    const auto entries=EntriesOf(zones);
    if(!entries.readable) {ReadMapManaged(map,zones);return;}
    if(!snapshot.zonesChecked&&entries.live) {CheckZones(zones,entries);snapshot.zonesChecked=true;}
    builder.Map(map,entries.live);
    for(int index=0;index<entries.count;++index) {
        if(!Live(entries,index))continue;
        const auto vertex=ValueAt(entries,index);
        if(!Exact(vertex,vertexClass)) {builder.MapValuesError("World route vertex mismatch");return;}
        builder.ZoneVertex(At<uint64_t>(vertex,VertexUid),At<int64_t>(vertex,VertexMap),At<int32_t>(vertex,VertexZone));
    }
}
// ens.wvs returns true at once when the first transition's criterion is empty,
// before any character check (verified in the 3.7.12.12 binary): only the other
// edges need the game's answer.
bool EmptyCriterion(Obj transition) {
    if(!Exact(transition,transitionClass))return false;
    const auto criterion=At<Obj>(transition,TransitionCriterion);
    return Exact(criterion,stringClass)&&At<int32_t>(criterion,StringLength)==0;
}
void ReadEdge(Obj edge) {
    auto& builder=snapshot.builder;
    snapshot.edges.push_back(edge);
    if(!Exact(edge,edgeClass)) {builder.BadEdge("World route edge mismatch");return;}
    const auto to=At<Obj>(edge,EdgeDestination);
    if(!Exact(to,vertexClass)) {builder.BadEdge("World route destination mismatch");return;}
    const auto uid=At<uint64_t>(to,VertexUid);
    const auto map=At<int64_t>(to,VertexMap);
    const auto list=At<Obj>(edge,EdgeTransitions);
    if(!list) {builder.EdgeTo(uid,map,0,true);return;}  // r111 counted no transition
    if(!snapshot.transitionLists&&ListClass(list))snapshot.transitionLists=api.object_get_class(list);
    const auto items=ItemsOf(list,snapshot.transitionLists,route_graph::TransitionsLimit);
    if(items.readable) {builder.EdgeTo(uid,map,items.count,!items.first||!EmptyCriterion(ItemAt(items,0)));return;}
    // r111 counted any other list through get_Count; ens.wvs then decides.
    ++snapshot.managed;
    try {builder.EdgeTo(uid,map,api.Value<int>(api.Call(list,"get_Count")),true);}
    catch(const std::exception& e) {builder.EdgeTo(uid,map,1,true);builder.TransitionsError(e.what());}
}
void ReadListManaged(uint64_t uid,Obj list) {
    auto& builder=snapshot.builder;
    ++snapshot.managed;
    int count=0;
    try {count=api.Value<int>(api.Call(list,"get_Count"));}
    catch(const std::exception& e) {builder.EdgeListError(uid,e.what());return;}
    builder.EdgeList(uid,count);
    if(count<0||count>route_graph::EdgesLimit)return;
    for(int index=0;index<count;++index) {
        Obj edge=nullptr;
        try {edge=api.Call(list,"get_Item",{&index});}
        catch(const std::exception& e) {snapshot.edges.push_back(nullptr);builder.BadEdge(e.what());continue;}
        snapshot.held.emplace_back(edge);
        ReadEdge(edge);
    }
}
void ReadList(uint64_t uid,Obj wrapper) {
    auto& builder=snapshot.builder;
    if(!wrapper)return;  // r111: TryGetValue gave no list, no edge
    Obj list=nullptr;
    if(Exact(wrapper,wrapperClass)) {
        list=At<Obj>(wrapper,WrapperEdges);
        // The type of EdgeListWrapper.m_edgeList: List<Edge>.
        if(list&&!snapshot.edgeLists&&ListClass(list))snapshot.edgeLists=api.object_get_class(list);
    } else {
        // r111 read m_edgeList by its name, whatever the wrapper's class.
        Ref field;
        try {field.reset(api.Field(wrapper,"m_edgeList"));}
        catch(const std::exception& e) {builder.EdgeListError(uid,e.what());return;}
        list=field.get();
        if(list)snapshot.held.push_back(std::move(field));
    }
    if(!list) {builder.EdgeList(uid,0);return;}
    const auto items=ItemsOf(list,snapshot.edgeLists,route_graph::EdgesLimit);
    if(!items.readable) {ReadListManaged(uid,list);return;}
    builder.EdgeList(uid,items.count);
    if(!items.first)return;
    for(int index=0;index<items.count;++index)ReadEdge(ItemAt(items,index));
}
// The copy stands while the game's graph keeps the same dictionaries, unchanged.
bool Unchanged() {
    const auto managedGraph=snapshot.graph.get();
    return At<Obj>(managedGraph,GraphVertices)==snapshot.vertices&&At<Obj>(managedGraph,GraphOutgoing)==snapshot.outgoing&&
        At<int>(snapshot.vertices,DictionaryVersion)==snapshot.verticesVersion&&At<int>(snapshot.outgoing,DictionaryVersion)==snapshot.outgoingVersion;
}
// Reads the game's graph into the snapshot until the deadline; true when done.
bool ReadSnapshot(Clock::time_point deadline) {
    auto& s=snapshot;
    const auto started=Clock::now();
    ++s.slices;
    try {
        if(!s.vertices) {
            const auto managedGraph=s.graph.get();
            const auto vertices=At<Obj>(managedGraph,GraphVertices),outgoing=At<Obj>(managedGraph,GraphOutgoing);
            if(!vertices||!outgoing)throw std::runtime_error("World route graph incomplete");
            if(!Exact(vertices,verticesClass)||!Exact(outgoing,outgoingClass))throw std::runtime_error("World route graph mismatch");
            const auto maps=GraphEntries<int64_t>(vertices,"maps"),lists=GraphEntries<uint64_t>(outgoing,"edges");
            s.vertices=vertices;s.outgoing=outgoing;
            s.verticesVersion=At<int>(vertices,DictionaryVersion);s.outgoingVersion=At<int>(outgoing,DictionaryVersion);
            s.builder.Reserve(static_cast<size_t>(maps.live)+maps.live/8,static_cast<size_t>(lists.live)*3,static_cast<size_t>(maps.live));
            s.edges.reserve(static_cast<size_t>(lists.live)*3);
        }
        // At least 64 entries per slice, then the clock every 64.
        size_t read=0;
        const auto due=[&] {return !(++read&63)&&Clock::now()>=deadline;};
        const auto maps=EntriesOf(s.vertices),lists=EntriesOf(s.outgoing);
        if(!maps.readable||!lists.readable)throw std::runtime_error("World route graph changed while read");
        while(s.mapCursor<maps.count) {
            const auto index=s.mapCursor++;
            if(!Live(maps,index))continue;
            ReadMap(KeyAt<int64_t>(maps,index),ValueAt(maps,index));
            if(due()) {s.spent+=Clock::now()-started;return false;}
        }
        while(s.listCursor<lists.count) {
            const auto index=s.listCursor++;
            if(!Live(lists,index))continue;
            ReadList(KeyAt<uint64_t>(lists,index),ValueAt(lists,index));
            if(due()) {s.spent+=Clock::now()-started;return false;}
        }
        s.data=s.builder.Finish();
        if(s.data.edges.size()!=s.edges.size())throw std::runtime_error("World route edges misaligned");
    } catch(...) {
        Ref managedGraph(s.graph.get());
        s=Snapshot{};  // a failed read starts over, on the same graph
        s.graph=std::move(managedGraph);
        throw;
    }
    s.areas.assign(s.data.edges.size(),Unasked);
    s.ready=true;
    s.spent+=Clock::now()-started;
    size_t asked=0,malformed=0;
    for(const auto& edge:s.data.edges) {
        asked+=edge.criteria&&edge.transitions>0&&edge.state==route_graph::EdgeState::Ok;
        malformed+=edge.state!=route_graph::EdgeState::Ok;
    }
    for(const auto& vertex:s.data.vertices)malformed+=vertex.error||vertex.degree<0||vertex.degree>route_graph::EdgesLimit;
    for(const auto& map:s.data.maps)malformed+=map.countError||map.valuesError;
    Log("CHAT_ROUTE_SNAPSHOT vertices="+std::to_string(s.data.vertices.size())+" edges="+std::to_string(s.data.edges.size())+
        " maps="+std::to_string(s.data.maps.size())+" criteria="+std::to_string(asked)+" managed="+std::to_string(s.managed)+
        " malformed="+std::to_string(malformed)+" buildMs="+Ms(s.spent)+" slices="+std::to_string(s.slices));
    return true;
}
// The game's own filters, as ens.wvq applies them: character criteria (ens.wvs),
// then the areas excluded from automatic travel (ens.wvt). No UI/network action.
bool GameAllows(uint32_t edge) {
    const auto object=snapshot.edges[edge];
    if(snapshot.data.edges[edge].criteria) {
        // Quests, level, alignment... can change between two searches.
        auto& known=criteria[edge];
        if(known==Unasked) {++criteriaCalls;known=api.Value<bool>(api.Invoke(edgeAllowed,nullptr,{object}))?Accepted:Refused;}
        if(known==Refused)return false;
    }
    // ens.wvt depends on static game data only (verified in the 3.7.12.12 binary):
    // the maps of the edge's ends (MapsInformationDataRoot.GetMapInformationById,
    // subAreaId) and the subareas excluded from automatic travel (ens.wvo lists
    // SubAreaData.mountAutoTripAllowed == false). Kept for the graph's lifetime.
    auto& area=snapshot.areas[edge];
    if(area==Unasked) {
        if(!snapshot.filter.get()) {Ref instance(api.New(filterClass));api.Invoke(initFilter,instance.get());snapshot.filter=std::move(instance);}
        ++areaCalls;
        area=api.Value<bool>(api.Invoke(areaAllowed,snapshot.filter.get(),{object}))?Accepted:Refused;
    }
    return area==Accepted;
}
const route_graph::Filter gameFilters=GameAllows;
void Seed() {
    route_graph::Query query;
    for(const auto& choice:choices)query.departures.push_back(choice.map);
    query.target=target;query.current=current;query.currentZone=currentZone;query.templeAllowed=templeAllowed;
    for(const auto& shortcut:searchShortcuts)query.shortcuts.push_back({shortcut.id,shortcut.from,shortcut.to,shortcut.cost});
    criteria.assign(snapshot.data.edges.size(),Unasked);
    search.Start(snapshot.data,std::move(query));
    seeded=true;
    const auto& stats=search.Statistics();
    Log("CHAT_ROUTE_SEEDED target="+std::to_string(target)+" candidates="+std::to_string(choices.size())+" starts="+std::to_string(stats.starts)+
        " hereStarts="+std::to_string(stats.hereStarts)+" splitZaaps="+std::to_string(stats.splitZaaps)+" temple="+std::to_string(templeAllowed)+
        " shortcuts="+std::to_string(stats.shortcuts));
}
void LogEnd(const char* what,Clock::time_point now,int shortcut) {
    const auto& stats=search.Statistics();
    const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(now-searchStarted).count();
    Log(std::string(what)+" elapsedMs="+std::to_string(elapsed)+" slices="+std::to_string(slices)+" vertices="+std::to_string(stats.visited)+
        " edges="+std::to_string(stats.inspected)+" criteria="+std::to_string(criteriaCalls)+" areas="+std::to_string(areaCalls)+
        " cpuMs="+Ms(searchSpent)+" graph="+(readHere?"read":"reused")+(shortcut>=0?" shortcut="+std::to_string(shortcut):std::string()));
}
}
void Install() {
    loaderClass=api.Class("Core.dll","","ezc");filterClass=api.Class("Core.dll","","ens");serviceClass=api.Class("Core.dll","","eza");
    graphClass=api.Class("Core.dll",Space,"PathFindingData");vertexClass=api.Class("Core.dll",Space,"Vertex");edgeClass=api.Class("Core.dll",Space,"Edge");
    wrapperClass=api.Class("Core.dll",Space,"EdgeListWrapper");transitionClass=api.Class("Core.dll",Space,"Transition");
    verticesClass=api.Class("Core.dll",Space,"DictionaryMapIdVertices");zonesClass=api.Class("Core.dll",Space,"DictionaryZoneVertex");
    outgoingClass=api.Class("Core.dll",Space,"DictionaryUlongEdgeList");stringClass=api.Class("mscorlib.dll","System","String");
    getGraph=api.FindVerified(loaderClass,"bbno",0,0x19B7A00);
    init=api.FindVerified(loaderClass,"bbnn",0,0x19B7970);
    api.FindVerified(loaderClass,".ctor",0,0x19B7910);
    api.FindVerified(filterClass,".ctor",0,0x17E38E0);
    initFilter=api.FindVerified(filterClass,"wvo",0,0x17E5FB0);
    edgeAllowed=api.FindVerified(filterClass,"wvs",1,0x17E6C60);
    areaAllowed=api.FindVerified(filterClass,"wvt",1,0x17E6E70);
    RequireField(graphClass,"m_vertices",GraphVertices);RequireField(graphClass,"m_outgoingEdges",GraphOutgoing);
    RequireField(vertexClass,"m_mapId",VertexMap);RequireField(vertexClass,"m_zoneId",VertexZone);RequireField(vertexClass,"m_uid",VertexUid);
    RequireField(edgeClass,"m_to",EdgeDestination);RequireField(edgeClass,"m_transitions",EdgeTransitions);
    RequireField(wrapperClass,"m_edgeList",WrapperEdges);RequireField(transitionClass,"m_criterion",TransitionCriterion);
    for(const auto dictionary:{verticesClass,zonesClass,outgoingClass}) {
        RequireField(dictionary,"_entries",DictionaryEntries);RequireField(dictionary,"_count",DictionaryCount);
        RequireField(dictionary,"_freeCount",DictionaryFree);RequireField(dictionary,"_version",DictionaryVersion);
    }
    RequireField(stringClass,"_stringLength",StringLength);
}
void SetTickBudget(int microseconds) {tickBudget=std::chrono::microseconds(std::max(microseconds,0));}
void SetShortcuts(std::vector<Shortcut> next) {
    const bool same=next.size()==shortcuts.size()&&std::equal(next.begin(),next.end(),shortcuts.begin(),[](const Shortcut& a,const Shortcut& b) {
        return std::tie(a.id,a.from,a.to,a.cost)==std::tie(b.id,b.from,b.to,b.cost);
    });
    shortcuts=std::move(next);
    if(!same)Log("CHAT_ROUTE_SHORTCUTS count="+std::to_string(shortcuts.size()));
}
void Clear() {
    search.Clear();criteria.clear();choices.clear();searchShortcuts.clear();graph.reset();loader.reset();pathService.reset();dependencies.reset();
    target=current=0;currentZone=-1;templeAllowed=true;seeded=loading=readHere=false;slices=criteriaCalls=areaCalls=0;searchSpent={};
    // The copy of the graph outlives a search; the module's stop releases it.
    if(!module||module->stopped||module->stopRequested)snapshot=Snapshot{};
}
void Begin(std::vector<chat_travel::Destination> candidates,int64_t destination,int64_t source,int sourceZone,bool roublardTemple) {
    Clear();target=destination;current=source;currentZone=sourceZone;templeAllowed=roublardTemple;searchStarted=Clock::now();
    pathService.reset(module->FindService("fnf"));dependencies.reset(module->FindService("fkq"));
    if(!Exact(pathService.get(),serviceClass)||!dependencies.get())throw std::runtime_error("Native route conditions unavailable");
    for(auto value:candidates)if(value.map>0&&value.known&&value.type==0&&value.cost>=0)choices.push_back(value);
    // An equal route from the current map beats a teleport: no kamas, no loading.
    std::sort(choices.begin(),choices.end(),[](const auto& a,const auto& b){
        return std::tuple(a.map!=current,a.cost,a.map)<std::tuple(b.map!=current,b.cost,b.map);
    });
    searchShortcuts=shortcuts;
}
Result Tick() {
    if(!target||!Session())return {Status::Unreachable};
    const auto started=Clock::now();
    const auto spend=[&] {searchSpent+=Clock::now()-started;};
    if(!graph.get()) {
        graph.reset(api.Invoke(getGraph,nullptr));
        if(!graph.get()) {
            if(!loading) {loader.reset(api.New(loaderClass));api.Invoke(init,loader.get());loading=true;}
            spend();return {};
        }
        if(!Exact(graph.get(),graphClass))throw std::runtime_error("World route graph mismatch");
    } else if(api.Invoke(getGraph,nullptr)!=graph.get())return {Status::Unreachable};
    const auto deadline=started+tickBudget;
    ++slices;
    if(snapshot.graph.get()!=graph.get()) {snapshot=Snapshot{};snapshot.graph.reset(graph.get());}
    else if(snapshot.vertices&&!Unchanged()) {
        // The game changed its graph's dictionaries: copy them again. A search
        // already under way ends, as r111 ended on another graph.
        Log("CHAT_ROUTE_SNAPSHOT_CHANGED searching="+std::to_string(seeded));
        const bool searching=seeded;
        search.Clear();criteria.clear();seeded=false;
        snapshot=Snapshot{};snapshot.graph.reset(graph.get());
        if(searching) {spend();return {Status::Unreachable};}
    }
    if(!snapshot.ready) {
        readHere=true;
        if(!ReadSnapshot(deadline)) {spend();return {};}
    }
    if(!seeded)Seed();
    const auto outcome=search.Run(gameFilters,deadline);
    spend();
    if(outcome.status==route_graph::Status::Pending)return {};
    const auto now=Clock::now();
    if(outcome.status!=route_graph::Status::Found) {LogEnd("CHAT_ROUTE_UNREACHABLE",now,-1);return {Status::Unreachable};}
    LogEnd("CHAT_ROUTE_FOUND",now,outcome.shortcut);
    return {Status::Found,choices[outcome.departure],outcome.steps,outcome.shortcut};
}
}
