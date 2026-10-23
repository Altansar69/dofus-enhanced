// « Statistiques »: the store and its file (round trip, two clients merging
// under the file's lock, resets made by another client, a corrupt, partial,
// missing or later file, bounds), then the module's side (stats.cpp: counting
// only while statsEnabled is on and the module runs, a write at a change of
// character, after a minute and at Stop, the played character and the names
// of the game's data) on a fake runtime. No game, process, window or network.
#include "stats/stats.cpp"
#include "appearance/features.hpp"
#include "ui/options-ui.hpp"
#include "core/signatures.hpp"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>

namespace fake {
using managed::Obj;
using managed::Klass;
using managed::Method;
using managed::GCHandle;
struct Object { std::string type, text; std::map<std::string, Obj> fields; int64_t number = 0; };
struct MethodData { std::string type, name; int argc = 0; uintptr_t rva = 0; };
struct FieldData { std::string type, name; size_t offset = 0; };
std::vector<std::unique_ptr<Object>> objects;
std::vector<std::unique_ptr<MethodData>> methods;
std::map<std::string, std::unique_ptr<std::string>> classes;
std::map<GCHandle, Obj> handles;
GCHandle serial = 0x1000;
std::vector<std::string> logs;
std::vector<uintptr_t> verified;
// The fake client: no runtime at all, no character service, or a character
// service whose base information (esg.dpzj) is not there yet.
bool runtimeMissing = false, noService = false, noBase = false;
int64_t characterId = 0;
std::string characterName;
std::map<int64_t, std::string> spells, dungeons;
std::map<int, std::string> worlds;
unsigned nameReads = 0;
// The fields RequireField checks, at the pinned snapshot's offsets.
const FieldData Fields[] = {{"esg", "dpzj", 0x128}, {"z", "dbrm", 0x18}};
// The calls stats.cpp verifies, exactly as in the pinned snapshot.
const MethodData Contracts[] = {
    {"esg", "zcx", 0, 0x18BB140}, {"SpellData", "GetById", 1, 0x7CCE20}, {"SpellData", "get_name", 0, 0x7CD300},
    {"DataCenterModule", "get_dungeonsDataRoot", 0, 0x7BC7C0}, {"MetadataRoot`1", "GetObjectById", 1, 0x3874B80},
    {"DungeonData", "get_name", 0, 0x7C24C0}, {"WorldMapData", "GetWorldMapById", 1, 0x7D3D50}, {"WorldMapData", "get_name", 0, 0x7D3DE0}};

void Check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
Klass Type(const std::string& name) {
    auto& entry = classes[name];
    if (!entry) entry = std::make_unique<std::string>(name);
    return entry.get();
}
const std::string& TypeName(Klass type) { return *static_cast<const std::string*>(type); }
Obj Make(const std::string& type, const std::string& text = {}, int64_t number = 0) {
    auto object = std::make_unique<Object>();
    object->type = type;
    object->text = text;
    object->number = number;
    objects.push_back(std::move(object));
    return objects.back().get();
}
Object& Get(Obj object) {
    Check(object != nullptr, "Null fake object");
    return *static_cast<Object*>(object);
}
void InitializeApi() {
    using managed::api;
    api.class_is_assignable_from = [](Klass expected, Klass actual) { return expected == actual; };
    api.object_get_class = [](Obj object) -> Klass { return Type(Get(object).type); };
    api.object_unbox = [](Obj object) -> void* { return &Get(object).number; };
    api.class_get_parent = [](Klass type) -> Klass { return TypeName(type) == "DungeonsDataRoot" ? Type("MetadataRoot`1") : nullptr; };
    api.class_get_name = [](Klass type) -> const char* { return TypeName(type).c_str(); };
    api.class_get_field_from_name = [](Klass type, const char* name) -> void* {
        for (const auto& field : Fields)
            if (field.type == TypeName(type) && field.name == name) return const_cast<FieldData*>(&field);
        return nullptr;
    };
    api.field_get_offset = [](void* field) -> size_t { return static_cast<FieldData*>(field)->offset; };
    api.gchandle_new = [](Obj object, bool) -> GCHandle { handles[++serial] = object; return serial; };
    api.gchandle_get_target = [](GCHandle handle) -> Obj {
        const auto found = handles.find(handle);
        Check(found != handles.end(), "Unknown GC handle");
        return found->second;
    };
    api.gchandle_free = [](GCHandle handle) { Check(handles.erase(handle) == 1, "GC handle freed twice"); };
}
}

namespace managed {
Api api{};
uintptr_t gameBase = 0;
Klass Api::Class(const char*, const char*, const char* name) const {
    if (fake::runtimeMissing) throw std::runtime_error("No runtime in this test");
    return fake::Type(name);
}
Method Api::FindVerified(Klass klass, const char* name, int argc, uintptr_t rva) const {
    const auto& type = fake::TypeName(klass);
    fake::Check(std::any_of(std::begin(fake::Contracts), std::end(fake::Contracts), [&](const fake::MethodData& contract) {
        return contract.type == type && contract.name == name && contract.argc == argc && contract.rva == rva;
    }), "Unexpected statistics contract: " + type + "." + name);
    fake::Check(std::any_of(std::begin(Signatures), std::end(Signatures), [rva](const NativeSignature& guard) { return guard.rva == rva; }),
        "A statistics contract without its 16-byte guard: " + type + "." + name);
    fake::verified.push_back(rva);
    fake::methods.push_back(std::make_unique<fake::MethodData>(fake::MethodData{type, name, argc, rva}));
    return fake::methods.back().get();
}
Obj Api::Invoke(Method method, Obj self, std::initializer_list<void*> args) const {
    const auto& call = *static_cast<const fake::MethodData*>(method);
    const auto argument = [&](size_t index) { return *(args.begin() + index); };
    if (call.name == "zcx") {
        fake::Check(self && fake::Get(self).type == "esg" && fake::Get(self).fields.at("dpzj"), "zcx needs esg.dpzj");
        return fake::Make("Int64", {}, fake::characterId);
    }
    if (call.name == "GetById") {
        fake::Check(!self && args.size() == 1, "SpellData.GetById is static, with an Int64");
        const auto found = fake::spells.find(*static_cast<int64_t*>(argument(0)));
        return found == fake::spells.end() ? nullptr : fake::Make("SpellData", found->second);
    }
    if (call.name == "get_dungeonsDataRoot") return fake::Make("DungeonsDataRoot");
    if (call.name == "GetObjectById") {
        fake::Check(self && fake::Get(self).type == "DungeonsDataRoot" && args.size() == 1, "GetObjectById on the dungeons' root");
        const auto found = fake::dungeons.find(*static_cast<int64_t*>(argument(0)));
        return found == fake::dungeons.end() ? nullptr : fake::Make("DungeonData", found->second);
    }
    if (call.name == "GetWorldMapById") {
        fake::Check(!self && args.size() == 1, "WorldMapData.GetWorldMapById is static, with an Int32");
        const auto found = fake::worlds.find(*static_cast<int*>(argument(0)));
        return found == fake::worlds.end() ? nullptr : fake::Make("WorldMapData", found->second);
    }
    if (call.name == "get_name") {
        ++fake::nameReads;
        fake::Check(fake::Get(self).type == call.type, "A name read on another type");
        return fake::Make("String", fake::Get(self).text);
    }
    throw std::runtime_error("Unmodelled statistics call: " + call.name);
}
Obj Api::Field(Obj self, const char* name) const {
    if (!self) return nullptr;
    const auto& fields = fake::Get(self).fields;
    const auto found = fields.find(name);
    return found != fields.end() ? found->second : nullptr;
}
std::string Api::String(Obj self) const { return self ? fake::Get(self).text : std::string{}; }
}

Module* module = nullptr;
Module::Module(std::filesystem::path path) : folder(std::move(path)) {}
// The played character service (fng, class esg) of the fake client.
managed::Obj Module::FindService(const char* name) const {
    if (fake::noService || std::string(name) != "fng") return nullptr;
    const auto service = fake::Make("esg");
    if (!fake::noBase) {
        const auto base = fake::Make("z");
        fake::Get(base).fields["dbrm"] = fake::Make("String", fake::characterName);
        fake::Get(service).fields["dpzj"] = base;
    }
    return service;
}
void Log(const std::string& text) { fake::logs.push_back(text); }

namespace {
using fake::Check;
using stats::Counter;
using stats::Record;
using stats::Table;
using stats::Store;
using Result = stats::Store::Result;
namespace fs = std::filesystem;
fs::path root;

fs::path Folder(const std::string& name) {
    const auto folder = root / name;
    std::error_code ignored;
    fs::remove_all(folder, ignored);
    fs::create_directories(folder);
    return folder;
}
fs::path File(const std::string& name) { return Folder(name) / L"DofusMiniatures-stats.json"; }
fs::path With(const fs::path& path, const wchar_t* suffix) { auto result = path; result += suffix; return result; }
std::string Bytes(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}
void Put(const fs::path& path, const std::string& bytes) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << bytes;
}
stats::Book Saved(const fs::path& path) { return stats::Parse(Bytes(path)); }
const stats::Sheet& SheetOf(const stats::Book& book, int64_t id) {
    const auto sheet = book.Find(id);
    Check(sheet != nullptr, "Character " + std::to_string(id) + " missing from the file");
    return *sheet;
}
int64_t Count(const stats::Sheet& sheet, Counter counter) { return sheet.counters[static_cast<size_t>(counter)]; }
int64_t Best(const stats::Sheet& sheet, Record record) { return sheet.records[static_cast<size_t>(record)]; }
const std::map<int, int64_t>& Entries(const stats::Sheet& sheet, Table table) { return sheet.tables[static_cast<size_t>(table)]; }
constexpr int64_t Day1 = 1792771200, Day2 = 1792857600, Day3 = 1792944000; // 2026-10-23..25, UTC

// Every counter, record and table entry, written and read back unchanged,
// under the file's stable names; the newest name and the counted dates.
void RoundTrip() {
    const auto path = File("round-trip");
    Store store;
    store.Open(path);
    Check(!store.Pending() && store.Show(0).characters == 0 && !fs::exists(path), "A missing file is an empty store, never created by reading");
    for (size_t i = 0; i < stats::CounterCount; ++i) store.Add(7, static_cast<Counter>(i), 1000 + static_cast<int64_t>(i), Day2);
    store.Raise(7, Record::BiggestHit, 4321, Day1);
    store.Raise(7, Record::LongestSessionMs, 900, Day3);
    store.AddTo(7, Table::Spells, 413, 12, Day2);
    store.AddTo(7, Table::Spells, -5, 1, Day2); // keys are ints, any sign
    store.AddTo(7, Table::Elements, 2, 900, Day2);
    store.AddTo(7, Table::Dungeons, 51, 3, Day2);
    store.AddTo(7, Table::Dimensions, 1, 4, Day2);
    store.Name(7, "Altansar");
    // Nothing for character 0, a non-positive amount, or a bad index.
    store.Add(0, Counter::Xp, 5, Day2);
    store.Add(7, Counter::Xp, 0, Day2);
    store.Add(7, Counter::Xp, -9, Day2);
    store.Add(7, Counter::Count, 9, Day2);
    store.Raise(7, Record::BiggestHit, -1, Day2);
    store.AddTo(7, Table::Count, 1, 1, Day2);
    store.Name(7, std::string("bad\nname"));
    Check(store.Pending() && store.Flush(50) == Result::Written && fs::exists(path) && !store.Pending(), "One write of the pending counts");
    const auto bytes = Bytes(path);
    for (const char* key : {"\"schema\"", "\"characters\"", "\"playMs\"", "\"modTrips\"", "\"biggestHit\"", "\"longestSessionMs\"",
            "\"spells\"", "\"elements\"", "\"dungeons\"", "\"dimensions\"", "\"413\"", "\"Altansar\""})
        Check(bytes.find(key) != std::string::npos, std::string("The file keeps its stable key ") + key);
    const auto book = Saved(path);
    const auto& sheet = SheetOf(book, 7);
    Check(book.sheets.size() == 1 && sheet.name == "Altansar" && sheet.first == Day1 && sheet.last == Day3 && sheet.resets == 0,
        "Name and first/last counted dates");
    for (size_t i = 0; i < stats::CounterCount; ++i)
        Check(sheet.counters[i] == 1000 + static_cast<int64_t>(i), "Every counter round trips");
    Check(Best(sheet, Record::BiggestHit) == 4321 && Best(sheet, Record::LongestSessionMs) == 900, "Records round trip");
    Check(Entries(sheet, Table::Spells) == std::map<int, int64_t>{{-5, 1}, {413, 12}} && Entries(sheet, Table::Elements).at(2) == 900 &&
        Entries(sheet, Table::Dungeons).at(51) == 3 && Entries(sheet, Table::Dimensions).at(1) == 4, "Tables round trip");
    // Another client reads the same totals.
    Store reader;
    reader.Open(path);
    const auto totals = reader.Show(7);
    Check(totals.character == 7 && totals.name == "Altansar" && totals.characters == 1 && totals.first == Day1 && totals.last == Day3 &&
        totals.Count(Counter::Xp) == 1000 + static_cast<int64_t>(Counter::Xp) && totals.Best(Record::BiggestHit) == 4321 &&
        totals.Entries(Table::Spells).front() == std::make_pair(413, int64_t{12}), "A client opening the file shows its totals");
    Check(reader.Show(8).characters == 0 && reader.Show(8).character == 8 && reader.Show(8).first == 0, "An unknown character has nothing counted");
    // Unchanged on disk when nothing is pending; a name already saved needs no write.
    reader.Name(7, "Altansar");
    Check(!reader.Pending() && reader.Flush(50) == Result::Nothing && Bytes(path) == bytes, "Nothing pending, nothing written");
    // Saturating sums.
    store.Add(7, Counter::Xp, std::numeric_limits<int64_t>::max(), Day3);
    Check(store.Flush(50) == Result::Written && Count(SheetOf(Saved(path), 7), Counter::Xp) == std::numeric_limits<int64_t>::max(),
        "Sums saturate instead of wrapping");
    // The previous file is kept as the backup.
    Check(fs::exists(With(path, L".bak")) && Count(SheetOf(Saved(With(path, L".bak")), 7), Counter::Xp) == 1000 + static_cast<int64_t>(Counter::Xp),
        "Each write keeps the previous file as DofusMiniatures-stats.json.bak");
}

// Two clients (one character each, then the same character later on the
// other): each adds what it counted to the file's current content under the
// lock; neither erases the other's counts, whatever the order.
void TwoClientsMerge() {
    const auto path = File("two-clients");
    Store first, second;
    first.Open(path);
    second.Open(path);
    first.Add(1, Counter::Xp, 100, Day2);
    first.Raise(1, Record::BiggestHit, 50, Day2);
    first.AddTo(1, Table::Spells, 10, 3, Day2);
    first.Name(1, "Un");
    second.Add(2, Counter::Maps, 7, Day1);
    second.Add(1, Counter::Xp, 5, Day3);
    second.Raise(1, Record::BiggestHit, 80, Day3);
    second.AddTo(1, Table::Spells, 10, 2, Day3);
    second.AddTo(1, Table::Spells, 11, 1, Day3);
    second.Name(1, "Uno");
    Check(first.Flush(50) == Result::Written && second.Flush(50) == Result::Written, "Both clients write");
    auto book = Saved(path);
    const auto& one = SheetOf(book, 1);
    Check(book.sheets.size() == 2 && Count(one, Counter::Xp) == 105 && Best(one, Record::BiggestHit) == 80 &&
        Entries(one, Table::Spells) == std::map<int, int64_t>{{10, 5}, {11, 1}} && one.first == Day2 && one.last == Day3 && one.name == "Uno",
        "Sums, maxima, dates and the newest name merge");
    Check(Count(SheetOf(book, 2), Counter::Maps) == 7, "The other character's counts are kept");
    // Interleaved again: the first client has not read the second's write.
    first.Add(1, Counter::Xp, 1, Day3);
    second.Add(2, Counter::Maps, 1, Day3);
    Check(second.Flush(50) == Result::Written && first.Flush(50) == Result::Written, "Writes in the other order");
    book = Saved(path);
    Check(Count(SheetOf(book, 1), Counter::Xp) == 106 && Count(SheetOf(book, 2), Counter::Maps) == 8, "No write erases another's counts");
    // Every character's sums, after the last write (and for a reader, after a refresh).
    const auto all = first.Show(0);
    Check(all.character == 0 && all.characters == 2 && all.Count(Counter::Xp) == 106 && all.Count(Counter::Maps) == 8 &&
        all.Best(Record::BiggestHit) == 80 && all.first == Day1 && all.last == Day3, "Every character's sums, records as the largest");
    second.Add(2, Counter::Maps, 5, Day3);
    first.Refresh();
    Check(first.Show(0).Count(Counter::Maps) == 8, "Unwritten counts of another client are not in the file yet");
    Check(second.Show(0).Count(Counter::Maps) == 13 && second.Show(2).Count(Counter::Maps) == 13, "A client shows its own unwritten counts at once");
    Check(second.Flush(50) == Result::Written, "Written");
    first.Refresh();
    Check(first.Show(0).Count(Counter::Maps) == 13 && first.Show(2).Count(Counter::Maps) == 13, "A refresh shows another client's latest write");
}

// Erasing: this character's statistics (its name, a reset count and its time
// stay), or every character's. A client drops its unwritten counts that
// predate a reset made by another client (all of a sheet's when they straddle
// it); counts made after it are written, whether this client read the file
// again since (it then applies the reset at once) or not (the reset's time tells).
void ResetsAcrossClients() {
    const auto path = File("resets");
    Store mine, other;
    mine.Open(path);
    other.Open(path);
    mine.Add(1, Counter::Xp, 10, Day1);
    mine.Name(1, "Lou");
    mine.Add(3, Counter::Maps, 2, Day1);
    Check(mine.Flush(50) == Result::Written, "Written");
    mine.Add(1, Counter::Xp, 5, Day2);   // counted before the other client's reset
    mine.Add(3, Counter::Maps, 1, Day2);
    other.Add(2, Counter::Quests, 4, Day2);
    Check(other.Clear(1, 50, Day2 + 10) == Result::Written, "Another client erases character 1");
    auto book = Saved(path);
    Check(Bytes(path).find("\"resetAt\"") != std::string::npos, "The reset's time is in the file");
    Check(SheetOf(book, 1).Empty() && SheetOf(book, 1).name == "Lou" && SheetOf(book, 1).resets == 1 && SheetOf(book, 1).resetAt == Day2 + 10 &&
        SheetOf(book, 1).first == 0 && book.resetAt == 0 &&
        Count(SheetOf(book, 3), Counter::Maps) == 2 && Count(SheetOf(book, 2), Counter::Quests) == 4,
        "One character erased, its name and reset count kept; the other counts written in the same go");
    mine.Refresh();
    Check(mine.Show(1).Count(Counter::Xp) == 0 && mine.Show(1).name == "Lou" && mine.Show(3).Count(Counter::Maps) == 3,
        "Read again: counts older than another client's reset are dropped at once");
    mine.Add(1, Counter::Xp, 7, Day2 + 10); // the reset's second, once seen: after it
    Check(mine.Show(1).Count(Counter::Xp) == 7, "Counted after a reset seen: shown at once");
    Check(mine.Flush(50) == Result::Written, "Written");
    book = Saved(path);
    Check(Count(SheetOf(book, 1), Counter::Xp) == 7 && Count(SheetOf(book, 3), Counter::Maps) == 3, "...and written, the older ones not; the others are");
    mine.Add(1, Counter::Xp, 4, Day3);
    Check(mine.Flush(50) == Result::Written && Count(SheetOf(Saved(path), 1), Counter::Xp) == 11 && SheetOf(Saved(path), 1).first == Day2 + 10,
        "Counted after the reset: written, its first date the first count after it");
    // Every character's: the file's reset count changes, the other client drops all its unwritten counts.
    mine.Add(3, Counter::Maps, 9, Day3);
    Check(other.Clear(0, 50, Day3) == Result::Written, "Every character erased");
    book = Saved(path);
    Check(book.sheets.empty() && book.resets == 1 && book.resetAt == Day3, "No sheet left, the file's reset count bumped, its time kept");
    Check(mine.Flush(50) == Result::Written && Saved(path).sheets.empty(), "Unwritten counts older than a full reset are dropped (its second is before)");
    // A client's own reset drops its own unwritten counts at once.
    mine.Add(4, Counter::Kills, 3, Day3);
    mine.Add(5, Counter::Kills, 2, Day3);
    Check(mine.Clear(4, 50, Day3) == Result::Written && !mine.Pending(), "Its own reset");
    book = Saved(path);
    Check(SheetOf(book, 4).Empty() && Count(SheetOf(book, 5), Counter::Kills) == 2, "Its unwritten counts for that character are dropped, the others written");
    Check(mine.Show(4).Count(Counter::Kills) == 0 && mine.Show(0).Count(Counter::Kills) == 2, "Shown at once");
    mine.Add(4, Counter::Kills, 1, Day3);
    Check(mine.Flush(50) == Result::Written && Count(SheetOf(Saved(path), 4), Counter::Kills) == 1, "Counted after its own reset, in its second: written");
    Check(mine.Clear(0, 50, Day3) == Result::Written && mine.Show(0).characters == 0 && Saved(path).resets == 2, "Every character, by this client");
    Check(mine.Clear(99, 50, Day3) == Result::Written && SheetOf(Saved(path), 99).resets == 1, "A character never counted can be erased");

    // Another client's resets, this client not having read the file since:
    // the latest reset's time tells which sheets were counted after it.
    const auto later = File("resets-later");
    Store a, b;
    a.Open(later);
    b.Open(later);
    a.Add(5, Counter::Xp, 10, Day1);
    a.Add(6, Counter::Xp, 10, Day1);
    Check(a.Flush(50) == Result::Written, "Written");
    a.Add(6, Counter::Xp, 3, Day2);          // before the resets...
    Check(b.Clear(0, 50, Day2 + 60) == Result::Written && b.Clear(5, 50, Day2 + 120) == Result::Written,
        "Another client erases every character, then character 5");
    a.Add(6, Counter::Xp, 2, Day3);          // ...and after them: this sheet straddles them
    a.Add(5, Counter::Xp, 4, Day3);          // after both
    a.Add(7, Counter::Xp, 1, Day2 + 61);     // after the full reset (no reset of its own)
    a.Add(8, Counter::Xp, 1, Day2 + 60);     // in the full reset's second
    a.Add(9, Counter::Xp, 1, Day2 + 90);     // after the full reset, before the reset of character 5: its own sheet is not reset
    a.Add(5, Counter::Maps, 0, Day2);        // nothing: no date either
    Check(a.Flush(50) == Result::Written, "Written");
    book = Saved(later);
    Check(Count(SheetOf(book, 5), Counter::Xp) == 4 && SheetOf(book, 5).resets == 1 && !book.Find(6) && Count(SheetOf(book, 7), Counter::Xp) == 1 &&
        !book.Find(8) && Count(SheetOf(book, 9), Counter::Xp) == 1, "Counted after the latest reset of their sheet: written; started before it: dropped");
    Check(a.Show(5).Count(Counter::Xp) == 4 && a.Show(0).Count(Counter::Xp) == 6, "And shown so");
    // Read again (the pump does every few seconds): a reset seen there applies at once.
    a.Add(7, Counter::Xp, 5, Day3);
    Check(b.Clear(0, 50, Day3 + 60) == Result::Written, "Every character erased again");
    a.Refresh();
    Check(!a.Pending() && a.Show(0).characters == 0 && a.Show(7).Count(Counter::Xp) == 0, "Read again: the counts made before the reset are dropped at once");
    a.Add(7, Counter::Xp, 6, Day3 + 60);
    Check(a.Show(7).Count(Counter::Xp) == 6 && a.Show(0).Count(Counter::Xp) == 6, "Counted after a reset seen, even in its second: shown...");
    Check(a.Flush(50) == Result::Written && Count(SheetOf(Saved(later), 7), Counter::Xp) == 6, "...and written");
    // A reset whose time the file does not tell (edited by hand): the sheet's unwritten counts all go.
    a.Add(7, Counter::Xp, 1, Day3 + 500);
    auto json = nlohmann::json::parse(Bytes(later));
    json["resets"] = 9;
    json.erase("resetAt");
    Put(later, json.dump());
    Check(a.Flush(50) == Result::Written && Count(SheetOf(Saved(later), 7), Counter::Xp) == 6, "A reset of unknown time: dropped");
    // An older file without any reset (its backup put back): nothing of this client's is dropped.
    a.Add(7, Counter::Xp, 2, Day1);
    Put(later, "{\"schema\": 1, \"characters\": [{\"id\": 7, \"counters\": {\"xp\": 6}}]}");
    Check(a.Flush(50) == Result::Written && Count(SheetOf(Saved(later), 7), Counter::Xp) == 8, "A file without resets drops nothing");
}

// « Plus longue session » is never longer than the time played: a session
// going on through a reset (here or in another client) counts from it.
void SessionWithinPlayTime() {
    const auto path = File("session");
    Store mine, other;
    mine.Open(path);
    other.Open(path);
    constexpr int64_t Hour = 3600000;
    mine.Add(1, Counter::PlayMs, 3 * Hour, Day1);
    mine.Raise(1, Record::LongestSessionMs, 3 * Hour, Day1);
    Check(mine.Flush(50) == Result::Written && Best(SheetOf(Saved(path), 1), Record::LongestSessionMs) == 3 * Hour, "A three-hour session");
    mine.Raise(1, Record::LongestSessionMs, 3 * Hour + 1, Day1);
    Check(!mine.Pending(), "Never longer than the time played, even without a reset");
    // Erased here while the session goes on: it counts from the reset.
    Check(mine.Clear(1, 50, Day1 + 10) == Result::Written, "Erased");
    mine.Raise(1, Record::LongestSessionMs, 3 * Hour, Day1 + 10);
    Check(!mine.Pending(), "Nothing played since the reset: no session");
    mine.Add(1, Counter::PlayMs, 60000, Day1 + 70);
    mine.Raise(1, Record::LongestSessionMs, 3 * Hour + 60000, Day1 + 70);
    auto shown = mine.Show(1);
    Check(shown.Best(Record::LongestSessionMs) == 60000 && shown.Count(Counter::PlayMs) == 60000 && mine.Show(0).Best(Record::LongestSessionMs) == 60000,
        "Shown from the reset");
    Check(mine.Flush(50) == Result::Written && Best(SheetOf(Saved(path), 1), Record::LongestSessionMs) == 60000, "Written so");
    // Later sessions: a shorter one leaves the record, a longer one raises it.
    mine.Add(1, Counter::PlayMs, 30000, Day2);
    mine.Raise(1, Record::LongestSessionMs, 30000, Day2);
    Check(mine.Show(1).Best(Record::LongestSessionMs) == 60000, "A shorter session leaves it");
    mine.Add(1, Counter::PlayMs, 90000, Day2);
    mine.Raise(1, Record::LongestSessionMs, 120000, Day2);
    Check(mine.Show(1).Best(Record::LongestSessionMs) == 120000 && mine.Flush(50) == Result::Written &&
        Best(SheetOf(Saved(path), 1), Record::LongestSessionMs) == 120000, "A longer one raises it");
    // Erased by another client this one has not read since: bounded when written.
    Check(other.Clear(0, 50, Day2 + 10) == Result::Written, "Another client erases every character");
    mine.Add(1, Counter::PlayMs, 5000, Day3);
    mine.Raise(1, Record::LongestSessionMs, 125000, Day3);
    Check(mine.Flush(50) == Result::Written && Count(SheetOf(Saved(path), 1), Counter::PlayMs) == 5000 &&
        Best(SheetOf(Saved(path), 1), Record::LongestSessionMs) == 5000, "Within the time played counted after that reset");
    // The same when this client's reset of another character writes them.
    Check(other.Clear(0, 50, Day3 + 10) == Result::Written, "Every character erased again");
    mine.Add(1, Counter::PlayMs, 2000, Day3 + 20);
    mine.Raise(1, Record::LongestSessionMs, 7000, Day3 + 20);
    Check(mine.Clear(42, 50, Day3 + 30) == Result::Written && Count(SheetOf(Saved(path), 1), Counter::PlayMs) == 2000 &&
        Best(SheetOf(Saved(path), 1), Record::LongestSessionMs) == 2000, "Within the time played when another character's reset writes them");
    // Other records are not bounded so.
    mine.Raise(1, Record::BiggestHit, 900000, Day3);
    Check(mine.Show(1).Best(Record::BiggestHit) == 900000, "The biggest hit is not a duration");
}

// A corrupt, partial or missing file never stops the store: an unreadable file
// is moved aside (never overwritten) and the backup is used instead.
void CorruptAndPartialFiles() {
    {
        const auto path = File("partial");
        const std::string partial = "{\"schema\": 1, \"characters\": [{\"id\": 5, \"counters\": {\"xp\": 1";
        Put(path, partial);
        Store store;
        store.Open(path);
        Check(store.Show(0).characters == 0 && !store.Frozen(), "A partial file reads as nothing, without throwing");
        store.Add(9, Counter::Xp, 3, Day1);
        Check(store.Flush(50) == Result::Written, "The next write works");
        Check(Bytes(With(path, L".corrupt")) == partial, "The partial file is kept aside as .corrupt");
        const auto book = Saved(path);
        Check(book.sheets.size() == 1 && Count(SheetOf(book, 9), Counter::Xp) == 3, "A fresh file with the new counts");
    }
    {
        const auto path = File("corrupt-with-backup");
        Store writer;
        writer.Open(path);
        writer.Add(1, Counter::Crafts, 2, Day1);
        Check(writer.Flush(50) == Result::Written, "First write");
        writer.Add(1, Counter::Crafts, 3, Day2);
        Check(writer.Flush(50) == Result::Written, "Second write: the first one is the backup");
        Put(path, std::string("\xFF\xFE garbage", 10));
        Store store;
        store.Open(path);
        Check(store.Show(1).Count(Counter::Crafts) == 2, "An unreadable file shows the backup");
        store.Refresh();
        Check(store.Show(1).Count(Counter::Crafts) == 2, "Read again: still the backup, without throwing");
        store.Add(1, Counter::Crafts, 10, Day3);
        Check(store.Flush(50) == Result::Written, "Written");
        Check(Bytes(With(path, L".corrupt")) == std::string("\xFF\xFE garbage", 10), "The unreadable file is kept aside");
        Check(Count(SheetOf(Saved(path), 1), Counter::Crafts) == 12, "The backup plus the new counts: at most one write lost");
    }
    {
        // A write interrupted between its two renames: only the backup is there.
        const auto path = File("interrupted");
        Store writer;
        writer.Open(path);
        writer.Add(2, Counter::Quests, 1, Day1);
        Check(writer.Flush(50) == Result::Written, "Written");
        writer.Add(2, Counter::Quests, 1, Day1);
        Check(writer.Flush(50) == Result::Written, "Written");
        fs::remove(path);
        Store store;
        store.Open(path);
        Check(store.Show(2).Count(Counter::Quests) == 1, "No file: the backup is shown");
        store.Add(2, Counter::Quests, 5, Day2);
        Check(store.Flush(50) == Result::Written && Count(SheetOf(Saved(path), 2), Counter::Quests) == 6, "And written upon");
    }
    {
        // Not the store's structure: aside too.
        for (const std::string content : {"[]", "{}", "{\"schema\": \"1\"}", "{\"schema\": 0}", "{\"schema\": 1, \"characters\": {}}",
                "{\"schema\": 1, \"characters\": [{\"name\": \"x\"}]}", "{\"schema\": 1, \"characters\": [{\"id\": -3}]}",
                "{\"schema\": 1, \"characters\": [{\"id\": 3, \"counters\": []}]}"}) {
            bool corrupt = false;
            try { stats::Parse(content); } catch (const stats::Corrupt&) { corrupt = true; }
            Check(corrupt, "Not a statistics file: " + content);
        }
        const auto path = File("too-large");
        Put(path, "{\"schema\": 1, \"characters\": []" + std::string(stats::MaxFileBytes, ' ') + "}");
        Store store;
        store.Open(path);
        store.Add(1, Counter::Deaths, 1, Day1);
        Check(store.Flush(50) == Result::Written && fs::file_size(With(path, L".corrupt")) > stats::MaxFileBytes &&
            fs::file_size(path) < 4096, "A file over 4 MiB is kept aside, never read");
    }
    {
        // Lenient on values: negative or invalid entries are dropped, two sheets of one character merged.
        const auto book = stats::Parse("{\"schema\": 1, \"resets\": 2, \"characters\": ["
            "{\"id\": 4, \"name\": \"Ana\", \"first\": 5, \"last\": 9, \"counters\": {\"xp\": -5, \"maps\": 3, \"playMs\": 1.5},"
            " \"tables\": {\"spells\": {\"12\": 4, \"x\": 1, \"13\": -1, \"14\": 0, \"9999999999\": 2}}},"
            "{\"id\": 4, \"first\": 2, \"counters\": {\"maps\": 1}}], \"resetAt\": 77}");
        const auto& sheet = SheetOf(book, 4);
        Check(book.resets == 2 && book.resetAt == 77 && sheet.resetAt == 0 && book.sheets.size() == 1 && sheet.name == "Ana" && sheet.first == 2 && sheet.last == 9 &&
            Count(sheet, Counter::Xp) == 0 && Count(sheet, Counter::Maps) == 4 && Count(sheet, Counter::PlayMs) == 0 &&
            Entries(sheet, Table::Spells) == std::map<int, int64_t>{{12, 4}}, "Invalid values dropped, duplicates merged");
    }
}

// A file of a later revision is never rewritten, by a write or a reset.
void LaterRevision() {
    const auto path = File("later");
    const std::string later = "{\"schema\": 2, \"characters\": [{\"id\": 1, \"counters\": {\"xp\": 5}}], \"novelty\": true}";
    Put(path, later);
    Store store;
    store.Open(path);
    Check(store.Frozen(), "Schema 2: frozen");
    store.Add(1, Counter::Xp, 10, Day1);
    Check(store.Flush(50) == Result::Newer && store.Clear(1, 50, Day1) == Result::Newer && store.Clear(0, 50, Day1) == Result::Newer &&
        Bytes(path) == later && !fs::exists(With(path, L".corrupt")), "Never rewritten, never moved aside");
    Check(store.Show(1).Count(Counter::Xp) == 10, "Counted in memory meanwhile");
    // Found only at the first write (the file appeared later).
    const auto other = File("later-found");
    Store late;
    late.Open(other);
    late.Add(1, Counter::Xp, 1, Day1);
    Put(other, later);
    late.Refresh();
    Check(!late.Frozen() && late.Pending() && late.Show(1).Count(Counter::Xp) == 1, "Read again: a later file is not taken, nothing is lost");
    Check(late.Flush(50) == Result::Newer && late.Frozen() && late.Pending() && Bytes(other) == later, "Found at the first write: frozen, counts kept");
}

// Another client holds the lock: the counts wait for the next try.
void BusyLock() {
    const auto path = File("busy");
    Store store;
    store.Open(path);
    store.Add(1, Counter::Harvests, 2, Day1);
    HANDLE lock = CreateFileW(With(path, L".lock").c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(lock != INVALID_HANDLE_VALUE, "Test lock");
    const auto before = GetTickCount64();
    const auto busy = store.Flush(30);
    const auto waited = GetTickCount64() - before;
    store.Add(1, Counter::Harvests, 3, Day2);
    const auto clear = store.Clear(1, 10, Day2);
    CloseHandle(lock);
    Check(busy == Result::Busy && clear == Result::Busy && waited >= 20 && waited < 2000 && !fs::exists(path), "Busy: nothing written, after a bounded wait");
    Check(store.Pending() && store.Show(1).Count(Counter::Harvests) == 5, "The counts wait in memory, with the ones counted meanwhile");
    Check(store.Flush(30) == Result::Written && Count(SheetOf(Saved(path), 1), Counter::Harvests) == 5, "Written at the next try");
}

// Bounds: 512 spells (the largest kept when two clients' tables merge), 256 characters.
void Bounds() {
    const auto path = File("bounds");
    Store store;
    store.Open(path);
    for (int spell = 1; spell <= 600; ++spell) store.AddTo(1, Table::Spells, spell, spell, Day1);
    Check(store.Flush(50) == Result::Written, "Written");
    auto spells = Entries(SheetOf(Saved(path), 1), Table::Spells);
    Check(spells.size() == 512 && spells.count(1) && spells.count(512) && !spells.count(513), "A full table takes no new key until it is written");
    Store other;
    other.Open(path);
    for (int spell = 1000; spell < 1100; ++spell) other.AddTo(1, Table::Spells, spell, 100000, Day2);
    Check(other.Flush(50) == Result::Written, "Written");
    spells = Entries(SheetOf(Saved(path), 1), Table::Spells);
    Check(spells.size() == 512 && spells.count(1099) && spells.count(512) && !spells.count(1) && !spells.count(100),
        "Merged tables keep their 512 largest entries");
    Store one;
    one.Open(path);
    one.AddTo(1, Table::Spells, 5000, 1000000, Day2);
    Check(one.Flush(50) == Result::Written, "Written");
    spells = Entries(SheetOf(Saved(path), 1), Table::Spells);
    Check(spells.size() == 512 && spells.count(5000) && !spells.count(101) && spells.count(102), "One entry over: the smallest goes");
    for (int64_t id = 2; id <= 256; ++id) store.Add(id, Counter::Levels, 1, Day1);
    Check(store.Flush(50) == Result::Written && Saved(path).sheets.size() == 256, "256 characters");
    store.Add(257, Counter::Levels, 1, Day1);
    store.Name(257, "Trop");
    Check(!store.Pending() && Saved(path).sheets.size() == 256 && store.Show(0).characters == 256, "A 257th character is not counted");
    store.Add(256, Counter::Levels, 1, Day2);
    Check(store.Flush(50) == Result::Written && Count(SheetOf(Saved(path), 256), Counter::Levels) == 2, "The known ones still are");
    // A file with more characters (a later revision's bound): all kept, never moved aside; no new one.
    const auto many = File("many");
    nlohmann::json json = {{"schema", 1}, {"characters", nlohmann::json::array()}};
    for (int64_t id = 1; id <= 300; ++id) json["characters"].push_back({{"id", id}, {"counters", {{"xp", 1}}}});
    Put(many, json.dump());
    Store big;
    big.Open(many);
    big.Add(301, Counter::Xp, 1, Day1);
    big.Add(300, Counter::Xp, 1, Day1);
    const auto result = big.Flush(50);
    const auto book = Saved(many);
    Check(result == Result::Written && book.sheets.size() == 300 && Count(SheetOf(book, 300), Counter::Xp) == 2 && !book.Find(301) &&
        !fs::exists(With(many, L".corrupt")), "300 characters kept, a new one refused");
}

// Fields of a later revision of schema 1 are written back unchanged.
void UnknownFieldsKept() {
    const auto path = File("unknown");
    Put(path, "{\"schema\": 1, \"note\": \"later\", \"characters\": [{\"id\": 3, \"badge\": [1, 2], \"counters\": {\"xp\": 2, \"future\": 9},"
        " \"tables\": {\"spells\": {\"5\": 1}, \"pets\": {\"7\": 2}}}]}");
    Store store;
    store.Open(path);
    store.Add(3, Counter::Xp, 1, Day1);
    Check(store.Flush(50) == Result::Written, "Written");
    const auto json = nlohmann::json::parse(Bytes(path));
    const auto& sheet = json.at("characters").at(0);
    Check(json.at("note") == "later" && sheet.at("badge") == nlohmann::json({1, 2}) && sheet.at("counters").at("future") == 9 &&
        sheet.at("counters").at("xp") == 3 && sheet.at("tables").at("pets").at("7") == 2 && sheet.at("tables").at("spells").at("5") == 1,
        "Unknown fields, counters and tables kept; known ones updated");
}

// The module's side, on the fake runtime.
struct Client {
    Module state{L"stats-test"};
    explicit Client(const fs::path& folder) {
        module = &state;
        state.settingsPath = folder / L"DofusMiniatures-3.7.11.11-settings.ini";
        state.ready.store(true);
        state.servicesReady = true;
        state.settings.statsEnabled = true;
    }
    ~Client() { module = nullptr; }
};
void Install() {
    fake::verified.clear();
    InstallStats();
}
void NextSecond() { stats::state.nextRead = 0; }

// Counting only while statsEnabled is on and the module runs; the played
// character read from esg; a write after a minute, at a change of character
// and at Stop; the totals, the resets and the names the options show.
void ModuleSide() {
    const auto folder = Folder("module");
    const auto path = folder / L"DofusMiniatures-stats.json";
    Client client(folder);
    fake::characterId = 101;
    fake::characterName = "Altansar";
    fake::spells = {{413, "Pression"}};
    fake::dungeons = {{5, "Donjon des Bouftous"}};
    fake::worlds = {{1, "Monde des Douze"}};
    Install();
    auto expected = std::vector<uintptr_t>{};
    for (const auto& contract : fake::Contracts) expected.push_back(contract.rva);
    auto verified = fake::verified;
    std::sort(expected.begin(), expected.end());
    std::sort(verified.begin(), verified.end());
    Check(verified == expected, "Every statistics contract verified once, each with its guard");
    Check(stats::Counting() && stats::Current() == 0, "Counting; the character not read yet");
    TickStats();
    Check(stats::Current() == 101 && stats::Shown(false).name == "Altansar", "The played character from esg.zcx, its name from esg.dpzj.dbrm");
    stats::Add(101, Counter::Xp, 500);
    stats::Raise(101, Record::BiggestHit, 900);
    stats::AddTo(101, Table::Spells, 413, 2);
    stats::AddTo(101, Table::Dungeons, 5, 1);
    stats::AddTo(101, Table::Dimensions, 1, 3);
    TickStats();
    Check(!fs::exists(path) && stats::ModuleStore().Pending(), "No write before the interval");
    // Off: nothing counted, nothing read for the trackers.
    client.state.settings.statsEnabled = false;
    stats::Add(101, Counter::Xp, 1000);
    stats::Name(101, "Autre");
    Check(!stats::Counting() && stats::Shown(false).Count(Counter::Xp) == 500 && stats::Shown(false).name == "Altansar",
        "Unticked: nothing counted, the statistics still shown");
    client.state.settings.statsEnabled = true;
    for (const auto off : {0, 1}) {
        if (off == 0) client.state.ready.store(false);
        else client.state.accountOptionsUnbound = true; // logged out, reconnecting, choosing a character
        stats::Add(101, Counter::Xp, 1000);
        Check(!stats::Counting() && stats::Shown(false).Count(Counter::Xp) == 500, "Nothing counted before the module is ready or while it waits for the account");
        client.state.ready.store(true);
        client.state.accountOptionsUnbound = false;
    }
    // Once the interval is over: one write.
    stats::state.nextWrite = 0;
    TickStats();
    Check(fs::exists(path) && Count(SheetOf(Saved(path), 101), Counter::Xp) == 500 && SheetOf(Saved(path), 101).name == "Altansar" &&
        !stats::ModuleStore().Pending(), "A write once the interval is over");
    stats::Add(101, Counter::Xp, 1);
    TickStats();
    Check(Count(SheetOf(Saved(path), 101), Counter::Xp) == 500, "Then not before the next interval");
    // Another character in this client: its predecessor's counts written at once.
    fake::characterId = 202;
    fake::characterName = "Bouftou";
    NextSecond();
    TickStats();
    Check(stats::Current() == 202 && Count(SheetOf(Saved(path), 101), Counter::Xp) == 501, "A change of character writes at once");
    stats::Add(202, Counter::Maps, 3);
    stats::Add(202, Counter::KamasEarned, 1500);
    const auto mine = stats::Shown(false), all = stats::Shown(true);
    Check(mine.character == 202 && mine.name == "Bouftou" && mine.Count(Counter::Maps) == 3 && mine.Count(Counter::Xp) == 0 &&
        all.characters == 2 && all.Count(Counter::Xp) == 501 && all.Count(Counter::Maps) == 3 && all.Best(Record::BiggestHit) == 900,
        "This character, and every character's sums");
    // Counting stops (a logout, a reconnection, the character selection): written at once.
    client.state.accountOptionsUnbound = true;
    TickStats();
    Check(Count(SheetOf(Saved(path), 202), Counter::Maps) == 3 && Count(SheetOf(Saved(path), 202), Counter::KamasEarned) == 1500 &&
        !stats::ModuleStore().Pending(), "Counting stopped: the counts made until then are written at once");
    client.state.accountOptionsUnbound = false;
    // Loading or between characters: the last character stays.
    fake::noBase = true;
    NextSecond();
    TickStats();
    fake::noBase = false;
    fake::noService = true;
    NextSecond();
    TickStats();
    fake::noService = false;
    Check(stats::Current() == 202, "No character read: the last one stays");
    // The names the options show, from the game's data, once each.
    const auto reads = fake::nameReads;
    Check(stats::KeyName(Table::Spells, 413) == "Pression" && stats::KeyName(Table::Spells, 413) == "Pression" &&
        stats::KeyName(Table::Dungeons, 5) == "Donjon des Bouftous" && stats::KeyName(Table::Dimensions, 1) == "Monde des Douze" &&
        fake::nameReads == reads + 3, "Spell, dungeon and world names, each read once");
    Check(stats::KeyName(Table::Spells, 999).empty() && stats::KeyName(Table::Dungeons, 6).empty() && stats::KeyName(Table::Dimensions, 77).empty(),
        "Unknown ids have no name");
    Check(stats::KeyName(Table::Elements, 0) == "neutre" && stats::KeyName(Table::Elements, 1) == "terre" && stats::KeyName(Table::Elements, 2) == "feu" &&
        stats::KeyName(Table::Elements, 3) == "eau" && stats::KeyName(Table::Elements, 4) == "air" && stats::KeyName(Table::Elements, 5).empty(),
        "The elements' names");
    // Resets of what the options show.
    Check(stats::Clear(false) && stats::Shown(false).first == 0 && stats::Shown(false).Count(Counter::Maps) == 0 &&
        stats::Shown(true).Count(Counter::Xp) == 501, "This character's statistics erased, the others kept");
    Check(SheetOf(Saved(path), 202).resets == 1 && SheetOf(Saved(path), 202).name == "Bouftou", "In the file at once");
    Check(stats::Clear(true) && stats::Shown(true).characters == 0 && Saved(path).sheets.empty(), "Every character's statistics erased");
    // Stop, in Module::Stop's order: stopped (a stop may also be requested
    // before), then the trackers' last counts as it restores them, then
    // FlushStats writes them; nothing counted afterwards.
    stats::Add(202, Counter::Deaths, 2);
    client.state.stopRequested.store(true);
    client.state.stopped.store(true);
    stats::Add(202, Counter::CellsRoleplay, 4);
    Check(stats::Counting(), "Still counting while Stop restores the trackers");
    FlushStats();
    Check(Count(SheetOf(Saved(path), 202), Counter::Deaths) == 2 && Count(SheetOf(Saved(path), 202), Counter::CellsRoleplay) == 4,
        "Stop writes the last counts, the trackers' last ones too");
    const auto bytes = Bytes(path);
    stats::Add(202, Counter::Deaths, 5);
    TickStats();
    FlushStats();
    Check(!stats::Counting() && Bytes(path) == bytes && !stats::Clear(false), "Nothing counted, written or erased after Stop");
    Check(std::none_of(fake::logs.begin(), fake::logs.end(), [](const std::string& line) { return line.find("DISABLED") != std::string::npos; }),
        "No part disabled on the pinned client");
}

// Another client's reset reaches this client within a few seconds (its pump
// reads the file again once another client wrote it): the counts made before
// it are dropped, those made after it are written, in its second too.
void ModuleFollowsOtherResets() {
    const auto folder = Folder("module-resets");
    const auto path = folder / L"DofusMiniatures-stats.json";
    Client client(folder);
    fake::characterId = 303;
    fake::characterName = "Lou";
    Install();
    TickStats();
    stats::Add(303, Counter::Xp, 5);
    stats::state.nextWrite = 0;
    TickStats();
    Check(Count(SheetOf(Saved(path), 303), Counter::Xp) == 5, "Written");
    stats::Add(303, Counter::Xp, 6); // before the other client's reset
    Store other;
    other.Open(path);
    Check(other.Clear(0, 50, stats::Now()) == Result::Written, "Another client erases every character");
    TickStats();
    Check(stats::Shown(false).Count(Counter::Xp) == 11, "Not seen before the next read of the file");
    stats::state.nextRefresh = 0;
    TickStats();
    Check(stats::Shown(false).Count(Counter::Xp) == 0 && stats::Shown(true).characters == 0, "Seen at the next read: the counts made before it are dropped");
    stats::Add(303, Counter::Xp, 7);
    Check(stats::Shown(false).Count(Counter::Xp) == 7, "Counted after it: shown...");
    FlushStats();
    Check(Count(SheetOf(Saved(path), 303), Counter::Xp) == 7 && Saved(path).resets == 1, "...and written");
}

// Without the game's contracts: still counting, the character unknown, no names; never a throw.
void ModuleWithoutRuntime() {
    const auto folder = Folder("no-runtime");
    Client client(folder);
    fake::runtimeMissing = true;
    fake::logs.clear();
    Install();
    fake::runtimeMissing = false;
    for (const char* part : {"STATS_CHARACTER_DISABLED", "STATS_SPELL_NAMES_DISABLED", "STATS_DUNGEON_NAMES_DISABLED", "STATS_WORLD_NAMES_DISABLED"})
        Check(std::any_of(fake::logs.begin(), fake::logs.end(), [part](const std::string& line) { return line.rfind(part, 0) == 0; }),
            std::string("Each part disables only itself: ") + part);
    TickStats();
    stats::Add(7, Counter::Quests, 1);
    Check(stats::Counting() && stats::Current() == 0 && stats::Shown(false).character == 0 && stats::Shown(true).Count(Counter::Quests) == 1 &&
        stats::KeyName(Table::Spells, 413).empty() && !stats::Clear(false), "Counting goes on; no character, no names");
    FlushStats();
    Check(Count(SheetOf(Saved(folder / L"DofusMiniatures-stats.json"), 7), Counter::Quests) == 1, "And is written at Stop");
    // No module: nothing at all.
    module = nullptr;
    InstallStats();
    TickStats();
    FlushStats();
    stats::Add(7, Counter::Quests, 1);
    Check(!stats::Counting(), "No module, no counting");
}
}

int main() {
    try {
        fake::InitializeApi();
        root = fs::temp_directory_path() / (L"dofus-stats-store-tests-" + std::to_wstring(GetCurrentProcessId()));
        std::error_code ignored;
        fs::remove_all(root, ignored);
        RoundTrip();
        TwoClientsMerge();
        ResetsAcrossClients();
        SessionWithinPlayTime();
        CorruptAndPartialFiles();
        LaterRevision();
        BusyLock();
        Bounds();
        UnknownFieldsKept();
        ModuleSide();
        ModuleFollowsOtherResets();
        ModuleWithoutRuntime();
        stats::UseGame(nullptr);
        fs::remove_all(root, ignored);
        std::cout << "PASS: statistics store (round trip of every counter, record and table under stable keys; two clients merging under the file's lock in any order; "
                     "resets of one or every character, by this client or another, with their time: unwritten counts older than a reset dropped, later ones written; "
                     "the longest session within the time played; corrupt, partial, oversized and missing files kept aside "
                     "or recovered from the backup; a later revision's file never rewritten; a busy lock retried; 512 spells and 256 characters; unknown fields kept) "
                     "and module side (counting only while statsEnabled is on and the module runs, until Stop's flush; the played character from esg; a write after a minute, "
                     "at a change of character and at Stop; another client's reset applied at the next read of the file; this character's and every character's "
                     "totals; resets; spell, dungeon, world and element names; each contract with its guard)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
