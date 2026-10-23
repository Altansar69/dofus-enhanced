// The plain-data route search: unit rules, then the r111 search and a brute
// force on a synthetic world graph with the game's characteristics, and on the
// game's own world graph when its local fixture exists (never committed).
#include "travel/route-graph.hpp"
#include "travel/route-fixture.hpp"
#include <chrono>
#include <filesystem>
#include <cstdio>
#include <iostream>

namespace {
using namespace route_fixture;
using route_graph::Clock;
void Check(bool ok, const std::string& message) { if (!ok) throw std::runtime_error(message); }
template<class F> std::string Throws(F&& f) {
    try { f(); } catch (const std::exception& e) { return e.what(); }
    return {};
}
constexpr auto Never = Clock::time_point::max();
route_graph::Outcome Finish(route_graph::Search& search, const route_graph::Filter& filter) {
    for (int i = 0; i < 1000000; ++i) {
        const auto outcome = search.Run(filter, Never);
        if (outcome.status != route_graph::Status::Pending) return outcome;
    }
    throw std::runtime_error("Search did not finish");
}
const route_graph::Filter Everything = [](uint32_t) { return true; };

// A small graph: uid = map * 10 + zone; Edge(from, to) in call order.
struct Small {
    std::vector<std::pair<int64_t, std::vector<int>>> maps;  // map -> zones
    std::vector<std::tuple<uint64_t, uint64_t, int, bool>> edges;  // from, to, transitions, criteria
    std::map<uint64_t, int> degree;  // forced m_edgeList.Count
    void Vertex(int64_t map, int zone = 1) {
        for (auto& [m, zones] : maps) if (m == map) { zones.push_back(zone); return; }
        maps.push_back({map, {zone}});
    }
    void Edge(uint64_t from, uint64_t to, int transitions = 1, bool criteria = false) { edges.emplace_back(from, to, transitions, criteria); }
    route_graph::Graph Build() const {
        route_graph::Builder builder;
        for (const auto& [map, zones] : maps) {
            builder.Map(map, static_cast<int32_t>(zones.size()));
            for (const auto zone : zones) builder.ZoneVertex(static_cast<uint64_t>(map * 10 + zone), map, zone);
        }
        std::vector<uint64_t> order;
        for (const auto& edge : edges) if (std::find(order.begin(), order.end(), std::get<0>(edge)) == order.end()) order.push_back(std::get<0>(edge));
        for (const auto& [uid, count] : degree) if (std::find(order.begin(), order.end(), uid) == order.end()) order.push_back(uid);
        for (const auto uid : order) {
            const auto forced = degree.find(uid);
            int count = 0;
            for (const auto& edge : edges) count += std::get<0>(edge) == uid;
            builder.EdgeList(uid, forced != degree.end() ? forced->second : count);
            if (forced != degree.end()) continue;
            for (const auto& [from, to, transitions, criteria] : edges)
                if (from == uid) builder.EdgeTo(to, static_cast<int64_t>(to / 10), transitions, criteria);
        }
        return builder.Finish();
    }
};
route_graph::Query Query(std::vector<int64_t> departures, int64_t target, int64_t current = 99, int zone = -1, bool temple = true,
                         std::vector<route_graph::Shortcut> shortcuts = {}) {
    route_graph::Query query;
    query.departures = std::move(departures); query.target = target; query.current = current; query.currentZone = zone;
    query.templeAllowed = temple; query.shortcuts = std::move(shortcuts);
    return query;
}

void UnitTests() {
    route_graph::Search search;
    // Builder: one vertex per uid, whatever copy names it; every copy keeps its map.
    {
        route_graph::Builder builder;
        builder.Map(1, 1); builder.ZoneVertex(11, 1, 1);
        builder.EdgeList(11, 1); builder.EdgeTo(21, 2, 1, false);
        builder.EdgeList(21, 1); builder.EdgeTo(11, 9, 1, false);
        const auto graph = builder.Finish();
        Check(graph.vertices.size() == 2 && graph.edges.size() == 2 && graph.Find(21) == 1 && graph.Zones(1) && !graph.Zones(2) &&
              graph.edges[1].toMap == 9 && graph.zones[0].map == 1, "A vertex only an edge names is still a vertex; only m_vertices gives zones; copies keep their maps");
        route_graph::Builder twice;
        twice.Map(1, 0); twice.Map(1, 0);
        Check(Throws([&] { twice.Finish(); }).find("repeats map=1") != std::string::npos, "A map key is unique");
    }
    // As in r111, a vertex's map is the one of the copy that reached it.
    {
        route_graph::Builder builder;
        builder.Map(1, 1); builder.ZoneVertex(11, 1, 1);
        builder.Map(2, 1); builder.ZoneVertex(21, 2, 1);
        builder.EdgeList(11, 1); builder.EdgeTo(21, 9, 1, false);  // a copy of 21 that claims map 9
        builder.EdgeList(21, 1); builder.EdgeTo(31, 3, 1, false);
        const auto graph = builder.Finish();
        search.Start(graph, Query({1}, 9, 1));
        Check(Finish(search, Everything).steps == 1, "Reached through a copy claiming map 9: map 9");
        search.Start(graph, Query({1}, 2, 1));
        Check(Finish(search, Everything).status == route_graph::Status::Unreachable, "...not map 2");
        search.Start(graph, Query({2}, 2, 2));
        Check(Finish(search, Everything).steps == 0, "Seeded from its copy in m_vertices: map 2");
        search.Start(graph, Query({1}, 3, 1));
        Check(Finish(search, Everything).steps == 2, "The walk goes on from it");
    }
    // The r111 tie-breaks: distance, then departure rank, then the walk from here.
    {
        Small s; s.Vertex(1); s.Vertex(2); s.Vertex(3); s.Vertex(4); s.Vertex(5);
        s.Edge(11, 31); s.Edge(31, 41); s.Edge(41, 51); s.Edge(21, 51);
        const auto graph = s.Build();
        search.Start(graph, Query({1, 2}, 5));
        auto outcome = Finish(search, Everything);
        Check(outcome.status == route_graph::Status::Found && outcome.departure == 1 && outcome.steps == 2, "The actual shorter route wins over the first-ranked departure");
        search.Start(graph, Query({2, 1}, 4));
        outcome = Finish(search, Everything);
        Check(outcome.departure == 1 && outcome.steps == 3, "Distance first");
        search.Start(graph, Query({1, 2}, 4, 1));
        outcome = Finish(search, Everything);
        Check(outcome.departure == 0 && outcome.steps == 2, "The current map starts at 0");
    }
    {
        Small s; s.Vertex(1); s.Vertex(2); s.Vertex(3); s.Edge(11, 31); s.Edge(21, 31);
        const auto graph = s.Build();
        search.Start(graph, Query({2, 1}, 3));
        Check(Finish(search, Everything).departure == 0, "Equal routes: the better-ranked departure");
    }
    // Zones: a split zaap starts at 2, the current map's zones at 0, the player's own zone alone.
    {
        Small s; s.Vertex(5, 1); s.Vertex(5, 2); s.Vertex(6); s.Vertex(3); s.Vertex(7); s.Vertex(8);
        s.Edge(51, 31); s.Edge(61, 71); s.Edge(71, 81); s.Edge(81, 31);
        const auto graph = s.Build();
        search.Start(graph, Query({6, 5}, 3));
        auto outcome = Finish(search, Everything);
        Check(outcome.departure == 1 && outcome.steps == 3 && search.Statistics().splitZaaps == 1 && search.Statistics().starts == 3,
              "A split zaap starts at 2 from every zone");
        search.Start(graph, Query({5, 6}, 3, 5, 2));
        outcome = Finish(search, Everything);
        Check(outcome.departure == 1 && outcome.steps == 4 && search.Statistics().hereStarts == 1, "The player's own zone alone starts at 0");
        search.Start(graph, Query({5, 6}, 3, 5, 9));
        outcome = Finish(search, Everything);
        Check(outcome.departure == 0 && outcome.steps == 1 && search.Statistics().hereStarts == 2, "A zone missing from the graph: every zone starts");
    }
    // Filters: asked only for an improving edge with transitions; a refusal never marks the destination.
    {
        Small s; s.Vertex(1); s.Vertex(2); s.Vertex(3);
        s.Edge(11, 21, 1, true); s.Edge(11, 21, 1, true); s.Edge(11, 21, 0); s.Edge(21, 31);
        const auto graph = s.Build();
        std::vector<uint32_t> asked;
        search.Start(graph, Query({1}, 3, 1));
        auto outcome = Finish(search, [&](uint32_t edge) { asked.push_back(edge); return edge == 1 || edge == 3; });
        Check(outcome.steps == 2 && asked == std::vector<uint32_t>{0, 1, 3}, "A refused first edge does not hide its parallel edge; no transitions, no question");
        asked.clear();
        search.Start(graph, Query({1}, 3, 1));
        Finish(search, [&](uint32_t edge) { asked.push_back(edge); return true; });
        Check(asked == std::vector<uint32_t>{0, 3}, "An equal parallel edge is not asked again");
    }
    // The bounds and malformed elements fail where the managed search failed,
    // and only when the search reaches them.
    {
        Small s; s.Vertex(1); s.Vertex(2); s.degree[11] = 1025;
        const auto graph = s.Build();
        search.Start(graph, Query({1}, 2));
        Check(Throws([&] { Finish(search, Everything); }) == "Route outgoing edges count=1025 limit=1024", "A malformed edge list fails with its count when expanded");
        Small t; t.Vertex(1); t.Vertex(2); t.Vertex(3); t.Edge(11, 21, 200); t.Edge(31, 21, 200);
        const auto other = t.Build();
        search.Start(other, Query({1}, 3));
        Check(Throws([&] { Finish(search, Everything); }) == "Route transitions count=200 limit=128", "Malformed transitions fail when examined");
        search.Start(other, Query({1}, 1, 1));
        Check(Finish(search, Everything).steps == 0, "...and never before");
        route_graph::Builder zones;
        zones.Map(1, 65);
        const auto many = zones.Finish();
        Check(Throws([&] { search.Start(many, Query({1}, 2)); }) == "Route zones count=65 limit=64", "Too many zones fail when seeded");
        route_graph::Builder builder;
        builder.Map(1, 1); builder.ZoneVertex(11, 1, 1);
        builder.Map(3, 1); builder.ZoneVertex(31, 3, 1);
        builder.Map(5, 0); builder.MapCountError("get_Count failed");
        builder.Map(6, 1); builder.MapValuesError("World route vertex mismatch");
        builder.Map(8, 1); builder.ZoneVertex(81, 8, 1);
        builder.EdgeList(11, 1); builder.EdgeTo(21, 2, 1, false);
        builder.EdgeList(31, 2); builder.BadEdge("World route edge mismatch"); builder.EdgeTo(21, 2, 1, false);
        builder.EdgeList(21, 1); builder.EdgeTo(41, 4, 1, false);
        builder.EdgeListError(41, "Field unavailable: m_edgeList");
        builder.EdgeList(81, 1); builder.EdgeTo(21, 2, 1, false); builder.TransitionsError("get_Count of transitions failed");
        const auto broken = builder.Finish();
        search.Start(broken, Query({1}, 2, 1));
        Check(Finish(search, Everything).steps == 1, "Malformed elements away from the route change nothing");
        Check(Throws([&] { search.Start(broken, Query({5, 1}, 2, 1)); }) == "get_Count failed", "An unreadable zone count fails when seeded");
        Check(Throws([&] { search.Start(broken, Query({6, 1}, 2, 1)); }) == "World route vertex mismatch", "A non-vertex zone fails when seeded");
        search.Start(broken, Query({3}, 2, 3));
        Check(Throws([&] { Finish(search, Everything); }) == "World route edge mismatch", "A non-edge fails when examined");
        search.Start(broken, Query({1}, 4, 1));
        Check(Finish(search, Everything).steps == 2, "An unreadable edge list does not matter while its vertex is the target");
        search.Start(broken, Query({1}, 7, 1));
        Check(Throws([&] { Finish(search, Everything); }) == "Field unavailable: m_edgeList", "...and fails when its vertex is expanded");
        search.Start(broken, Query({8}, 2, 8));
        Check(Throws([&] { Finish(search, Everything); }) == "get_Count of transitions failed", "Uncountable transitions fail where they are counted");
        search.Start(broken, Query({8}, 8, 8));
        Check(Finish(search, Everything).steps == 0, "...and never before");
        // A route with a shortcut only skips them: no failure r111 could not have had.
        search.Start(broken, Query({1}, 4, 1, -1, true, {{7, 1, 3, 1}}));
        Check(Finish(search, Everything).steps == 2, "Behind a shortcut, a malformed element is skipped, the walk still found");
        search.Start(broken, Query({1}, 2, 1, -1, true, {{7, 1, 3, 1}}));
        Check(Finish(search, Everything).steps == 1, "...and a shorter walk stays the walk");
    }
    // The Roublard temple option.
    {
        Small s; s.Vertex(82313729); s.Vertex(82314240); s.Vertex(67108864); s.Vertex(188219392);
        s.Edge(823137291, 823142401); s.Edge(823142401, 823137291); s.Edge(823142401, 671088641);
        s.Edge(671088641, 1882193921); s.Edge(671088641, 823142401); s.Edge(1882193921, 671088641);
        const auto graph = s.Build();
        search.Start(graph, Query({82313729}, 188219392, 82313729, -1, true));
        Check(Finish(search, Everything).steps == 3, "Temple on: through the temple");
        search.Start(graph, Query({82313729}, 188219392, 82313729, -1, false));
        Check(Finish(search, Everything).status == route_graph::Status::Unreachable, "Temple off: no way in");
        search.Start(graph, Query({67108864}, 82313729, 67108864, -1, false));
        Check(Finish(search, Everything).steps == 2, "Temple off: walking out through the door stays allowed");
        search.Start(graph, Query({188219392}, 82313729, 188219392, -1, false));
        Check(Finish(search, Everything).status == route_graph::Status::Unreachable, "Temple off: not the statue's return either");
    }
    // Shortcuts: a second layer, at most one per route, taken only when strictly shorter.
    {
        Small s; for (int map = 1; map <= 9; ++map) s.Vertex(map);
        s.Vertex(4, 2);
        s.Edge(11, 21); s.Edge(21, 31); s.Edge(31, 41); s.Edge(41, 51);  // 1 -> 5 in 4
        const auto graph = s.Build();
        search.Start(graph, Query({1}, 5, 1, -1, true, {{7, 1, 4, 3}}));
        auto outcome = Finish(search, Everything);
        Check(outcome.steps == 4 && outcome.shortcut == -1, "An equally long shortcut is not taken");
        search.Start(graph, Query({1}, 5, 1, -1, true, {{7, 1, 4, 2}}));
        outcome = Finish(search, Everything);
        Check(outcome.steps == 3 && outcome.shortcut == 7, "A strictly shorter shortcut is taken, from every zone to every zone");
        search.Start(graph, Query({1}, 5, 1, -1, true, {{7, 1, 3, 1}, {8, 3, 4, 0}, {9, 2, 4, 1}}));
        outcome = Finish(search, Everything);
        Check(outcome.steps == 3 && outcome.shortcut == 7 && search.Statistics().shortcuts == 2, "Equal shortcut routes: the first-ranked shortcut; a free one is ignored");
        search.Start(graph, Query({1}, 5, 1, -1, true, {{6, 1, 5, 65}, {5, 1, 5, 64}}));
        outcome = Finish(search, Everything);
        Check(outcome.steps == 4 && outcome.shortcut == -1 && search.Statistics().shortcuts == 1, "A shortcut costs 64 map changes at most");
        search.Start(graph, Query({1}, 9, 1, -1, true, {{3, 1, 8, 2}, {4, 8, 9, 1}}));
        Check(Finish(search, Everything).status == route_graph::Status::Unreachable, "A route takes at most one shortcut: the trip is planned again after it");
        search.Start(graph, Query({1}, 9, 1, -1, true, {{3, 1, 8, 2}, {4, 8, 9, 1}, {5, 1, 9, 5}}));
        outcome = Finish(search, Everything);
        Check(outcome.steps == 5 && outcome.shortcut == 5, "The id of the route's shortcut");
        Small t; t.Vertex(1); t.Vertex(67108864); t.Vertex(3); t.Edge(11, 31); t.Edge(31, 671088641);
        const auto temple = t.Build();
        search.Start(temple, Query({1}, 67108864, 1, -1, false, {{5, 1, 67108864, 1}}));
        outcome = Finish(search, Everything);
        Check(outcome.status == route_graph::Status::Unreachable && search.Statistics().shortcuts == 0, "Temple off: neither the graph nor a shortcut enters the temple");
        search.Start(temple, Query({1}, 67108864, 1, -1, true, {{5, 1, 67108864, 1}}));
        outcome = Finish(search, Everything);
        Check(outcome.steps == 1 && outcome.shortcut == 5, "Temple on: the shortcut into the temple");
        // Departure rank before shortcut rank, distance before both.
        Small u; for (int map = 1; map <= 6; ++map) u.Vertex(map);
        u.Edge(21, 31); u.Edge(31, 41); u.Edge(41, 51);
        const auto ranks = u.Build();
        search.Start(ranks, Query({1, 2}, 5, 99, -1, true, {{1, 2, 4, 1}, {2, 1, 5, 2}}));
        outcome = Finish(search, Everything);
        Check(outcome.departure == 0 && outcome.steps == 3 && outcome.shortcut == 2, "Equal shortcut routes: the better-ranked departure first");
    }
    // Routes with a shortcut stop below the r111 distance bound; routes without fail at it.
    {
        route_graph::Builder builder;
        builder.Map(1, 1); builder.ZoneVertex(1, 1, 1);
        builder.Map(2, 1); builder.ZoneVertex(2, 2, 1);
        for (uint64_t uid = 2; uid < 4200; ++uid) { builder.EdgeList(uid, 1); builder.EdgeTo(uid + 1, static_cast<int64_t>(uid + 1), 1, false); }
        const auto chain = builder.Finish();
        search.Start(chain, Query({1}, 4200, 1, -1, true, {{1, 1, 2, 64}}));
        Check(Finish(search, Everything).status == route_graph::Status::Unreachable, "A route with a shortcut stops at the distance bound");
        search.Start(chain, Query({1}, 100, 1, -1, true, {{1, 1, 2, 64}}));
        Check(Finish(search, Everything).steps == 64 + 98, "...and reaches what lies below it");
        search.Start(chain, Query({2}, 4200, 2));
        Check(Throws([&] { Finish(search, Everything); }) == "World route search bounds", "A route without one fails at it, as in r111");
    }
    // Slicing never changes a result: one step per call.
    {
        Small s; for (int map = 1; map <= 6; ++map) s.Vertex(map);
        s.Edge(11, 21); s.Edge(21, 31); s.Edge(31, 61); s.Edge(11, 41); s.Edge(41, 51); s.Edge(51, 61);
        const auto graph = s.Build();
        search.Start(graph, Query({1}, 6, 1, -1, true, {{4, 2, 6, 1}}));
        route_graph::Outcome outcome;
        int calls = 0;
        do { outcome = search.Run(Everything, Never, 1); ++calls; } while (outcome.status == route_graph::Status::Pending);
        Check(calls > 5 && outcome.steps == 2 && outcome.shortcut == 4, "A search resumes where its budget stopped it");
        search.Start(graph, Query({1}, 6));
        Check(search.Run(Everything, Clock::now() - std::chrono::seconds(1)).status == route_graph::Status::Pending, "A spent deadline yields at once");
    }
}

route_graph::Graph Build(const World& world, const std::vector<int>& transitions) {
    route_graph::Builder builder;
    builder.Reserve(world.Vertices(), world.edges.size(), world.maps.size() + world.emptyMaps.size());
    for (const auto map : world.maps) {
        const auto& zones = world.zones.at(map);
        builder.Map(map, static_cast<int32_t>(zones.size()));
        for (const auto v : zones) builder.ZoneVertex(v, map, world.zone[v]);
    }
    for (const auto map : world.emptyMaps) builder.Map(map, 0);
    for (uint32_t v = 0; v < world.Vertices(); ++v) {
        if (world.first[v] == world.first[v + 1]) continue;
        builder.EdgeList(v, static_cast<int32_t>(world.first[v + 1] - world.first[v]));
        for (uint32_t e = world.first[v]; e < world.first[v + 1]; ++e) {
            const auto to = world.edges[e].to;
            builder.EdgeTo(to, world.ToMap(world.edges[e]), transitions[e], world.edges[e].criterion >= 0);
        }
    }
    return builder.Finish();
}
struct Timing {
    std::vector<double> samples;
    void Add(Clock::duration time) { samples.push_back(std::chrono::duration<double, std::milli>(time).count()); }
    std::string Text() {
        if (samples.empty()) return "none";
        std::sort(samples.begin(), samples.end());
        double sum = 0; for (const auto s : samples) sum += s;
        char text[160];
        std::snprintf(text, sizeof text, "mean %.3f ms, median %.3f ms, p99 %.3f ms, max %.3f ms over %zu", sum / samples.size(),
                      samples[samples.size() / 2], samples[samples.size() * 99 / 100], samples.back(), samples.size());
        return text;
    }
};

// The synthetic world has what the search depends on (the committed tests run on it alone).
void SyntheticTraits(const World& world, const route_graph::Graph& graph) {
    size_t without = 0, criteria = 0, later = 0, refused = 0, crossing = 0;
    for (size_t e = 0; e < world.edges.size(); ++e) {
        const auto& edge = world.edges[e];
        without += edge.transitions == 0; criteria += edge.criterion >= 0; later += edge.laterCriterion;
        if (world.subarea.at(world.map[edge.from]) != world.subarea.at(world.map[edge.to])) { ++crossing; refused += !AreaAllowed(world, e, 5); }
    }
    bool temple = true;
    for (const int64_t map : {67108864ll, 67109888ll, 67110912ll, 67111936ll, 67112960ll, 67113984ll, 67110147ll, 82314240ll, 188219392ll, 192416776ll, 67372032ll})
        temple = temple && world.zones.count(map);
    const auto* hub = graph.Zones(81789954);
    Check(world.Vertices() >= 4000 && world.split.size() >= 100 && world.zones.at(200020020).size() == 13, "Thousands of vertices, maps split in up to 13 zones");
    Check(without >= 100 && criteria >= 100 && later >= 10, "Edges without transitions, with a criterion, with one on a later transition only");
    Check(crossing >= 1000 && refused >= 50 && refused < crossing, "Moves across subareas, some refused by the area rule");
    Check(temple && world.zones.at(82313729).size() == 2 && world.emptyMaps.size() == 2, "The temple with the game's ids, its split cemetery, map keys without zones");
    Check(hub && graph.vertices[graph.zones[hub->first].vertex].edgeCount == 300, "A 300-edge hub");
}

struct Sizes { int cases, shortcutCases, sanityCases; };
void Suites(const World& world, const Sizes& sizes, uint64_t seed, bool real) {
    std::vector<int> transitions(world.edges.size());
    for (size_t e = 0; e < world.edges.size(); ++e) transitions[e] = world.edges[e].transitions;
    const auto builtAt = Clock::now();
    const auto graph = Build(world, transitions);
    const auto buildTime = Clock::now() - builtAt;
    size_t criteria = 0;
    for (const auto& edge : graph.edges) criteria += edge.criteria;
    if (real) Check(graph.vertices.size() == 11327 && graph.edges.size() == 30909 && graph.maps.size() == 10981 && criteria == 947,
                    "The plain copy of the game's graph holds every vertex, edge and map; 947 edges carry a criterion");
    else SyntheticTraits(world, graph);
    // A variant: 3% more edges without transitions (never taken, never asked).
    std::mt19937_64 random(seed);
    auto sparse = transitions;
    for (auto& t : sparse) if (random() % 100 < 3) t = 0;
    const auto sparseGraph = Build(world, sparse);
    const Reference reference(world), sparseReference(world, &sparse);
    // The character's state: each criterion class allowed or not; the subareas closed to automatic travel.
    const auto wvsOf = [&world](const Case& c) { return [&world, salt = c.salt](size_t e) { return CriteriaAllowed(world, e, salt); }; };
    const auto wvtOf = [&world](const Case& c) { return [&world, salt = c.salt * 7 + 1](size_t e) { return AreaAllowed(world, e, salt); }; };

    route_graph::Search search;
    Timing newTimes, oldTimes;
    size_t found = 0, unreachable = 0, asked = 0, criteriaAsked = 0, cases = 0;
    std::map<int, size_t> steps;
    for (int i = 0; i < sizes.cases; ++i) {
        const bool useSparse = i % 4 == 3;
        const auto& g = useSparse ? sparseGraph : graph;
        const auto& r = useSparse ? sparseReference : reference;
        const auto c = RandomCase(world, random, false);
        const auto wvs = wvsOf(c);
        const auto wvt = wvtOf(c);
        std::vector<size_t> oldAsked;
        const auto oldStart = Clock::now();
        const auto expected = r111::Search(r.graph, c.departures, c.target, c.current, c.currentZone, c.temple, wvs, wvt, &oldAsked);
        oldTimes.Add(Clock::now() - oldStart);
        std::vector<size_t> newAsked;
        const auto filter = [&](uint32_t e) { newAsked.push_back(e); return wvs(e) && wvt(e); };
        const auto newStart = Clock::now();
        search.Start(g, Query(c.departures, c.target, c.current, c.currentZone, c.temple));
        const auto outcome = Finish(search, filter);
        newTimes.Add(Clock::now() - newStart);
        const auto& stats = search.Statistics();
        const std::string where = " (" + world.name + ", case " + std::to_string(i) + ")";
        Check((outcome.status == route_graph::Status::Found) == (expected.status == r111::Status::Found), "Same reachability" + where);
        Check(outcome.departure == expected.source && outcome.steps == expected.steps && outcome.shortcut == -1, "Same departure and steps" + where);
        Check(stats.starts == expected.starts && stats.hereStarts == expected.hereStarts && stats.splitZaaps == expected.splitZaaps, "Same seeds" + where);
        Check(stats.visited == expected.visited && stats.inspected == expected.inspected, "Same vertices and edges examined" + where);
        Check(newAsked == oldAsked, "The game's filters asked for the same edges, in the same order" + where);
        ++cases; asked += newAsked.size();
        for (const auto e : newAsked) criteriaAsked += world.edges[e].criterion >= 0;
        if (outcome.status == route_graph::Status::Found) { ++found; ++steps[std::min(outcome.steps, 40)]; } else ++unreachable;
    }
    // Shortcuts: against the brute force; and a route without one keeps the r111 result.
    size_t shortcutCases = 0, taken = 0, kept = 0;
    Timing shortcutTimes;
    for (int i = 0; i < sizes.shortcutCases; ++i) {
        const auto c = RandomCase(world, random, true);
        const auto wvs = wvsOf(c);
        const auto wvt = wvtOf(c);
        const auto allowed = [&](size_t e) { return wvs(e) && wvt(e); };
        std::vector<route_graph::Shortcut> shortcuts;
        for (const auto& s : c.shortcuts) shortcuts.push_back({s.id, s.from, s.to, s.cost});
        const auto shortcutStart = Clock::now();
        search.Start(graph, Query(c.departures, c.target, c.current, c.currentZone, c.temple, shortcuts));
        const auto outcome = Finish(search, [&](uint32_t e) { return allowed(e); });
        shortcutTimes.Add(Clock::now() - shortcutStart);
        const auto expected = brute::Search(reference.graph, c.departures, c.target, c.current, c.currentZone, c.temple, c.shortcuts, allowed);
        const std::string where = " (" + world.name + ", shortcut case " + std::to_string(i) + ")";
        Check((outcome.status == route_graph::Status::Found) == expected.found, "Same reachability with shortcuts" + where);
        Check(outcome.departure == expected.source && outcome.steps == expected.steps && outcome.shortcut == expected.shortcut, "Same route with shortcuts" + where);
        const auto plain = r111::Search(reference.graph, c.departures, c.target, c.current, c.currentZone, c.temple, wvs, wvt);
        if (outcome.shortcut < 0) {
            Check((plain.status == r111::Status::Found) == (outcome.status == route_graph::Status::Found) &&
                  plain.source == outcome.departure && plain.steps == outcome.steps, "Without a strictly shorter shortcut, the r111 route" + where);
            ++kept;
        } else {
            Check(plain.status == r111::Status::Unreachable || outcome.steps < plain.steps, "A shortcut only when strictly shorter" + where);
            ++taken;
        }
        ++shortcutCases;
    }
    // The brute force agrees with the r111 search without shortcuts.
    for (int i = 0; i < sizes.sanityCases; ++i) {
        const auto c = RandomCase(world, random, false);
        const auto wvs = wvsOf(c);
        const auto wvt = wvtOf(c);
        const auto allowed = [&](size_t e) { return wvs(e) && wvt(e); };
        const auto plain = r111::Search(reference.graph, c.departures, c.target, c.current, c.currentZone, c.temple, allowed, [](size_t) { return true; });
        const auto expected = brute::Search(reference.graph, c.departures, c.target, c.current, c.currentZone, c.temple, {}, allowed);
        Check((plain.status == r111::Status::Found) == expected.found && plain.source == expected.source && plain.steps == expected.steps,
              "The brute force is the r111 search without shortcuts (" + world.name + ", case " + std::to_string(i) + ")");
    }
    std::cout << "[" << world.name << "] " << graph.vertices.size() << " vertices, " << graph.edges.size() << " edges, " << graph.maps.size()
              << " maps, " << criteria << " edges with a criterion; plain copy built in "
              << std::chrono::duration<double, std::milli>(buildTime).count() << " ms\n";
    std::cout << "[" << world.name << "] r111 equivalence: " << cases << " cases (" << found << " found, " << unreachable << " unreachable), filters asked "
              << asked << " times (" << criteriaAsked << " for an edge with a criterion: the only native ens.wvs calls left); "
              << shortcutCases << " shortcut cases (" << taken << " take a shortcut, " << kept << " keep the r111 route)\n";
    std::cout << "[" << world.name << "] search time, new: " << newTimes.Text() << "; r111 on plain data: " << oldTimes.Text()
              << "; with shortcuts: " << shortcutTimes.Text() << "\n";
    std::cout << "[" << world.name << "] steps of the found routes:";
    for (const auto& [s, n] : steps) std::cout << ' ' << s << ':' << n;
    std::cout << '\n';
}
}

int main() {
    try {
        UnitTests();
        // Disagreeing copies of a vertex for the r111 equivalence; agreeing ones for
        // the brute force, which needs them.
        const auto copies = Synthetic(20261023, true);
        size_t disagreeing = 0;
        for (const auto& edge : copies.edges) disagreeing += edge.toMap != 0 || edge.toZone >= 0;
        Check(disagreeing >= 50, "Copies of a vertex that disagree on their map or zone");
        Suites(copies, {3000, 0, 0}, 1123, false);
        Suites(Synthetic(20261024, false), {1000, 1200, 200}, 1124, false);
        // The game's own graph: a local fixture only (derived from a game asset,
        // never committed), configured by CMake when it exists.
#ifdef ROUTE_GRAPH_FIXTURE
        const bool real = std::filesystem::exists(ROUTE_GRAPH_FIXTURE);
        if (real) Suites(LoadWorld(ROUTE_GRAPH_FIXTURE), {4000, 1500, 300}, 20261023, true);
        std::cout << (real ? "Real-graph suites: ran (" : "Real-graph suites: skipped, the local fixture is missing (") << ROUTE_GRAPH_FIXTURE << ")\n";
#else
        std::cout << "Real-graph suites: skipped, no local fixture (work/r112-analysis/world-graph-fixture.txt)\n";
#endif
        std::cout << "PASS: plain route graph and search: r111 rules and bounds, temple option, shortcuts, slicing, "
                     "and the r111 search and a brute force on the synthetic world graph\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
