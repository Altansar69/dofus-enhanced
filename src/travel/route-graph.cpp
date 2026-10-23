#include "travel/route-graph.hpp"
#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <string>
#include <tuple>

namespace route_graph {
namespace {
// The Roublard temple under the Amakna cemetery: its interior maps (world -1,
// subarea 6, all at [10,16]) and the cemetery map of its door, as the game's own
// graph names them. Its statue teleports to the Temple des Douze and the Domaine
// des dieux; the game's criteria (ens.wvs) decide who may use them.
constexpr int64_t TempleMaps[] = {67108864, 67109888, 67110912, 67111936, 67112960, 67113984, 67110147};
constexpr int64_t TempleDoorMap = 82314240;
bool InTemple(int64_t map) { return std::find(std::begin(TempleMaps), std::end(TempleMaps), map) != std::end(TempleMaps); }
[[noreturn]] void Malformed(const char* context, int64_t count, int limit) {
    throw std::runtime_error(std::string("Route ") + context + " count=" + std::to_string(count) + " limit=" + std::to_string(limit));
}
bool Bad(int32_t count, int limit) { return count < 0 || count > limit; }
constexpr int Unset = std::numeric_limits<int>::max();
// Start distances, in map changes; a teleport adds 1. The current map starts at
// 0: only the player's own zone when the game's cell-to-zone link gave it, else
// every zone of the map (the game's autopilot then walks from the real cell). A
// zaap starts at 1, but on a map split into several zones the arrival side of
// the zaap is unknown: every zone then starts at 2. A closer zaap is never
// hidden, and an unambiguous arrival still wins an otherwise equal route.
constexpr int HereStart = 0, ZaapStart = 1, SplitZaapStart = 2;
}

bool TempleShortcut(int64_t from, int64_t to) { return InTemple(to) ? !InTemple(from) : InTemple(from) && to != TempleDoorMap; }

const MapZones* Graph::Zones(int64_t map) const {
    const auto found = std::lower_bound(maps.begin(), maps.end(), map, [](const MapZones& zones, int64_t key) { return zones.map < key; });
    return found != maps.end() && found->map == map ? &*found : nullptr;
}
uint32_t Graph::Find(uint64_t uid) const {
    const auto found = byUid.find(uid);
    return found == byUid.end() ? NoVertex : found->second;
}

void Builder::Reserve(size_t vertices, size_t edges, size_t maps) {
    graph.vertices.reserve(vertices);
    graph.byUid.reserve(vertices + vertices / 2);
    graph.edges.reserve(edges);
    graph.zones.reserve(vertices);
    graph.maps.reserve(maps);
    lists.reserve(vertices);
}
uint32_t Builder::Intern(uint64_t uid, int64_t map) {
    if (graph.vertices.size() >= NoVertex) throw std::runtime_error("World route graph too large");
    const auto [found, added] = graph.byUid.try_emplace(uid, static_cast<uint32_t>(graph.vertices.size()));
    if (added) {
        Vertex vertex;
        vertex.uid = uid;
        vertex.map = map;
        graph.vertices.push_back(vertex);
    }
    return found->second;
}
uint32_t Builder::Message(const std::string& message) {
    const auto [found, added] = messages.try_emplace(message, static_cast<uint32_t>(graph.errors.size()));
    if (added) graph.errors.push_back(message);
    return found->second;
}
void Builder::Map(int64_t map, int32_t declared) {
    MapZones zones;
    zones.map = map;
    zones.first = static_cast<uint32_t>(graph.zones.size());
    zones.declared = declared;
    graph.maps.push_back(zones);
    inMap = true;
}
void Builder::MapCountError(const std::string& message) {
    if (!inMap) throw std::logic_error("Route builder: error outside a map");
    graph.maps.back().countError = Message(message);
}
void Builder::MapValuesError(const std::string& message) {
    if (!inMap) throw std::logic_error("Route builder: error outside a map");
    if (!graph.maps.back().valuesError) graph.maps.back().valuesError = Message(message);
}
void Builder::ZoneVertex(uint64_t uid, int64_t map, int32_t zone) {
    if (!inMap) throw std::logic_error("Route builder: zone outside a map");
    graph.zones.push_back({Intern(uid, map), zone, map});
    ++graph.maps.back().count;
}
void Builder::EdgeList(uint64_t uid, int32_t count) {
    lists.push_back({uid, static_cast<uint32_t>(graph.edges.size()), 0, count, 0});
    inList = !Bad(count, EdgesLimit);
}
void Builder::EdgeListError(uint64_t uid, const std::string& message) {
    lists.push_back({uid, static_cast<uint32_t>(graph.edges.size()), 0, 0, Message(message)});
    inList = false;
}
void Builder::EdgeTo(uint64_t uid, int64_t map, int32_t transitions, bool criteria) {
    if (!inList) throw std::logic_error("Route builder: edge outside a readable list");
    Edge edge;
    edge.to = Intern(uid, map);
    edge.toMap = map;
    edge.transitions = transitions;
    edge.criteria = criteria;
    graph.edges.push_back(edge);
    ++lists.back().count;
}
void Builder::TransitionsError(const std::string& message) {
    if (!inList || !lists.back().count) throw std::logic_error("Route builder: no edge to mark");
    graph.edges.back().state = EdgeState::Transitions;
    graph.edges.back().error = Message(message);
}
void Builder::BadEdge(const std::string& message) {
    if (!inList) throw std::logic_error("Route builder: edge outside a readable list");
    Edge edge;
    edge.state = EdgeState::Examined;
    edge.error = Message(message);
    graph.edges.push_back(edge);
    ++lists.back().count;
}
Graph Builder::Finish() {
    // A list whose uid neither a map nor an edge names is never reached, as in
    // the game's graph. Dictionary keys are unique.
    for (const auto& list : lists) {
        const auto index = graph.Find(list.uid);
        if (index == NoVertex) continue;
        auto& vertex = graph.vertices[index];
        if (vertex.edgeCount || vertex.degree || vertex.error) throw std::runtime_error("World route graph repeats uid=" + std::to_string(list.uid));
        vertex.firstEdge = list.first;
        vertex.edgeCount = list.count;
        vertex.degree = list.degree;
        vertex.error = list.error;
    }
    std::stable_sort(graph.maps.begin(), graph.maps.end(), [](const MapZones& a, const MapZones& b) { return a.map < b.map; });
    for (size_t i = 1; i < graph.maps.size(); ++i)
        if (graph.maps[i].map == graph.maps[i - 1].map) throw std::runtime_error("World route graph repeats map=" + std::to_string(graph.maps[i].map));
    lists.clear();
    messages.clear();
    inMap = inList = false;
    Graph result = std::move(graph);
    graph = {};
    return result;
}

bool Search::Later::operator()(const Node& a, const Node& b) const {
    return std::tie(a.distance, a.layer, a.source, a.first, a.uid) > std::tie(b.distance, b.layer, b.source, b.first, b.uid);
}
void Search::Clear() {
    graph = nullptr;
    query = {};
    distance.clear(); source.clear(); first.clear(); visited.clear();
    queue = {};
    usable.clear();
    expanding = false;
    current = {};
    nextEdge = endEdge = 0;
    nextShortcut = 0;
    plainVisited = plainInspected = 0;
    stats = {};
}
void Search::Fail(uint32_t error) const { throw std::runtime_error(graph->errors.at(error)); }
bool Search::Improves(size_t state, int next, uint32_t from, int32_t shortcut) const {
    if (visited[state]) return false;
    if (distance[state] == Unset) return true;
    return std::tie(next, from, shortcut) < std::tie(distance[state], source[state], first[state]);
}
void Search::Enqueue(uint32_t vertex, uint8_t layer, int next, uint32_t from, int32_t shortcut, int64_t map) {
    const auto state = State(vertex, layer);
    distance[state] = next;
    source[state] = from;
    first[state] = shortcut;
    queue.push({next, layer, from, shortcut, graph->vertices[vertex].uid, vertex, map});
}
void Search::Start(const Graph& graphData, Query next) {
    Clear();
    graph = &graphData;
    query = std::move(next);
    // Layer 0: routes without a shortcut (the r111 search); layer 1: routes
    // that took one, with its rank. A layer-1 state only ever wins at a
    // strictly shorter distance, since the queue orders the layers after it.
    const auto states = graphData.vertices.size() * 2;
    distance.assign(states, Unset);
    source.assign(states, 0);
    first.assign(states, -1);
    visited.assign(states, 0);
    for (size_t rank = 0; rank < query.departures.size(); ++rank) {
        const auto map = query.departures[rank];
        const auto* zones = graphData.Zones(map);
        if (!zones) continue;
        if (zones->countError) Fail(zones->countError);
        if (Bad(zones->declared, ZonesLimit)) Malformed("zones", zones->declared, ZonesLimit);
        if (!zones->declared) continue;
        if (zones->valuesError) Fail(zones->valuesError);
        const auto begin = graphData.zones.begin() + zones->first;
        std::vector<Zone> starts(begin, begin + zones->count);
        const bool here = map == query.current;
        if (here && query.currentZone >= 0) {
            const auto own = std::find_if(starts.begin(), starts.end(), [&](const Zone& zone) { return zone.zone == query.currentZone; });
            if (own != starts.end()) { const Zone zone = *own; starts.assign(1, zone); }
        }
        const int start = here ? HereStart : starts.size() == 1 ? ZaapStart : SplitZaapStart;
        if (!here && starts.size() > 1) ++stats.splitZaaps;
        for (const auto& zone : starts) {
            if (!Improves(State(zone.vertex, 0), start, static_cast<uint32_t>(rank), -1)) continue;
            Enqueue(zone.vertex, 0, start, static_cast<uint32_t>(rank), -1, zone.map);
            if (here) ++stats.hereStarts;
        }
    }
    stats.starts = queue.size();
    for (size_t index = 0; index < query.shortcuts.size(); ++index) {
        const auto& shortcut = query.shortcuts[index];
        if (shortcut.cost < 1 || shortcut.cost > ShortcutCostLimit) continue;
        if (!query.templeAllowed && TempleShortcut(shortcut.from, shortcut.to)) continue;
        const auto* zones = graphData.Zones(shortcut.to);
        if (!zones || !zones->count) continue;
        usable.push_back(static_cast<uint32_t>(index));
    }
    stats.shortcuts = usable.size();
}
Outcome Search::Run(const Filter& allowed, Clock::time_point deadline, size_t steps) {
    if (!graph) return {Status::Unreachable};
    const auto& data = *graph;
    size_t spent = 0;
    bool askedGame = false;
    // The clock is read every 32 steps, and after each question to the game.
    const auto exhausted = [&] {
        if (spent >= steps) return true;
        ++spent;
        if (!askedGame && (spent & 31)) return false;
        askedGame = false;
        return Clock::now() >= deadline;
    };
    for (;;) {
        if (!expanding) {
            if (queue.empty()) return {Status::Unreachable};
            if (exhausted()) return {};
            const Node node = queue.top();
            queue.pop();
            const auto state = State(node.vertex, node.layer);
            if (distance[state] != node.distance || source[state] != node.source || first[state] != node.first) continue;
            if (visited[state]) continue;
            visited[state] = 1;
            ++stats.visited;
            if (!node.layer && (++plainVisited > VisitedLimit || node.distance > DistanceLimit)) throw std::runtime_error("World route search bounds");
            if (node.map == query.target)
                return {Status::Found, node.source, node.distance, node.layer ? query.shortcuts[static_cast<size_t>(node.first)].id : -1};
            const auto& vertex = data.vertices[node.vertex];
            const bool broken = vertex.error || Bad(vertex.degree, EdgesLimit);
            if (broken && !node.layer) {
                if (vertex.error) Fail(vertex.error);
                Malformed("outgoing edges", vertex.degree, EdgesLimit);
            }
            current = node;
            expanding = true;
            nextEdge = vertex.firstEdge;
            endEdge = broken ? nextEdge : vertex.firstEdge + vertex.edgeCount;
            nextShortcut = node.layer ? usable.size() : 0;  // a route takes at most one shortcut
        }
        const bool plain = !current.layer;
        while (nextEdge < endEdge) {
            if (exhausted()) return {};
            const auto index = nextEdge++;
            const auto& edge = data.edges[index];
            ++stats.inspected;
            if (plain && ++plainInspected > InspectedLimit) throw std::runtime_error("World route edge bounds");
            if (edge.state == EdgeState::Examined) { if (plain) Fail(edge.error); continue; }
            const int next = current.distance + 1;
            if (!plain && next > DistanceLimit) continue;
            // Only skip when an already accepted path is as good or better.
            // A rejected edge never marks its destination reached.
            if (!Improves(State(edge.to, current.layer), next, current.source, current.first)) continue;
            if (!query.templeAllowed && TempleShortcut(current.map, edge.toMap)) continue;
            if (edge.state == EdgeState::Transitions) { if (plain) Fail(edge.error); continue; }
            if (Bad(edge.transitions, TransitionsLimit)) { if (plain) Malformed("transitions", edge.transitions, TransitionsLimit); continue; }
            if (!edge.transitions) continue;
            ++stats.filtered;
            askedGame = true;
            if (!allowed(index)) continue;
            Enqueue(edge.to, current.layer, next, current.source, current.first, edge.toMap);
        }
        while (nextShortcut < usable.size()) {
            if (exhausted()) return {};
            const auto index = usable[nextShortcut++];
            const auto& shortcut = query.shortcuts[index];
            if (shortcut.from != current.map) continue;
            const int next = current.distance + shortcut.cost;
            if (next > DistanceLimit) continue;
            const auto* zones = data.Zones(shortcut.to);
            for (uint32_t zone = 0; zone < zones->count; ++zone) {
                const auto& to = data.zones[zones->first + zone];
                if (Improves(State(to.vertex, 1), next, current.source, static_cast<int32_t>(index)))
                    Enqueue(to.vertex, 1, next, current.source, static_cast<int32_t>(index), to.map);
            }
        }
        expanding = false;
    }
}
}
