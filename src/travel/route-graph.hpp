#pragma once
// The world route graph as plain data, and the search over it (no game access).
//
// chat_route copies the game's graph (Core.PathFinding.WorldPathfinding.
// PathFindingData) here once per graph object; the search then runs in plain
// C++ and asks the game's own filters only for the edges they can refuse.
// The search is the r111 one, unchanged: a multi-source Dijkstra over map
// changes (a teleport adds its start distance), ties broken by the distance,
// then the departure's rank, then the vertex uid. As in r111, a vertex is its
// uid and its map is read from the copy that reached it, and a malformed
// element fails only when the search reaches it, where r111 failed. Shortcuts
// (ways the mod takes itself, outside the game's graph) add a second layer of
// states: a route takes at most one, and only when strictly shorter.
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

namespace route_graph {
constexpr uint32_t NoVertex = std::numeric_limits<uint32_t>::max();
// The r111 bounds, with their messages.
constexpr int ZonesLimit = 64, EdgesLimit = 1024, TransitionsLimit = 128, DistanceLimit = 4096;
constexpr size_t VisitedLimit = 65536, InspectedLimit = 262144;
// A shortcut costs at least one map change and at most this many.
constexpr int ShortcutCostLimit = 64;

struct Vertex {
    uint64_t uid = 0;
    int64_t map = 0;      // the map of the first copy read (diagnostics: the search uses each copy's own)
    uint32_t firstEdge = 0, edgeCount = 0;
    int32_t degree = 0;   // m_edgeList.Count: outside [0, EdgesLimit], it fails when expanded
    uint32_t error = 0;   // a message thrown when expanded (an unreadable edge list), 0: none
};
// Where a malformed edge fails, as in r111: when examined, or when its
// transitions are counted (once it would improve its destination).
enum class EdgeState : uint8_t { Ok, Examined, Transitions };
struct Edge {
    uint32_t to = NoVertex;
    int64_t toMap = 0;        // the map of this edge's own copy of its destination
    int32_t transitions = 0;  // 0 is never taken, beyond TransitionsLimit is malformed
    bool criteria = false;    // the first transition's criterion is not provably empty: ens.wvs decides
    EdgeState state = EdgeState::Ok;
    uint32_t error = 0;       // the message of a malformed edge
};
// A zone of a map, as m_vertices holds its copy of the vertex.
struct Zone { uint32_t vertex = 0; int32_t zone = 0; int64_t map = 0; };
struct MapZones {
    int64_t map = 0;
    uint32_t first = 0, count = 0;  // range in Graph::zones, in the game's enumeration order
    int32_t declared = 0;           // the zone dictionary's Count, checked when seeded
    uint32_t countError = 0;        // its Count could not be read: thrown when seeded
    uint32_t valuesError = 0;       // a zone that is not a vertex: thrown when seeded with zones
};
struct Graph {
    std::vector<Vertex> vertices;
    std::vector<Edge> edges;  // edges of a vertex: [firstEdge, firstEdge + edgeCount)
    std::vector<Zone> zones;
    std::vector<MapZones> maps;                      // sorted by map
    std::vector<std::string> errors{std::string()};  // messages; 0 is none
    std::unordered_map<uint64_t, uint32_t> byUid;
    const MapZones* Zones(int64_t map) const;
    uint32_t Find(uint64_t uid) const;  // NoVertex when absent
};

// Fills a Graph in the order the game's dictionaries enumerate.
class Builder {
public:
    void Reserve(size_t vertices, size_t edges, size_t maps);
    // m_vertices: one Map per key, then its zones, or the error its zones gave.
    void Map(int64_t map, int32_t declared);
    void MapCountError(const std::string& message);
    void MapValuesError(const std::string& message);
    void ZoneVertex(uint64_t uid, int64_t map, int32_t zone);
    // m_outgoingEdges: one EdgeList per key, then its edges (none when the
    // count is outside [0, EdgesLimit]), or the error that list gave.
    void EdgeList(uint64_t uid, int32_t count);
    void EdgeListError(uint64_t uid, const std::string& message);
    void EdgeTo(uint64_t uid, int64_t map, int32_t transitions, bool criteria);
    void TransitionsError(const std::string& message);  // the last edge's transitions could not be counted
    void BadEdge(const std::string& message);           // an edge that fails when examined
    size_t EdgeCount() const { return graph.edges.size(); }
    Graph Finish();
private:
    uint32_t Intern(uint64_t uid, int64_t map);
    uint32_t Message(const std::string& message);
    struct List { uint64_t uid; uint32_t first, count; int32_t degree; uint32_t error; };
    Graph graph;
    std::vector<List> lists;
    std::unordered_map<std::string, uint32_t> messages;
    bool inMap = false, inList = false;
};

// With « Passer par le temple Roublard » off: entering the temple (its cemetery
// door, the statue's return from the Domaine des dieux) and leaving it by the
// statue's teleports. Walking inside it and out through the cemetery door stays
// allowed, so a character already inside can still leave.
bool TempleShortcut(int64_t from, int64_t to);

// A way between two maps outside the game's graph, from every zone of map
// `from` to every zone of map `to`, costing `cost` map changes.
struct Shortcut { int id = 0; int64_t from = 0, to = 0; int cost = 1; };
struct Query {
    std::vector<int64_t> departures;  // best first: the rank breaks ties
    int64_t target = 0, current = 0;
    int currentZone = -1;  // the player's zone on `current`, -1 when unknown
    bool templeAllowed = true;
    std::vector<Shortcut> shortcuts;
};
enum class Status { Pending, Found, Unreachable };
struct Outcome { Status status = Status::Pending; size_t departure = 0; int steps = 0; int shortcut = -1; };
struct Stats {
    size_t starts = 0, hereStarts = 0, splitZaaps = 0, shortcuts = 0;
    size_t visited = 0, inspected = 0, filtered = 0;
};
// The game's own filters for one edge (ens.wvs, then ens.wvt): asked only for
// an edge that improves its destination, when its transitions allow it.
using Filter = std::function<bool(uint32_t edge)>;
using Clock = std::chrono::steady_clock;

class Search {
public:
    // Seeds the departures; throws on a malformed seed, as the r111 search did.
    void Start(const Graph& graph, Query query);
    // Runs until the result or the budget: the deadline, or `steps` pops and
    // relaxations (for tests). Resumes where the previous call stopped. Routes
    // without a shortcut fail on malformed elements and bounds as in r111;
    // routes with one only skip them.
    Outcome Run(const Filter& allowed, Clock::time_point deadline, size_t steps = std::numeric_limits<size_t>::max());
    const Stats& Statistics() const { return stats; }
    bool Started() const { return graph != nullptr; }
    void Clear();
private:
    struct Node { int distance; uint8_t layer; uint32_t source; int32_t first; uint64_t uid; uint32_t vertex; int64_t map; };
    struct Later { bool operator()(const Node& a, const Node& b) const; };
    static size_t State(uint32_t vertex, uint8_t layer) { return static_cast<size_t>(vertex) * 2 + layer; }
    bool Improves(size_t state, int distance, uint32_t source, int32_t first) const;
    void Enqueue(uint32_t vertex, uint8_t layer, int distance, uint32_t source, int32_t first, int64_t map);
    [[noreturn]] void Fail(uint32_t error) const;
    const Graph* graph = nullptr;
    Query query;
    std::vector<int> distance;
    std::vector<uint32_t> source;
    std::vector<int32_t> first;
    std::vector<uint8_t> visited;
    std::priority_queue<Node, std::vector<Node>, Later> queue;
    std::vector<uint32_t> usable;  // shortcuts the temple option and their bounds allow
    bool expanding = false;
    Node current{};
    uint32_t nextEdge = 0, endEdge = 0;
    size_t nextShortcut = 0;
    size_t plainVisited = 0, plainInspected = 0;  // the r111 bounds count routes without a shortcut
    Stats stats;
};
}
