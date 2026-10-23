#pragma once
#include <atomic>
#include <string>
#include <vector>

namespace treasure {
struct Query {
    int x = 0, y = 0, direction = 0;
    bool operator==(const Query&) const = default;
};
struct Clue {
    int id = 0, x = 0, y = 0, distance = 0;
    std::string name;
};
struct Page {
    int total = 0, skip = 0, count = 0;
    std::vector<Clue> clues;
};
bool Coordinate(const std::string&, int&);
bool Valid(const Query&);
std::wstring RequestPath(const Query&, int skip);
Page ParsePage(const std::string&, const Query&, int skip);
std::vector<Clue> Nearest(std::vector<Clue>);
std::string SearchKey(const std::string&);
std::vector<Clue> Filter(const std::vector<Clue>&, const std::string&);
// Exact point-of-interest match for the step read from the game's hunt.
std::vector<Clue> FilterById(const std::vector<Clue>&, int id);
// HTTPS only; no account credentials, cookies, game objects or client state.
std::vector<Clue> Fetch(const Query&, const std::atomic_bool& cancelled);
}
