#include "ui/preset-windows-store.hpp"
#include "core/common.hpp"
#include <atomic>
#include <cstdint>
#include <stdexcept>

namespace presetWindows {
namespace {
using Json = nlohmann::json;
// The file's keys. Never rename them: other clients read the same file.
constexpr const char* PanelKeys[] = {"stats", "notebook", "treasure"};
constexpr unsigned ReplaceAttempts = 15;
unsigned lockWaitMs = 250; // on the game's main thread: short

struct Corrupt : std::runtime_error { using std::runtime_error::runtime_error; };

std::filesystem::path With(const std::filesystem::path& path, const wchar_t* suffix) { auto result = path; result += suffix; return result; }
bool Link(const std::filesystem::path& path) {
    const auto attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT);
}
std::optional<bool> Flag(const Json& object, const char* key) {
    const auto found = object.find(key);
    if (found == object.end() || !found->is_boolean()) return std::nullopt;
    return found->get<bool>();
}
std::optional<int> Number(const Json& object, const char* key) {
    const auto found = object.find(key);
    if (found == object.end() || !found->is_number_integer()) return std::nullopt;
    const auto value = found->get<int64_t>();
    if (value < INT32_MIN || value > INT32_MAX) return std::nullopt; // each value checked by its own rule after

    return static_cast<int>(value);
}
std::optional<Panel> ReadPanel(const Json& windows, const char* key) {
    const auto found = windows.find(key);
    if (found == windows.end() || !found->is_object()) return std::nullopt;
    const auto open = Flag(*found, "open"), small = Flag(*found, "smallHeader");
    if (!open || !small) return std::nullopt;
    Panel panel{*open, std::nullopt, *small};
    const auto x = Number(*found, "x"), y = Number(*found, "y"), width = Number(*found, "width"), height = Number(*found, "height");
    if (x && y && width && height) {
        const Place place{*x, *y, *width, *height};
        if (ValidPlace(place)) panel.place = place;
    }
    return panel;
}
Json WritePanel(const Panel& panel) {
    Json result{{"open", panel.open}, {"smallHeader", panel.smallHeader}};
    if (panel.place && ValidPlace(*panel.place)) {
        result["x"] = panel.place->x; result["y"] = panel.place->y;
        result["width"] = panel.place->width; result["height"] = panel.place->height;
    }
    return result;
}
std::string Key(int uid) { return std::to_string(uid); }
// A name kept to MaxNameBytes, never cut inside a UTF-8 character.
std::string Clip(std::string name) {
    if (name.size() <= MaxNameBytes) return name;
    size_t end = MaxNameBytes;
    while (end > 0 && (static_cast<unsigned char>(name[end]) & 0xC0) == 0x80) --end;
    name.resize(end);
    return name;
}
Json Parse(const std::string& bytes) {
    Json json;
    try { json = Json::parse(bytes); }
    catch (const Json::exception&) { throw Corrupt("layouts file unreadable"); }
    if (!json.is_object()) throw Corrupt("layouts file is not an object");
    const auto schema = json.find("schema");
    if (schema == json.end() || !schema->is_number_integer() || schema->get<int64_t>() < 1) throw Corrupt("layouts file without schema");
    const auto presets = json.find("shared");
    if (presets != json.end() && !presets->is_object()) throw Corrupt("layouts file shared entry invalid");
    return json;
}
// The file as JSON; nullopt when it is not there.
std::optional<Json> Read(const std::filesystem::path& path) {
    if (Link(path)) throw std::runtime_error("layouts file is a link");
    Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!file) {
        const auto error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return std::nullopt;
        throw std::runtime_error("layouts file unreadable");
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file.value, &size)) throw std::runtime_error("layouts file unreadable");
    if (size.QuadPart < 0 || static_cast<uintmax_t>(size.QuadPart) > MaxFileBytes) throw Corrupt("layouts file too large");
    std::string bytes(static_cast<size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    if (!bytes.empty() && (!ReadFile(file.value, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) || read != bytes.size()))
        throw std::runtime_error("layouts file unreadable");
    return Parse(bytes);
}
void Write(const std::filesystem::path& path, const Json& json) {
    const auto bytes = json.dump(2, ' ', false, Json::error_handler_t::replace);
    if (bytes.size() > MaxFileBytes) throw std::runtime_error("layouts file too large");
    static std::atomic_uint64_t serial{0};
    const auto temporary = With(path, (L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(++serial)).c_str());
    struct Cleanup {
        const std::filesystem::path& path;
        ~Cleanup() { std::error_code ignored; std::filesystem::remove(path, ignored); }
    } cleanup{temporary};
    {
        Handle file(CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
        DWORD written = 0;
        if (!file || !WriteFile(file.value, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) ||
            written != bytes.size() || !FlushFileBuffers(file.value))
            throw std::runtime_error("layouts file not written");
    }
    if (Link(path)) throw std::runtime_error("layouts file is a link");
    for (unsigned attempt = 0; ; ++attempt) {
        if (MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) break;
        const auto error = GetLastError();
        if (attempt + 1 >= ReplaceAttempts || (error != ERROR_SHARING_VIOLATION && error != ERROR_LOCK_VIOLATION && error != ERROR_ACCESS_DENIED))
            throw std::runtime_error("layouts file not replaced");
        Sleep(5);
    }
}
// One client at a time reads, changes and writes the file.
class FileLock {
public:
    explicit FileLock(const std::filesystem::path& path) {
        const auto lock = With(path, L".lock");
        if (Link(lock)) throw std::runtime_error("layouts lock is a link");
        const auto deadline = GetTickCount64() + lockWaitMs;
        for (;;) {
            handle.value = CreateFileW(lock.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (handle) return;
            const auto error = GetLastError();
            if (error != ERROR_SHARING_VIOLATION && error != ERROR_LOCK_VIOLATION && error != ERROR_ACCESS_DENIED)
                throw std::runtime_error("layouts lock unavailable");
            if (GetTickCount64() >= deadline) throw std::runtime_error("layouts file in use by another client");
            Sleep(2);
        }
    }
private:
    Handle handle;
};
// Under the lock: the file to change (an unreadable one moved aside, never
// overwritten in place), refused when a later revision wrote it.
template<class Change> auto Update(const std::filesystem::path& path, Change change) {
    FileLock lock(path);
    std::optional<Json> json;
    try { json = Read(path); }
    catch (const Corrupt&) {
        if (!MoveFileExW(path.c_str(), With(path, L".corrupt").c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("unreadable layouts file not moved aside");
    }
    if (!json) json = Json{{"schema", 1}};
    if ((*json)["schema"].get<int64_t>() > 1) throw std::runtime_error("layouts file written by a later revision");
    if (!json->contains("shared")) (*json)["shared"] = Json::object();
    return change(*json);
}
}

bool ValidPlace(const Place& place) {
    return place.x >= 0 && place.x <= PlaceScale && place.y >= 0 && place.y <= PlaceScale &&
        place.width > 0 && place.width <= PlaceScale && place.height > 0 && place.height <= PlaceScale;
}
nlohmann::json ToJson(const Snapshot& snapshot) {
    Json windows = Json::object();
    const std::optional<Panel>* panels[] = {&snapshot.stats, &snapshot.notebook, &snapshot.treasure};
    for (size_t i = 0; i < std::size(PanelKeys); ++i)
        if (*panels[i]) windows[PanelKeys[i]] = WritePanel(**panels[i]);
    if (snapshot.notebookCompact) {
        if (!windows.contains("notebook")) windows["notebook"] = Json::object();
        windows["notebook"]["compact"] = *snapshot.notebookCompact;
    }
    if (snapshot.soul) windows["soul"] = {{"enabled", snapshot.soul->enabled}, {"smallHeader", snapshot.soul->smallHeader}};
    if (const auto& perf = snapshot.perf)
        windows["perf"] = {{"shown", perf->shown}, {"fps", perf->fps}, {"gpu", perf->gpu}, {"disk", perf->disk},
            {"x", perf->x}, {"y", perf->y}, {"scaleX", perf->scaleX}, {"scaleY", perf->scaleY}};
    if (const auto& gauges = snapshot.gauges) {
        Json boxes = Json::object();
        // Only the boxes of their own, keyed by the gauge's fqd+GaugesType bit in decimal.
        for (size_t bit = 0; bit < gauges->boxes.size(); ++bit)
            if (!(gauges->boxes[bit] == Settings::GaugeVerticalBox{}))
                boxes[std::to_string(1 << bit)] = {{"length", gauges->boxes[bit].length}, {"x", gauges->boxes[bit].x}, {"y", gauges->boxes[bit].y}};
        windows["gauges"] = {{"vertical", gauges->vertical}, {"boxes", boxes}};
    }
    return windows;
}
Snapshot FromJson(const nlohmann::json& windows) {
    Snapshot snapshot;
    if (!windows.is_object()) return snapshot;
    snapshot.stats = ReadPanel(windows, "stats");
    snapshot.notebook = ReadPanel(windows, "notebook");
    snapshot.treasure = ReadPanel(windows, "treasure");
    if (const auto notebook = windows.find("notebook"); notebook != windows.end() && notebook->is_object())
        snapshot.notebookCompact = Flag(*notebook, "compact");
    if (const auto soul = windows.find("soul"); soul != windows.end() && soul->is_object()) {
        const auto enabled = Flag(*soul, "enabled"), small = Flag(*soul, "smallHeader");
        if (enabled && small) snapshot.soul = Soul{*enabled, *small};
    }
    if (const auto perf = windows.find("perf"); perf != windows.end() && perf->is_object()) {
        const auto shown = Flag(*perf, "shown"), fps = Flag(*perf, "fps"), gpu = Flag(*perf, "gpu"), disk = Flag(*perf, "disk");
        const auto x = Number(*perf, "x"), y = Number(*perf, "y"), scaleX = Number(*perf, "scaleX"), scaleY = Number(*perf, "scaleY");
        if (shown && fps && gpu && disk && x && y && scaleX && scaleY && Settings::ValidPerfOverlayPlace(*x, *y) &&
            Settings::ValidPerfOverlayScale(*scaleX) && Settings::ValidPerfOverlayScale(*scaleY))
            snapshot.perf = Perf{*shown, *fps, *gpu, *disk, *x, *y, *scaleX, *scaleY};
    }
    if (const auto gauges = windows.find("gauges"); gauges != windows.end() && gauges->is_object()) {
        const auto vertical = Number(*gauges, "vertical");
        const auto boxes = gauges->find("boxes");
        if (vertical && Settings::ValidGaugeVertical(*vertical) && boxes != gauges->end() && boxes->is_object()) {
            Gauges result{*vertical, {}};
            bool valid = true;
            for (auto item = boxes->begin(); item != boxes->end(); ++item) {
                const std::string& key = item.key();
                const Json& value = item.value();
                int type = 0;
                try { size_t used = 0; type = std::stoi(key, &used); if (used != key.size()) valid = false; }
                catch (const std::exception&) { valid = false; }
                if (!valid || type <= 0 || (type & ~Settings::GaugeVerticalBits) || (type & (type - 1)) || !value.is_object()) { valid = false; break; }
                const auto length = Number(value, "length"), x = Number(value, "x"), y = Number(value, "y");
                if (!length || !x || !y) { valid = false; break; }
                const Settings::GaugeVerticalBox box{*length, *x, *y};
                if (!Settings::ValidGaugeVerticalBox(box)) { valid = false; break; }
                int bit = 0;
                while (!(type & (1 << bit))) ++bit;
                result.boxes[static_cast<size_t>(bit)] = box;
            }
            if (valid) snapshot.gauges = result;
        }
    }
    return snapshot;
}
std::filesystem::path DataPath(const std::filesystem::path& settingsPath) {
    return settingsPath.parent_path() / L"DofusMiniatures-layouts.json";
}
std::optional<Snapshot> Load(const std::filesystem::path& path, int uid) {
    if (!ValidUid(uid)) return std::nullopt;
    const auto json = Read(path);
    if (!json) return std::nullopt;
    const auto shared = json->find("shared");
    if (shared == json->end()) return std::nullopt;
    const auto entry = shared->find(Key(uid));
    if (entry == shared->end() || !entry->is_object()) return std::nullopt;
    const auto windows = entry->find("windows");
    if (windows == entry->end() || !windows->is_object()) return std::nullopt;
    return FromJson(*windows);
}
void Put(const std::filesystem::path& path, int uid, const std::string& name, const Snapshot& snapshot) {
    if (!ValidUid(uid)) throw std::runtime_error("invalid layout uid");
    Update(path, [&](Json& json) {
        auto& shared = json["shared"];
        if (!shared.contains(Key(uid)) && shared.size() >= MaxLayouts) throw std::runtime_error("too many layouts kept");
        Json entry = shared.contains(Key(uid)) && shared[Key(uid)].is_object() ? shared[Key(uid)] : Json::object();
        entry["name"] = Clip(name);
        entry["windows"] = ToJson(snapshot);
        shared[Key(uid)] = std::move(entry);
        Write(path, json);
    });
}
bool Erase(const std::filesystem::path& path, int uid) {
    if (!ValidUid(uid)) return false;
    return Update(path, [&](Json& json) {
        if (json["shared"].erase(Key(uid)) == 0) return false;
        Write(path, json);
        return true;
    });
}
bool Rename(const std::filesystem::path& path, int uid, const std::string& name) {
    if (!ValidUid(uid)) return false;
    return Update(path, [&](Json& json) {
        auto& shared = json["shared"];
        if (!shared.contains(Key(uid)) || !shared[Key(uid)].is_object()) return false;
        shared[Key(uid)]["name"] = Clip(name);
        Write(path, json);
        return true;
    });
}
void UseLockWait(unsigned milliseconds) { lockWaitMs = milliseconds; }
}
