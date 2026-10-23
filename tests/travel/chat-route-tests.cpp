// chat_route against a fake IL2CPP runtime that lays its objects out as the
// 3.7.12.12 client does (the class at 0, fields from 0x10, arrays, strings,
// List`1 and Dictionary`2 internals), with the r111 planner kept verbatim as
// the reference (chat-route-r111.inl), on a synthetic world graph with the
// game's characteristics, and on the game's own world graph when its local
// fixture exists (never committed).
#include "travel/chat-route.hpp"
#include "core/module.hpp"
#include "appearance/features.hpp"
#include "ui/options-ui.hpp"
#include "travel/route-fixture.hpp"
#include "travel/chat-route-r111.inl"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>

namespace fake {
using namespace managed;
void Check(bool ok, const std::string& message) { if (!ok) throw std::runtime_error(message); }

// ---------------------------------------------------------------------------
// Types, with the client's field offsets.
// ---------------------------------------------------------------------------
struct Field { size_t offset = 0; };
struct Type { std::string name, shortName; Type* parent = nullptr; std::map<std::string, Field> fields; size_t size = 0x10; };
std::map<std::string, std::unique_ptr<Type>> types;
Type* TypeNamed(const std::string& name) {
    auto& type = types[name];
    if (!type) { type = std::make_unique<Type>(); type->name = name; type->shortName = name.substr(0, name.find('<')); }
    return type.get();
}
void Define(const std::string& name, size_t size, std::initializer_list<std::pair<const char*, size_t>> fields, const std::string& parent = {}) {
    auto* type = TypeNamed(name);
    type->size = size;
    if (!parent.empty()) type->parent = TypeNamed(parent);
    for (const auto& [field, offset] : fields) type->fields[field] = {offset};
}
void Layout() {
    Define("PathFindingData", 0x30, {{"m_vertices", 0x18}, {"m_edges", 0x20}, {"m_outgoingEdges", 0x28}});
    Define("Vertex", 0x28, {{"m_mapId", 0x10}, {"m_zoneId", 0x18}, {"m_uid", 0x20}});
    Define("Edge", 0x28, {{"m_from", 0x10}, {"m_to", 0x18}, {"m_transitions", 0x20}});
    Define("Transition", 0x40, {{"m_type", 0x10}, {"m_direction", 0x14}, {"m_skillId", 0x18}, {"m_criterion", 0x20},
                                {"m_transitionMapId", 0x28}, {"m_cellId", 0x30}, {"m_id", 0x38}});
    Define("EdgeListWrapper", 0x18, {{"m_edgeList", 0x10}});
    // Look-alikes the game never builds, which r111 read through its own calls.
    Define("EdgeListWrapperTwin", 0x20, {{"m_other", 0x10}, {"m_edgeList", 0x18}});
    Define("EdgeListWrapperBare", 0x18, {{"m_other", 0x10}});
    for (const char* odd : {"ObservableList`1<Edge>", "ObservableList`1<Transition>", "ZoneTable"}) Define(odd, 0x18, {});
    const std::pair<const char*, const char*> dictionaries[] = {
        {"DictionaryMapIdVertices", "<Int64,DictionaryZoneVertex>"}, {"DictionaryZoneVertex", "<Int32,Vertex>"}, {"DictionaryUlongEdgeList", "<UInt64,EdgeListWrapper>"}};
    for (const auto& [name, arguments] : dictionaries) {
        const auto dictionary = std::string("Dictionary`2") + arguments, serializable = std::string("SerializableDictionary`2") + arguments;
        Define(dictionary, 0x50, {{"_buckets", 0x10}, {"_entries", 0x18}, {"_count", 0x20}, {"_freeList", 0x24}, {"_freeCount", 0x28},
                                  {"_version", 0x2C}, {"_comparer", 0x30}, {"_keys", 0x38}, {"_values", 0x40}, {"_syncRoot", 0x48}});
        Define(serializable, 0x60, {{"m_keys", 0x50}, {"m_values", 0x58}}, dictionary);
        Define(name, 0x60, {}, serializable);
    }
    for (const char* list : {"List`1<Edge>", "List`1<Transition>"}) Define(list, 0x28, {{"_items", 0x10}, {"_size", 0x18}, {"_version", 0x1C}, {"_syncRoot", 0x20}});
    Define("String", 0x14, {{"_stringLength", 0x10}, {"_firstChar", 0x14}});
    Define("ens", 0x60, {{"docd", 0x50}});
    Define("ezc", 0x30, {});
    Define("eza", 0x48, {});
}
size_t Offset(Type* type, const std::string& name) {
    for (auto* owner = type; owner; owner = owner->parent) {
        const auto found = owner->fields.find(name);
        if (found != owner->fields.end()) return found->second.offset;
    }
    throw std::runtime_error("Field unavailable: " + name);
}

// ---------------------------------------------------------------------------
// The heap: zeroed blocks, the class pointer first. Boxes and enumerators go
// to a scratch arena dropped between cases.
// ---------------------------------------------------------------------------
struct Arena {
    std::vector<std::unique_ptr<unsigned char[]>> chunks;
    unsigned char* current = nullptr;
    size_t used = 0, capacity = 0;
    void* Take(size_t size) {
        size = (size + 15) & ~static_cast<size_t>(15);
        constexpr size_t Chunk = 1 << 20;
        if (size > Chunk / 4) { chunks.push_back(std::make_unique<unsigned char[]>(size)); return chunks.back().get(); }
        if (!current || used + size > capacity) { chunks.push_back(std::make_unique<unsigned char[]>(Chunk)); current = chunks.back().get(); used = 0; capacity = Chunk; }
        void* block = current + used;
        used += size;
        return block;
    }
    void Clear() { chunks.clear(); current = nullptr; used = capacity = 0; }
};
Arena heap, scratch;
Obj Allocate(Arena& arena, Type* type, size_t size) {
    auto object = arena.Take(std::max<size_t>(size, 0x10));
    std::memcpy(object, &type, sizeof type);
    return object;
}
Type* TypeOf(Obj object) { Type* type; std::memcpy(&type, object, sizeof type); return type; }
Obj New(const std::string& name) { auto* type = TypeNamed(name); return Allocate(heap, type, type->size); }
template<class T> void Set(Obj object, size_t offset, T value) { std::memcpy(static_cast<char*>(object) + offset, &value, sizeof value); }
template<class T> T Get(Obj object, size_t offset) { T value; std::memcpy(&value, static_cast<const char*>(object) + offset, sizeof value); return value; }
template<class T> Obj Box(T value) { auto box = Allocate(scratch, TypeNamed("Box"), 0x20); Set<T>(box, 0x10, value); return box; }
Obj Array(const std::string& name, size_t length, size_t element) {
    auto array = Allocate(heap, TypeNamed(name), 0x20 + length * element);
    Set<uint64_t>(array, 0x18, length);
    return array;
}
Obj String(const std::string& text) {
    auto string = Allocate(heap, TypeNamed("String"), 0x16 + 2 * text.size());
    Set<int32_t>(string, 0x10, static_cast<int32_t>(text.size()));
    for (size_t i = 0; i < text.size(); ++i) Set<char16_t>(string, 0x14 + 2 * i, static_cast<char16_t>(text[i]));
    return string;
}
Obj List(const std::string& name, const std::string& arrayName, const std::vector<Obj>& items) {
    auto list = New(name), array = Array(arrayName, items.size() + 2, sizeof(Obj));
    for (size_t i = 0; i < items.size(); ++i) Set<Obj>(array, 0x20 + i * sizeof(Obj), items[i]);
    Set<Obj>(list, 0x10, array);
    Set<int32_t>(list, 0x18, static_cast<int32_t>(items.size()));
    return list;
}
// A dictionary: the client's entries, and the truth TryGetValue answers from.
// Ghosts are removed entries it still holds (hashCode -1, key and value left,
// the key maybe alive again elsewhere): the game's lookups and enumerator skip them.
struct Truth { int keyBytes = 8; std::map<int64_t, Obj> values; std::vector<Obj> order; };
std::unordered_map<Obj, Truth> truths;
Obj Dictionary(const std::string& name, int keyBytes, const std::vector<std::pair<int64_t, Obj>>& entries,
               const std::vector<std::pair<int64_t, Obj>>& ghosts = {}) {
    const auto count = ghosts.size() + entries.size();
    auto dictionary = New(name), array = Array("Entry[]", count + 1, 24);
    auto& truth = truths[dictionary];
    truth.keyBytes = keyBytes;
    int freeList = -1;
    for (size_t i = 0; i < count; ++i) {
        const bool ghost = i < ghosts.size();
        const auto& [key, value] = ghost ? ghosts[i] : entries[i - ghosts.size()];
        const auto entry = 0x20 + i * 24;
        Set<int32_t>(array, entry, ghost ? -1 : static_cast<int32_t>(key & 0x7FFFFFFF));
        Set<int32_t>(array, entry + 4, ghost ? freeList : -1);
        if (keyBytes == 4) Set<int32_t>(array, entry + 8, static_cast<int32_t>(key));
        else Set<int64_t>(array, entry + 8, key);
        Set<Obj>(array, entry + 16, value);
        if (ghost) { freeList = static_cast<int>(i); continue; }
        truth.values[key] = value;
        truth.order.push_back(value);
    }
    Set<Obj>(dictionary, 0x18, array);
    Set<int32_t>(dictionary, 0x20, static_cast<int32_t>(count));
    Set<int32_t>(dictionary, 0x24, freeList);
    Set<int32_t>(dictionary, 0x28, static_cast<int32_t>(ghosts.size()));
    Set<int32_t>(dictionary, 0x2C, static_cast<int32_t>(count + ghosts.size()));
    return dictionary;
}
// Collections of other classes, answering their own calls.
struct Odd { std::vector<Obj> items; std::string countError; int failItem = -1; };
std::unordered_map<Obj, Odd> odd;

// ---------------------------------------------------------------------------
// The game's graph, built like the old fake's: Vertex(map, zone) and Edge(from,
// to, allowed, area, type), the managed objects committed on first load.
// ---------------------------------------------------------------------------
// An edge of a world graph (world set) answers by the shared rules; a scenario
// edge by its own flags.
struct EdgeInfo { bool allowed = true, area = true; const route_fixture::World* world = nullptr; size_t index = 0; uint64_t salt = 0; };
// What the game never builds, or damaged, which r111 still read through its own calls.
enum ListStyle { PlainList, TwinWrapper, BareWrapper, ObservableList, ShortArray, FailingItem };
enum MapStyle { PlainZones, ZoneTable, FailingTable, GhostZone };
struct World {
    std::vector<std::pair<int64_t, std::vector<Obj>>> maps;     // m_vertices order
    std::vector<int64_t> nullMaps;                               // keys of m_vertices holding no zone dictionary
    std::vector<std::pair<uint64_t, std::vector<Obj>>> lists;   // m_outgoingEdges order
    std::unordered_map<int64_t, size_t> mapIndex;
    std::unordered_map<uint64_t, size_t> listIndex;
    std::unordered_map<uint64_t, ListStyle> listStyle;
    std::unordered_map<int64_t, MapStyle> mapStyle;
    std::vector<std::pair<int64_t, int>> ghostMaps;        // removed m_vertices entries: map, zone of a phantom vertex
    std::vector<std::pair<uint64_t, int64_t>> ghostLists;  // removed m_outgoingEdges entries: uid, map of a phantom edge
    Obj graph = nullptr;
};
World world;
std::unordered_map<Obj, EdgeInfo> edgeInfo;
Obj emptyCriterion = nullptr;
bool loaded = true;
int loads = 0, criteria = 0, areaChecks = 0, filterInits = 0, criteriaFree = 0;
uint64_t caseSalt = 0;  // the character's state for a world graph's criteria
std::map<GCHandle, Obj> handles;
GCHandle nextHandle = 1;
std::map<std::string, Obj> services;
std::vector<std::string> logs;
bool logged(const std::string& text) { return std::any_of(logs.begin(), logs.end(), [&](const std::string& line) { return line.find(text) != std::string::npos; }); }
std::string LogLine(const std::string& prefix) {
    for (auto line = logs.rbegin(); line != logs.rend(); ++line) if (line->rfind(prefix, 0) == 0) return *line;
    return {};
}
std::string Value(const std::string& line, const std::string& key) {
    const auto at = line.find(" " + key + "=");
    if (at == std::string::npos) return {};
    const auto begin = at + key.size() + 2;
    return line.substr(begin, line.find(' ', begin) - begin);
}
uint64_t UidOf(Obj vertex) { return Get<uint64_t>(vertex, 0x20); }
Obj VertexObject(int64_t map, int zone, uint64_t uid) {
    auto vertex = New("Vertex");
    Set<int64_t>(vertex, 0x10, map); Set<int32_t>(vertex, 0x18, zone); Set<uint64_t>(vertex, 0x20, uid);
    return vertex;
}
Obj Vertex(int64_t map, int zone = 1, uint64_t uid = 0) {
    Check(!world.graph, "The graph is already loaded");
    auto vertex = VertexObject(map, zone, uid ? uid : static_cast<uint64_t>(map * 10 + zone));
    const auto [found, added] = world.mapIndex.try_emplace(map, world.maps.size());
    if (added) world.maps.push_back({map, {}});
    world.maps[found->second].second.push_back(vertex);
    return vertex;
}
Obj Transition(int type, const std::string& criterion) {
    auto transition = New("Transition");
    if (!emptyCriterion) emptyCriterion = String("");
    Set<int32_t>(transition, 0x10, type);
    Set<Obj>(transition, 0x20, criterion.empty() ? emptyCriterion : String(criterion));
    return transition;
}
Obj Edge(Obj from, Obj to, bool allowed = true, bool area = true, int type = 1, const char* criterion = nullptr, int transitions = 1, const char* later = nullptr) {
    Check(!world.graph, "The graph is already loaded");
    auto edge = New("Edge");
    std::vector<Obj> list;
    for (int i = 0; i < transitions; ++i)
        list.push_back(Transition(type, i ? (i == 1 && later ? later : "") : criterion ? criterion : allowed ? "" : "Qf=1"));
    Set<Obj>(edge, 0x10, from); Set<Obj>(edge, 0x18, to);
    Set<Obj>(edge, 0x20, List("List`1<Transition>", "Transition[]", list));
    auto& info = edgeInfo[edge];
    info.allowed = allowed; info.area = area;
    const auto [found, added] = world.listIndex.try_emplace(UidOf(from), world.lists.size());
    if (added) world.lists.push_back({UidOf(from), {}});
    world.lists[found->second].second.push_back(edge);
    return edge;
}
uint64_t phantoms = 990000000;  // uids of the phantom vertices removed entries hold
Obj PhantomEdge(int64_t map) {
    auto edge = New("Edge");
    Set<Obj>(edge, 0x18, VertexObject(map, 1, ++phantoms));
    Set<Obj>(edge, 0x20, List("List`1<Transition>", "Transition[]", {Transition(1, "")}));
    edgeInfo[edge];
    return edge;
}
// Another list class for an edge's transitions, counted by its own calls.
void OddTransitions(Obj edge, bool failingCount) {
    const auto list = Get<Obj>(edge, 0x20), array = Get<Obj>(list, 0x10);
    auto table = New("ObservableList`1<Transition>");
    auto& c = odd[table];
    for (int i = 0; i < Get<int32_t>(list, 0x18); ++i) c.items.push_back(Get<Obj>(array, 0x20 + static_cast<size_t>(i) * sizeof(Obj)));
    if (failingCount) c.countError = "InvalidOperationException: transitions";
    Set<Obj>(edge, 0x20, table);
}
Obj Commit() {
    if (world.graph) return world.graph;
    std::vector<std::pair<int64_t, Obj>> maps, lists, ghostMaps, ghostLists;
    for (const auto& [map, zones] : world.maps) {
        const auto found = world.mapStyle.find(map);
        const auto style = found == world.mapStyle.end() ? PlainZones : found->second;
        if (style == ZoneTable || style == FailingTable) {
            auto table = New("ZoneTable");
            auto& c = odd[table];
            c.items = zones;
            if (style == FailingTable) c.countError = "InvalidOperationException: zone table";
            maps.emplace_back(map, table);
            continue;
        }
        std::vector<std::pair<int64_t, Obj>> entries, ghosts;
        for (const auto vertex : zones) entries.emplace_back(Get<int32_t>(vertex, 0x18), vertex);
        if (style == GhostZone) ghosts.emplace_back(99, VertexObject(map, 99, ++phantoms));
        maps.emplace_back(map, Dictionary("DictionaryZoneVertex", 4, entries, ghosts));
    }
    for (const auto map : world.nullMaps) maps.emplace_back(map, nullptr);
    for (const auto& [map, zone] : world.ghostMaps)
        ghostMaps.emplace_back(map, Dictionary("DictionaryZoneVertex", 4, {{zone, VertexObject(map, zone, ++phantoms)}}));
    for (const auto& [uid, edges] : world.lists) {
        const auto found = world.listStyle.find(uid);
        const auto style = found == world.listStyle.end() ? PlainList : found->second;
        Obj list;
        if (style == ObservableList || style == FailingItem) {
            list = New("ObservableList`1<Edge>");
            auto& c = odd[list];
            c.items = edges;
            if (style == FailingItem) c.failItem = static_cast<int>(edges.size()) - 1;
        } else {
            list = List("List`1<Edge>", "Edge[]", edges);
            if (style == ShortArray) Set<uint64_t>(Get<Obj>(list, 0x10), 0x18, edges.size() - 1);  // the last item outside its array
        }
        Obj wrapper;
        if (style == TwinWrapper) { wrapper = New("EdgeListWrapperTwin"); Set<Obj>(wrapper, 0x18, list); }
        else if (style == BareWrapper) wrapper = New("EdgeListWrapperBare");
        else { wrapper = New("EdgeListWrapper"); Set<Obj>(wrapper, 0x10, list); }
        lists.emplace_back(static_cast<int64_t>(uid), wrapper);
    }
    for (const auto& [uid, map] : world.ghostLists) {
        auto wrapper = New("EdgeListWrapper");
        Set<Obj>(wrapper, 0x10, List("List`1<Edge>", "Edge[]", {PhantomEdge(map)}));
        ghostLists.emplace_back(static_cast<int64_t>(uid), wrapper);
    }
    world.graph = New("PathFindingData");
    Set<Obj>(world.graph, 0x18, Dictionary("DictionaryMapIdVertices", 8, maps, ghostMaps));
    Set<Obj>(world.graph, 0x28, Dictionary("DictionaryUlongEdgeList", 8, lists, ghostLists));
    return world.graph;
}
chat_travel::Destination Choice(int64_t map, int x = 0, int y = 0) { return {map, x, y, 1, 0, true, 0}; }

// ---------------------------------------------------------------------------
// The natives: the loader, ens.wvo/wvs/wvt, and the collections called by name.
// ---------------------------------------------------------------------------
struct MethodInfo { std::string type, name; };
std::vector<std::unique_ptr<MethodInfo>> methods;
std::unordered_map<Obj, std::vector<Obj>> sequences;
std::unordered_map<Obj, int> cursors;
void ResetScratch() { scratch.Clear(); sequences.clear(); cursors.clear(); }
Obj Invoke(const MethodInfo& method, Obj self, std::initializer_list<void*> args) {
    const auto& name = method.name;
    const std::vector<void*> a(args);
    if (name == "bbno") return loaded ? Commit() : nullptr;
    if (name == "bbnn") { Check(self && TypeOf(self)->name == "ezc", "Loader must be privately allocated"); ++loads; return nullptr; }
    if (name == "wvo") {
        Check(self && TypeOf(self)->name == "ens", "Private route filter");
        if (!Get<Obj>(self, 0x50)) Set<Obj>(self, 0x50, New("List`1<Int32>"));
        ++filterInits;
        return nullptr;
    }
    if (name == "wvs") {
        ++criteria;
        const auto edge = static_cast<Obj>(a.at(0));
        Check(edge && TypeOf(edge)->name == "Edge", "ens.wvs takes an edge");
        const auto transitions = Get<Obj>(edge, 0x20);
        Check(transitions != nullptr, "ens.wvs reads the first transition: never asked without one");
        Obj first = nullptr;
        if (const auto found = odd.find(transitions); found != odd.end()) {
            Check(!found->second.items.empty(), "ens.wvs reads the first transition: never asked without one");
            first = found->second.items[0];
        } else {
            Check(Get<int32_t>(transitions, 0x18) > 0, "ens.wvs reads the first transition: never asked without one");
            const auto array = Get<Obj>(transitions, 0x10);
            if (!array || !Get<uint64_t>(array, 0x18)) throw std::runtime_error("IndexOutOfRangeException");  // as the game's get_Item(0)
            first = Get<Obj>(array, 0x20);
        }
        const auto criterion = Get<Obj>(first, 0x20);
        // The game's own early answer; the copy knows it for the game's List`1 (another
        // list class leaves the question to the game, as r111 did).
        if (!Get<int32_t>(criterion, 0x10)) { if (!odd.count(transitions)) ++criteriaFree; return Box(true); }
        const auto& info = edgeInfo.at(edge);
        return Box(info.world ? route_fixture::CriteriaAllowed(*info.world, info.index, caseSalt) : info.allowed);
    }
    if (name == "wvt") {
        Check(self && TypeOf(self)->name == "ens" && Get<Obj>(self, 0x50), "ens.wvt needs a private ens after ens.wvo");
        ++areaChecks;
        const auto& info = edgeInfo.at(static_cast<Obj>(a.at(0)));
        return Box(info.world ? route_fixture::AreaAllowed(*info.world, info.index, info.salt) : info.area);
    }
    Check(self != nullptr, "Null receiver: " + name);
    if (const auto found = odd.find(self); found != odd.end()) {
        const auto& c = found->second;
        if (name == "get_Count") {
            if (!c.countError.empty()) throw std::runtime_error(c.countError);
            return Box(static_cast<int>(c.items.size()));
        }
        if (name == "get_Item") {
            const int index = *static_cast<int*>(a.at(0));
            if (index == c.failItem) throw std::runtime_error("InvalidOperationException: item " + std::to_string(index));
            Check(index >= 0 && index < static_cast<int>(c.items.size()), "ArgumentOutOfRangeException");
            return c.items[static_cast<size_t>(index)];
        }
        if (name == "get_Values") { auto values = Allocate(scratch, TypeNamed("ValueCollection"), 0x18); sequences[values] = c.items; return values; }
        throw std::runtime_error("Unexpected call " + TypeOf(self)->name + "." + name);
    }
    const auto& type = TypeOf(self)->name;
    const bool list = type.rfind("List`1", 0) == 0;
    if (name == "TryGetValue") {
        const auto& truth = truths.at(self);
        const int64_t key = truth.keyBytes == 4 ? *static_cast<int32_t*>(a.at(0)) : *static_cast<int64_t*>(a.at(0));
        const auto found = truth.values.find(key);
        *static_cast<Obj*>(a.at(1)) = found == truth.values.end() ? nullptr : found->second;
        return Box(found != truth.values.end());
    }
    if (name == "get_Count") return Box(list ? Get<int32_t>(self, 0x18) : Get<int32_t>(self, 0x20) - Get<int32_t>(self, 0x28));
    if (name == "get_Values") {
        auto values = Allocate(scratch, TypeNamed("ValueCollection"), 0x18);
        sequences[values] = truths.at(self).order;
        return values;
    }
    if (name == "GetEnumerator") { auto iterator = Allocate(scratch, TypeNamed("Enumerator"), 0x18); sequences[iterator] = sequences.at(self); cursors[iterator] = -1; return iterator; }
    if (name == "MoveNext") return Box(++cursors.at(self) < static_cast<int>(sequences.at(self).size()));
    if (name == "get_Current") return sequences.at(self).at(static_cast<size_t>(cursors.at(self)));
    if (name == "get_Item" && list) {
        const int index = *static_cast<int*>(a.at(0));
        Check(index >= 0 && index < Get<int32_t>(self, 0x18), "ArgumentOutOfRangeException");
        const auto array = Get<Obj>(self, 0x10);
        if (static_cast<uint64_t>(index) >= Get<uint64_t>(array, 0x18)) throw std::runtime_error("IndexOutOfRangeException");
        return Get<Obj>(array, 0x20 + static_cast<size_t>(index) * sizeof(Obj));
    }
    throw std::runtime_error("Unexpected native route call " + type + "." + name);
}
void InitApi() {
    api.object_get_class = [](Obj o) -> Klass { return TypeOf(o); };
    api.object_unbox = [](Obj o) -> void* { return static_cast<char*>(o) + 0x10; };
    api.gchandle_new = [](Obj o, bool) -> GCHandle { handles[++nextHandle] = o; return nextHandle; };
    api.gchandle_get_target = [](GCHandle h) -> Obj { return handles.at(h); };
    api.gchandle_free = [](GCHandle h) { Check(handles.erase(h) == 1, "Lost GC root"); };
    api.class_get_parent = [](Klass k) -> Klass { return static_cast<Type*>(k)->parent; };
    api.class_get_name = [](Klass k) -> const char* { return static_cast<Type*>(k)->shortName.c_str(); };
    // Like il2cpp_class_get_field_from_name on this one class (callers walk the parents).
    api.class_get_field_from_name = [](Klass k, const char* name) -> void* {
        auto* type = static_cast<Type*>(k);
        const auto found = type->fields.find(name);
        return found == type->fields.end() ? nullptr : &found->second;
    };
    api.field_get_offset = [](void* field) -> size_t { return static_cast<Field*>(field)->offset; };
}
struct Fixture {
    Module owner{"offline"};
    Fixture() {
        module = nullptr; chat_route::Clear(); legacy_route::Clear();
        module = &owner; owner.servicesReady = true; loaded = true;
        loads = criteria = areaChecks = filterInits = 0; logs.clear();
        services = {{"fnf", New("eza")}, {"fkq", New("Criteria")}};
        world = World{};
    }
    ~Fixture() {
        owner.stopped = true;  // the module's stop releases the graph copy
        chat_route::Clear(); legacy_route::Clear(); chat_route::SetShortcuts({}); chat_route::SetTickBudget(6000);
        module = nullptr; ResetScratch();
    }
    static chat_route::Result Finish() {
        for (int i = 0; i < 1000000; ++i) { auto r = chat_route::Tick(); if (r.status != chat_route::Status::Pending) return r; }
        throw std::runtime_error("Planner did not finish");
    }
    static chat_route::Result Legacy() {
        for (int i = 0; i < 1000000; ++i) { auto r = legacy_route::Tick(); if (r.status != chat_route::Status::Pending) return r; }
        throw std::runtime_error("r111 planner did not finish");
    }
};
template<class F> std::string Throws(F&& f) {
    try { f(); } catch (const std::exception& e) { return e.what(); }
    return {};
}
}

namespace managed {
Api api{};
uintptr_t gameBase = 0;
Klass Api::Class(const char*, const char*, const char* name) const { return fake::TypeNamed(name); }
Method Api::FindVerified(Klass k, const char* name, int, uintptr_t) const {
    fake::methods.push_back(std::make_unique<fake::MethodInfo>(fake::MethodInfo{static_cast<fake::Type*>(k)->name, name}));
    return fake::methods.back().get();
}
Obj Api::Invoke(Method m, Obj o, std::initializer_list<void*> a) const { return fake::Invoke(*static_cast<const fake::MethodInfo*>(m), o, a); }
Obj Api::Call(Obj o, const char* n, std::initializer_list<void*> a) const {
    fake::Check(o != nullptr, std::string("Null receiver: ") + n);
    return fake::Invoke({fake::TypeOf(o)->name, n}, o, a);
}
Obj Api::New(Klass k, std::initializer_list<void*>) const { auto* type = static_cast<fake::Type*>(k); return fake::Allocate(fake::heap, type, type->size); }
Obj Api::Field(Obj o, const char* n) const { return o ? fake::Get<Obj>(o, fake::Offset(fake::TypeOf(o), n)) : nullptr; }
void* Api::FieldAddress(Obj o, const char* n) const {
    fake::Check(o != nullptr, "Null field owner");
    return static_cast<char*>(o) + fake::Offset(fake::TypeOf(o), n);
}
}
Module* module = nullptr;
void Log(const std::string& text) { fake::logs.push_back(text); }
Module::Module(std::filesystem::path p) : folder(p) {}
managed::Obj Module::FindService(const char* n) const { return fake::services[n]; }

namespace {
using namespace fake;
using chat_route::Begin;
using chat_route::Status;
using chat_route::Tick;

// The r111 scenarios, unchanged in their routes; the native filters now see
// only the edges they can refuse.
void Scenarios() {
    {Fixture f;auto hub=Vertex(81789954),dest=Vertex(999999);
        for(int i=0;i<295;++i)Edge(hub,Vertex(100000+i),true,true,1,"Qf=2");Edge(hub,dest,true,true,1,"Qf=2");
        chat_route::SetTickBudget(0);Begin({Choice(81789954)},999999,99);
        bool partial=false;chat_route::Result r;
        do {r=Tick();partial=partial||(r.status==Status::Pending&&criteria>0&&criteria<296);} while(r.status==Status::Pending);
        Check(partial&&r.status==Status::Found&&r.steps==2&&criteria==296,"A real 296-edge hub (81789954) resumes edge by edge without losing its remaining edges");}
    {Fixture f;auto hub=Vertex(1);Vertex(2);for(int i=0;i<1025;++i)Edge(hub,Vertex(10000+i));Begin({Choice(1)},2,99);
        Check(Throws([]{Fixture::Finish();}).find("outgoing edges count=1025")!=std::string::npos,"Malformed collection still fails with its name and actual count");}
    {Fixture f;auto closeStart=Vertex(1),distantStart=Vertex(2),a=Vertex(3),b=Vertex(4),dest=Vertex(5);Edge(closeStart,a);Edge(a,b);Edge(b,dest);Edge(distantStart,dest);
        Begin({Choice(1,0,0),Choice(2,50,50)},5,99);auto r=f.Finish();Check(r.status==Status::Found&&r.departure.map==2&&r.steps==2,"Actual shorter path must beat coordinate-nearest zaap");}
    {Fixture f;auto here=Vertex(1),zaap=Vertex(2),dest=Vertex(3);Edge(here,dest);Edge(zaap,dest);Begin({Choice(2),Choice(1)},3,1);Check(f.Finish().departure.map==1,"Walking directly avoids needless teleport");}
    {Fixture f;auto a=Vertex(1),b=Vertex(2),dest=Vertex(3);Edge(a,dest,false);Edge(b,dest);Begin({Choice(1),Choice(2)},3,99);Check(f.Finish().departure.map==2&&criteria>0,"Native criteria must reject locked shortcut");}
    {Fixture f;auto a=Vertex(1),dest=Vertex(2);for(int i=0;i<200;++i)Edge(a,dest,true,true,1,"Qf=2");Begin({Choice(1)},2,99);
        auto r=f.Finish();Check(r.status==Status::Found&&criteria==1&&areaChecks==1,"Equivalent accepted paths must not repeatedly evaluate native criteria");}
    {Fixture f;auto a=Vertex(1),dest=Vertex(2);Edge(a,dest,false);Edge(a,dest,true,true,1,"Qf=2");Begin({Choice(1)},2,99);
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
    {Fixture f;auto a=Vertex(1),dest=Vertex(2);Edge(a,dest);Begin({Choice(1)},2,99);services["fkq"]=New("Replacement");Check(Tick().status==Status::Unreachable,"Changed character criteria invalidates planning");}
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
    // Only the game's own destination ends the search (review case, Otomaï).
    {Fixture f;auto here=Vertex(63965954),mid=Vertex(63965442),chosen=Vertex(63964930),zaapLayer=Vertex(28050436);
        Edge(here,mid);Edge(mid,chosen);Obj from=zaapLayer;for(int i=0;i<4;++i){auto step=Vertex(800000+i);Edge(from,step);from=step;}Edge(from,chosen);
        Begin({Choice(28050436,-53,18),Choice(63965954,-51,18)},63964930,63965954);auto r=f.Finish();
        Check(r.status==Status::Found&&r.departure.map==63965954&&r.steps==2,"A zaap on another layer of the coordinates does not end the search: walk 2 maps to the game's choice");}
    // An equal route from the current map beats a teleport (no kamas, no loading).
    {Fixture f;auto zaap=Vertex(1),here=Vertex(2),mid=Vertex(4),dest=Vertex(3);Edge(zaap,dest);Edge(here,mid);Edge(mid,dest);
        Begin({Choice(1),Choice(2)},3,2);auto r=f.Finish();
        Check(r.departure.map==2&&r.steps==2,"Equal route: walking from the current map wins over the zaap");}
    // The user's r109 logs (the cemetery [9,17] and the Havre-Sac).
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
    {Fixture f;auto west1=Vertex(185860609,1),west2=Vertex(185860609,2),east=Vertex(68552706),dest=Vertex(190841348);(void)west2;
        const auto chain=[&](Obj from,int length,int64_t first){for(int i=0;i<length-1;++i){auto step=Vertex(first+i);Edge(from,step);from=step;}Edge(from,dest);};
        chain(west1,5,500000);chain(east,11,600000);
        Begin({Choice(68552706,3,-5),Choice(185860609,-5,-8)},190841348,162791424);auto r=f.Finish();
        Check(r.status==Status::Found&&r.departure.map==185860609&&r.steps==7,"The closer split-map zaap of [-5,-8] wins (2+5 < 1+11)");
        Check(logged("CHAT_ROUTE_SEEDED target=190841348 candidates=2 starts=3 hereStarts=0 splitZaaps=1"),"Every zone of the split zaap starts the search");}
    // r111. The Roublard temple under the Amakna cemetery, with the game's own ids;
    // the statue's edges carry the game's criteria (PG=13...), `statue` their answer.
    const auto temple=[](bool statue){
        auto cemetery=Vertex(82313729),door=Vertex(82314240),hall=Vertex(67108864),west=Vertex(67110912),inner=Vertex(67111936),room=Vertex(67113984);
        auto domaine=Vertex(188219392),douze=Vertex(192416776);
        Edge(cemetery,door);Edge(door,cemetery);Edge(door,hall,true,true,32);Edge(hall,door,true,true,8);
        Edge(hall,west,true,true,8);Edge(west,hall,true,true,32);Edge(west,inner,true,true,32);Edge(inner,west,true,true,32);
        Edge(inner,room,true,true,32);Edge(room,inner,true,true,32);
        Edge(room,domaine,statue,true,32,"PG=13");Edge(room,douze,statue,true,32,"PG=13");Edge(domaine,room,statue,true,32,"PG=13");
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
    {Fixture f;temple(false);
        Begin({Choice(82313729)},188219392,82313729);auto r=f.Finish();
        Check(r.status==Status::Unreachable&&criteria>0,"The game's criteria refuse the statue: no temple shortcut for that character");
        Begin({Choice(82313729)},192416776,82313729);r=f.Finish();
        Check(r.status==Status::Found&&r.steps==10,"Without the statue, the long way round");
        Begin({Choice(82313729)},67113984,82313729);r=f.Finish();
        Check(r.status==Status::Found&&r.steps==5,"The temple's doors stay open to every character");}
}

// r112: the graph copy, the filters asked, the layout checks and shortcuts.
void Snapshots() {
    {Fixture f;auto a=Vertex(1),b=Vertex(2),dest=Vertex(3);Edge(a,b);Edge(b,dest);
        Begin({Choice(1)},3,1);auto r=Tick();
        Check(r.status==Status::Found&&r.steps==2&&logged("CHAT_ROUTE_SNAPSHOT vertices=3 edges=2 maps=3 criteria=0")&&logged("graph=read"),
              "The first search copies the graph and ends in its first tick");
        logs.clear();Begin({Choice(1)},3,1);r=Tick();
        Check(r.status==Status::Found&&r.steps==2&&!logged("CHAT_ROUTE_SNAPSHOT")&&logged("graph=reused"),"The next search reuses the copy and ends in its first tick");
        Check(criteria==0&&areaChecks==2&&filterInits==1,"Without a criterion, ens.wvs is never asked; ens.wvt once per edge for the graph's lifetime");
        chat_route::Clear();Check(handles.size()==2,"Clear keeps the copy: the graph and the private ens");
        f.owner.stopped=true;chat_route::Clear();Check(handles.empty(),"The module's stop releases every root");}
    // A replaced graph object is read again; a replacement during a search ends it.
    {Fixture f;auto a=Vertex(1),dest=Vertex(2);Edge(a,dest);
        Begin({Choice(1)},2,1);Check(f.Finish().steps==1,"First graph");
        world=World{};auto a2=Vertex(1),mid=Vertex(5),dest2=Vertex(2);Edge(a2,mid);Edge(mid,dest2);
        logs.clear();Begin({Choice(1)},2,1);auto r=f.Finish();
        Check(r.steps==2&&logged("CHAT_ROUTE_SNAPSHOT vertices=3")&&logged("graph=read"),"Another graph object is copied again");
        chat_route::SetTickBudget(0);world=World{};auto a3=Vertex(1),dest3=Vertex(2);for(int i=0;i<200;++i)Edge(a3,Vertex(1000+i));Edge(a3,dest3);
        Begin({Choice(1)},2,1);Check(Tick().status==Status::Pending,"Sliced: the copy takes several ticks");
        world=World{};Vertex(1);Check(Tick().status==Status::Unreachable,"A graph replaced during the search ends it, as in r111");}
    // ens.wvs is asked again by every search (the character changes); ens.wvt is not.
    {Fixture f;auto a=Vertex(1),b=Vertex(2),c=Vertex(3),dest=Vertex(4);auto locked=Edge(a,dest,true,true,32,"Qf=7");Edge(a,b);Edge(b,c);Edge(c,dest);
        Begin({Choice(1)},4,1);auto r=f.Finish();
        Check(r.steps==1&&criteria==1&&areaChecks==3,"Only the edge with a criterion asks ens.wvs");
        Begin({Choice(1)},4,1);r=f.Finish();
        Check(r.steps==1&&criteria==2&&areaChecks==3,"The next search asks ens.wvs again, ens.wvt no more");
        edgeInfo[locked].allowed=false;
        Begin({Choice(1)},4,1);r=f.Finish();
        Check(r.steps==3&&criteria==3,"A character who no longer meets the criterion walks around");}
    // The layouts the copy relies on are checked, and a failed read starts over.
    {Fixture f;auto a=Vertex(1),dest=Vertex(2);Edge(a,dest);Commit();
        const auto zones=truths.at(Get<Obj>(world.graph,0x18)).values.at(1);
        const auto maps=Get<Obj>(world.graph,0x18),entries=Get<Obj>(maps,0x18);
        const auto value=Get<Obj>(entries,0x20+16);Set<Obj>(entries,0x20+16,Get<Obj>(entries,0x20+24+16));
        Begin({Choice(1)},2,1);Check(Throws([]{Fixture::Finish();})=="World route maps layout mismatch","Dictionary entries must match the game's own lookup");
        Set<Obj>(entries,0x20+16,value);
        const auto zoneEntries=Get<Obj>(zones,0x18),zone=Get<Obj>(zoneEntries,0x20+16);
        Set<Obj>(zoneEntries,0x20+16,New("Vertex"));
        Begin({Choice(1)},2,1);Check(Throws([]{Fixture::Finish();})=="World route zones layout mismatch","Zone entries must match the game's own enumeration");
        Set<Obj>(zoneEntries,0x20+16,zone);
        Begin({Choice(1)},2,1);Check(f.Finish().steps==1,"A failed read starts over");
        auto& uid=TypeNamed("Vertex")->fields["m_uid"].offset;uid=0x28;
        Check(Throws([]{chat_route::Install();})=="Route field mismatch: m_uid","Install checks the field offsets against the metadata");
        uid=0x20;TypeNamed("Dictionary`2<UInt64,EdgeListWrapper>")->fields["_count"].offset=0x24;
        Check(Throws([]{chat_route::Install();})=="Route field mismatch: _count","...including the dictionaries' own fields, through their parents");
        TypeNamed("Dictionary`2<UInt64,EdgeListWrapper>")->fields["_count"].offset=0x20;chat_route::Install();}
    // Malformed edges fail where r111 failed: when examined, not when copied.
    {Fixture f;auto a=Vertex(1),b=Vertex(2),dest=Vertex(3);Edge(a,b);auto bad=Edge(b,dest);Commit();
        Set<Obj>(bad,0x18,New("Edge"));
        Begin({Choice(1)},2,1);Check(f.Finish().steps==1,"A malformed edge elsewhere does not matter");
        Begin({Choice(1)},3,1);Check(Throws([]{Fixture::Finish();})=="World route destination mismatch","A malformed edge fails when examined");}
    // Shortcuts.
    {Fixture f;auto a=Vertex(1),b=Vertex(2),c=Vertex(3),d=Vertex(4),dest=Vertex(5);Vertex(4,2);Edge(a,b);Edge(b,c);Edge(c,d);Edge(d,dest);
        chat_route::SetShortcuts({{7,1,4,2}});
        Begin({Choice(1)},5,1);auto r=f.Finish();
        Check(r.status==Status::Found&&r.steps==3&&r.shortcut==7&&logged("CHAT_ROUTE_SHORTCUTS count=1")&&logged("shortcuts=1")&&logged("shortcut=7"),
              "A strictly shorter shortcut is taken, from every zone of its map to every zone of the other");
        logs.clear();chat_route::SetShortcuts({{7,1,4,2}});Check(!logged("CHAT_ROUTE_SHORTCUTS"),"An unchanged list is not logged again");
        chat_route::Clear();Begin({Choice(1)},5,1);Check(f.Finish().shortcut==7,"Clear keeps the shortcuts");
        chat_route::SetShortcuts({{7,1,4,3}});
        Begin({Choice(1)},5,1);r=f.Finish();
        Check(r.steps==4&&r.shortcut==-1,"An equally long shortcut is not taken");
        Begin({Choice(1)},5,1);chat_route::SetShortcuts({{8,1,5,1}});r=f.Finish();
        Check(r.steps==4&&r.shortcut==-1,"A search keeps the shortcuts it began with");
        Begin({Choice(1)},5,1);r=f.Finish();
        Check(r.steps==1&&r.shortcut==8,"...and the next one takes the new list");
        chat_route::SetShortcuts({{3,1,3,1},{4,3,5,1}});
        Begin({Choice(1)},5,1);r=f.Finish();
        Check(r.steps==3&&r.shortcut==3,"A route takes at most one shortcut: the trip is planned again after it");
        chat_route::SetShortcuts({{9,1,5,65}});logs.clear();
        Begin({Choice(1)},5,1);r=f.Finish();
        Check(r.steps==4&&r.shortcut==-1&&logged("shortcuts=0"),"A shortcut costs 64 map changes at most");}
    {Fixture f;
        auto astrub=Vertex(67372032),hall=Vertex(67108864),door=Vertex(82314240),out=Vertex(82313729);Edge(hall,door);Edge(door,out);Edge(out,door);Edge(door,hall);
        chat_route::SetShortcuts({{3,67372032,67108864,1}});
        Begin({Choice(67372032)},67108864,67372032,-1,false);auto r=f.Finish();
        Check(r.status==Status::Unreachable&&logged("shortcuts=0"),"Temple option off: no shortcut into the temple");
        Begin({Choice(67372032)},82313729,67372032,-1,true);r=f.Finish();
        Check(r.status==Status::Found&&r.steps==3&&r.shortcut==3,"Temple option on: through the passage and out of the temple");(void)astrub;}
}

// What the game never builds, and damaged elements: the r111 result, or its
// failure where r111 failed, never a failure of the whole graph.
struct Plan { std::string error; chat_route::Result result; };
Plan Now(const std::vector<chat_travel::Destination>& choices, int64_t target, int64_t current, int zone = -1) {
    try { Begin(choices, target, current, zone); return {{}, Fixture::Finish()}; }
    catch (const std::exception& e) { return {e.what(), {}}; }
}
Plan Then(const std::vector<chat_travel::Destination>& choices, int64_t target, int64_t current, int zone = -1) {
    try { legacy_route::Begin(choices, target, current, zone, true); return {{}, Fixture::Legacy()}; }
    catch (const std::exception& e) { return {e.what(), {}}; }
}
std::string Describe(const Plan& plan) {
    if (!plan.error.empty()) return "error " + plan.error;
    if (plan.result.status != Status::Found) return "unreachable";
    return "from " + std::to_string(plan.result.departure.map) + " in " + std::to_string(plan.result.steps);
}
void Same(const std::string& what, const std::vector<chat_travel::Destination>& choices, int64_t target, int64_t current, int zone = -1) {
    const auto old = Then(choices, target, current, zone), now = Now(choices, target, current, zone);
    Check(Describe(old) == Describe(now), what + ": r111 " + Describe(old) + ", r112 " + Describe(now));
}
// Every departure to every target, and all of them at once as zaaps.
size_t AllPairs(const std::string& what, const std::vector<int64_t>& maps) {
    size_t failures = 0;
    std::vector<chat_travel::Destination> zaaps;
    for (const auto from : maps) {
        zaaps.push_back(Choice(from));
        for (const auto target : maps) {
            Same(what + " from " + std::to_string(from) + " to " + std::to_string(target), {Choice(from)}, target, from);
            failures += !Now({Choice(from)}, target, from).error.empty();
        }
    }
    for (const auto target : maps) Same(what + ", every zaap to " + std::to_string(target), zaaps, target, 99);
    return failures;
}
void Anomalies() {
    // Copies of a vertex that disagree: r111 read the map of the copy that reached it.
    {Fixture f;auto a=Vertex(1),b=Vertex(2),c=Vertex(3),x=Vertex(5);
        Edge(a,VertexObject(9,5,UidOf(b)));Edge(b,c);Edge(x,b);
        AllPairs("Disagreeing copies",{1,2,3,5,9});
        Same("Disagreeing copies, the player's zone in m_vertices",{Choice(2)},3,2,1);
        Same("Disagreeing copies, a zone only an edge's copy has",{Choice(2)},3,2,5);
        Check(Now({Choice(1)},9,1).result.steps==1&&Now({Choice(1)},2,1).result.status==Status::Unreachable,"Reached through a copy claiming map 9: map 9, not 2");}
    // Removed entries the dictionaries still hold, their key sometimes alive
    // elsewhere: skipped, as the game's own lookups and enumerator skip them.
    {Fixture f;auto a=Vertex(1),b=Vertex(2),c=Vertex(3);auto split1=Vertex(5,1);Vertex(6);
        Edge(a,b);Edge(b,c);Edge(split1,c);
        world.mapStyle[5]=GhostZone;world.ghostMaps={{2,7},{8,1}};world.ghostLists={{UidOf(a),4},{UidOf(c),4}};
        Check(AllPairs("Removed entries",{1,2,3,4,5,6,8})==0,"Removed entries: no failure");
        Check(Now({Choice(5)},3,99).result.steps==2,"A removed zone leaves a map of one zone: a zaap at 1");}
    // Look-alike classes and damaged collections: read through r111's own
    // calls, failing where they failed.
    {Fixture f;auto a=Vertex(1),b=Vertex(2),c=Vertex(3),d=Vertex(4),e=Vertex(6),z=Vertex(8);Vertex(7);
        Edge(a,b);Edge(b,c);Edge(c,d);Edge(d,e);Edge(d,a);Edge(e,a);Edge(z,a);
        OddTransitions(Edge(a,c,true,true,1,"Qf=3"),false);
        world.listStyle[UidOf(b)]=TwinWrapper;world.listStyle[UidOf(c)]=ObservableList;
        world.listStyle[UidOf(d)]=ShortArray;world.listStyle[UidOf(e)]=BareWrapper;
        world.mapStyle[8]=ZoneTable;world.mapStyle[7]=FailingTable;
        Check(AllPairs("Look-alike classes",{1,2,3,4,6,7,8})>0,"Some searches fail, where r111 failed");
        Check(Now({Choice(1)},4,1).result.steps==2,"A wrapper of another class and another list class: the r111 route");
        Check(Now({Choice(7)},1,7).error=="InvalidOperationException: zone table","A zone table whose Count fails, when seeded");
        Check(Now({Choice(1)},6,1).error=="IndexOutOfRangeException","An item outside its array, when examined");
        Check(Now({Choice(6)},1,6).error=="Field unavailable: m_edgeList","A wrapper without m_edgeList, when expanded");
        Check(Now({Choice(8)},3,8).result.steps==2&&Now({Choice(2)},4,2).result.steps==2,"Away from them, the routes stay");}
    {Fixture f;auto a=Vertex(1),b=Vertex(2),c=Vertex(3);Edge(a,b);Edge(b,c);Edge(c,a);Edge(c,b);OddTransitions(Edge(b,a),true);
        world.listStyle[UidOf(c)]=FailingItem;
        AllPairs("Failing calls",{1,2,3});
        Check(Now({Choice(2)},1,2).error=="InvalidOperationException: transitions","Uncountable transitions, where they are counted");
        Check(Now({Choice(1)},2,1).result.steps==1,"...not before");}
    // A target the game never sends: unreachable, as in r111.
    {Fixture f;auto a=Vertex(1);(void)a;Same("No target",{Choice(1)},0,1);Check(Now({Choice(1)},0,1).result.status==Status::Unreachable,"No target");}
    // The copy stands while the game's graph keeps the same dictionaries, unchanged.
    {Fixture f;auto a=Vertex(1),b=Vertex(2);Edge(a,b);
        Begin({Choice(1)},2,1);Check(f.Finish().steps==1,"First search");
        const auto vertices=Get<Obj>(world.graph,0x18);Set<int32_t>(vertices,0x2C,Get<int32_t>(vertices,0x2C)+1);
        logs.clear();Begin({Choice(1)},2,1);
        Check(f.Finish().steps==1&&logged("CHAT_ROUTE_SNAPSHOT_CHANGED searching=0")&&logged("CHAT_ROUTE_SNAPSHOT vertices="),"A changed dictionary is copied again");
        const auto outgoing=Get<Obj>(world.graph,0x28);Set<Obj>(world.graph,0x28,Dictionary("DictionaryUlongEdgeList",8,{}));
        logs.clear();Begin({Choice(1)},2,1);
        Check(f.Finish().status==Status::Unreachable&&logged("CHAT_ROUTE_SNAPSHOT_CHANGED"),"A replaced dictionary is copied again");
        Set<Obj>(world.graph,0x28,outgoing);}
    {Fixture f;Obj from=Vertex(1);for(int i=0;i<300;++i){auto step=Vertex(1000+i);Edge(from,step);from=step;}
        chat_route::SetTickBudget(0);Begin({Choice(1)},1299,1);
        chat_route::Result r;do r=Tick(); while(r.status==Status::Pending&&!logged("CHAT_ROUTE_SEEDED"));
        Check(r.status==Status::Pending,"Sliced: still searching");
        const auto vertices=Get<Obj>(world.graph,0x18);Set<int32_t>(vertices,0x2C,Get<int32_t>(vertices,0x2C)+1);
        Check(Tick().status==Status::Unreachable&&logged("CHAT_ROUTE_SNAPSHOT_CHANGED searching=1"),"A change during a search ends it, as another graph object ended r111's");
        chat_route::SetTickBudget(6000);Begin({Choice(1)},1299,1);Check(f.Finish().steps==300,"The next search copies the graph again");}
}

// A world graph, in the fake runtime's managed memory.
struct LoadedWorld {
    Obj graph = nullptr;
    uint64_t salt = 0;
};
uint64_t WorldUid(uint32_t uid) { return static_cast<uint64_t>(uid) * 7919 + 100003; }  // not the dense uids
LoadedWorld LayOut(const route_fixture::World& source, uint64_t salt, bool unusual) {
    world = World{};
    std::vector<Obj> vertices(source.Vertices());
    for (const auto map : source.maps)
        for (const auto uid : source.zones.at(map)) vertices[uid] = Vertex(map, source.zone[uid], WorldUid(uid));
    // Keys of m_vertices without a zone: an empty dictionary, or no dictionary at all.
    for (size_t i = 0; i < source.emptyMaps.size(); ++i) {
        if (i % 2) world.nullMaps.push_back(source.emptyMaps[i]);
        else { world.mapIndex.try_emplace(source.emptyMaps[i], world.maps.size()); world.maps.push_back({source.emptyMaps[i], {}}); }
    }
    for (uint32_t v = 0; v < source.Vertices(); ++v)
        for (uint32_t e = source.first[v]; e < source.first[v + 1]; ++e) {
            const auto& edge = source.edges[e];
            // The game's edges hold their own copy of the destination vertex.
            const auto to = VertexObject(source.ToMap(edge), source.ToZone(edge), WorldUid(edge.to));
            auto object = Edge(vertices[v], to, true, true, 1, edge.criterion >= 0 ? ("crit" + std::to_string(edge.criterion)).c_str() : nullptr,
                               edge.transitions, edge.laterCriterion ? "Qf=later" : nullptr);
            auto& info = edgeInfo[object];
            info.world = &source; info.index = e; info.salt = salt;
            if (unusual && route_fixture::Mix(salt * 977 + e) % 100 == 0) OddTransitions(object, false);
        }
    if (unusual) {
        // What the game never builds and r111 still read, which changes no route:
        // look-alike wrappers, other list classes, zone tables, removed entries.
        size_t i = 0;
        for (const auto& list : world.lists) {
            const auto roll = route_fixture::Mix(salt * 131 + i++) % 100;
            if (roll < 2) world.listStyle[list.first] = TwinWrapper;
            else if (roll < 4) world.listStyle[list.first] = ObservableList;
        }
        i = 0;
        for (const auto& map : world.maps) {
            const auto roll = route_fixture::Mix(salt * 137 + i++) % 100;
            if (roll < 2) world.mapStyle[map.first] = ZoneTable;
            else if (roll < 4) world.mapStyle[map.first] = GhostZone;
        }
        world.ghostMaps = {{source.maps[0], 7}, {123456700, 1}};
        world.ghostLists = {{WorldUid(0), source.map[1]}, {WorldUid(static_cast<uint32_t>(source.Vertices() - 1)), source.map[0]}};
    }
    return {Commit(), salt};
}
struct Timing {
    std::vector<double> samples;
    void Add(double ms) { samples.push_back(ms); }
    std::string Text() {
        if (samples.empty()) return "none";
        std::sort(samples.begin(), samples.end());
        double sum = 0;
        for (const auto s : samples) sum += s;
        char text[160];
        std::snprintf(text, sizeof text, "mean %.2f ms, median %.2f ms, max %.2f ms over %zu", sum / samples.size(), samples[samples.size() / 2], samples.back(), samples.size());
        return text;
    }
};
double Ms(std::chrono::steady_clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); }
std::vector<chat_travel::Destination> Choices(const route_fixture::Case& c, std::mt19937_64& random) {
    std::vector<chat_travel::Destination> choices;
    for (size_t i = 0; i < c.departures.size(); ++i) { auto choice = Choice(c.departures[i]); choice.cost = c.costs[i]; choices.push_back(choice); }
    if (random() % 4 == 0) { auto unknown = Choice(c.target); unknown.known = false; choices.push_back(unknown); }
    if (random() % 4 == 0) { auto other = Choice(c.target); other.type = 1; choices.push_back(other); }
    std::shuffle(choices.begin(), choices.end(), random);
    return choices;
}

struct Sizes { int cases, slicedCases, shortcutCases; };
void WorldSuites(const route_fixture::World& source, const Sizes& sizes, uint64_t seed, bool unusual) {
    const route_fixture::Reference reference(source);
    Fixture f;
    const auto loadStart = std::chrono::steady_clock::now();
    auto loadedWorld = LayOut(source, seed, unusual);
    const auto loadTime = std::chrono::steady_clock::now() - loadStart;
    std::mt19937_64 random(seed);
    Timing legacyTimes, newTimes, readTimes;
    std::map<int, int> ticks;
    size_t legacyCriteria = 0, newCriteria = 0, legacyAreas = 0, newAreas = 0, cases = 0, found = 0;
    const std::string name = "[" + source.name + "] ";
    // The r111 planner and this one, on the same graph, character and options.
    for (int i = 0; i < sizes.cases; ++i) {
        if (i == sizes.cases / 2) loadedWorld = LayOut(source, seed + 1, unusual);  // another graph object: copied again, other areas
        const auto c = route_fixture::RandomCase(source, random, false);
        const auto choices = Choices(c, random);
        caseSalt = c.salt;
        const std::string where = " (" + source.name + ", case " + std::to_string(i) + ")";
        logs.clear();
        criteria = areaChecks = 0;
        auto start = std::chrono::steady_clock::now();
        legacy_route::Begin(choices, c.target, c.current, c.currentZone, c.temple);
        const auto expected = Fixture::Legacy();
        legacyTimes.Add(Ms(std::chrono::steady_clock::now() - start));
        legacyCriteria += static_cast<size_t>(criteria); legacyAreas += static_cast<size_t>(areaChecks);
        const auto legacySeed = LogLine("CHAT_ROUTE_SEEDED"), legacyEnd = LogLine("CHAT_ROUTE_FOUND");
        logs.clear();
        criteria = areaChecks = criteriaFree = 0;
        start = std::chrono::steady_clock::now();
        Begin(choices, c.target, c.current, c.currentZone, c.temple);
        chat_route::Result actual;
        int tickCount = 0;
        do { actual = Tick(); ++tickCount; } while (actual.status == Status::Pending);
        newTimes.Add(Ms(std::chrono::steady_clock::now() - start));
        newCriteria += static_cast<size_t>(criteria); newAreas += static_cast<size_t>(areaChecks);
        Check(criteriaFree == 0, "ens.wvs is only asked about an edge whose first transition has a criterion" + where);
        const auto snapshotLine = LogLine("CHAT_ROUTE_SNAPSHOT");
        if (!snapshotLine.empty()) readTimes.Add(std::stod(Value(snapshotLine, "buildMs")));
        else ++ticks[std::min(tickCount, 9)];
        Check(actual.status == expected.status, "Same reachability as r111" + where);
        Check(actual.departure.map == expected.departure.map && actual.departure.cost == expected.departure.cost && actual.steps == expected.steps && actual.shortcut == -1,
              "Same departure and steps as r111" + where);
        const auto seeded = LogLine("CHAT_ROUTE_SEEDED");
        Check(!seeded.empty() && seeded.rfind(legacySeed, 0) == 0, "Same seeds as r111" + where);
        if (actual.status == Status::Found) {
            const auto end = LogLine("CHAT_ROUTE_FOUND");
            Check(Value(end, "vertices") == Value(legacyEnd, "vertices") && Value(end, "edges") == Value(legacyEnd, "edges"), "Same vertices and edges examined as r111" + where);
            ++found;
        }
        ++cases;
        legacy_route::Clear(); chat_route::Clear(); ResetScratch();
    }
    Check(readTimes.samples.size() == 2, "One copy per graph object");
    // Sliced as finely as possible: the same results.
    chat_route::SetTickBudget(0);
    if (sizes.slicedCases) loadedWorld = LayOut(source, seed + 2, unusual);
    int slicedTicks = 0;
    for (int i = 0; i < sizes.slicedCases; ++i) {
        const auto c = route_fixture::RandomCase(source, random, false);
        const auto choices = Choices(c, random);
        caseSalt = c.salt;
        legacy_route::Begin(choices, c.target, c.current, c.currentZone, c.temple);
        const auto expected = Fixture::Legacy();
        Begin(choices, c.target, c.current, c.currentZone, c.temple);
        chat_route::Result actual;
        do { actual = Tick(); ++slicedTicks; } while (actual.status == Status::Pending);
        Check(actual.status == expected.status && actual.departure.map == expected.departure.map && actual.steps == expected.steps,
              "Slicing never changes the result (" + source.name + ", sliced case " + std::to_string(i) + ")");
        legacy_route::Clear(); chat_route::Clear(); ResetScratch();
    }
    chat_route::SetTickBudget(6000);
    // Shortcuts, against the brute force on the same filters.
    size_t taken = 0;
    for (int i = 0; i < sizes.shortcutCases; ++i) {
        const auto c = route_fixture::RandomCase(source, random, true);
        const auto choices = Choices(c, random);
        caseSalt = c.salt;
        std::vector<chat_route::Shortcut> shortcuts;
        for (const auto& s : c.shortcuts) shortcuts.push_back({s.id, s.from, s.to, s.cost});
        chat_route::SetShortcuts(shortcuts);
        Begin(choices, c.target, c.current, c.currentZone, c.temple);
        const auto actual = Fixture::Finish();
        const auto salt = loadedWorld.salt;
        const auto allowed = [&](size_t e) { return route_fixture::CriteriaAllowed(source, e, c.salt) && route_fixture::AreaAllowed(source, e, salt); };
        const auto expected = route_fixture::brute::Search(reference.graph, c.departures, c.target, c.current, c.currentZone, c.temple, c.shortcuts, allowed);
        const std::string where = " (" + source.name + ", shortcut case " + std::to_string(i) + ")";
        Check((actual.status == Status::Found) == expected.found, "Same reachability with shortcuts" + where);
        if (expected.found)
            Check(actual.departure.map == c.departures[expected.source] && actual.departure.cost == c.costs[expected.source] &&
                  actual.steps == expected.steps && actual.shortcut == expected.shortcut, "Same route with shortcuts" + where);
        taken += actual.shortcut >= 0;
        chat_route::Clear(); ResetScratch();
    }
    chat_route::SetShortcuts({});
    std::cout << name << source.Vertices() << " vertices, " << source.edges.size() << " edges laid out in the fake runtime in " << Ms(loadTime) << " ms\n";
    std::cout << name << "r111 equivalence through the fake runtime: " << cases << " cases (" << found << " found), " << slicedTicks << " ticks for "
              << sizes.slicedCases << " cases sliced to the minimum; " << sizes.shortcutCases << " shortcut cases (" << taken << " take one) match the brute force\n";
    std::cout << name << "native filter calls, r111: ens.wvs " << legacyCriteria << ", ens.wvt " << legacyAreas << "; r112: ens.wvs " << newCriteria
              << ", ens.wvt " << newAreas << " (kept per graph)\n";
    std::cout << name << "graph copy: " << readTimes.Text() << "; planning r111 (2 ms slices): " << legacyTimes.Text()
              << "; planning r112: " << newTimes.Text() << "; ticks per search once copied:";
    for (const auto& [count, n] : ticks) std::cout << ' ' << count << ':' << n;
    std::cout << '\n';
}
}

int main() {
    try {
        fake::Layout(); fake::InitApi(); chat_route::Install(); legacy_route::Install();
        Scenarios();
        Snapshots();
        Anomalies();
        // Disagreeing copies of a vertex for the r111 comparison; agreeing ones for
        // the brute force, which needs them. Both laid out with what the game never
        // builds but r111 still read (look-alike classes, removed entries).
        WorldSuites(route_fixture::Synthetic(20261023, true), {100, 10, 0}, 11, true);
        WorldSuites(route_fixture::Synthetic(20261024, false), {40, 0, 100}, 12, true);
        // The game's own graph: a local fixture only (derived from a game asset,
        // never committed), configured by CMake when it exists.
#ifdef ROUTE_GRAPH_FIXTURE
        const bool real = std::filesystem::exists(ROUTE_GRAPH_FIXTURE);
        if (real) WorldSuites(route_fixture::LoadWorld(ROUTE_GRAPH_FIXTURE), {120, 12, 150}, 21, false);
        std::cout << (real ? "Real-graph suites: ran (" : "Real-graph suites: skipped, the local fixture is missing (") << ROUTE_GRAPH_FIXTURE << ")\n";
#else
        std::cout << "Real-graph suites: skipped, no local fixture (work/r112-analysis/world-graph-fixture.txt)\n";
#endif
        Check(fake::handles.empty(), "Route roots leaked");
        std::cout << "PASS: real route comparison, barriers, conditions, interactive edges, cycles, current and zaap maps split into zones, "
                     "the player's own zone, only the game's destination map, the Roublard temple option, loading and session changes, "
                     "the graph copy and its layout checks, the filters asked, shortcuts, malformed and unusual elements as r111 met them, "
                     "and r111 on the synthetic world graph\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
