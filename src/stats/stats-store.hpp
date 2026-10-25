#pragma once
// « Statistiques »: the store behind stats/stats.hpp, without any game access.
//
// DofusMiniatures-stats.json, beside the shared settings, holds one sheet per
// character id. Each game client (one per character) keeps what it counted
// since its last write in memory and adds it to the file's current content
// under the file's lock (DofusMiniatures-stats.json.lock): no client erases
// another's counts. A write replaces the file atomically and keeps the
// previous one as DofusMiniatures-stats.json.bak; an unreadable file is moved
// aside as DofusMiniatures-stats.json.corrupt and the backup is used instead.
// A file written by a later revision (schema above 1) is never rewritten.
// Erasing counts bumps a reset counter and records its time (the file's, or
// the sheet's): a client drops its unwritten counts that predate a reset made
// by another client (all of a sheet's when they straddle it), as soon as it
// reads the file again, and keeps those counted after it.
//
// The spells (1.16.1). 1.15.0 to 1.16.0 counted under "spellCasts" and
// "spells" every spell the server casts in a character's name, its
// equipment's effects too (the Dofus Pourpre's « Pourpre Profond » topped the
// spells). Only a class's own spells count now, under keys of their own,
// "classSpellCasts" and "classSpells": the file's format marker. An older
// client never writes them and keeps them as they are (unknown fields), so
// whatever is found under the old keys, 1.15.0 to 1.16.0's counts or what an
// older client still running added since, is still to convert: a write turns
// the old spells whose kind the game told (Classify) into new ones, a class's
// spell kept with its casts and any other dropped, and removes the old casts
// counter, in the same write under the lock. The old counts are thus
// converted exactly once, whichever client writes first; one whose kind is
// not known yet waits under its old key, never shown.
//
// The character's own class (1.19.0). 1.16.1 to 1.18.0 counted any class's
// spell cast in the character's name, another class's too: the Steamer's
// « Surtension » that a « Bribe de Steamer » casts in its bearer's name, for
// instance. Only the character's own class's spells count now
// (stats-fights.cpp). The sheets counted before are checked once, in the
// first write after this client knows the sheet's character's class (Played:
// the character played here) and whose spell each of its "classSpells" is
// (Classify): the other classes' spells are dropped with their casts, then
// "spellsChecked" marks the sheet, never checked again (a character that
// changes class later keeps its former spells). Until then, the sheet is shown
// as it is.
#include "stats/stats.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <map>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

namespace stats {
// Bounds: characters in the file, bytes of a name, entries per table (spells,
// elements, dungeons, worlds) and bytes of the file.
constexpr size_t MaxCharacters = 256, MaxNameBytes = 96;
constexpr size_t TableLimits[TableCount] = {512, 16, 256, 128};
constexpr uintmax_t MaxFileBytes = 4u * 1024 * 1024;

// One character's statistics, or what a client counted for it since its last write.
struct Sheet {
    int64_t id = 0;
    std::string name;
    int64_t first = 0, last = 0;  // Unix time in seconds; 0: nothing counted
    uint32_t resets = 0;          // times this character's statistics were erased
    int64_t resetAt = 0;          // the last time, Unix time in seconds; 0: unknown
    std::array<int64_t, CounterCount> counters{};
    std::array<int64_t, RecordCount> records{};
    std::array<std::map<int, int64_t>, TableCount> tables{};
    // 1.15.0 to 1.16.0's spells ("spells", every spell cast in the character's
    // name) still to convert, and whether their casts counter ("spellCasts") is there.
    std::map<int, int64_t> legacy;
    bool legacyCasts = false;
    // Its spells were checked against its character's class ("spellsChecked", Check).
    bool checked = false;
    // The sheet as read from the file: fields of a later revision are written back unchanged.
    nlohmann::json raw = nlohmann::json::object();
    bool Empty() const;
};
// The whole file.
struct Book {
    uint32_t resets = 0;  // times every character's statistics were erased
    int64_t resetAt = 0;  // the last time, Unix time in seconds; 0: unknown
    std::vector<Sheet> sheets;
    nlohmann::json raw = nlohmann::json::object();
    const Sheet* Find(int64_t id) const;
    Sheet* Find(int64_t id);
};
// Content that is not a statistics file (moved aside, then the backup is used).
struct Corrupt : std::runtime_error { using std::runtime_error::runtime_error; };
// A file of a later revision: kept as it is, never rewritten.
struct Newer : std::runtime_error { using std::runtime_error::runtime_error; };

std::filesystem::path DataPath(const std::filesystem::path& settingsPath);
// Corrupt for anything that is not a schema-1 book, Newer above schema 1.
// Values are bounded: negative counts are dropped, tables keep their largest entries.
Book Parse(const std::string& bytes);
std::string Serialize(const Book& book);
// Adds what was counted: sums, maxima, the earliest first and latest last
// dates, the newest name. Saturating; tables stay within their limits.
void Merge(Sheet& into, const Sheet& counted);
// Converts a sheet's old spells whose kind is known (spell id -> its class,
// stats::ClassOf; NoClass for none): a class's spell joins the spells with its
// casts, any other is dropped, an unknown one waits; the old casts counter
// goes (it counted the dropped ones too). Nothing else changes. True when the
// sheet changed.
bool Convert(Sheet& sheet, const std::unordered_map<int, int>& kinds);
// Whether Check can check this sheet now: not checked yet, its character's
// class known (above 0) and the class of each of its spells too.
bool Checkable(const Sheet& sheet, int characterClass, const std::unordered_map<int, int>& kinds);
// Checks a Checkable sheet once: the spells of another class than its
// character's (or of none) are dropped, their casts taken from the casts
// counter (1.16.1 to 1.18.0 added each cast to both alike), and the sheet is
// marked checked. Nothing else changes. True when the sheet changed.
bool Check(Sheet& sheet, int characterClass, const std::unordered_map<int, int>& kinds);
// A session is never longer than the time played counted: one that went on
// through a reset of its sheet (here or in another client) counts from it.
void Within(Sheet& sheet);
// Sums of every sheet (records: the largest), or one sheet alone.
Totals Total(const Sheet& sheet);
Totals Total(const std::vector<const Sheet*>& sheets);
// A file's identity: every write makes a new file (another index), so another
// client's write is seen even within the file system's time resolution.
struct FileStamp {
    uint64_t index = 0, time = 0, size = 0;
    bool operator==(const FileStamp&) const = default;
};

class Store {
public:
    enum class Result { Written, Nothing, Busy, Failed, Newer };
    // Reads the file for the first time; never throws (a missing, busy or
    // unreadable file is read again at the first write).
    void Open(const std::filesystem::path& path);
    const std::filesystem::path& Path() const { return path; }
    // What a client counted; never throws. Ignored for character 0, a
    // non-positive amount or a 257th character.
    void Add(int64_t id, Counter counter, int64_t amount, int64_t now);
    void Raise(int64_t id, Record record, int64_t value, int64_t now);
    void AddTo(int64_t id, Table table, int key, int64_t amount, int64_t now);
    void Name(int64_t id, const std::string& name);
    // Unwritten counts, or old spells in the file whose kind is known now, or
    // a sheet to check (Converting: a write converts or checks them).
    bool Pending() const;
    bool Converting() const;
    // The spells of the file as last read whose class is not known yet (at
    // most this many, smallest ids first): the old ones, and those of a sheet
    // to check whose character's class is known. What the game's data says of
    // one: its class (stats::ClassOf), NoClass for none. Never throws.
    std::vector<int> Unsorted(size_t most) const;
    void Classify(int spell, int spellClass);
    // The class of a character played in this client (stats::ClassOfCharacter),
    // for its sheet's check. Ignored for character 0 or a class below 1. Never throws.
    void Played(int64_t id, int characterClass);
    // Adds the unwritten counts to the file under its lock, waiting for it at
    // most lockWaitMs, and converts its old spells whose kind is known (only
    // that when nothing is counted). Busy, Failed or Newer keep them for the
    // next try; Nothing when there was nothing to write after all.
    Result Flush(unsigned lockWaitMs);
    // Erases one character's statistics (its unwritten counts too), or every
    // character's for id 0, under the file's lock; now (Unix time in seconds)
    // tells the other clients which of their unwritten counts came after it.
    Result Clear(int64_t id, unsigned lockWaitMs, int64_t now);
    // Re-reads the file without its lock when another client replaced it, for
    // its latest counts: a reset made there drops the unwritten counts that
    // predate it at once, and what is counted from then on is written. Never
    // throws.
    void Refresh();
    // One character's statistics, or every character's sums for id 0.
    Totals Show(int64_t id) const;
    // A later revision's file was found: nothing is written any more.
    bool Frozen() const;
    // What the file held at the last read or write (tests).
    Book Saved() const;
private:
    struct Seen { bool known = false; uint32_t book = 0; std::unordered_map<int64_t, uint32_t> sheets; };
    Sheet* Counted(int64_t id);
    int64_t Missed(const Book& book, int64_t id) const;
    bool Current(const Book& book, const Sheet& unwritten) const;
    void Keep(std::vector<Sheet>& kept);
    void Saw(const Book& book);
    int PlayedClass(int64_t id) const;
    bool Convertible(const Book& book) const;
    bool ConvertAll(Book& book) const;
    mutable std::mutex mutex;
    std::filesystem::path path;
    Book saved;
    std::vector<Sheet> counted;
    Seen seen;
    std::unordered_map<int, int> kinds;         // spell id -> its class, NoClass for none (Classify)
    std::unordered_map<int64_t, int> classes;  // character id -> its class (Played)
    bool frozen = false;
    FileStamp stamp;  // the file as last read or written
};

// What the store reads from the game (stats.cpp): the native client, or a
// scripted one in tests. Main thread.
struct Game {
    virtual ~Game() = default;
    // The character played in this client, its name and its class (breed id,
    // 0 while unknown); 0 while unknown.
    virtual int64_t Character(std::string& name, int& characterClass) = 0;
    // A spell's, dungeon's or world's name in the game's data; empty when unknown.
    virtual std::string KeyName(Table table, int key) = 0;
    // Whose spell this is in the game's data (stats.hpp, ClassOf); may throw.
    virtual int Class(int spell) = 0;
};
void UseGame(Game* game);  // nullptr: the native client
Store& ModuleStore();      // the store of stats.hpp's functions (tests)
}
