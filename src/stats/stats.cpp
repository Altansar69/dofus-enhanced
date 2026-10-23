#include "stats/stats.hpp"
#include "stats/stats-store.hpp"
#include "core/module.hpp"
#include <ctime>
#include <unordered_map>

// The store of the statistics in the module: counting only while
// settings.statsEnabled is on and the module runs, a write every 15 seconds
// at most and at once when counting stops (a logout, a reconnection, the
// character selection, the box unticked; at a change of character, at Stop and
// for a reset too), and the
// game's data the options show (the played character, the spells', dungeons'
// and worlds' names).
namespace stats {
namespace {
using namespace managed;
constexpr const char* DataCenter = "Ankama.Dofus.Core.DataCenter.dll";
constexpr const char* WorldSpace = "Core.DataCenter.Metadata.World";
// A write every 15 s at most from the pump: closing the game loses at most
// that much. A failed one is tried again 10 s later.
constexpr ULONGLONG WriteMs = 15000, RetryMs = 10000;
// The character is read once a second while counting, or while the options
// show the statistics (WantedMs after the last look). Meanwhile the file is
// read again every RefreshMs when another client wrote it: its latest counts
// for the options, and its resets applied at once to the unwritten counts.
constexpr ULONGLONG ReadMs = 1000, WantedMs = 3000, RefreshMs = 5000, LogMs = 60000;
// How long the main thread waits for another client's lock: a pump, Stop, a reset.
constexpr unsigned TickLockMs = 50, StopLockMs = 1000, ClearLockMs = 300;
constexpr size_t MaxNames = 1024, MaxGameNameBytes = 128;

bool Is(Obj object, Klass type) { return object && type && api.class_is_assignable_from(type, api.object_get_class(object)); }
void RequireField(Klass type, const char* name, size_t offset) {
    const auto field = api.class_get_field_from_name(type, name);
    if (!field || api.field_get_offset(field) != offset) throw std::runtime_error(std::string("Statistics field mismatch: ") + name);
}
bool Printable(const std::string& text) {
    if (text.empty() || text.size() > MaxGameNameBytes) return false;
    for (const char c : text) if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) return false;
    return true;
}

// The game's side: the played character (esg, the fng service) and the
// names of the spells, dungeons and worlds in its data. Each part disables
// only itself when its contract does not match this client.
class Native final : public Game {
public:
    void Install() {
        characterReady = spellsReady = dungeonsReady = worldsReady = false;
        names.clear();
        try {
            character = api.Class("Core.dll", "", "esg");
            // esg.dpzj (z): the character's base information; zcx reads its id there.
            RequireField(character, "dpzj", 0x128);
            info = api.Class("Core.dll", "", "z");
            RequireField(info, "dbrm", 0x18);
            localId = api.FindVerified(character, "zcx", 0, 0x18BB140);
            characterReady = true;
        } catch (const std::exception& error) { Log(std::string("STATS_CHARACTER_DISABLED ") + error.what()); }
        try {
            spellData = api.Class(DataCenter, "Core.DataCenter.Metadata.Spell", "SpellData");
            spellById = api.FindVerified(spellData, "GetById", 1, 0x7CCE20);
            spellName = api.FindVerified(spellData, "get_name", 0, 0x7CD300);
            spellsReady = true;
        } catch (const std::exception& error) { Log(std::string("STATS_SPELL_NAMES_DISABLED ") + error.what()); }
        try {
            auto dataCenter = api.Class(DataCenter, "Core.DataCenter", "DataCenterModule");
            dungeonsRoot = api.FindVerified(dataCenter, "get_dungeonsDataRoot", 0, 0x7BC7C0);
            auto dungeonRoot = api.Class(DataCenter, WorldSpace, "DungeonsDataRoot");
            // DungeonsDataRoot has no lookup by id of its own: MetadataRoot`1's, fully shared.
            const auto base = api.class_get_parent(dungeonRoot);
            if (!base || std::string(api.class_get_name(base)) != "MetadataRoot`1") throw std::runtime_error("Dungeon data root base changed");
            dungeonById = api.FindVerified(api.class_get_parent(dungeonRoot), "GetObjectById", 1, 0x3874B80);
            dungeonData = api.Class(DataCenter, WorldSpace, "DungeonData");
            dungeonName = api.FindVerified(dungeonData, "get_name", 0, 0x7C24C0);
            dungeonsReady = true;
        } catch (const std::exception& error) { Log(std::string("STATS_DUNGEON_NAMES_DISABLED ") + error.what()); }
        try {
            worldData = api.Class(DataCenter, WorldSpace, "WorldMapData");
            worldById = api.FindVerified(worldData, "GetWorldMapById", 1, 0x7D3D50);
            worldName = api.FindVerified(worldData, "get_name", 0, 0x7D3DE0);
            worldsReady = true;
        } catch (const std::exception& error) { Log(std::string("STATS_WORLD_NAMES_DISABLED ") + error.what()); }
    }
    int64_t Character(std::string& name) override {
        if (!characterReady || !module) return 0;
        Ref service(module->FindService("fng"));
        if (!Is(service.get(), character)) return 0;
        // zcx dereferences esg.dpzj: no character while it is null.
        Ref base(api.Field(service.get(), "dpzj"));
        if (!base.get()) return 0;
        const auto id = api.Value<int64_t>(api.Invoke(localId, service.get()));
        if (Is(base.get(), info)) {
            auto text = api.String(api.Field(base.get(), "dbrm"));
            if (Printable(text)) name = std::move(text);
        }
        return id > 0 ? id : 0;
    }
    std::string KeyName(Table table, int key) override {
        const auto id = (static_cast<uint64_t>(table) << 32) | static_cast<uint32_t>(key);
        if (const auto known = names.find(id); known != names.end()) return known->second;
        std::string name;
        Ref data;
        Method getter = nullptr;
        if (table == Table::Spells && spellsReady) {
            int64_t spell = key;
            data.reset(api.Invoke(spellById, nullptr, {&spell}));
            if (!Is(data.get(), spellData)) data.reset();
            getter = spellName;
        } else if (table == Table::Dungeons && dungeonsReady) {
            Ref root(api.Invoke(dungeonsRoot, nullptr));
            int64_t dungeon = key;
            data.reset(root.get() ? api.Invoke(dungeonById, root.get(), {&dungeon}) : nullptr);
            if (!Is(data.get(), dungeonData)) data.reset();
            getter = dungeonName;
        } else if (table == Table::Dimensions && worldsReady) {
            int world = key;
            data.reset(api.Invoke(worldById, nullptr, {&world}));
            if (!Is(data.get(), worldData)) data.reset();
            getter = worldName;
        }
        if (data.get()) {
            auto text = api.String(api.Invoke(getter, data.get()));
            if (Printable(text)) name = std::move(text);
        }
        // Unknown keys are kept too: never looked up again at every refresh.
        if (names.size() >= MaxNames) names.clear();
        names.emplace(id, name);
        return name;
    }
private:
    Klass character{}, info{}, spellData{}, dungeonData{}, worldData{};
    Method localId{}, spellById{}, spellName{}, dungeonsRoot{}, dungeonById{}, dungeonName{}, worldById{}, worldName{};
    bool characterReady = false, spellsReady = false, dungeonsReady = false, worldsReady = false;
    std::unordered_map<uint64_t, std::string> names;
};

Native native;
Game* game = &native;
Store store;
struct State {
    bool installed = false, finished = false, changed = false;
    int64_t character = 0;  // the played character, the last time it was read
    std::string name;
    ULONGLONG nextRead = 0, nextWrite = 0, nextTry = 0, wantedUntil = 0, nextRefresh = 0, nextLog = 0;
} state;

int64_t Now() { return static_cast<int64_t>(_time64(nullptr)); }
const char* Describe(Store::Result result) {
    switch (result) {
    case Store::Result::Written: return "written";
    case Store::Result::Nothing: return "nothing";
    case Store::Result::Busy: return "busy";
    case Store::Result::Newer: return "newer";
    default: return "failed";
    }
}
// At most a line a minute: a file in use by another client is retried quietly.
void Note(const std::string& line, ULONGLONG now) {
    try {
        if (now < state.nextLog) return;
        state.nextLog = now + LogMs;
        Log(line);
    } catch (...) {}
}
void ReadCharacter(ULONGLONG now) {
    state.nextRead = now + ReadMs;
    std::string name;
    int64_t id = 0;
    try { id = game ? game->Character(name) : 0; }
    catch (...) { id = 0; Note("STATS_CHARACTER_ERROR", now); }
    if (id <= 0) return;  // loading or between characters: the last one stays shown
    if (id != state.character) {
        // Another character in this client: its predecessor's counts are written at once.
        if (state.character > 0) state.changed = true;
        state.character = id;
        state.name.clear();
    }
    if (name.empty()) return;
    state.name = name;
    // Cheap when the file already has it: only a new or changed name is written.
    if (Counting()) store.Name(id, name);
}
void Write(ULONGLONG now, unsigned lockWaitMs) {
    const auto result = store.Flush(lockWaitMs);
    if (result == Store::Result::Written || result == Store::Result::Nothing) {
        state.nextWrite = now + WriteMs;
        state.nextTry = 0;
        state.changed = false;
        return;
    }
    state.nextTry = now + RetryMs;
    Note(std::string("STATS_WRITE_DEFERRED ") + Describe(result), now);
}
}

void UseGame(Game* replacement) { game = replacement ? replacement : &native; }
Store& ModuleStore() { return store; }

// Off while logged out, reconnecting or choosing a character (the account
// options unbound): nothing of a character in between is counted. On until
// FlushStats: Module::Stop sets stopped, then restores the trackers, which give
// their last counts (their hooks already stop at stopped), then flushes them.
bool Counting() {
    return state.installed && !state.finished && module && module->ready.load() && !module->accountOptionsUnbound &&
        module->settings.statsEnabled;
}
void Add(int64_t character, Counter counter, int64_t amount) {
    try { if (Counting()) store.Add(character, counter, amount, Now()); } catch (...) {}
}
void Raise(int64_t character, Record record, int64_t value) {
    try { if (Counting()) store.Raise(character, record, value, Now()); } catch (...) {}
}
void AddTo(int64_t character, Table table, int key, int64_t amount) {
    try { if (Counting()) store.AddTo(character, table, key, amount, Now()); } catch (...) {}
}
void Name(int64_t character, const std::string& name) {
    try { if (Counting()) store.Name(character, name); } catch (...) {}
}
int64_t Current() { return state.character; }
Totals Shown(bool everyCharacter) {
    try {
        const auto now = GetTickCount64();
        state.wantedUntil = now + WantedMs;
        if (!state.installed) return {};
        if (now >= state.nextRead) ReadCharacter(now);
        if (now >= state.nextRefresh) { state.nextRefresh = now + RefreshMs; store.Refresh(); }
        if (everyCharacter) return store.Show(0);
        if (state.character <= 0) return {};
        auto totals = store.Show(state.character);
        if (!state.name.empty()) totals.name = state.name;
        return totals;
    } catch (...) { return {}; }
}
bool Clear(bool everyCharacter) {
    try {
        if (!state.installed || state.finished || (!everyCharacter && state.character <= 0)) return false;
        const auto result = store.Clear(everyCharacter ? 0 : state.character, ClearLockMs, Now());
        Log(std::string("STATS_RESET scope=") + (everyCharacter ? "all" : "character") + " result=" + Describe(result));
        return result == Store::Result::Written;
    } catch (...) { return false; }
}
std::string KeyName(Table table, int key) {
    if (table == Table::Elements) {
        static constexpr const char* Elements[] = {"neutre", "terre", "feu", "eau", "air"};
        return key >= 0 && key < static_cast<int>(std::size(Elements)) ? Elements[key] : std::string{};
    }
    try { return game ? game->KeyName(table, key) : std::string{}; } catch (...) { return {}; }
}
}

void InstallStats() {
    using namespace stats;
    try {
        if (!module) return;
        state = {};
        store.Open(DataPath(module->settingsPath));
        native.Install();
        state.installed = true;
        state.nextWrite = GetTickCount64() + WriteMs;
        Log(std::string("STATS_READY counting=") + (module->settings.statsEnabled ? "1" : "0") + (store.Frozen() ? " frozen=newer" : ""));
    } catch (...) {
        try { Log("STATS_DISABLED"); } catch (...) {}
    }
}
void TickStats() {
    using namespace stats;
    try {
        if (!state.installed || state.finished) return;
        const auto now = GetTickCount64();
        if ((Counting() || now < state.wantedUntil) && now >= state.nextRead) ReadCharacter(now);
        // Another client's reset: only the counts made before it are dropped.
        if (Counting() && now >= state.nextRefresh) { state.nextRefresh = now + RefreshMs; store.Refresh(); }
        if (!store.Pending()) { state.changed = false; return; }
        // Counting stopped (logout, reconnection, character selection, box
        // unticked): what was counted is written at once.
        if (now < state.nextTry || (!state.changed && Counting() && now < state.nextWrite)) return;
        Write(now, TickLockMs);
    } catch (...) {}
}
void FlushStats() {
    using namespace stats;
    try {
        if (!state.installed || state.finished) return;
        // Nothing is counted any more: the hooks keep running until they are disabled.
        state.finished = true;
        const auto result = store.Flush(StopLockMs);
        if (result != Store::Result::Nothing) Log(std::string("STATS_FLUSHED ") + Describe(result));
    } catch (...) {}
}
