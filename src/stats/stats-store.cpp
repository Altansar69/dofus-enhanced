#include "stats/stats-store.hpp"
#include "core/common.hpp"
#include <algorithm>
#include <atomic>
#include <charconv>
#include <limits>
#include <optional>
#include <set>

namespace stats {
namespace {
using Json = nlohmann::json;
constexpr int64_t Largest = std::numeric_limits<int64_t>::max();
// The file's keys. Never rename them: older clients read the same file.
constexpr const char* CounterNames[] = {
    "playMs", "cellsRoleplay", "cellsFight", "maps", "dimensions", "xp", "levels", "kamasEarned", "kamasSpent",
    "harvests", "crafts", "quests", "achievements", "captures", "fightsPve", "fightsPvp", "fightsKolizeum",
    "winsPve", "winsPvp", "winsKolizeum", "fightMs", "fightTurns", "damageDealt", "damageTaken", "heals", "kills",
    "deaths", "dungeons", "spellCasts", "teleportZaap", "teleportZaapi", "teleportPrism", "teleportOther",
    "recallPotions", "havenBag", "modTrips"};
constexpr const char* RecordNames[] = {"biggestHit", "longestSessionMs"};
constexpr const char* TableNames[] = {"spells", "elements", "dungeons", "dimensions"};
static_assert(std::size(CounterNames) == CounterCount && std::size(RecordNames) == RecordCount &&
    std::size(TableNames) == TableCount && std::size(TableLimits) == TableCount);
// A writer waits at most this long for the file to be replaced (another client reading it).
constexpr unsigned ReplaceAttempts = 15;

struct LockBusy : std::runtime_error { using std::runtime_error::runtime_error; };

int64_t Sum(int64_t total, int64_t amount) {
    if (amount <= 0) return total;
    return total > Largest - amount ? Largest : total + amount;
}
// A count read from the file: a non-negative integer, else nothing.
int64_t Natural(const Json& value) {
    if (value.is_number_unsigned()) {
        const auto number = value.get<uint64_t>();
        return number > static_cast<uint64_t>(Largest) ? Largest : static_cast<int64_t>(number);
    }
    if (value.is_number_integer()) return std::max<int64_t>(0, value.get<int64_t>());
    return 0;
}
uint32_t Resets(const Json& object) {
    const auto found = object.find("resets");
    if (found == object.end()) return 0;
    const auto count = Natural(*found);
    return count > std::numeric_limits<uint32_t>::max() ? std::numeric_limits<uint32_t>::max() : static_cast<uint32_t>(count);
}
// The largest entries only (ties: the smaller key stays).
void Bound(std::map<int, int64_t>& table, size_t limit) {
    if (table.size() <= limit) return;
    std::vector<std::pair<int, int64_t>> entries(table.begin(), table.end());
    std::partial_sort(entries.begin(), entries.begin() + static_cast<std::ptrdiff_t>(limit), entries.end(),
        [](const auto& left, const auto& right) { return left.second != right.second ? left.second > right.second : left.first < right.first; });
    entries.resize(limit);
    table = std::map<int, int64_t>(entries.begin(), entries.end());
}
bool Printable(const std::string& name) {
    if (name.empty() || name.size() > MaxNameBytes) return false;
    return std::none_of(name.begin(), name.end(), [](char c) { return static_cast<unsigned char>(c) < 0x20 || c == 0x7F; });
}
void Dates(Sheet& sheet, int64_t now) {
    if (now <= 0) return;
    sheet.first = sheet.first > 0 ? std::min(sheet.first, now) : now;
    sheet.last = std::max(sheet.last, now);
}
Sheet ParseSheet(const Json& item) {
    if (!item.is_object()) throw Corrupt("statistics file: invalid character");
    const auto id = item.find("id");
    if (id == item.end() || !id->is_number_integer() || Natural(*id) <= 0) throw Corrupt("statistics file: invalid character id");
    Sheet sheet;
    sheet.raw = item;
    sheet.id = Natural(*id);
    if (const auto name = item.find("name"); name != item.end() && name->is_string() && Printable(name->get<std::string>()))
        sheet.name = name->get<std::string>();
    if (const auto first = item.find("first"); first != item.end()) sheet.first = Natural(*first);
    if (const auto last = item.find("last"); last != item.end()) sheet.last = Natural(*last);
    sheet.resets = Resets(item);
    if (const auto at = item.find("resetAt"); at != item.end()) sheet.resetAt = Natural(*at);
    const auto values = [&](const char* field, const auto& names, auto& into) {
        const auto found = item.find(field);
        if (found == item.end()) return;
        if (!found->is_object()) throw Corrupt("statistics file: invalid values");
        for (size_t i = 0; i < std::size(names); ++i)
            if (const auto value = found->find(names[i]); value != found->end()) into[i] = Natural(*value);
    };
    values("counters", CounterNames, sheet.counters);
    values("records", RecordNames, sheet.records);
    if (const auto tables = item.find("tables"); tables != item.end()) {
        if (!tables->is_object()) throw Corrupt("statistics file: invalid tables");
        for (size_t i = 0; i < TableCount; ++i) {
            const auto table = tables->find(TableNames[i]);
            if (table == tables->end()) continue;
            if (!table->is_object()) throw Corrupt("statistics file: invalid table");
            for (const auto& [text, value] : table->items()) {
                int key = 0;
                const auto end = text.data() + text.size();
                const auto parsed = std::from_chars(text.data(), end, key);
                const auto amount = Natural(value);
                if (parsed.ec != std::errc() || parsed.ptr != end || amount <= 0) continue;
                sheet.tables[i][key] = Sum(sheet.tables[i][key], amount);
            }
            Bound(sheet.tables[i], TableLimits[i]);
        }
    }
    return sheet;
}
Json SheetJson(const Sheet& sheet) {
    Json item = sheet.raw.is_object() ? sheet.raw : Json::object();
    item["id"] = sheet.id;
    if (sheet.name.empty()) item.erase("name");
    else item["name"] = sheet.name;
    item["first"] = sheet.first;
    item["last"] = sheet.last;
    item["resets"] = sheet.resets;
    if (sheet.resetAt > 0) item["resetAt"] = sheet.resetAt;
    else item.erase("resetAt");
    const auto values = [&](const char* field, const auto& names, const auto& from) {
        Json object = item.contains(field) && item[field].is_object() ? item[field] : Json::object();
        for (size_t i = 0; i < std::size(names); ++i) {
            if (from[i] > 0) object[names[i]] = from[i];
            else object.erase(names[i]);
        }
        item[field] = std::move(object);
    };
    values("counters", CounterNames, sheet.counters);
    values("records", RecordNames, sheet.records);
    Json tables = item.contains("tables") && item["tables"].is_object() ? item["tables"] : Json::object();
    for (size_t i = 0; i < TableCount; ++i) {
        Json table = Json::object();
        for (const auto& [key, amount] : sheet.tables[i]) if (amount > 0) table[std::to_string(key)] = amount;
        if (table.empty()) tables.erase(TableNames[i]);
        else tables[TableNames[i]] = std::move(table);
    }
    item["tables"] = std::move(tables);
    return item;
}

std::filesystem::path With(const std::filesystem::path& path, const wchar_t* suffix) {
    auto result = path;
    result += suffix;
    return result;
}
// Never through a link: a reparse point there is not the store's file.
bool Link(const std::filesystem::path& path) {
    const auto attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT);
}
bool Missing(DWORD error) { return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND; }
uint64_t Ticks(const FILETIME& time) { return (static_cast<uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime; }
// The file's identity, date and size; nullopt when it is not there.
std::optional<FileStamp> StampOf(const std::filesystem::path& path) {
    Handle file(CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    BY_HANDLE_FILE_INFORMATION info{};
    if (!file || !GetFileInformationByHandle(file.value, &info)) return std::nullopt;
    FileStamp stamp;
    stamp.index = (static_cast<uint64_t>(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
    stamp.time = Ticks(info.ftLastWriteTime);
    stamp.size = (static_cast<uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    return stamp;
}
// The book in a file: nullopt when there is none. Corrupt or Newer for its
// content, runtime_error when it cannot be read now. Readers share deletion:
// a writer can replace the file meanwhile.
std::optional<Book> Load(const std::filesystem::path& path) {
    const auto attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        if (Missing(GetLastError())) return std::nullopt;
        throw std::runtime_error("statistics file inaccessible");
    }
    if (attributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) throw Corrupt("statistics file is not a plain file");
    Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!file) {
        if (Missing(GetLastError())) return std::nullopt;
        throw std::runtime_error("statistics file in use");
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file.value, &size)) throw std::runtime_error("statistics file unreadable");
    if (size.QuadPart < 0 || static_cast<uintmax_t>(size.QuadPart) > MaxFileBytes) throw Corrupt("statistics file too large");
    std::string bytes(static_cast<size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    if (!bytes.empty() && (!ReadFile(file.value, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) || read != bytes.size()))
        throw std::runtime_error("statistics file unreadable");
    return Parse(bytes);
}
// The file to update, under its lock: the file, else its backup (a write
// interrupted between its two renames leaves only the backup). An unreadable
// file is kept aside, never overwritten.
Book LoadForUpdate(const std::filesystem::path& path) {
    try {
        if (auto book = Load(path)) return std::move(*book);
    } catch (const Corrupt&) {
        if (!MoveFileExW(path.c_str(), With(path, L".corrupt").c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("unreadable statistics file not moved aside");
    }
    try {
        if (auto backup = Load(With(path, L".bak"))) return std::move(*backup);
    } catch (const Corrupt&) {}
    return {};
}
void Save(const std::filesystem::path& path, const Book& book) {
    const auto bytes = Serialize(book);
    if (bytes.size() > MaxFileBytes) throw std::runtime_error("statistics file too large");
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
            throw std::runtime_error("statistics file not written");
    }
    if (Link(path)) throw std::runtime_error("statistics file is a link");
    // The previous file becomes the backup; without it (in use), the file is replaced anyway.
    const auto backup = With(path, L".bak");
    if (!Link(backup)) MoveFileExW(path.c_str(), backup.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    for (unsigned attempt = 0; ; ++attempt) {
        if (MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) break;
        const auto error = GetLastError();
        if (attempt + 1 >= ReplaceAttempts || (error != ERROR_SHARING_VIOLATION && error != ERROR_LOCK_VIOLATION && error != ERROR_ACCESS_DENIED))
            throw std::runtime_error("statistics file not replaced");
        Sleep(5);
    }
}
// The file's lock: one client at a time reads, merges and writes it.
class FileLock {
public:
    FileLock(const std::filesystem::path& path, unsigned waitMs) {
        const auto lock = With(path, L".lock");
        if (Link(lock)) throw std::runtime_error("statistics lock is a link");
        const auto deadline = GetTickCount64() + waitMs;
        for (;;) {
            handle.value = CreateFileW(lock.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (handle) return;
            const auto error = GetLastError();
            if (error != ERROR_SHARING_VIOLATION && error != ERROR_LOCK_VIOLATION && error != ERROR_ACCESS_DENIED)
                throw std::runtime_error("statistics lock unavailable");
            if (GetTickCount64() >= deadline) throw LockBusy("statistics file in use by another client");
            Sleep(2);
        }
    }
private:
    Handle handle;
};
}

bool Sheet::Empty() const {
    return std::all_of(counters.begin(), counters.end(), [](int64_t value) { return value == 0; }) &&
        std::all_of(records.begin(), records.end(), [](int64_t value) { return value == 0; }) &&
        std::all_of(tables.begin(), tables.end(), [](const auto& table) { return table.empty(); });
}
const Sheet* Book::Find(int64_t id) const {
    for (const auto& sheet : sheets) if (sheet.id == id) return &sheet;
    return nullptr;
}
Sheet* Book::Find(int64_t id) {
    for (auto& sheet : sheets) if (sheet.id == id) return &sheet;
    return nullptr;
}

std::filesystem::path DataPath(const std::filesystem::path& settingsPath) {
    return settingsPath.parent_path() / L"DofusMiniatures-stats.json";
}
Book Parse(const std::string& bytes) {
    Json json;
    try { json = Json::parse(bytes); }
    catch (const Json::exception&) { throw Corrupt("statistics file: invalid content"); }
    if (!json.is_object()) throw Corrupt("statistics file: unknown format");
    const auto schema = json.find("schema");
    if (schema == json.end() || !schema->is_number_integer()) throw Corrupt("statistics file: unknown format");
    if (schema->is_number_unsigned() ? schema->get<uint64_t>() > 1 : schema->get<int64_t>() > 1) throw Newer("statistics file of a later revision");
    if (schema->get<int64_t>() != 1) throw Corrupt("statistics file: unknown format");
    Book book;
    book.raw = json;
    book.raw.erase("characters");
    book.resets = Resets(json);
    if (const auto at = json.find("resetAt"); at != json.end()) book.resetAt = Natural(*at);
    const auto characters = json.find("characters");
    if (characters == json.end()) return book;
    // More than MaxCharacters (a later revision's bound) are all kept: only new characters are refused.
    if (!characters->is_array()) throw Corrupt("statistics file: invalid characters");
    for (const auto& item : *characters) {
        auto sheet = ParseSheet(item);
        // Two sheets of one character (edited by hand): one, with both counts.
        if (auto known = book.Find(sheet.id)) Merge(*known, sheet);
        else book.sheets.push_back(std::move(sheet));
    }
    return book;
}
std::string Serialize(const Book& book) {
    Json json = book.raw.is_object() ? book.raw : Json::object();
    json["schema"] = 1;
    json["resets"] = book.resets;
    if (book.resetAt > 0) json["resetAt"] = book.resetAt;
    else json.erase("resetAt");
    Json characters = Json::array();
    for (const auto& sheet : book.sheets) characters.push_back(SheetJson(sheet));
    json["characters"] = std::move(characters);
    return json.dump(1);
}
void Merge(Sheet& into, const Sheet& counted) {
    for (size_t i = 0; i < CounterCount; ++i) into.counters[i] = Sum(into.counters[i], counted.counters[i]);
    for (size_t i = 0; i < RecordCount; ++i) into.records[i] = std::max(into.records[i], counted.records[i]);
    for (size_t i = 0; i < TableCount; ++i) {
        for (const auto& [key, amount] : counted.tables[i]) into.tables[i][key] = Sum(into.tables[i][key], amount);
        Bound(into.tables[i], TableLimits[i]);
    }
    if (counted.first > 0) Dates(into, counted.first);
    if (counted.last > 0) Dates(into, counted.last);
    if (!counted.name.empty()) into.name = counted.name;
}
void Within(Sheet& sheet) {
    auto& longest = sheet.records[static_cast<size_t>(Record::LongestSessionMs)];
    longest = std::min(longest, sheet.counters[static_cast<size_t>(Counter::PlayMs)]);
}
Totals Total(const std::vector<const Sheet*>& sheets) {
    Totals totals;
    std::array<std::map<int, int64_t>, TableCount> tables;
    std::set<int64_t> counted;
    for (const auto sheet : sheets) {
        if (!sheet) continue;
        for (size_t i = 0; i < CounterCount; ++i) totals.counters[i] = Sum(totals.counters[i], sheet->counters[i]);
        for (size_t i = 0; i < RecordCount; ++i) totals.records[i] = std::max(totals.records[i], sheet->records[i]);
        for (size_t i = 0; i < TableCount; ++i)
            for (const auto& [key, amount] : sheet->tables[i]) tables[i][key] = Sum(tables[i][key], amount);
        if (sheet->first > 0 && (totals.first == 0 || sheet->first < totals.first)) totals.first = sheet->first;
        totals.last = std::max(totals.last, sheet->last);
        if (!sheet->Empty()) counted.insert(sheet->id);
    }
    totals.characters = static_cast<int>(counted.size());
    for (size_t i = 0; i < TableCount; ++i) {
        auto& entries = totals.tables[i];
        entries.assign(tables[i].begin(), tables[i].end());
        std::sort(entries.begin(), entries.end(),
            [](const auto& left, const auto& right) { return left.second != right.second ? left.second > right.second : left.first < right.first; });
    }
    return totals;
}
Totals Total(const Sheet& sheet) {
    auto totals = Total(std::vector<const Sheet*>{&sheet});
    totals.character = sheet.id;
    totals.name = sheet.name;
    return totals;
}

void Store::Open(const std::filesystem::path& file) {
    std::lock_guard guard(mutex);
    path = file;
    saved = {};
    seen = {};
    counted.clear();
    frozen = false;
    stamp = {};
    try {
        const auto present = StampOf(path);
        if (auto book = Load(path)) { saved = std::move(*book); Saw(saved); if (present) stamp = *present; return; }
        // No file: what the backup holds, if a write was interrupted; its first write reads it again.
        if (!present) {
            try { if (auto backup = Load(With(path, L".bak"))) { saved = std::move(*backup); Saw(saved); return; } } catch (...) {}
            Saw(saved);
        }
    } catch (const Newer&) {
        frozen = true;
    } catch (...) {
        // Busy or unreadable: shown from the backup meanwhile, read again under the lock at the first write.
        try { if (auto backup = Load(With(path, L".bak"))) saved = std::move(*backup); } catch (...) {}
    }
}
Sheet* Store::Counted(int64_t id) {
    for (auto& sheet : counted) if (sheet.id == id) return &sheet;
    if (!saved.Find(id)) {
        size_t characters = saved.sheets.size();
        for (const auto& sheet : counted) if (!saved.Find(sheet.id)) ++characters;
        if (characters >= MaxCharacters) return nullptr;
    }
    counted.emplace_back();
    counted.back().id = id;
    return &counted.back();
}
void Store::Add(int64_t id, Counter counter, int64_t amount, int64_t now) {
    const auto index = static_cast<size_t>(counter);
    if (id <= 0 || amount <= 0 || index >= CounterCount) return;
    try {
        std::lock_guard guard(mutex);
        if (auto sheet = Counted(id)) { sheet->counters[index] = Sum(sheet->counters[index], amount); Dates(*sheet, now); }
    } catch (...) {}
}
void Store::Raise(int64_t id, Record record, int64_t value, int64_t now) {
    const auto index = static_cast<size_t>(record);
    if (id <= 0 || value <= 0 || index >= RecordCount) return;
    try {
        std::lock_guard guard(mutex);
        const auto saving = saved.Find(id);
        const auto known = saving && Missed(saved, id) == 0 ? saving : nullptr;
        Sheet* counting = nullptr;
        for (auto& sheet : counted) if (sheet.id == id) counting = &sheet;
        if (record == Record::LongestSessionMs) {
            // Within the time played (Within): a session going on through a
            // reset of this character counts from the reset.
            constexpr auto played = static_cast<size_t>(Counter::PlayMs);
            value = std::min(value, Sum(known ? known->counters[played] : 0, counting ? counting->counters[played] : 0));
            if (value <= 0) return;
        }
        // Only a new record is worth a write.
        if (known && known->records[index] >= value && !counting) return;
        if (auto sheet = counting ? counting : Counted(id)) { sheet->records[index] = std::max(sheet->records[index], value); Dates(*sheet, now); }
    } catch (...) {}
}
void Store::AddTo(int64_t id, Table table, int key, int64_t amount, int64_t now) {
    const auto index = static_cast<size_t>(table);
    if (id <= 0 || amount <= 0 || index >= TableCount) return;
    try {
        std::lock_guard guard(mutex);
        auto sheet = Counted(id);
        if (!sheet) return;
        auto& entries = sheet->tables[index];
        // A full table takes no new key until the next write empties it.
        if (entries.size() >= TableLimits[index] && !entries.count(key)) return;
        entries[key] = Sum(entries[key], amount);
        Dates(*sheet, now);
    } catch (...) {}
}
void Store::Name(int64_t id, const std::string& name) {
    if (id <= 0 || !Printable(name)) return;
    try {
        std::lock_guard guard(mutex);
        for (auto& sheet : counted) if (sheet.id == id) { sheet.name = name; return; }
        // Already in the file: nothing to write.
        if (const auto known = saved.Find(id); known && known->name == name) return;
        if (auto sheet = Counted(id)) sheet->name = name;
    } catch (...) {}
}
bool Store::Pending() const {
    std::lock_guard guard(mutex);
    return !counted.empty();
}
bool Store::Frozen() const {
    std::lock_guard guard(mutex);
    return frozen;
}
Book Store::Saved() const {
    std::lock_guard guard(mutex);
    return saved;
}
// The latest reset of this sheet, or of every sheet, that this client has not
// seen yet (made by another client since this one last read the file): its
// time, Largest when the file does not tell it, 0 when there is none.
int64_t Store::Missed(const Book& book, int64_t id) const {
    if (!seen.known) return 0;
    // No reset at all in the file (an older one, recovered from its backup): nothing to drop.
    const auto at = [](uint32_t resets, int64_t time) { return resets == 0 ? 0 : time > 0 ? time : Largest; };
    int64_t missed = book.resets != seen.book ? at(book.resets, book.resetAt) : 0;
    const auto sheet = book.Find(id);
    const auto known = seen.sheets.find(id);
    const uint32_t resets = sheet ? sheet->resets : 0;
    if (resets != (known != seen.sheets.end() ? known->second : 0)) missed = std::max(missed, at(resets, sheet ? sheet->resetAt : 0));
    return missed;
}
// Counts kept in memory still belong to the file's sheet: no reset was made
// since this client last read the file, or they were all counted after it
// (in the same second counts as before).
bool Store::Current(const Book& book, const Sheet& unwritten) const {
    const auto missed = Missed(book, unwritten.id);
    return missed == 0 || unwritten.first > missed;
}
void Store::Saw(const Book& book) {
    seen.known = true;
    seen.book = book.resets;
    seen.sheets.clear();
    for (const auto& sheet : book.sheets) seen.sheets[sheet.id] = sheet.resets;
}
// Counts that could not be written: back in memory, before what was counted meanwhile.
void Store::Keep(std::vector<Sheet>& kept) {
    for (auto& sheet : counted) {
        Sheet* into = nullptr;
        for (auto& older : kept) if (older.id == sheet.id) into = &older;
        if (into) Merge(*into, sheet);
        else kept.push_back(std::move(sheet));
    }
    counted = std::move(kept);
}
Store::Result Store::Flush(unsigned lockWaitMs) {
    std::vector<Sheet> work;
    std::filesystem::path file;
    {
        std::lock_guard guard(mutex);
        if (frozen) return Result::Newer;
        if (counted.empty()) return Result::Nothing;
        if (path.empty()) return Result::Failed;
        work.swap(counted);
        file = path;
    }
    try {
        FileLock lock(file, lockWaitMs);
        auto book = LoadForUpdate(file);
        {
            std::lock_guard guard(mutex);
            for (const auto& sheet : work) {
                // Erased by another client since this one last read the file: counts older than that are dropped.
                if (!Current(book, sheet)) continue;
                auto into = book.Find(sheet.id);
                if (!into) {
                    if (book.sheets.size() >= MaxCharacters) continue;
                    book.sheets.emplace_back();
                    into = &book.sheets.back();
                    into->id = sheet.id;
                }
                Merge(*into, sheet);
                Within(*into);
            }
        }
        Save(file, book);
        const auto stamped = StampOf(file);
        std::lock_guard guard(mutex);
        saved = std::move(book);
        Saw(saved);
        if (stamped) stamp = *stamped;
        return Result::Written;
    } catch (const Newer&) {
        std::lock_guard guard(mutex);
        frozen = true;
        Keep(work);
        return Result::Newer;
    } catch (const LockBusy&) {
        std::lock_guard guard(mutex);
        Keep(work);
        return Result::Busy;
    } catch (...) {
        try { std::lock_guard guard(mutex); Keep(work); } catch (...) {}
        return Result::Failed;
    }
}
Store::Result Store::Clear(int64_t id, unsigned lockWaitMs, int64_t now) {
    std::vector<Sheet> work;
    std::filesystem::path file;
    {
        std::lock_guard guard(mutex);
        if (frozen) return Result::Newer;
        if (path.empty() || id < 0) return Result::Failed;
        work.swap(counted);
        file = path;
    }
    try {
        FileLock lock(file, lockWaitMs);
        auto book = LoadForUpdate(file);
        {
            std::lock_guard guard(mutex);
            // The other characters' unwritten counts are written in the same go.
            for (const auto& sheet : work) {
                if (id == 0 || sheet.id == id || !Current(book, sheet)) continue;
                auto into = book.Find(sheet.id);
                if (!into) {
                    if (book.sheets.size() >= MaxCharacters) continue;
                    book.sheets.emplace_back();
                    into = &book.sheets.back();
                    into->id = sheet.id;
                }
                Merge(*into, sheet);
                Within(*into);
            }
        }
        if (id == 0) {
            book.sheets.clear();
            book.resets = book.resets == std::numeric_limits<uint32_t>::max() ? 1 : book.resets + 1;
            book.resetAt = std::max<int64_t>(now, 0);
        } else {
            // The character keeps its name, a reset count and its time, nothing else.
            Sheet erased;
            erased.id = id;
            erased.resetAt = std::max<int64_t>(now, 0);
            auto sheet = book.Find(id);
            const uint32_t resets = sheet ? sheet->resets : 0;
            erased.resets = resets == std::numeric_limits<uint32_t>::max() ? 1 : resets + 1;
            if (sheet) { erased.name = sheet->name; *sheet = std::move(erased); }
            else if (book.sheets.size() < MaxCharacters) book.sheets.push_back(std::move(erased));
            else throw std::runtime_error("statistics file full");
        }
        Save(file, book);
        const auto stamped = StampOf(file);
        std::lock_guard guard(mutex);
        // Counted meanwhile (on another thread): kept for the next write, unless erased.
        std::vector<Sheet> later;
        for (auto& sheet : counted) if (id != 0 && sheet.id != id) later.push_back(std::move(sheet));
        counted = std::move(later);
        saved = std::move(book);
        Saw(saved);
        if (stamped) stamp = *stamped;
        return Result::Written;
    } catch (const Newer&) {
        std::lock_guard guard(mutex);
        frozen = true;
        Keep(work);
        return Result::Newer;
    } catch (const LockBusy&) {
        std::lock_guard guard(mutex);
        Keep(work);
        return Result::Busy;
    } catch (...) {
        try { std::lock_guard guard(mutex); Keep(work); } catch (...) {}
        return Result::Failed;
    }
}
void Store::Refresh() {
    try {
        std::filesystem::path file;
        FileStamp known;
        {
            std::lock_guard guard(mutex);
            if (path.empty()) return;
            file = path;
            known = stamp;
        }
        const auto current = StampOf(file);
        if (!current || *current == known) return;
        std::optional<Book> book;
        // Unreadable content or a later revision's: read once until it changes
        // (the next write puts it aside, or finds it); busy: read again next time.
        try { book = Load(file); } catch (const Corrupt&) {} catch (const Newer&) {}
        std::lock_guard guard(mutex);
        stamp = *current;
        if (!book) return;
        // Another client's reset: the unwritten counts older than it go now
        // (as they would at the next write); what is counted from now on is after it.
        std::erase_if(counted, [&](const Sheet& sheet) { return !Current(*book, sheet); });
        saved = std::move(*book);
        Saw(saved);
    } catch (...) {}
}
Totals Store::Show(int64_t id) const {
    std::lock_guard guard(mutex);
    std::vector<const Sheet*> sheets;
    if (id > 0) {
        sheets.push_back(saved.Find(id));
        for (const auto& sheet : counted) if (sheet.id == id && Current(saved, sheet)) sheets.push_back(&sheet);
        auto totals = Total(sheets);
        totals.character = id;
        for (const auto sheet : sheets) if (sheet && !sheet->name.empty()) totals.name = sheet->name;
        return totals;
    }
    for (const auto& sheet : saved.sheets) sheets.push_back(&sheet);
    for (const auto& sheet : counted) if (Current(saved, sheet)) sheets.push_back(&sheet);
    return Total(sheets);
}
}
