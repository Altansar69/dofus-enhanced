#pragma once
// Route search references for the offline tests: a synthetic world graph with
// the game's characteristics (committed tests), the game's own world graph when
// its local fixture exists (work/r112-analysis, never committed: it derives from
// a game asset), the r111 chat_route search transcribed to plain data, and an
// independent brute force for shortcuts.
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <queue>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace route_fixture {
// ---------------------------------------------------------------------------
// A world graph: vertex uid = index; edges in the game's order.
// ---------------------------------------------------------------------------
struct FixtureEdge {
    uint32_t from = 0, to = 0;
    int criterion = -1;           // the first transition's criterion class (-1: none)
    int transitions = 1;
    bool laterCriterion = false;  // a criterion on a later transition only (ens.wvs ignores it)
    int64_t toMap = 0;            // this edge's copy of its destination disagrees: its map (0: agrees)
    int32_t toZone = -1;          // ... or its zone (-1: agrees)
};
struct World {
    std::string name;
    std::vector<int64_t> map;
    std::vector<int32_t> zone;
    std::vector<uint32_t> first;  // edges of vertex v: [first[v], first[v + 1])
    std::vector<FixtureEdge> edges;
    std::vector<int64_t> maps;                              // in first-appearance order (the m_vertices order)
    std::unordered_map<int64_t, std::vector<uint32_t>> zones;  // map -> vertices, in uid order
    std::vector<int64_t> emptyMaps;                          // keys of m_vertices without a zone
    std::unordered_map<int64_t, int> subarea;               // map -> subarea, when known (the ens.wvt rule)
    std::vector<int64_t> split;                              // maps with several zones
    int criterionClasses = 0;
    size_t Vertices() const { return map.size(); }
    void Finish() {
        split.clear();
        for (const auto m : maps) if (m > 0 && zones.at(m).size() > 1) split.push_back(m);
    }
    // The map and zone of an edge's own copy of its destination.
    int64_t ToMap(const FixtureEdge& e) const { return e.toMap ? e.toMap : map[e.to]; }
    int32_t ToZone(const FixtureEdge& e) const { return e.toZone >= 0 ? e.toZone : zone[e.to]; }
};
inline int64_t Base36(const std::string& text) {
    if (text.empty()) throw std::runtime_error("Fixture: empty number");
    size_t i = 0;
    bool negative = text[0] == '-';
    if (negative) i = 1;
    int64_t value = 0;
    for (; i < text.size(); ++i) {
        const char c = text[i];
        const int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'z' ? c - 'a' + 10 : -1;
        if (digit < 0) throw std::runtime_error("Fixture: bad number " + text);
        value = value * 36 + digit;
    }
    return negative ? -value : value;
}
inline World LoadWorld(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Fixture missing: " + path);
    World world;
    world.name = "the game's world graph";
    std::string line;
    size_t vertices = 0, edges = 0;
    bool header = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();  // a CRLF checkout
        if (line.empty() || line[0] == '#') continue;
        std::istringstream fields(line);
        if (!header) {
            std::string magic; int version = 0;
            fields >> magic >> version >> vertices >> edges;
            if (magic != "route-graph" || version != 1) throw std::runtime_error("Fixture: bad header");
            header = true;
            world.map.reserve(vertices); world.zone.reserve(vertices); world.first.reserve(vertices + 1); world.edges.reserve(edges);
            continue;
        }
        std::string map, zone, item;
        fields >> map >> zone;
        const auto uid = static_cast<uint32_t>(world.map.size());
        world.map.push_back(Base36(map));
        world.zone.push_back(static_cast<int32_t>(Base36(zone)));
        world.first.push_back(static_cast<uint32_t>(world.edges.size()));
        auto& zones = world.zones[world.map.back()];
        if (zones.empty()) world.maps.push_back(world.map.back());
        zones.push_back(uid);
        while (fields >> item) {
            FixtureEdge edge;
            edge.from = uid;
            const auto star = item.find('*'), plus = item.find('+');
            edge.to = static_cast<uint32_t>(Base36(item.substr(0, std::min(star, plus))));
            if (star != std::string::npos) {
                edge.criterion = static_cast<int>(Base36(item.substr(star + 1, plus == std::string::npos ? std::string::npos : plus - star - 1)));
                world.criterionClasses = std::max(world.criterionClasses, edge.criterion + 1);
            }
            if (plus != std::string::npos) edge.transitions = static_cast<int>(Base36(item.substr(plus + 1)));
            world.edges.push_back(edge);
        }
    }
    world.first.push_back(static_cast<uint32_t>(world.edges.size()));
    if (!header || world.map.size() != vertices || world.edges.size() != edges) throw std::runtime_error("Fixture: truncated");
    for (const auto& edge : world.edges) if (edge.to >= vertices) throw std::runtime_error("Fixture: edge out of range");
    world.Finish();
    return world;
}

inline uint64_t Mix(uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}
// The game's filters as the tests simulate them. ens.wvs: the character meets a
// criterion class or not (salt: the character's state). ens.wvt: a move into
// another subarea is refused when that subarea is closed to automatic travel
// (salt: which ones); without subarea data (the game's graph fixture), a few
// edges refused at random.
inline bool CriteriaAllowed(const World& world, size_t edge, uint64_t salt) {
    const auto k = world.edges[edge].criterion;
    return k < 0 || Mix(salt ^ static_cast<uint64_t>(k)) % 3 != 0;
}
inline bool AreaAllowed(const World& world, size_t edge, uint64_t salt) {
    const auto& e = world.edges[edge];
    if (world.subarea.empty()) return Mix(salt * 31 + edge) % 40 != 0;
    const auto from = world.subarea.at(world.map[e.from]), to = world.subarea.at(world.ToMap(e));
    return from == to || Mix(salt * 131 + static_cast<uint64_t>(to)) % 6 != 0;
}

// ---------------------------------------------------------------------------
// A synthetic world with the characteristics the search depends on: a grid of
// maps, some split into zones (joined or not, up to 13), edges one-way or not,
// without transitions or with several, criteria on the first transition (and
// on a later one only), subareas, dungeons with keys, long transports, a
// 300-edge hub, the Roublard temple with the game's own map ids (door,
// interior, statue to the Domaine des dieux and the Temple des Douze), the Cité
// d'Astrub map of the passage, and map keys without zones. With `copies`, a few
// edges hold a copy of their destination that disagrees on its map or zone
// (the search reads the map from the copy that reached a vertex, as r111 did).
// ---------------------------------------------------------------------------
inline World Synthetic(uint64_t seed, bool copies) {
    World w;
    w.name = copies ? "the synthetic world graph (disagreeing copies)" : "the synthetic world graph";
    std::mt19937_64 random(seed);
    const auto pick = [&](size_t n) { return static_cast<size_t>(random() % n); };
    const auto chance = [&](int percent) { return static_cast<int>(random() % 100) < percent; };
    std::vector<std::vector<FixtureEdge>> out;
    const auto vertex = [&](int64_t map, int32_t zone, int subarea) {
        const auto uid = static_cast<uint32_t>(w.map.size());
        w.map.push_back(map); w.zone.push_back(zone); out.emplace_back();
        auto& zones = w.zones[map];
        if (zones.empty()) w.maps.push_back(map);
        zones.push_back(uid);
        w.subarea[map] = subarea;
        return uid;
    };
    constexpr int Random = -2, Plain = -1;  // or a criterion class
    const auto edge = [&](uint32_t from, uint32_t to, int kind) {
        FixtureEdge e;
        e.from = from; e.to = to;
        if (kind == Random) {
            const int roll = static_cast<int>(pick(1000));
            e.transitions = roll < 20 ? 0 : roll < 930 ? 1 : roll < 980 ? 2 : roll < 995 ? 3 : 4;
            if (e.transitions > 0 && chance(3)) e.criterion = static_cast<int>(pick(150));
            if (e.transitions > 1 && e.criterion < 0 && chance(40)) e.laterCriterion = true;
        } else e.criterion = kind;
        out[from].push_back(e);
    };
    const auto both = [&](uint32_t a, uint32_t b, int kind) { edge(a, b, kind); edge(b, a, kind); };
    constexpr int Width = 64, Height = 56;
    std::vector<std::vector<uint32_t>> grid(static_cast<size_t>(Width * Height));
    const auto cell = [&](int x, int y) -> const std::vector<uint32_t>& { return grid[static_cast<size_t>(y * Width + x)]; };
    const auto anyZone = [&](const std::vector<uint32_t>& zones) { return zones[pick(zones.size())]; };
    const auto anyCell = [&]() -> const std::vector<uint32_t>& { return grid[pick(grid.size())]; };
    for (int y = 0; y < Height; ++y)
        for (int x = 0; x < Width; ++x) {
            const int roll = static_cast<int>(pick(1000));
            int zones = roll < 40 ? 2 : roll < 50 ? 3 : roll < 53 ? 4 + static_cast<int>(pick(2)) : 1;
            if (x == 20 && y == 20) zones = 13;  // the game's most split map has 13 zones
            auto& zoneList = grid[static_cast<size_t>(y * Width + x)];
            for (int z = 1; z <= zones; ++z) zoneList.push_back(vertex(200000000 + x * 1000 + y, z, 1 + x / 8 + (y / 8) * 8));
            for (size_t z = 1; z < zoneList.size(); ++z) if (chance(50)) both(zoneList[z - 1], zoneList[z], Random);
        }
    for (int y = 0; y < Height; ++y)
        for (int x = 0; x < Width; ++x) {
            if (x + 1 < Width) {
                if (chance(88)) edge(anyZone(cell(x, y)), anyZone(cell(x + 1, y)), Random);
                if (chance(88)) edge(anyZone(cell(x + 1, y)), anyZone(cell(x, y)), Random);
            }
            if (y + 1 < Height) {
                if (chance(88)) edge(anyZone(cell(x, y)), anyZone(cell(x, y + 1)), Random);
                if (chance(88)) edge(anyZone(cell(x, y + 1)), anyZone(cell(x, y)), Random);
            }
        }
    int64_t interior = 300100000;
    const auto chain = [&](uint32_t from, uint32_t to, int length, int subarea) {
        uint32_t previous = from;
        for (int i = 0; i < length; ++i) { const auto step = vertex(interior++, 1, subarea); both(previous, step, Plain); previous = step; }
        both(previous, to, Plain);
    };
    // Dungeons and houses: a door from the grid, sometimes a key, sometimes another exit.
    for (int i = 0; i < 120; ++i) {
        uint32_t previous = anyZone(anyCell());
        const int key = chance(30) ? static_cast<int>(pick(150)) : Plain;
        const int length = 2 + static_cast<int>(pick(7));
        for (int step = 0; step < length; ++step) {
            const auto room = vertex(interior++, 1, 1000 + i);
            if (step) both(previous, room, Random);
            else { edge(previous, room, key); edge(room, previous, Plain); }
            previous = room;
        }
        if (chance(30)) edge(previous, anyZone(anyCell()), Plain);
    }
    for (int i = 0; i < 40; ++i) edge(anyZone(anyCell()), anyZone(anyCell()), Random);  // boats and other transports
    const auto hub = vertex(81789954, 1, 500);  // like the game's 296-edge hub
    for (int i = 0; i < 300; ++i) edge(hub, anyZone(anyCell()), Random);
    for (int i = 0; i < 12; ++i) edge(anyZone(anyCell()), hub, Random);
    // The Roublard temple under the Amakna cemetery (split in two zones), with the
    // game's ids; the statue's teleports carry their own criteria (classes 150, 151).
    const auto cemeteryA = vertex(82313729, 1, 7), cemeteryB = vertex(82313729, 2, 7), door = vertex(82314240, 1, 7);
    const auto hall = vertex(67108864, 1, 6), side = vertex(67109888, 1, 6), west = vertex(67110912, 1, 6), inner = vertex(67111936, 1, 6);
    const auto crypt = vertex(67112960, 1, 6), room = vertex(67113984, 1, 6), annex = vertex(67110147, 1, 6);
    const auto domaine = vertex(188219392, 1, 600), douze = vertex(192416776, 1, 601), astrub = vertex(67372032, 1, 602);
    both(cemeteryA, door, Plain); both(cemeteryA, anyZone(cell(30, 30)), Plain); both(cemeteryB, anyZone(cell(31, 30)), Plain);
    edge(door, hall, Plain); edge(hall, door, Plain);
    both(hall, west, Plain); both(west, inner, Plain); both(inner, room, Plain); both(hall, side, Plain); both(inner, crypt, Plain); both(west, annex, Plain);
    edge(room, domaine, 150); edge(room, douze, 151); edge(domaine, room, 150);
    chain(domaine, anyZone(cell(50, 10)), 3, 603);
    chain(douze, anyZone(cell(10, 45)), 4, 604);
    both(astrub, anyZone(cell(5, 5)), Plain);
    w.emptyMaps = {300900001, 300900002};
    w.criterionClasses = 152;
    if (copies)
        for (auto& list : out)
            for (auto& e : list) {
                const int roll = static_cast<int>(pick(1000));
                if (roll < 3) e.toMap = w.maps[pick(w.maps.size())];
                else if (roll < 6) e.toZone = 7 + static_cast<int>(pick(3));
            }
    for (uint32_t v = 0; v < out.size(); ++v) {
        w.first.push_back(static_cast<uint32_t>(w.edges.size()));
        w.edges.insert(w.edges.end(), out[v].begin(), out[v].end());
    }
    w.first.push_back(static_cast<uint32_t>(w.edges.size()));
    w.Finish();
    return w;
}

// ---------------------------------------------------------------------------
// The r111 search (src/travel/chat-route.cpp at 08a5958: Seed and Tick), on
// plain data and without its time slices, which never changed its result.
// ---------------------------------------------------------------------------
namespace r111 {
struct Vertex { uint64_t uid = 0; int64_t map = 0; int32_t zone = 0; };
struct Edge { size_t id = 0; Vertex to; int transitions = 1; };
struct Graph {
    // TryGetValue(m_vertices, map) then its values; TryGetValue(m_outgoingEdges, uid).
    std::function<const std::vector<Vertex>*(int64_t)> zones;
    std::function<const std::vector<Edge>*(uint64_t)> outgoing;
};
enum class Status { Found, Unreachable };
struct Result {
    Status status = Status::Unreachable; size_t source = 0; int steps = 0;
    size_t starts = 0, hereStarts = 0, splitZaaps = 0, visited = 0, inspected = 0;
};
using Filter = std::function<bool(size_t edge)>;
constexpr int64_t TempleMaps[] = {67108864, 67109888, 67110912, 67111936, 67112960, 67113984, 67110147};
constexpr int64_t TempleDoorMap = 82314240;
inline bool InTemple(int64_t map) { return std::find(std::begin(TempleMaps), std::end(TempleMaps), map) != std::end(TempleMaps); }
inline bool TempleShortcut(int64_t from, int64_t to) { return InTemple(to) ? !InTemple(from) : InTemple(from) && to != TempleDoorMap; }
inline void Count(int64_t n, int limit, const char* context) {
    if (n < 0 || n > limit) throw std::runtime_error(std::string("Route ") + context + " count=" + std::to_string(n) + " limit=" + std::to_string(limit));
}
// departures: the choices after Begin's sort. asked: the edges given to the
// native filters, in order (ens.wvs, then ens.wvt when wvs allowed).
inline Result Search(const Graph& graph, const std::vector<int64_t>& departures, int64_t target, int64_t current, int currentZone,
                     bool templeAllowed, const Filter& wvs, const Filter& wvt, std::vector<size_t>* asked = nullptr) {
    struct Node { int distance; size_t source; uint64_t uid; Vertex vertex; };
    struct Later { bool operator()(const Node& a, const Node& b) const { return std::tie(a.distance, a.source, a.uid) > std::tie(b.distance, b.source, b.uid); } };
    std::priority_queue<Node, std::vector<Node>, Later> queue;
    std::unordered_set<uint64_t> visited;
    std::unordered_map<uint64_t, std::pair<int, size_t>> best;
    Result result;
    const auto improves = [&](uint64_t uid, int distance, size_t source) {
        if (visited.count(uid)) return false;
        const auto found = best.find(uid);
        return found == best.end() || std::pair{distance, source} < found->second;
    };
    const auto enqueue = [&](const Vertex& vertex, int distance, size_t source) {
        best[vertex.uid] = {distance, source};
        queue.push({distance, source, vertex.uid, vertex});
    };
    for (size_t i = 0; i < departures.size(); ++i) {
        const auto* zones = graph.zones(departures[i]);
        const int count = zones ? static_cast<int>(zones->size()) : 0;
        Count(count, 64, "zones");
        if (!count) continue;
        std::vector<Vertex> starts(zones->begin(), zones->end());
        const bool here = departures[i] == current;
        if (here && currentZone >= 0) {
            const auto own = std::find_if(starts.begin(), starts.end(), [&](const Vertex& v) { return v.zone == currentZone; });
            if (own != starts.end()) { const Vertex vertex = *own; starts.assign(1, vertex); }
        }
        const int distance = here ? 0 : starts.size() == 1 ? 1 : 2;
        if (!here && starts.size() > 1) ++result.splitZaaps;
        for (const auto& vertex : starts) {
            if (!improves(vertex.uid, distance, i)) continue;
            enqueue(vertex, distance, i);
            if (here) ++result.hereStarts;
        }
    }
    result.starts = queue.size();
    while (!queue.empty()) {
        const Node node = queue.top();
        queue.pop();
        if (best.at(node.uid) != std::pair{node.distance, node.source}) continue;
        if (!visited.insert(node.uid).second) continue;
        if (visited.size() > 65536 || node.distance > 4096) throw std::runtime_error("World route search bounds");
        if (node.vertex.map == target) {
            result.status = Status::Found; result.source = node.source; result.steps = node.distance; result.visited = visited.size();
            return result;
        }
        const auto* edges = graph.outgoing(node.uid);
        if (!edges) continue;
        Count(static_cast<int64_t>(edges->size()), 1024, "outgoing edges");
        for (const auto& edge : *edges) {
            if (++result.inspected > 262144) throw std::runtime_error("World route edge bounds");
            if (!improves(edge.to.uid, node.distance + 1, node.source)) continue;
            if (!templeAllowed && TempleShortcut(node.vertex.map, edge.to.map)) continue;
            Count(edge.transitions, 128, "transitions");
            if (edge.transitions == 0) continue;
            if (asked) asked->push_back(edge.id);
            if (!wvs(edge.id) || !wvt(edge.id)) continue;
            enqueue(edge.to, node.distance + 1, node.source);
        }
    }
    result.visited = visited.size();
    return result;
}
}

// ---------------------------------------------------------------------------
// Shortcuts, by brute force: one plain Dijkstra per departure over (vertex,
// took a shortcut), then the lexicographic best (distance, took a shortcut,
// departure rank, shortcut rank) over every departure. Independent of the
// multi-source search and of its queue order. A route takes at most one
// shortcut. Needs a world whose copies of a vertex agree.
// ---------------------------------------------------------------------------
namespace brute {
struct Shortcut { int id = 0; int64_t from = 0, to = 0; int cost = 1; };
struct Result { bool found = false; size_t source = 0; int steps = 0; int shortcut = -1; };
inline Result Search(const r111::Graph& graph, const std::vector<int64_t>& departures, int64_t target, int64_t current, int currentZone,
                     bool templeAllowed, const std::vector<Shortcut>& shortcuts, const std::function<bool(size_t)>& allowed) {
    using Key = std::tuple<int, int, size_t, int>;  // distance, layer, departure, first shortcut
    bool any = false;
    Key best{};
    for (size_t i = 0; i < departures.size(); ++i) {
        const auto* zones = graph.zones(departures[i]);
        if (!zones || zones->empty()) continue;
        std::vector<r111::Vertex> starts(zones->begin(), zones->end());
        const bool here = departures[i] == current;
        if (here && currentZone >= 0) {
            const auto own = std::find_if(starts.begin(), starts.end(), [&](const r111::Vertex& v) { return v.zone == currentZone; });
            if (own != starts.end()) { const r111::Vertex vertex = *own; starts.assign(1, vertex); }
        }
        const int start = here ? 0 : starts.size() == 1 ? 1 : 2;
        // Per departure: label (distance, first shortcut) per (uid, layer).
        using Label = std::pair<int, int>;
        std::map<std::pair<uint64_t, int>, Label> label;
        std::set<std::pair<uint64_t, int>> done;
        using Item = std::tuple<int, int, int, uint64_t>;  // distance, layer, first, uid
        std::priority_queue<Item, std::vector<Item>, std::greater<Item>> queue;
        std::unordered_map<uint64_t, r111::Vertex> vertexOf;
        const auto offer = [&](const r111::Vertex& v, int layer, int distance, int first) {
            vertexOf[v.uid] = v;
            const auto key = std::pair{v.uid, layer};
            if (done.count(key)) return;
            const auto found = label.find(key);
            if (found != label.end() && found->second <= Label{distance, first}) return;
            label[key] = {distance, first};
            queue.push({distance, layer, first, v.uid});
        };
        for (const auto& v : starts) offer(v, 0, start, -1);
        while (!queue.empty()) {
            const auto [distance, layer, first, uid] = queue.top();
            queue.pop();
            const auto key = std::pair{uid, layer};
            if (done.count(key) || label.at(key) != Label{distance, first}) continue;
            done.insert(key);
            const Key candidate{distance, layer, i, first};
            if (any && std::get<0>(best) < distance) break;
            const auto& vertex = vertexOf.at(uid);
            if (vertex.map == target) {
                if (!any || candidate < best) { best = candidate; any = true; }
                break;  // the queue order makes this the departure's best
            }
            if (const auto* edges = graph.outgoing(uid)) {
                for (const auto& edge : *edges) {
                    if (edge.transitions <= 0 || edge.transitions > 128) continue;
                    if (!templeAllowed && r111::TempleShortcut(vertex.map, edge.to.map)) continue;
                    if (!allowed(edge.id)) continue;
                    offer(edge.to, layer, distance + 1, first);
                }
            }
            for (size_t j = 0; j < shortcuts.size() && !layer; ++j) {
                const auto& shortcut = shortcuts[j];
                if (shortcut.from != vertex.map || shortcut.cost < 1 || shortcut.cost > 64) continue;
                if (!templeAllowed && r111::TempleShortcut(shortcut.from, shortcut.to)) continue;
                const auto* targets = graph.zones(shortcut.to);
                if (!targets) continue;
                for (const auto& to : *targets) offer(to, 1, distance + shortcut.cost, static_cast<int>(j));
            }
        }
    }
    Result result;
    if (!any) return result;
    result.found = true;
    result.steps = std::get<0>(best);
    result.source = std::get<2>(best);
    result.shortcut = std::get<1>(best) ? shortcuts[static_cast<size_t>(std::get<3>(best))].id : -1;
    return result;
}
}

// ---------------------------------------------------------------------------
// A world as the references read it.
// ---------------------------------------------------------------------------
struct Reference {
    std::unordered_map<int64_t, std::vector<r111::Vertex>> zones;
    std::vector<std::vector<r111::Edge>> outgoing;
    r111::Graph graph;
    explicit Reference(const World& world, const std::vector<int>* transitions = nullptr) {
        outgoing.resize(world.Vertices());
        for (const auto& [map, list] : world.zones)
            for (const auto uid : list) zones[map].push_back({uid, map, world.zone[uid]});
        for (const auto map : world.emptyMaps) zones[map];
        for (uint32_t v = 0; v < world.Vertices(); ++v)
            for (uint32_t e = world.first[v]; e < world.first[v + 1]; ++e) {
                const auto to = world.edges[e].to;
                outgoing[v].push_back({e, {to, world.ToMap(world.edges[e]), world.ToZone(world.edges[e])}, transitions ? (*transitions)[e] : world.edges[e].transitions});
            }
        graph.zones = [this](int64_t map) -> const std::vector<r111::Vertex>* {
            const auto found = zones.find(map);
            return found == zones.end() ? nullptr : &found->second;
        };
        graph.outgoing = [this](uint64_t uid) -> const std::vector<r111::Edge>* {
            return uid < outgoing.size() && !outgoing[uid].empty() ? &outgoing[uid] : nullptr;
        };
    }
    Reference(const Reference&) = delete;
    Reference& operator=(const Reference&) = delete;
};

// ---------------------------------------------------------------------------
// Random cases on a world.
// ---------------------------------------------------------------------------
struct Case {
    std::vector<int64_t> departures;  // already in Begin's order
    std::vector<int> costs;           // the kamas of each departure (for chat_route's own sort)
    int64_t target = 0, current = 0;
    int currentZone = -1;
    bool temple = true;
    std::vector<brute::Shortcut> shortcuts;
    uint64_t salt = 0;  // the character's state for ens.wvs
};
// Neighbourhood of a vertex within `steps` edges (to pick nearby targets).
inline std::vector<uint32_t> Around(const World& world, uint32_t from, int steps) {
    std::vector<uint32_t> frontier{from}, all{from};
    std::unordered_set<uint32_t> seen{from};
    for (int s = 0; s < steps; ++s) {
        std::vector<uint32_t> next;
        for (const auto v : frontier)
            for (uint32_t e = world.first[v]; e < world.first[v + 1]; ++e)
                if (seen.insert(world.edges[e].to).second) { next.push_back(world.edges[e].to); all.push_back(world.edges[e].to); }
        frontier.swap(next);
    }
    return all;
}
inline Case RandomCase(const World& world, std::mt19937_64& random, bool withShortcuts) {
    const auto pick = [&](size_t n) { return static_cast<size_t>(random() % n); };
    const auto chance = [&](int percent) { return static_cast<int>(random() % 100) < percent; };
    const auto& split = world.split;
    // Begin drops a departure without a positive map; the game never targets one.
    const auto anyMap = [&] { for (;;) { const auto m = world.maps[pick(world.maps.size())]; if (m > 0) return m; } };
    // A key of m_vertices without a zone, when the world has one.
    const auto emptyMap = [&](int64_t otherwise) { return world.emptyMaps.empty() ? otherwise : world.emptyMaps[pick(world.emptyMaps.size())]; };
    Case c;
    c.salt = random();
    c.temple = chance(50);
    // The current map: often split into zones, sometimes the Roublard temple.
    const int64_t templeMaps[] = {82313729, 82314240, 67108864, 67113984, 188219392, 192416776};
    c.current = chance(25) ? split[pick(split.size())] : chance(10) ? templeMaps[pick(std::size(templeMaps))] : anyMap();
    if (chance(40)) {
        const auto& zones = world.zones.at(c.current);
        c.currentZone = chance(85) ? world.zone[zones[pick(zones.size())]] : 7;
    }
    std::vector<int64_t> maps;
    if (chance(70)) maps.push_back(c.current);
    const size_t zaaps = pick(14);
    for (size_t i = 0; i < zaaps; ++i) {
        const int roll = static_cast<int>(pick(100));
        maps.push_back(roll < 30 ? split[pick(split.size())] : roll < 35 ? 999999999 + static_cast<int64_t>(i) : roll < 38 ? emptyMap(anyMap()) : anyMap());
    }
    if (chance(15) && !maps.empty()) maps.push_back(maps[pick(maps.size())]);  // a repeated departure
    // Begin's order: the current map first, then by kamas and map.
    std::vector<std::tuple<bool, int, int64_t>> order;
    for (const auto map : maps) order.emplace_back(map != c.current, static_cast<int>(pick(4)) * 100, map);
    std::sort(order.begin(), order.end());
    for (const auto& [remote, cost, map] : order) { (void)remote; c.departures.push_back(map); c.costs.push_back(cost); }
    // A vertex of a map in the graph: the given one when it is, another otherwise.
    const auto vertexOf = [&](int64_t map) {
        const auto found = world.zones.find(map);
        const auto& zones = found != world.zones.end() ? found->second : world.zones.at(anyMap());
        return zones[pick(zones.size())];
    };
    // The target: near a departure, anywhere, the temple, or outside the graph.
    const int roll = static_cast<int>(pick(100));
    if (roll < 45 && !c.departures.empty()) {
        const auto close = Around(world, vertexOf(c.departures[pick(c.departures.size())]), 1 + static_cast<int>(pick(12)));
        c.target = world.map[close[pick(close.size())]];
    } else if (roll < 55) c.target = templeMaps[pick(std::size(templeMaps))];
    else if (roll < 60) c.target = 123456789;
    else if (roll < 63) c.target = emptyMap(anyMap());
    else c.target = anyMap();
    if (withShortcuts) {
        const size_t count = pick(4);
        for (size_t i = 0; i < count; ++i) {
            brute::Shortcut s;
            s.id = static_cast<int>(pick(5));  // ids may repeat: the rank breaks ties
            const int kind = static_cast<int>(pick(100));
            if (kind < 30) {  // a passage between the Cité d'Astrub [5,-15] and the temple
                const bool in = chance(50);
                s.from = in ? 67372032 : 67112960; s.to = in ? 67112960 : 67372032;
            } else if (kind < 75 && !c.departures.empty()) {  // from near a departure to near the target
                const auto a = Around(world, vertexOf(c.departures[pick(c.departures.size())]), static_cast<int>(pick(4)));
                s.from = world.map[a[pick(a.size())]];
                const auto b = Around(world, vertexOf(c.target), static_cast<int>(pick(5)));
                s.to = world.map[b[pick(b.size())]];
            } else {
                s.from = anyMap(); s.to = chance(10) ? emptyMap(anyMap()) : anyMap();
            }
            s.cost = chance(4) ? 0 : chance(3) ? 65 : 1 + static_cast<int>(pick(6));
            c.shortcuts.push_back(s);
        }
    }
    return c;
}
}
