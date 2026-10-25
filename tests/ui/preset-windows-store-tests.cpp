#include "ui/preset-windows-store.hpp"
#include "core/common.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

// The file the mod keeps beside the game's saved layouts, in a temporary
// folder of its own: no game, no settings or layouts of the player.
using namespace presetWindows;
namespace {
void Check(bool ok, const std::string& message) { if (!ok) throw std::runtime_error(message); }
std::string Text(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}
void WriteText(const std::filesystem::path& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
}
struct Folder {
    std::filesystem::path path;
    Folder() {
        path = std::filesystem::temp_directory_path() / ("preset-windows-store-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
        std::filesystem::create_directories(path);
    }
    ~Folder() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
    std::filesystem::path File() const { return DataPath(path / L"DofusMiniatures-3.7.11.11-settings.ini"); }
};
// Every kind of window, away from its default.
Snapshot Full() {
    Snapshot snapshot;
    snapshot.stats = Panel{true, Place{1200, 800, 4500, 6000}, true};
    snapshot.notebook = Panel{false, std::nullopt, false};
    snapshot.notebookCompact = true;
    snapshot.treasure = Panel{true, std::nullopt, false};
    snapshot.soul = Soul{false, true};
    snapshot.perf = Perf{false, true, true, false, 9000, 50, 150, 80};
    Gauges gauges;
    gauges.vertical = 0x1 | 0x10 | 0x2000000;
    gauges.boxes[0] = {884, 7418, 1815};
    gauges.boxes[4] = {0, 120, 9000};
    gauges.boxes[25] = {300, -1, -1};
    snapshot.gauges = gauges;
    return snapshot;
}

void FileBesideTheSettings() {
    Check(DataPath(L"C:\\Mods\\DofusMiniatures-3.7.11.11-settings.ini") == std::filesystem::path(L"C:\\Mods\\DofusMiniatures-layouts.json"),
        "The file sits beside the shared settings");
}
void RoundTrip() {
    Folder folder;
    const auto file = folder.File();
    Check(!Load(file, 3), "Nothing kept before the first save");
    Put(file, 3, "pc travail combat", Full());
    const auto loaded = Load(file, 3);
    Check(loaded && *loaded == Full(), "Every window comes back as saved");
    Check(!Load(file, 2), "Another layout keeps nothing");
    const auto text = Text(file);
    Check(text.find("\"schema\": 1") != std::string::npos && text.find("\"shared\"") != std::string::npos &&
        text.find("\"3\"") != std::string::npos && text.find("\"name\": \"pc travail combat\"") != std::string::npos,
        "Keyed by the layout's uid under the game's shared scope, with its name");
    Check(text.find("\"33554432\"") != std::string::npos && text.find("\"16\"") != std::string::npos && text.find("\"2\":") == std::string::npos,
        "Only the gauges' own boxes, by their type");
    // Overwritten (SavePreset over the same layout): the latest only.
    auto other = Full();
    other.stats->open = false; other.stats->place.reset();
    Put(file, 3, "pc travail combat", other);
    Check(*Load(file, 3) == other, "A layout saved again keeps its latest windows");
}
void PartsLeftOut() {
    // A part missing (an older or later revision) or invalid leaves that window as it is.
    Snapshot partial;
    partial.soul = Soul{true, false};
    Check(FromJson(ToJson(partial)) == partial, "A layout with one part keeps only that part");
    auto json = ToJson(Full());
    json["stats"]["open"] = 1;                       // not a boolean
    json["treasure"]["x"] = 20000;                   // a place out of the screen: the panel without its place
    json["treasure"]["y"] = 0; json["treasure"]["width"] = 100; json["treasure"]["height"] = 100;
    json["perf"]["scaleX"] = 400;                    // beyond 300 %
    json["gauges"]["boxes"]["4"] = {{"length", 10}, {"x", -1}, {"y", -1}}; // no gauge has type 4
    json["later"] = {{"anything", true}};
    const auto read = FromJson(json);
    Check(!read.stats && read.treasure && !read.treasure->place && read.treasure->open, "An invalid panel is left out, an invalid place alone too");
    Check(!read.perf && !read.gauges && read.soul && read.notebook && read.notebookCompact == true, "Each invalid part alone is left out");
    Check(FromJson(nlohmann::json::array()) == Snapshot{}, "Not an object: nothing");
    Check(!ValidPlace({0, 0, 0, 10}) && !ValidPlace({-1, 0, 10, 10}) && ValidPlace({0, 0, 10000, 10000}) && !ValidPlace({0, 0, 10001, 10}),
        "A place inside the screen, with a size");
}
void EraseAndRename() {
    Folder folder;
    const auto file = folder.File();
    Check(!Erase(file, 1) && !Rename(file, 1, "x"), "Nothing to erase or rename in a missing file");
    Check(!std::filesystem::exists(file), "Erasing nothing writes no file");
    Put(file, 0, "Classico 1", Full());
    Put(file, 1, "Combat 1", Snapshot{});
    Check(Rename(file, 1, "Combat renommé") && Text(file).find("Combat renommé") != std::string::npos && Load(file, 1) == Snapshot{},
        "A renamed layout keeps its windows");
    Check(Erase(file, 1) && !Load(file, 1) && Load(file, 0) && !Erase(file, 1), "A deleted layout loses its entry, the others stay");
    Check(!ValidUid(-1) && !Load(file, -1) && !Erase(file, -1), "A layout without uid (a copy of the game's) is never kept");
    // A name kept short, never cut inside a character.
    std::string longName;
    for (int i = 0; i < 200; ++i) longName += "é";
    Put(file, 2, longName, Snapshot{});
    const auto json = nlohmann::json::parse(Text(file));
    const auto kept = json["shared"]["2"]["name"].get<std::string>();
    Check(kept.size() <= MaxNameBytes && kept.size() % 2 == 0, "The name cut between characters");
}
void OtherFilesKept() {
    Folder folder;
    const auto file = folder.File();
    // Fields of a later revision of the same schema are written back.
    WriteText(file, R"({"schema":1,"future":42,"shared":{"5":{"name":"A","windows":{"soul":{"enabled":true,"smallHeader":false}},"extra":"kept"}}})");
    Put(file, 6, "B", Full());
    auto json = nlohmann::json::parse(Text(file));
    Check(json["future"] == 42 && json["shared"]["5"]["extra"] == "kept" && Load(file, 5)->soul, "Another client's entries and fields stay");
    Put(file, 5, "A2", Full());
    json = nlohmann::json::parse(Text(file));
    Check(json["shared"]["5"]["extra"] == "kept" && json["shared"]["5"]["name"] == "A2", "An entry saved again keeps its unknown fields");
    // A later schema: read, never rewritten.
    WriteText(file, R"({"schema":2,"shared":{"5":{"windows":{"soul":{"enabled":false,"smallHeader":true}}}}})");
    Check(Load(file, 5) && !Load(file, 5)->soul->enabled, "A later revision's file is read");
    bool refused = false;
    try { Put(file, 5, "C", Full()); } catch (const std::exception&) { refused = true; }
    Check(refused && Text(file).find("\"schema\":2") != std::string::npos, "and never rewritten");
    // An unreadable file: Load says so; a save moves it aside and starts again.
    WriteText(file, "{ not json");
    bool threw = false;
    try { (void)Load(file, 5); } catch (const std::exception&) { threw = true; }
    Check(threw, "An unreadable file is reported");
    Put(file, 7, "D", Full());
    Check(Load(file, 7) == Full() && Text(std::filesystem::path(file).concat(L".corrupt")) == "{ not json", "The unreadable file is kept aside");
}
void LockedByAnotherClient() {
    Folder folder;
    const auto file = folder.File();
    UseLockWait(50);
    auto lock = file; lock += L".lock";
    {
        Handle held(CreateFileW(lock.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
        Check(static_cast<bool>(held), "Test lock");
        bool busy = false;
        try { Put(file, 1, "x", Full()); } catch (const std::exception&) { busy = true; }
        Check(busy && !std::filesystem::exists(file), "Another client's lock: nothing written, the caller told");
    }
    Put(file, 1, "x", Full());
    Check(Load(file, 1) == Full(), "Written once the lock is free");
    // Two writers at once: both entries kept.
    UseLockWait(5000);
    std::thread a([&] { for (int i = 0; i < 20; ++i) Put(file, 10, "a", Full()); });
    std::thread b([&] { for (int i = 0; i < 20; ++i) Put(file, 11, "b", Snapshot{}); });
    a.join(); b.join();
    Check(Load(file, 10) == Full() && Load(file, 11) == Snapshot{} && Load(file, 1), "Writers under the lock never lose each other's entries");
    UseLockWait(250);
}
}

int main() {
    const struct { const char* name; void (*run)(); } tests[] = {
        {"FileBesideTheSettings", FileBesideTheSettings}, {"RoundTrip", RoundTrip}, {"PartsLeftOut", PartsLeftOut},
        {"EraseAndRename", EraseAndRename}, {"OtherFilesKept", OtherFilesKept}, {"LockedByAnotherClient", LockedByAnotherClient}};
    int failed = 0;
    for (const auto& test : tests) {
        try { test.run(); std::cout << "PASS " << test.name << '\n'; }
        catch (const std::exception& error) { ++failed; std::cout << "FAIL " << test.name << ": " << error.what() << '\n'; }
    }
    std::cout << (failed ? "PRESET_WINDOWS_STORE_TESTS_FAILED" : "PRESET_WINDOWS_STORE_TESTS_PASSED") << '\n';
    return failed ? 1 : 0;
}
