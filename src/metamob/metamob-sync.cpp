#include "metamob/metamob-sync.hpp"
#include "souls/chat-soul-data.hpp"
#include "metamob/metamob-data.hpp"
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

void Log(const std::string& text);

// The game thread queues captures and key operations; one detached worker at a
// time takes the whole state ("book"), runs Work (key operations, then one sync
// cycle, or the chat part's reading of the quest and the calendar's reading of
// the Kralamoure openings; plain C++ only: no IL2CPP call), and hands it back to
// TickSync, which posts its notices and keeps what it learnt of the stored key
// for the options' key row, of the quest for the chat and of the openings for
// the calendar. Logs carry fixed codes and numbers only: never the key, a
// header, a body, the username, a slug, a monster or a character name, an
// opening's id, creator or description.
namespace metamob {
namespace {
// Visible texts: local chat notices, prefix included.
constexpr char KeyInvalid[] = "Metamob : clé invalide (lettres, chiffres et symboles, sans espace).",
    KeyRejected[] = "Metamob : clé refusée par Metamob, rien n’a été enregistré.",
    KeyUnverified[] = "Metamob : clé enregistrée ; Metamob est injoignable pour la vérifier.",
    KeyNotStored[] = "Metamob : impossible d’enregistrer la clé sur ce PC.",
    KeyForgotten[] = "Metamob : clé oubliée.", KeyNotForgotten[] = "Metamob : impossible d’oublier la clé sur ce PC.",
    KeyRefused[] = "Metamob : clé refusée (révoquée ou régénérée ?). Supprime-la avec la corbeille dans les options, puis enregistre une nouvelle clé.",
    KeyUnreadable[] = "Metamob : clé illisible sur ce compte Windows. Supprime-la avec la corbeille dans les options, puis enregistre-la à nouveau.";
std::string Quoted(const std::string& name) { return "« " + name + " »"; }
std::string Number(int value) { return std::to_string(value); }
std::string SavedText(const std::string& user) { return "Metamob : clé enregistrée pour le compte " + Quoted(user) + "."; }
// One write confirmed: « +1 « Rat Noir » (de 2 à 3) ». The writes a cycle
// confirms one after the other (a dungeon-room soul: its boss and the bosses of
// its room) are told in one notice: « Metamob : +1 « Sphincter Cell » (de 3 à
// 4), +1 « Rat Blanc » (de 0 à 1), +1 « Rat Noir » (de 2 à 3). »
std::string AddedPart(const std::string& name, int before, int after) {
    return "+" + Number(after - before) + " " + Quoted(name) + " (de " + Number(before) + " à " + Number(after) + ")";
}
std::string AddedText(const std::vector<std::string>& parts) {
    std::string text = "Metamob : ";
    for (size_t i = 0; i < parts.size(); ++i) text += (i ? ", " : "") + parts[i];
    return text + ".";
}
// « « A » », « « A » et « B » », « « A », « B » et « C » ».
std::string Names(const std::vector<std::string>& names) {
    std::string text;
    for (size_t i = 0; i < names.size(); ++i) text += (i == 0 ? "" : i + 1 == names.size() ? " et " : ", ") + Quoted(names[i]);
    return text;
}
std::string NoKeyText(const std::vector<std::string>& names) {
    return names.size() == 1 ?
        "Metamob : capture de " + Names(names) + " non synchronisée : aucune clé enregistrée (Options › Dofus Enhanced › Pierre d’âme)." :
        "Metamob : captures de " + Names(names) + " non synchronisées : aucune clé enregistrée (Options › Dofus Enhanced › Pierre d’âme).";
}
std::string NoKeyText(const std::string& name) { return NoKeyText(std::vector<std::string>{name}); }
std::string UnmatchedText(const std::string& name) { return "Metamob : " + Quoted(name) + " introuvable dans ta quête Ocre, rien n’a été modifié."; }
std::string CappedText(const std::string& name) {
    return "Metamob : " + Quoted(name) + " est déjà à " + Number(MaxQuantity) + ", la quantité maximale.";
}
std::string UnreachableText(int pending) {
    return "Metamob injoignable : " + Number(pending) + " capture(s) en attente, nouvel essai automatique.";
}
std::string ChangedText(const std::string& name, int before, int after) {
    return "Metamob : " + Quoted(name) + " a changé pendant l’envoi (de " + Number(before) + " à " + Number(after) + ") ; vérifie ta quête sur Metamob.";
}
std::string RefusedText(const std::string& name) { return "Metamob : mise à jour refusée pour " + Quoted(name) + "."; }
// Given up while an earlier send of it may have landed: never said refused
// (entered by hand, it would count twice).
std::string UnsureText(const std::string& name) {
    return "Metamob : envoi de " + Quoted(name) + " non confirmé ; vérifie ta quête sur Metamob.";
}
// The sync turned off in the options: what waits is dropped, never kept for later
// (the user may enter it by hand meanwhile); a write that may have landed is named.
std::string DroppedText(int pending) {
    return "Metamob : synchronisation désactivée, " + Number(pending) + " capture(s) en attente abandonnée(s), rien n’a été envoyé.";
}
std::string OffUnsureText(const std::string& name) {
    return "Metamob : synchronisation désactivée pendant l’envoi de " + Quoted(name) + " ; vérifie ta quête sur Metamob.";
}
// Likewise when the key goes (forgotten here or by another client): a write that
// may have landed is never said unsynchronised, it would be entered twice.
std::string ForgetUnsureText(const std::string& name) {
    return "Metamob : clé oubliée pendant l’envoi de " + Quoted(name) + " ; vérifie ta quête sur Metamob.";
}

// A fight's captures go out together (2 s after the last one), at most one
// request every 1.1 s from this process, slower and slower while Metamob fails.
constexpr uint64_t DebounceMs = 2000, SpacingMs = 1100, SenderWaitMs = 10000, BusyMs = 2000, PauseCheckMs = 30000,
    BackoffMs = 30000, BackoffCapMs = 15 * 60 * 1000, ErrorLogMs = 60000, RefusedRetryMs = 10 * 60 * 1000;
constexpr int DefaultRetryAfter = 60, MaxPending = 500, MaxPages = 10, CycleRequests = 30, MaxNotices = 32;
// « Metamob dans le chat »: the quest's list is read again at most every 10 min,
// only while a chat label asked for it within the last minute; the key file is
// looked at again at most every second for it.
constexpr uint64_t RefreshMs = 10 * 60 * 1000, WantedMs = 60000, StatusMs = 1000;
constexpr size_t MaxWrites = 64;
constexpr wchar_t SenderName[] = L"Local\\DofusMiniatures.Metamob";
constexpr char QuestsRoute[] = "/quests", ListRoute[] = "/quests/*/monsters", WriteRoute[] = "/quests/*/monsters/*";
// « Ouvertures Kralamoure »: the calendar's list, asked from a week before this
// PC's date (the week shown when the calendar opens is read whole, whatever
// day the answer's dates fall on in UTC).
constexpr char KraloveRoute[] = "/kralove";
constexpr int64_t KraloveDaysBack = 7;
// The calendar asks at every pump while its row is shown: a second without it
// is a closed calendar, never read for.
constexpr uint64_t KraloveWantedMs = 1000;

uint64_t RealNow() { return GetTickCount64(); }
int64_t RealEpoch() {
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    const auto ticks = (static_cast<uint64_t>(now.dwHighDateTime) << 32) | now.dwLowDateTime;
    return static_cast<int64_t>(ticks / 10000000) - 11644473600; // FILETIME counts 100 ns from 1601
}
void RealSleep(uint64_t milliseconds) { Sleep(static_cast<DWORD>(milliseconds)); }
// Replaced by the tests only (fake time, no real waiting), never while a worker runs.
struct Clock { uint64_t (*now)() = RealNow; int64_t (*epoch)() = RealEpoch; void (*sleep)(uint64_t) = RealSleep; };
Clock timing;
std::atomic<uint64_t> lastRequest{0}; // end of this process's last request, whichever worker sent it

// Plaintext copies of the key are overwritten before release. A string that
// grew past its inline buffer keeps its first bytes there: wiped too.
void Wipe(std::string& text) noexcept {
    text.resize(text.capacity()); SecureZeroMemory(text.data(), text.size()); text.clear();
    text.shrink_to_fit();
    text.resize(text.capacity()); SecureZeroMemory(text.data(), text.size()); text.clear();
}
// FNV-1a: tells keys apart without keeping one, names a flight without its slug.
uint64_t Hash(std::string_view bytes) {
    uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char c : bytes) { hash ^= c; hash *= 1099511628211ULL; }
    return hash;
}
// Tells keys apart (a refused key, a new one saved by another client) without keeping one.
uint64_t Fingerprint(const Secret& key) { return Hash(key.value()); }
// A monster name in a chat notice (rich text): no control character, no markup, bounded.
std::string Shown(std::string_view name) {
    std::string shown;
    for (const char c : name)
        if (static_cast<unsigned char>(c) >= 32 && c != 127 && c != '<' && c != '>') shown += c;
    if (shown.size() > 128) {
        size_t end = 128;
        while (end > 0 && (static_cast<unsigned char>(shown[end]) & 0xC0) == 0x80) --end; // never half a character
        shown.resize(end);
    }
    return shown.empty() ? std::string("?") : shown;
}

// Captures of one monster by one character, not sent yet.
struct Group {
    std::string character, name; int64_t monster = 0; int type = 0, count = 0, stale = 0;
    uint64_t waitUntil = 0; // held by another client's unsettled write of this monster: checked again then
};
// A write without a confirmed result. Its absolute target was kept before
// sending: a fresh read decides (at the target: done; at the base: the same
// target again; anything else: a conflict). Never +n on a value that may hold it.
struct Flight {
    Group group; std::string slug, shown; int64_t id = 0; int step = 0, base = 0, target = 0, misses = 0;
    bool uncertain = false;     // a send of this target may have reached Metamob: it may land at any time
    std::shared_ptr<void> mark; // Announce, from its first unknown result: closed with the flight (or its process)
};
// A write that may have been received is announced to this PC's other game
// clients by a named event until it is settled: none of them writes that quest
// monster meanwhile, so the value its owner reads back can only be its own
// write. Named after a hash: no slug in the name.
std::wstring FlightName(const std::string& slug, int64_t id) {
    const auto hash = Hash(slug + "/" + std::to_string(id));
    std::wstring name = L"Local\\DofusMiniatures.Metamob.";
    for (int shift = 60; shift >= 0; shift -= 4) name += L"0123456789abcdef"[(hash >> shift) & 15];
    return name;
}
std::shared_ptr<void> Announce(const std::string& slug, int64_t id) {
    const HANDLE event = CreateEventW(nullptr, TRUE, FALSE, FlightName(slug, id).c_str());
    if (!event) { Log("METAMOB_SENDER_ERROR"); return {}; }
    return std::shared_ptr<void>(event, CloseHandle);
}
bool Announced(const std::string& slug, int64_t id) {
    const HANDLE event = OpenEventW(SYNCHRONIZE, FALSE, FlightName(slug, id).c_str());
    if (!event) return GetLastError() == ERROR_ACCESS_DENIED; // there all the same, another client's rights
    CloseHandle(event);
    return true;
}
// The key file as seen: absent, or its size and dates (never its content). A
// status learnt with one key file holds for that file only.
struct KeyFile {
    bool present = false;
    uint64_t size = 0, created = 0, written = 0;
    bool operator==(const KeyFile&) const = default;
};
// nullopt: a Windows error other than a missing file, nothing to conclude.
std::optional<KeyFile> Identity(const std::filesystem::path& path) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        const auto error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return KeyFile{};
        return std::nullopt;
    }
    const auto ticks = [](const FILETIME& time) { return (static_cast<uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime; };
    return KeyFile{true, (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow, ticks(data.ftCreationTime),
        ticks(data.ftLastWriteTime)};
}
// What is known of one key file: its status for the key row.
struct Known { KeyFile file; KeyStatus status = KeyStatus::None; };
// Everything a cycle reads and updates. The game thread owns it while no worker runs.
struct Book {
    std::vector<Group> groups;
    std::vector<Flight> flights;
    std::optional<std::vector<Quest>> quests;                   // GET /quests, kept while it picks a quest
    uint64_t questsAt = 0;                                      // when they were read (the calendar reads them again after 10 min)
    std::map<std::string, std::vector<QuestMonster>> monsters;  // per quest slug: ids and steps (quantities are read again)
    int failures = 0;           // consecutive transient failures
    uint64_t notBefore = 0;     // backoff, Retry-After, a busy sender, the refused key check
    bool refused = false;       // a capture's request refused (Metamob's JSON 401/403): the captures paused, told once; the chat part ignores it
    uint64_t refusedKey = 0;    // fingerprint of the refused key
    uint64_t retryRefused = 0;  // when that same key is tried again (one request, no new notice)
};
int Count(const std::vector<Group>& groups) {
    int units = 0;
    for (const auto& group : groups) units += group.count;
    return units;
}
int Units(const Book& book) {
    int units = Count(book.groups);
    for (const auto& flight : book.flights) units += flight.group.count;
    return units;
}
void Merge(std::vector<Group>& groups, Group group) {
    for (auto& known : groups)
        if (known.monster == group.monster && known.type == group.type && known.character == group.character) {
            known.count += group.count;
            return;
        }
    groups.push_back(std::move(group));
}
// A new key may be another account's: nothing learnt with the previous one is kept.
void Fresh(Book& book) {
    book.quests.reset(); book.monsters.clear();
    book.failures = 0; book.notBefore = 0; book.refused = false; book.refusedKey = 0; book.retryRefused = 0;
}
// A stopped job's book, emptied without allocating: the announcements of its flights close.
void Empty(Book& book) noexcept { book.groups.clear(); book.flights.clear(); book.quests.reset(); book.monsters.clear(); }

// The chat part's reading: the character's quest and its whole list with the
// quantities, or why not (handed to the game thread with the job, kept there).
struct Listed {
    std::optional<KeyFile> file;      // the key file it was read with (read before the key)
    std::string character, slug;      // the character the quest was chosen for; that quest
    std::vector<QuestMonster> monsters;
    bool ok = false;                  // read
    QuestProblem problem = QuestProblem::None; // no Unity Ocre quest, or several: read again 10 min later
};
// A write of the sync confirmed by Metamob: the chat part shows its quantity at once.
struct Written { std::string slug; int64_t id = 0; int quantity = 0; };
// The calendar's reading of the Kralamoure openings, handed to the game thread
// with the job like the chat part's (Loading: nothing learnt, no key then).
struct KraloveRead {
    std::optional<KeyFile> file;      // the key file it was read with (read before the key)
    std::string character;            // the character the quest was chosen for
    KraloveStatus status = KraloveStatus::Loading;
    std::string server;               // that quest's server
    int64_t fromDay = 0;              // the first day asked
    std::vector<Kralove> openings;    // that server's only
};
struct Job {
    Book book;
    std::optional<Secret> submit;   // a key to verify with GET /me, then store
    bool forget = false, cycle = false, submitting = false;
    bool listing = false;           // read the chat part's list (never while a capture waits)
    std::string character;          // the local character, which picks that quest
    std::optional<Listed> listed;   // what that reading found, taken once the worker has ended
    bool kralove = false;           // read the calendar's openings (never while a capture waits)
    int64_t kraloveFromDay = 0;     // from that day (DayNumber), sent as kraloveFrom (YYYY-MM-DD)
    std::string kraloveFrom;
    std::optional<KraloveRead> kraloveRead; // what that reading found, taken once the worker has ended
    std::optional<KeyFile> check;   // the key row's check of this key file (CheckStoredKey)
    bool told = false;              // that check told the key's status (read once the worker has ended)
    std::filesystem::path keyPath;
    Transport transport;
    std::atomic_bool cancelled{false}, done{false};
    // Asked by the game thread, read by the cycle only, never by the transport: a
    // request under way ends normally, so a write Metamob has applied is confirmed.
    std::atomic_bool off{false},    // the sync was turned off: nothing more is written
        forgetting{false};          // the trash then « Oui »: nothing more is sent with the key
    std::mutex lock;
    std::vector<std::string> notices; // taken by TickSync, posted on the game thread
    std::optional<Known> found;       // the stored key's status learnt last, taken by TickSync like the notices
    std::vector<Written> written;     // the sync's confirmed writes, taken by TickSync like the notices
    // Confirmed writes not told yet (AddedPart): told in one notice before any
    // other notice of the job, else when it ends (Work). Under the lock.
    std::vector<std::string> added;
    void Told() {
        if (added.empty()) return;
        if (notices.size() < MaxNotices) notices.push_back(AddedText(added));
        added.clear();
    }
    void Post(std::string text) {
        std::lock_guard<std::mutex> guard(lock);
        Told();
        if (notices.size() < MaxNotices) notices.push_back(std::move(text));
    }
    void Added(std::string part) {
        std::lock_guard<std::mutex> guard(lock);
        added.push_back(std::move(part));
    }
    void Confirmed(Written write) {
        std::lock_guard<std::mutex> guard(lock);
        if (written.size() < MaxWrites) written.push_back(std::move(write));
    }
    // What an answer or a load told of the stored key, for the key file read
    // before it (not read: nothing can be tied to it). The latest one counts.
    void Found(KeyStatus status, const std::optional<KeyFile>& file) {
        if (!file) return;
        std::lock_guard<std::mutex> guard(lock);
        found = Known{*file, status};
    }
};
// The game thread asked the cycle to end early: the sync turned off (no write
// any more) or the key being forgotten (no request any more).
bool Ending(const Job& job) { return job.off.load() || job.forgetting.load(); }

// One request, at least 1.1 s after this process's previous one. A transport
// that throws gave no answer (for a write: maybe received, so unknown).
Response Send(Job& job, const Secret& key, const char* method, std::string path, const char* route, std::string body = {}) {
    const bool write = std::string_view(method) == "PATCH";
    const auto last = lastRequest.load();
    for (auto now = timing.now(); !job.cancelled.load() && now >= last && now - last < SpacingMs; now = timing.now())
        timing.sleep(std::min<uint64_t>(50, SpacingMs - (now - last)));
    Response response;
    if (job.cancelled.load()) { response.error = ERROR_CANCELLED; return response; }
    try { response = job.transport(Request{method, std::move(path), std::move(body)}, key, job.cancelled); }
    catch (...) { response = Response{}; response.sent = write; response.error = ERROR_INTERNAL_ERROR; }
    lastRequest.store(timing.now());
    if (response.status) Log(std::string("METAMOB_HTTP method=") + method + " route=" + route + " status=" + std::to_string(response.status));
    else Log(std::string("METAMOB_NET method=") + method + " route=" + route + " error=" + std::to_string(response.error) +
        " sent=" + (response.sent ? "1" : "0"));
    return response;
}

struct Halt {};      // the cycle stops: its wait (pause, Retry-After, backoff) is already recorded
struct QuestGone {}; // 404 on a quest path: the quest is looked for again, once per cycle
struct Session {
    Job& job;
    Book& book;
    const Secret& key;
    std::optional<KeyFile> file;       // read before the key: what the answers tell holds for it
    int requests = 0, done = 0;
    bool rediscovered = false, askQuestsAgain = false, accepted = false;
    std::vector<std::string> problems; // quest choice notices already given in this cycle
    QuestProblem questProblem = QuestProblem::None; // why the last quest choice found none
    bool listing = false, refused = false; // the chat part's silent reading; Metamob refused its key
    bool limited = false, bad = false;     // a 429 (Limit), an answer that does not read (BadAnswer): why a silent reading halted
};
// A 2xx answer to the stored key: Metamob accepts it (told to the key row once per cycle).
void Accepted(Session& s) {
    if (!std::exchange(s.accepted, true)) s.job.Found(KeyStatus::Verified, s.file);
}
// Metamob's own refusal of the key is JSON ({"message":...}); a 401/403 page that
// is not (a Cloudflare challenge, a filtering proxy) says nothing about the key.
bool KeyRefusal(const Response& response) { return (response.status == 401 || response.status == 403) && response.json; }
// Network errors, 5xx, a page that is not JSON (Cloudflare), an unreadable answer:
// 30 s, then twice longer each time up to 15 min; told at the 3rd in a row.
[[noreturn]] void Backoff(Session& s) {
    // The chat part's reading only stops, untold: the game thread spaces its next
    // try, so it neither pauses the captures nor hides their third failure.
    if (s.listing) throw Halt{};
    auto& book = s.book;
    book.failures = std::min(book.failures + 1, 1000);
    book.notBefore = timing.now() + std::min<uint64_t>(BackoffCapMs, BackoffMs << std::min(book.failures - 1, 5));
    if (book.failures == 3) s.job.Post(UnreachableText(Units(book)));
    throw Halt{};
}
// The key refused: the captures are kept until it changes; said once. The same
// key is tried again every 10 min (refused again: no new notice).
[[noreturn]] void Refuse(Session& s) {
    if (s.listing) {
        // Silent: the key row says it and the chat part stops with that key; a
        // capture still tells it at its first request.
        s.job.Found(KeyStatus::Refused, s.file);
        s.refused = true;
        throw Halt{};
    }
    if (!s.book.refused) { s.job.Post(KeyRefused); Log("METAMOB_KEY_REFUSED"); }
    s.job.Found(KeyStatus::Refused, s.file); // until Metamob accepts it again or another key is saved
    s.book.refused = true;
    s.book.refusedKey = Fingerprint(s.key);
    s.book.notBefore = timing.now() + PauseCheckMs;
    s.book.retryRefused = timing.now() + RefusedRetryMs;
    throw Halt{};
}
[[noreturn]] void Limit(Session& s, const Response& response) {
    const int seconds = RetryAfterSeconds(response.retryAfter, timing.epoch()).value_or(DefaultRetryAfter);
    s.book.notBefore = timing.now() + static_cast<uint64_t>(seconds) * 1000;
    Log("METAMOB_RATE_LIMIT seconds=" + std::to_string(seconds));
    s.limited = true;
    throw Halt{};
}
[[noreturn]] void BadAnswer(Session& s, const char* route) {
    Log(std::string("METAMOB_BAD_ANSWER route=") + route);
    s.bad = true;
    Backoff(s);
}
// The body of a 2xx JSON answer; anything else stops the cycle.
std::string Get(Session& s, std::string path, const char* route, bool questPath) {
    if (s.job.forgetting.load()) throw Halt{}; // nothing more is sent with a key being forgotten
    ++s.requests;
    auto response = Send(s.job, s.key, "GET", std::move(path), route);
    if (s.job.cancelled.load()) throw Halt{};
    if (response.status >= 200 && response.status < 300 && response.json) { Accepted(s); return std::move(response.body); }
    if (KeyRefusal(response)) Refuse(s);
    if (response.status == 429) Limit(s, response);
    if (response.status == 404 && questPath) throw QuestGone{};
    Backoff(s);
}
// The parsers' errors are fixed texts, but none of them is logged either.
template<class Parse> auto Parsed(Session& s, const char* route, Parse parse) {
    try { return parse(); } catch (...) { BadAnswer(s, route); }
}
// The character's quest. No quest or an ambiguous choice: told once per cycle
// (never by the chat part's or the calendar's reading), and the list is asked
// again next time (the user may fix it meanwhile).
std::optional<Quest> ChosenQuest(Session& s, const std::string& character) {
    if (!s.book.quests) {
        const auto body = Get(s, "/quests", QuestsRoute, false);
        s.book.quests = Parsed(s, QuestsRoute, [&] { return ParseQuests(body); });
        s.book.questsAt = timing.now();
    }
    auto choice = ChooseQuest(*s.book.quests, character);
    if (choice.quest) return std::move(choice.quest);
    s.questProblem = choice.kind;
    if (!s.listing && std::find(s.problems.begin(), s.problems.end(), choice.problem) == s.problems.end()) {
        s.job.Post(choice.problem);
        s.problems.push_back(std::move(choice.problem));
    }
    s.askQuestsAgain = true;
    return std::nullopt;
}
std::optional<std::string> QuestSlug(Session& s, const std::string& character) {
    auto quest = ChosenQuest(s, character);
    if (!quest) return std::nullopt;
    return std::move(quest->slug);
}
// The quest's whole monster list with its quantities (Unity Ocre: 337, two
// pages). complete (the chat part): a page missing before the total is a bad
// answer, never a shorter list.
std::vector<QuestMonster> ReadList(Session& s, const std::string& slug, bool complete = false) {
    std::vector<QuestMonster> monsters;
    int offset = 0, total = 0;
    for (int pages = 0;; ++pages) {
        if (pages == MaxPages) BadAnswer(s, ListRoute);
        const auto body = Get(s, "/quests/" + slug + "/monsters?limit=200&offset=" + std::to_string(offset), ListRoute, true);
        auto page = Parsed(s, ListRoute, [&] { return ParseMonsters(body); });
        if (page.slug != slug || page.offset != offset) BadAnswer(s, ListRoute);
        offset += static_cast<int>(page.monsters.size());
        total = page.total;
        monsters.insert(monsters.end(), std::make_move_iterator(page.monsters.begin()), std::make_move_iterator(page.monsters.end()));
        if (page.monsters.empty() || offset >= page.total) break;
    }
    if (complete && offset != total) BadAnswer(s, ListRoute);
    return monsters;
}
// Kept for the session: the sync reads its ids and steps (quantities are read again).
const std::vector<QuestMonster>& Monsters(Session& s, const std::string& slug) {
    if (const auto known = s.book.monsters.find(slug); known != s.book.monsters.end()) return known->second;
    return s.book.monsters.emplace(slug, ReadList(s, slug)).first->second;
}
// The monster's quantity read again right before a write; nullopt when its step does not list it.
std::optional<int> Current(Session& s, const std::string& slug, int64_t id, int step) {
    const auto body = Get(s, "/quests/" + slug + "/monsters?step=" + std::to_string(step) + "&limit=200", ListRoute, true);
    const auto page = Parsed(s, ListRoute, [&] { return ParseMonsters(body); });
    if (page.slug != slug) BadAnswer(s, ListRoute);
    for (const auto& monster : page.monsters)
        if (monster.id == id) return monster.quantity;
    return std::nullopt;
}
// One sender at a time among this PC's game clients: the read, the write and its
// check happen under Local\DofusMiniatures.Metamob. A mutex abandoned by a client
// that ended while holding it is owned all the same.
class Sender {
public:
    explicit Sender(Session& s) : mutex(CreateMutexW(nullptr, FALSE, SenderName)) {
        if (!mutex) { Log("METAMOB_SENDER_ERROR"); Backoff(s); }
        for (const auto start = timing.now();;) {
            const auto result = WaitForSingleObject(mutex, 0);
            if (result == WAIT_OBJECT_0 || result == WAIT_ABANDONED) return;
            const auto now = timing.now();
            const bool ending = s.job.cancelled.load() || Ending(s.job); // nothing left to wait for
            if (result == WAIT_TIMEOUT && !ending && now >= start && now - start < SenderWaitMs) { timing.sleep(50); continue; }
            CloseHandle(mutex);
            if (ending) throw Halt{};
            if (result != WAIT_TIMEOUT) { Log("METAMOB_SENDER_ERROR"); Backoff(s); }
            // Another client is sending: tried again shortly, not a failure.
            Log("METAMOB_BUSY");
            s.book.notBefore = now + BusyMs;
            throw Halt{};
        }
    }
    ~Sender() { ReleaseMutex(mutex); CloseHandle(mutex); }
    Sender(const Sender&) = delete;
    Sender& operator=(const Sender&) = delete;
private:
    HANDLE mutex;
};
Flight Take(Session& s) {
    auto flight = std::move(s.book.flights.front());
    s.book.flights.erase(s.book.flights.begin());
    return flight;
}
void Confirm(Session& s) {
    s.book.refused = false; // the key writes again: a later refusal is told again
    const auto flight = Take(s);
    s.job.Added(AddedPart(flight.shown, flight.base, flight.target)); // told with the cycle's other writes
    if (flight.base + flight.group.count > flight.target) s.job.Post(CappedText(flight.shown)); // the rest stays uncounted
    s.job.Confirmed({flight.slug, flight.id, flight.target}); // the chat part shows it at once
    s.done += flight.target - flight.base;
}
void Conflict(Session& s, int quantity) {
    const auto flight = Take(s);
    s.job.Post(ChangedText(flight.shown, flight.base, quantity));
    Log("METAMOB_CONFLICT monster=" + std::to_string(flight.group.monster));
}
// unsure: an earlier send of this target may have landed (in Write, `uncertain`
// is already set for the send just refused, so the caller says which).
void Drop(Session& s, const char* code, bool unsure) {
    const auto flight = Take(s);
    s.job.Post(unsure ? UnsureText(flight.shown) : RefusedText(flight.shown));
    Log(std::string(code) + " monster=" + std::to_string(flight.group.monster));
}
// Not written for sure: the captures go back to a group and are read afresh.
void Unsend(Session& s) {
    auto group = Take(s).group;
    group.stale = 0; group.waitUntil = 0;
    for (auto& known : s.book.groups)
        if (known.monster == group.monster && known.type == group.type && known.character == group.character) {
            known.count += group.count;
            return;
        }
    s.book.groups.insert(s.book.groups.begin(), std::move(group));
}
// PATCH of the first flight's absolute target, then what the answer means. Always
// under the sender lock (Sync, Resolve): a write that may have been received is
// read back before that lock goes, while no other client of this PC writes the
// monster, so the target found there is this write's; found at its base, the
// same target is sent once more.
void Write(Session& s, bool resend = true) {
    auto& flight = s.book.flights.front();
    if (Ending(s.job)) {
        // Turned off, or the key being forgotten: nothing more is written. Never
        // sent: back to the groups; maybe landed: kept as a flight, to be reported.
        if (!flight.uncertain) Unsend(s);
        throw Halt{};
    }
    const bool earlier = flight.uncertain; // an earlier send of this target may still land
    flight.uncertain = true;               // and so may this one, from now on
    ++s.requests;
    const auto response = Send(s.job, s.key, "PATCH", "/quests/" + flight.slug + "/monsters/" + std::to_string(flight.id), WriteRoute,
        QuantityBody(flight.target));
    if (s.job.cancelled.load()) throw Halt{};
    if (response.status >= 200 && response.status < 300 && response.json) {
        Accepted(s);
        std::optional<QuantityUpdate> update;
        try { update = ParseQuantityUpdate(response.body); } catch (...) {}
        if (update && update->monsterId == flight.id) {
            if (update->quantity == flight.target) Confirm(s);
            else Conflict(s, update->quantity); // the answer itself says another value
            return;
        }
    }
    if (KeyRefusal(response) || response.status == 404 || response.status == 429 || (!response.status && !response.sent)) {
        // This send was not applied. After an earlier one that may still land, the
        // flight keeps its base and target for the next read: never +n afresh.
        const auto status = response.status;
        if (!earlier) {
            Unsend(s);
            if (status == 404) throw QuestGone{};
        } else if (status == 404) {
            // Its quest refuses the write of a monster it listed: like a lost read,
            // asked again after a pause, then given up with a notice.
            s.book.quests.reset(); s.book.monsters.clear();
            if (++flight.misses < 2) Backoff(s);
            Drop(s, "METAMOB_LOST", true);
            return;
        }
        if (status == 429) Limit(s, response);
        if (status) Refuse(s);
        Backoff(s);
    }
    if (response.status >= 400 && response.status < 500 && response.status != 408 && response.status != 401 && response.status != 403) {
        Drop(s, "METAMOB_REFUSED", earlier); // a resend refused says nothing of the earlier send
        return;
    }
    // Sent without a confirmed result (no answer, 5xx, a 401/403 page that is not
    // Metamob's, an unexpected answer): announced, then read back at once.
    Log("METAMOB_UNCONFIRMED monster=" + std::to_string(flight.group.monster));
    if (!flight.mark) flight.mark = Announce(flight.slug, flight.id);
    std::optional<int> current; // a failed read stops the cycle: kept in flight, still announced
    try { current = Current(s, flight.slug, flight.id, flight.step); } catch (const QuestGone&) {}
    if (current && *current == flight.target) { Confirm(s); return; }
    if (current && *current != flight.base) { Conflict(s, *current); return; }
    if (current && resend) { Write(s, false); return; } // not applied (yet): the same target once more
    Backoff(s); // still unknown: read again at the next cycle
}
// A write sent earlier without a confirmed result: settled before anything else.
void Resolve(Session& s) {
    const Sender sender(s);
    auto& flight = s.book.flights.front();
    std::optional<int> current;
    try { current = Current(s, flight.slug, flight.id, flight.step); } catch (const QuestGone&) {}
    if (!current) {
        // Its quest or its step answers without it (a deleted quest or a passing
        // error): asked again after a pause, then given up with a notice.
        s.book.quests.reset(); s.book.monsters.clear();
        if (++flight.misses < 2) Backoff(s);
        Drop(s, "METAMOB_LOST", true); // only unsettled flights reach Resolve
        return;
    }
    if (*current == flight.target) Confirm(s);
    else if (*current == flight.base) Write(s);
    else Conflict(s, *current);
}
// The first group not held by another client: its quest, its Metamob monster,
// then read, write and check under the sender lock. Called only when no flight
// is left. A group held by another client's unsettled write waits alone: the
// captures of other monsters go on meanwhile.
void Sync(Session& s) {
    auto& groups = s.book.groups;
    const auto now = timing.now();
    const auto ready = std::find_if(groups.begin(), groups.end(), [&](const Group& group) { return group.waitUntil <= now; });
    if (ready == groups.end()) {
        // Every capture left waits for another client: checked again at the earliest.
        const auto earliest = std::min_element(groups.begin(), groups.end(),
            [](const Group& a, const Group& b) { return a.waitUntil < b.waitUntil; })->waitUntil;
        s.book.notBefore = std::max(s.book.notBefore, earliest);
        throw Halt{};
    }
    std::rotate(groups.begin(), ready, ready + 1); // taken first, the others keep their order
    auto& group = groups.front();
    const auto slug = QuestSlug(s, group.character);
    if (!slug) { s.book.groups.erase(s.book.groups.begin()); return; } // already told why
    const auto monster = FindMonster(Monsters(s, *slug), group.name, group.type);
    const auto unmatched = [&] {
        s.job.Post(UnmatchedText(Shown(group.name)));
        Log("METAMOB_UNMATCHED monster=" + std::to_string(group.monster));
        s.book.groups.erase(s.book.groups.begin());
    };
    if (!monster) { unmatched(); return; }
    const Sender sender(s);
    if (Announced(*slug, monster->id)) {
        // Another client's write of this monster is not settled yet (this one has
        // no flight left when Sync runs): read and written after it, afresh.
        Log("METAMOB_WAIT monster=" + std::to_string(group.monster));
        group.waitUntil = timing.now() + PauseCheckMs;
        return; // the next group goes on; the cycle halts once every one left waits
    }
    const auto current = Current(s, *slug, monster->id, monster->step);
    if (!current) {
        // Not in its step: the kept list may be stale. Read it again once (the
        // list read again is kept when the monster is still missing).
        if (++group.stale >= 2) { unmatched(); return; }
        s.book.monsters.erase(*slug);
        return;
    }
    if (*current >= MaxQuantity) {
        s.job.Post(CappedText(Shown(monster->name)));
        s.book.groups.erase(s.book.groups.begin());
        return;
    }
    const int target = std::min(MaxQuantity, *current + group.count);
    // Saved before sending: an unconfirmed write is read again, never sent again as +n.
    s.book.flights.push_back(Flight{std::move(group), *slug, Shown(monster->name), monster->id, monster->step, *current, target});
    s.book.groups.erase(s.book.groups.begin());
    Write(s);
}
// No key any more: the captures not sent yet are said unsynchronised; a write
// that may have landed is named instead (entered by hand, it would count twice).
void Abandon(Job& job) {
    for (const auto& group : job.book.groups) job.Post(NoKeyText(Shown(group.name)));
    for (const auto& flight : job.book.flights)
        job.Post(flight.uncertain ? ForgetUnsureText(flight.shown) : NoKeyText(Shown(flight.group.name)));
    if (const int pending = Units(job.book)) Log("METAMOB_NO_KEY pending=" + std::to_string(pending));
    job.book.groups.clear(); job.book.flights.clear();
}
void Cycle(Job& job) {
    auto& book = job.book;
    const auto file = Identity(job.keyPath); // read first: a key saved meanwhile is never taken for the one loaded
    std::optional<Secret> key;
    try { key = LoadKey(job.keyPath); }
    catch (...) {
        // Another Windows account or PC, a corrupt file: the captures cannot be sent.
        job.Found(KeyStatus::Unreadable, file);
        job.Post(KeyUnreadable);
        Log("METAMOB_KEY_UNREADABLE pending=" + std::to_string(Units(book)));
        book.groups.clear(); book.flights.clear();
        return;
    }
    if (!key) { Abandon(job); return; } // forgotten since the capture, here or by another client
    if (book.refused) {
        if (Fingerprint(*key) != book.refusedKey) Fresh(book); // another game client saved a new key meanwhile
        else if (timing.now() < book.retryRefused) { book.notBefore = timing.now() + PauseCheckMs; return; }
        // else the same key is tried again: refused at its first request, it pauses without a new notice
    }
    Session s{job, book, *key, file};
    try {
        while (!book.flights.empty() || !book.groups.empty()) {
            // The request budget spent: the next job goes on at once. Turned off or
            // forgotten meanwhile: the game thread drops or reports what is left.
            if (s.requests >= CycleRequests || Ending(job)) break;
            try {
                if (!book.flights.empty()) Resolve(s);
                else Sync(s);
            } catch (const QuestGone&) {
                if (s.rediscovered) { Log("METAMOB_QUEST_GONE"); Backoff(s); }
                s.rediscovered = true;
                book.quests.reset(); book.monsters.clear();
            }
        }
        book.failures = 0;
    } catch (const Halt&) {}
    if (s.askQuestsAgain) book.quests.reset();
    Log("METAMOB_SYNC done=" + std::to_string(s.done) + " pending=" + std::to_string(Units(book)));
}
// « Metamob dans le chat »: the character's quest and its whole list with the
// quantities, read afresh and silently (fixed log codes, never a notice). The
// game thread keeps what it found (Keep) and decides when to read it again.
void ReadCatalog(Job& job) {
    auto& book = job.book;
    Listed listed;
    listed.character = job.character;
    const char* failure = "error";
    try {
        listed.file = Identity(job.keyPath); // read first: a key saved meanwhile is never taken for the one loaded
        std::optional<Secret> key;
        try { key = LoadKey(job.keyPath); }
        catch (...) { job.Found(KeyStatus::Unreadable, listed.file); }
        // No key (forgotten, unreadable): nothing is sent. A key Metamob refused is
        // kept away by the key row's status (Usable); the same key saved again is
        // tried once, its refusal silent (Refuse). book.refused only holds the
        // capture sync's pause and its one notice: a key accepted again without a
        // confirmed write leaves it set, and the chat part reads with that key.
        if (!key) failure = "key";
        else {
            Session s{job, book, *key, listed.file};
            s.listing = true;
            try {
                for (;;) {
                    try {
                        const auto slug = QuestSlug(s, job.character);
                        if (!slug) { listed.problem = s.questProblem; failure = "quest"; break; } // no Unity Ocre quest, or several
                        std::vector<QuestMonster> monsters;
                        std::set<int64_t> ids; // overlapping pages: each monster once
                        for (auto& monster : ReadList(s, *slug, true))
                            if (ids.insert(monster.id).second) monsters.push_back(std::move(monster));
                        book.monsters[*slug] = monsters; // the sync's own list, fresher
                        listed.slug = *slug;
                        listed.monsters = std::move(monsters);
                        listed.ok = true;
                        break;
                    } catch (const QuestGone&) {
                        // Its quest deleted (maybe created again): looked for once more, as a cycle does.
                        if (s.rediscovered) { Log("METAMOB_QUEST_GONE"); Backoff(s); }
                        s.rediscovered = true;
                        book.quests.reset(); book.monsters.clear();
                    }
                }
            } catch (const Halt&) { failure = s.refused ? "refused" : "request"; }
            if (s.askQuestsAgain) book.quests.reset();
        }
    } catch (...) { listed.ok = false; listed.monsters.clear(); }
    const auto count = listed.monsters.size();
    const bool ok = listed.ok;
    job.listed = std::move(listed); // always handed back: the game thread spaces the next reading
    if (ok) Log("METAMOB_CATALOG monsters=" + std::to_string(count));
    else Log(std::string("METAMOB_CATALOG_FAILED reason=") + failure);
}
// « Ouvertures Kralamoure dans le calendrier »: the openings Metamob lists for
// the server of the character's Unity Ocre quest (the quests kept by the book
// when read less than 10 min ago, read again otherwise), read afresh and
// silently (fixed log codes, never a notice). Only that server's openings are
// kept. The game thread keeps what it found (KeepKralove) and decides when to
// read again.
void ReadKralove(Job& job) {
    auto& book = job.book;
    KraloveRead read;
    read.character = job.character;
    read.fromDay = job.kraloveFromDay;
    const char* failure = "error";
    try {
        read.file = Identity(job.keyPath); // read first: a key saved meanwhile is never taken for the one loaded
        std::optional<Secret> key;
        try { key = LoadKey(job.keyPath); }
        catch (...) { job.Found(KeyStatus::Unreadable, read.file); read.status = KraloveStatus::KeyUnreadable; }
        if (!key) failure = "key"; // forgotten or unreadable: nothing is sent
        else {
            Session s{job, book, *key, read.file};
            s.listing = true; // silent: a failure halts, untold; the game thread spaces the next try
            // The quest's server as it is now: quests read 10 min ago or more are read again.
            const auto now = timing.now();
            if (book.quests && (now < book.questsAt || now - book.questsAt >= RefreshMs)) book.quests.reset();
            try {
                const auto quest = ChosenQuest(s, job.character);
                if (!quest) {
                    read.status = s.questProblem == QuestProblem::Several ? KraloveStatus::SeveralQuests : KraloveStatus::NoQuest;
                    failure = "quest";
                } else if (quest->serverId <= 0) {
                    read.status = KraloveStatus::NoServer;
                    failure = "server";
                    s.askQuestsAgain = true; // the user may name it meanwhile
                } else {
                    if (job.forgetting.load()) throw Halt{}; // nothing more is sent with a key being forgotten
                    ++s.requests;
                    auto response = Send(job, *key, "GET", "/kralove?server=" + std::to_string(quest->serverId) + "&from=" + job.kraloveFrom,
                        KraloveRoute);
                    if (job.cancelled.load()) throw Halt{};
                    const bool answered = response.status >= 200 && response.status < 300 && response.json;
                    const bool refused = KeyRefusal(response);
                    if (answered) Accepted(s);
                    else if (response.status == 429) Limit(s, response);
                    else if (!refused && (response.status != 404 || !response.json)) Backoff(s);
                    if (refused) {
                        // Refused for this list only: the key itself is judged by the quests,
                        // read again with it every 10 min, and by the chat and the captures.
                        read.status = KraloveStatus::Refused;
                        failure = "refused";
                    } else if (!answered) {
                        // Metamob's own 404: no such list on its API (asked again 10 min later).
                        read.status = KraloveStatus::Unavailable;
                        failure = "unavailable";
                    } else {
                        std::optional<std::vector<Kralove>> openings;
                        try { openings = ParseKralove(response.body); } catch (...) {}
                        if (!openings) {
                            Log(std::string("METAMOB_BAD_ANSWER route=") + KraloveRoute);
                            read.status = KraloveStatus::Unreadable;
                            failure = "answer";
                        } else {
                            size_t others = 0;
                            for (auto& opening : *openings) {
                                if (opening.serverId == quest->serverId) read.openings.push_back(std::move(opening));
                                else ++others;
                            }
                            read.server = quest->serverName;
                            if (read.server.empty() && !read.openings.empty()) read.server = read.openings.front().server;
                            read.status = KraloveStatus::Ready;
                            if (others) Log("METAMOB_KRALOVE_OTHER_SERVER openings=" + std::to_string(others));
                        }
                    }
                }
            } catch (const Halt&) {
                read.status = s.refused ? KraloveStatus::Refused : s.limited ? KraloveStatus::Busy :
                    s.bad ? KraloveStatus::Unreadable : KraloveStatus::Unreachable;
                failure = s.refused ? "refused" : s.limited ? "busy" : s.bad ? "answer" : "request";
            }
            if (s.askQuestsAgain) book.quests.reset();
        }
    } catch (...) { read.status = KraloveStatus::Unreachable; read.openings.clear(); failure = "error"; }
    const auto count = read.openings.size();
    const bool ok = read.status == KraloveStatus::Ready;
    job.kraloveRead = std::move(read); // always handed back: the game thread spaces the next reading
    if (ok) Log("METAMOB_KRALOVE openings=" + std::to_string(count));
    else Log(std::string("METAMOB_KRALOVE_FAILED reason=") + failure);
}
// Options › « Enregistrer »: GET /v1/me with the candidate key. Refused by
// Metamob (its JSON 401/403): nothing stored. Otherwise stored, verified or not
// (Metamob unreachable, or a page that is not its answer).
void Submit(Job& job) {
    const Secret key = std::move(*job.submit);
    job.submit.reset();
    const auto response = Send(job, key, "GET", "/me", "/me");
    if (job.cancelled.load() || job.forgetting.load()) return; // stopped, or « Oui » confirmed since: not stored
    if (KeyRefusal(response)) { job.Post(KeyRejected); Log("METAMOB_KEY_REJECTED"); return; }
    std::optional<std::string> user;
    if (response.status >= 200 && response.status < 300 && response.json) {
        try { user = SafeUsername(ParseMe(response.body).username); } catch (...) {}
    }
    try { StoreKey(job.keyPath, key); }
    catch (...) { job.Post(KeyNotStored); Log("METAMOB_KEY_STORE_FAILED"); return; }
    job.Found(user ? KeyStatus::Verified : KeyStatus::Unverified, Identity(job.keyPath)); // the file just written
    Log(user ? "METAMOB_KEY_SAVED verified=1" : "METAMOB_KEY_SAVED verified=0");
    job.Post(user ? SavedText(*user) : std::string(KeyUnverified));
    Fresh(job.book);
    if (response.status == 429)
        job.book.notBefore = timing.now() + 1000 * static_cast<uint64_t>(RetryAfterSeconds(response.retryAfter, timing.epoch()).value_or(DefaultRetryAfter));
}
// Options › the key row's trash, then « Oui »: the key file goes, and so do the
// captures that can no longer be sent.
void Forget(Job& job) {
    try { ForgetKey(job.keyPath); }
    catch (...) { job.Post(KeyNotForgotten); Log("METAMOB_KEY_FORGET_FAILED"); return; }
    Log("METAMOB_KEY_FORGOTTEN");
    job.Found(KeyStatus::None, KeyFile{});
    job.Post(KeyForgotten);
    Abandon(job);
    job.book = Book{};
}
// Options › the key row shows a key not checked in this session, or « Clé non
// vérifiée » clicked: GET /v1/me with the stored key, silent. Only the row's
// status changes; without a clear answer the next request waits 30 s (or the
// Retry-After given).
void Check(Job& job) {
    if (Identity(job.keyPath) != job.check) return; // replaced or forgotten since it was asked: told apart by the game thread
    std::optional<Secret> key;
    try { key = LoadKey(job.keyPath); }
    catch (...) { Log("METAMOB_KEY_CHECKED status=unreadable"); job.Found(KeyStatus::Unreadable, job.check); job.told = true; return; }
    if (!key) return; // forgotten meanwhile, here or by another client
    const auto response = Send(job, *key, "GET", "/me", "/me");
    if (job.cancelled.load() || job.forgetting.load()) return; // stopped, or « Oui » confirmed since: nothing learnt
    auto status = KeyRefusal(response) ? KeyStatus::Refused : KeyStatus::Unverified;
    if (response.status >= 200 && response.status < 300 && response.json) {
        try { static_cast<void>(ParseMe(response.body)); status = KeyStatus::Verified; } catch (...) {}
    }
    if (status == KeyStatus::Unverified) {
        const int seconds = response.status == 429 ? RetryAfterSeconds(response.retryAfter, timing.epoch()).value_or(DefaultRetryAfter) : 0;
        job.book.notBefore = std::max(job.book.notBefore, timing.now() + (seconds > 0 ? 1000 * static_cast<uint64_t>(seconds) : BackoffMs));
    }
    Log(std::string("METAMOB_KEY_CHECKED status=") +
        (status == KeyStatus::Verified ? "verified" : status == KeyStatus::Refused ? "refused" : "unverified"));
    job.Found(status, job.check);
    job.told = true;
}
// The synchronous core: what one worker does, in the order the user asked.
void Work(Job& job) noexcept {
    try {
        // A confirmed forget is done even by a stopped job: one local file, no request.
        if (job.forget) Forget(job);
        // The key row's check, before a key typed meanwhile replaces that key file.
        if (job.check && !job.cancelled.load() && !job.forgetting.load()) Check(job);
        // A key typed before a forget confirmed while this job runs is never sent nor stored.
        if (job.submit && !job.cancelled.load() && !job.forgetting.load()) Submit(job);
        // A pause set just before (a failed check, a rate-limited key) holds the
        // captures too: Collect gives them back, Start sends them once it ends.
        if (job.cycle && !job.cancelled.load() && !Ending(job) && Units(job.book) > 0 && timing.now() >= job.book.notBefore) Cycle(job);
        // The chat part's list, given only to a job without captures (Start): never
        // before them. Reads only: the sync turned off does not stop it, a forget does.
        if (job.listing && !job.cancelled.load() && !job.forgetting.load() && timing.now() >= job.book.notBefore) ReadCatalog(job);
        // The calendar's openings, likewise; after a 429 of the list just read, at its Retry-After.
        if (job.kralove && !job.cancelled.load() && !job.forgetting.load() && timing.now() >= job.book.notBefore) ReadKralove(job);
    } catch (...) {
        try { Log("METAMOB_WORKER_ERROR"); } catch (...) {}
    }
    // A stopped job is never collected (Install, Restore): its book goes, and with
    // it the announcement of a write it holds. Its end and its cancel (Cancel) are
    // handed over under its lock: whichever comes second empties the book.
    try {
        std::lock_guard<std::mutex> guard(job.lock);
        if (job.cancelled.load()) Empty(job.book);
        job.Told(); // the writes confirmed last, in one notice
        job.done.store(true);
    } catch (...) { job.done.store(true); }
}

// The chat part's copy of the quest (game thread only): the list the last
// reading found, for the current key file and the character it was chosen for.
struct Catalog {
    std::string character, slug;
    std::vector<QuestMonster> monsters;
    MonsterIndex index;                // FindMonster's answers for that list, built once per reading
    bool read = false, failed = false; // a list read; the last reading failed (« ? » until one succeeds)
    QuestProblem problem = QuestProblem::None; // that failure lasts (no Unity Ocre quest, or several): the tooltip says why
    int failures = 0;                  // passing failures in a row: tried again 30 s later, twice longer each time
    uint64_t next = 0;                 // no reading before
};
// The calendar's copy of the Kralamoure openings (game thread only), for the
// current key file and the character its quest was chosen for.
struct KraloveCache {
    std::string character;
    bool tried = false;                // a reading came back: its character is known
    KraloveStatus status = KraloveStatus::Loading;
    bool read = false;                 // openings read; kept through passing failures
    std::string server;
    int64_t fromDay = 0;
    std::vector<Kralove> openings;
    int failures = 0;                  // passing failures in a row: tried again 30 s later, twice longer each time
    uint64_t next = 0;                 // no reading before
};
// Changes with the calendar's copy (KraloveView::version), across Install and Restore too.
uint64_t kraloveVersion = 1;
// Game-thread state. While a worker runs it owns the book; captures wait in the inbox.
struct State {
    bool installed = false;
    std::filesystem::path keyPath;
    Transport transport;
    Notice notice;
    Book book;
    std::vector<Group> inbox;
    std::optional<Secret> submit;
    bool forget = false;
    // The key row's status: the key file as last seen here and what the workers
    // told of it (never in the book, which a running worker holds).
    Known known;
    bool check = false;     // a check the key row asked for, not given to a worker yet
    std::vector<std::string> outbox;
    // Captures refused for want of a key, not told yet (shown names, each once):
    // a dungeon-room soul's bosses in one notice, before any other one.
    std::vector<std::string> noKey;
    uint64_t lastCapture = 0, nextError = 0;
    int jobUnits = 0;
    std::shared_ptr<Job> job;
    // « Metamob dans le chat »: the game as the chat part reads it, the quest's
    // list, the chat monsters matched with it (their entry in catalog.monsters;
    // none: not in the quest) and when a chat label last asked for it.
    GameView game;
    Catalog catalog;
    std::map<int, std::optional<size_t>> matched;
    bool wanted = false;
    uint64_t wantedAt = 0, statusAt = 0;
    // « Ouvertures Kralamoure »: the calendar's openings and when it last asked for them.
    KraloveCache kralove;
    bool kraloveWanted = false;
    uint64_t kraloveWantedAt = 0;
};
State state;
// A cancelled worker that has not ended yet keeps a new one from starting:
// never two at a time, even after Restore and Install (as treasure and jobs).
std::shared_ptr<Job> activeJob;
bool inlineJobs = false;
// Install, Restore: the job given to a worker is stopped. Ended already but not
// collected (it never will be), its book goes here; still running, the worker
// empties it when it ends (Work). Under the job's lock: neither side misses it.
void Cancel() noexcept {
    if (!activeJob) return;
    try {
        std::lock_guard<std::mutex> guard(activeJob->lock);
        activeJob->cancelled.store(true);
        if (activeJob->done.load()) Empty(activeJob->book);
    } catch (...) { activeJob->cancelled.store(true); }
    if (activeJob->done.load()) activeJob.reset(); // still running: kept, never two workers at a time
}

void NoKeyTold() {
    if (state.noKey.empty()) return;
    if (state.outbox.size() < MaxNotices) state.outbox.push_back(NoKeyText(state.noKey));
    state.noKey.clear();
}
void Post(std::string text) {
    NoKeyTold();
    if (state.outbox.size() < MaxNotices) state.outbox.push_back(std::move(text));
}
int Pending() { return Units(state.book) + Count(state.inbox) + state.jobUnits; }
// « Oui » confirmed, until the worker that deletes the key file (first of all)
// has been taken back: no key for the key row meanwhile, then the key file as
// it is, a new key saved right after included.
bool Forgetting() { return state.forget || (state.job && state.job->forget); }
// The chat part's list and the calendar's openings go with the key they were
// read with (another key file, a forget) and with the character their quest was
// chosen for: read again for the new ones.
void Unlist() {
    state.catalog = Catalog{}; state.matched.clear();
    state.kralove = KraloveCache{};
    ++kraloveVersion;
}
// The key file seen now. Another one than the known one (saved, replaced or
// forgotten, here or by another client) is a key not checked yet, or none.
void Track(const KeyFile& file) {
    if (file == state.known.file) return;
    state.known = Known{file, file.present ? KeyStatus::Saved : KeyStatus::None};
    state.check = false;
    Unlist();
}
// What a worker learnt holds for the key file it read, if that file is still there.
void Learn(const Known& found) {
    if (const auto file = Identity(state.keyPath)) Track(*file);
    if (found.file == state.known.file) state.known.status = found.status;
}
void Error(const char* code) noexcept {
    try {
        const auto now = timing.now();
        if (now < state.nextError) return;
        state.nextError = now + ErrorLogMs;
        Log(code);
    } catch (...) {}
}
// « Metamob dans le chat »: the monsters of L’éternelle moisson (the Ocre part's
// own table) are the only ones a chat label gives a Metamob part.
bool OcreMonster(int monsterId) { return chat_souls::IsOcreGoal(monsterId); }
// The local character, which picks the quest when there are several (empty: unknown).
std::string Character() {
    try { return state.game.character ? state.game.character() : std::string(); } catch (...) { return {}; }
}
// The stored key as the chat part may use it: saved, valid or not verified, no
// forget under way. The chat asks at every pump: its file is looked at again at
// most every second (the workers' answers and the options keep it meanwhile).
bool Usable() {
    if (Forgetting()) return false;
    const auto now = timing.now();
    if (now < state.statusAt || now - state.statusAt >= StatusMs) {
        state.statusAt = now;
        if (const auto file = Identity(state.keyPath)) Track(*file);
    }
    const auto status = state.known.status;
    return status == KeyStatus::Saved || status == KeyStatus::Verified || status == KeyStatus::Unverified;
}
// The list is read while a chat label asked for it within the last minute, with
// a usable key: at once the first time, then 10 min after the last reading.
bool ListingDue(uint64_t now) {
    if (!state.wanted || (now >= state.wantedAt && now - state.wantedAt >= WantedMs)) return false;
    return Usable() && now >= state.catalog.next;
}
// A reading handed back by its worker. One made with another key file than the
// one there now is dropped (Track cleared the list: read again for the new key);
// one that cannot be told apart is kept, so it is always spaced. A failure keeps
// the list but shows « ? », its tooltip saying why when no quest could be chosen
// (a passing failure: the generic text again); the next try comes 30 s later,
// twice longer each time up to 15 min (10 min when no quest could be chosen).
void Keep(Listed listed) {
    const auto file = Identity(state.keyPath);
    if (file) Track(*file);
    if (file && listed.file && *listed.file != *file) return;
    const auto now = timing.now();
    auto& catalog = state.catalog;
    catalog.character = std::move(listed.character);
    if (listed.ok) {
        catalog.slug = std::move(listed.slug);
        catalog.monsters = std::move(listed.monsters);
        catalog.index = IndexMonsters(catalog.monsters);
        catalog.read = true; catalog.failed = false; catalog.failures = 0; catalog.problem = QuestProblem::None;
        catalog.next = now + RefreshMs;
        state.matched.clear(); // matched again with the new list
        return;
    }
    catalog.failed = true;
    catalog.problem = listed.problem;
    catalog.failures = std::min(catalog.failures + 1, 1000);
    catalog.next = now + (listed.problem != QuestProblem::None ? RefreshMs :
        std::min<uint64_t>(BackoffCapMs, BackoffMs << std::min(catalog.failures - 1, 5)));
}
// The sync's own write, confirmed by Metamob: the chat part shows it at once.
void Wrote(const Written& write) {
    auto& catalog = state.catalog;
    if (!catalog.read || write.slug != catalog.slug) return;
    for (auto& monster : catalog.monsters)
        if (monster.id == write.id) monster.quantity = write.quantity;
}
// Another character on this client: the quest read for the previous one may not
// be this one's (several Ocre quests), nor its server. An unknown character
// keeps what was read.
void Follow() {
    const bool catalog = state.catalog.read || state.catalog.failed, kralove = state.kralove.tried;
    if (!catalog && !kralove) return;
    const auto character = Character();
    if (character.empty()) return;
    if ((catalog && character != state.catalog.character) || (kralove && character != state.kralove.character)) Unlist();
}
// The calendar's openings are read while it is shown (asked within the last
// second), with a usable key: at once the first time, then 10 min after the
// last reading.
bool KraloveDue(uint64_t now) {
    if (!state.kraloveWanted || (now >= state.kraloveWantedAt && now - state.kraloveWantedAt >= KraloveWantedMs)) return false;
    return Usable() && now >= state.kralove.next;
}
// The first day asked: this PC's date a week back (UTC's when Windows cannot tell).
int64_t KraloveFromDay() {
    const auto epoch = timing.epoch();
    if (const auto local = LocalTime(epoch)) return DayNumber(local->year, local->month, local->day) - KraloveDaysBack;
    return (epoch >= 0 ? epoch / 86400 : (epoch - 86399) / 86400) - KraloveDaysBack;
}
std::string DateText(int64_t day) {
    const auto date = CivilDay(day);
    char text[16]{};
    std::snprintf(text, sizeof(text), "%04d-%02d-%02d", date.year, date.month, date.day);
    return text;
}
// A reading of the openings handed back by its worker. One made with another
// key file than the one there now is dropped (Track cleared the copy). Read:
// kept, read again 10 min later. A lasting problem (no usable quest or server,
// no such list on the API) drops the openings of a quest no longer chosen and
// waits 10 min; the key refused or unreadable waits for a usable key (the key
// row tells it). A passing failure keeps what was read: 30 s, then twice longer
// up to 15 min (a 429 also holds every request until its Retry-After).
void KeepKralove(KraloveRead read) {
    const auto file = Identity(state.keyPath);
    if (file) Track(*file);
    if (file && read.file && *read.file != *file) return;
    const auto now = timing.now();
    auto& kralove = state.kralove;
    ++kraloveVersion;
    kralove.character = std::move(read.character);
    kralove.tried = true;
    switch (read.status) {
    case KraloveStatus::Ready:
        kralove.status = read.status; kralove.read = true; kralove.server = std::move(read.server); kralove.fromDay = read.fromDay;
        kralove.openings = std::move(read.openings); kralove.failures = 0; kralove.next = now + RefreshMs;
        return;
    case KraloveStatus::NoQuest: case KraloveStatus::SeveralQuests: case KraloveStatus::NoServer: case KraloveStatus::Unavailable:
        kralove.status = read.status; kralove.read = false; kralove.server.clear(); kralove.openings.clear();
        kralove.failures = 0; kralove.next = now + RefreshMs;
        return;
    case KraloveStatus::Refused: case KraloveStatus::KeyUnreadable:
        kralove.status = read.status; kralove.next = now + RefreshMs;
        return;
    default:
        break;
    }
    if (read.status != KraloveStatus::Loading) kralove.status = read.status;
    kralove.failures = std::min(kralove.failures + 1, 1000);
    kralove.next = now + std::min<uint64_t>(BackoffCapMs, BackoffMs << std::min(kralove.failures - 1, 5));
}
// A game monster's chat part: none (outside L’éternelle moisson, unknown to the
// game view, not in the quest, no usable key), unknown (« ? »: the list not read
// yet, or its last reading failed, maybe for a lasting quest problem), or its
// quantity in the list.
struct Part { bool shown = false; int quantity = -1; QuestProblem problem = QuestProblem::None; };
Part ChatPart(int monsterId) {
    if (!state.installed || !state.game.monster || monsterId <= 0 || !OcreMonster(monsterId) || !Usable()) return {};
    const auto& catalog = state.catalog;
    const bool known = catalog.read && !catalog.failed;
    const auto match = known ? state.matched.find(monsterId) : state.matched.end();
    if (match != state.matched.end()) return match->second ? Part{true, catalog.monsters[*match->second].quantity} : Part{};
    const auto game = state.game.monster(monsterId);
    if (!game || game->name.empty()) return {};
    if (!known) return {true, -1, catalog.problem};
    // Matched once per list read, through the list's index (FindMonster's rules):
    // the maps ask for every missing monster of the quest at once.
    const auto entry = FindIndexed(catalog.index, game->name, game->type);
    state.matched.emplace(monsterId, entry);
    return entry ? Part{true, catalog.monsters[*entry].quantity} : Part{};
}
std::string ChatCaption(const Part& part, bool color) {
    if (part.quantity < 0) return "Metamob : ?";
    const auto text = "Metamob : " + Number(part.quantity);
    if (!color) return text;
    return (part.quantity > 0 ? "<color=#8DDB91>" : "<color=#F18C8C>") + text + "</color>";
}
std::string ChatDetail(const Part& part) {
    const auto title = ChatCaption(part, false) + ".\n";
    // A lasting quest problem says what to fix on Metamob (read again 10 min later).
    if (part.quantity < 0) switch (part.problem) {
    case QuestProblem::NoQuest:
        return title + "Aucune quête Ocre Dofus Unity sur ton compte Metamob : crée-la sur metamob.fr, puis elle sera lue automatiquement.";
    case QuestProblem::Several:
        return title + "Plusieurs quêtes Ocre Dofus Unity sur ton compte Metamob, et aucune n’indique ce personnage dans "
            "« Personnage à contacter » : ajoute son nom dans la bonne quête sur metamob.fr.";
    default:
        return title + "Ta quête Ocre sur Metamob n’a pas encore été lue ou n’a pas pu l’être ; nouvel essai automatique.";
    }
    return title + "Ta quête Ocre sur Metamob compte " + Number(part.quantity) + (part.quantity > 1 ? " exemplaires" : " exemplaire") +
        " de ce monstre ; elle est relue au plus toutes les 10 minutes et mise à jour aussitôt après une capture synchronisée par le mod.";
}
// « Synchroniser les captures avec Metamob » off: the captures not sent yet are
// dropped and told once, never kept for later (the user may enter them by hand
// meanwhile); a write that may have landed is named, to be checked. Neither is
// read nor written again.
void Discard(bool inbox) {
    int pending = Count(state.book.groups) + (inbox ? Count(state.inbox) : 0), unsettled = 0;
    for (const auto& flight : state.book.flights) {
        if (!flight.uncertain) { pending += flight.group.count; continue; } // never sent
        Post(OffUnsureText(flight.shown));
        ++unsettled;
    }
    if (inbox) state.inbox.clear();
    state.book.groups.clear(); state.book.flights.clear();
    if (pending == 0 && unsettled == 0) return;
    if (pending > 0) Post(DroppedText(pending));
    Log("METAMOB_SYNC_OFF dropped=" + std::to_string(pending) + " unsettled=" + std::to_string(unsettled));
}
// The worker's notices so far, then its book once it has finished.
void Collect() {
    if (!state.job) return;
    const bool finished = state.job->done.load(); // read first: no notice posted before it is missed
    std::optional<Known> found;
    std::vector<Written> written;
    {
        std::lock_guard<std::mutex> guard(state.job->lock);
        for (auto& text : state.job->notices) Post(std::move(text));
        state.job->notices.clear();
        found.swap(state.job->found);
        written.swap(state.job->written);
    }
    if (found) Learn(*found);
    for (const auto& write : written) Wrote(write);
    if (!finished) return;
    const auto job = std::move(state.job);
    state.job.reset();
    if (activeJob == job) activeJob.reset();
    state.book = std::move(job->book);
    state.jobUnits = 0;
    // A check that told nothing of its key file (an unexpected failure): not
    // verified. Every check thus leaves its key file out of Saved: never twice.
    // One that told something, Saved again since, was asked once more
    // (« Clé non vérifiée » clicked meanwhile): that request stands.
    if (job->check && !job->told && *job->check == state.known.file && state.known.status == KeyStatus::Saved)
        state.known.status = KeyStatus::Unverified;
    if (job->listed) Keep(std::move(*job->listed));
    if (job->kraloveRead) KeepKralove(std::move(*job->kraloveRead));
    if (job->off.load()) Discard(false); // turned off while it ran: nothing of it is resumed
}
void Start(bool enabled) {
    for (auto& group : state.inbox) Merge(state.book.groups, std::move(group));
    state.inbox.clear();
    const auto now = timing.now();
    const bool due = enabled && Units(state.book) > 0 && (now < state.lastCapture || now - state.lastCapture >= DebounceMs) && now >= state.book.notBefore;
    // The chat part's list: never while a capture waits (the captures go first),
    // nor with a key being saved or forgotten.
    const bool listing = Units(state.book) == 0 && !state.submit && !state.forget && now >= state.book.notBefore && ListingDue(now);
    // The calendar's openings: likewise.
    const bool kralove = Units(state.book) == 0 && !state.submit && !state.forget && now >= state.book.notBefore && KraloveDue(now);
    // The key row's check: dropped once its key file's status is known, never before a pause ends.
    if (state.known.status != KeyStatus::Saved) state.check = false;
    const bool check = state.check && now >= state.book.notBefore;
    if (!due && !check && !listing && !kralove && !state.submit && !state.forget) return;
    if (activeJob && !activeJob->done.load()) return;
    auto job = std::make_shared<Job>();
    job->book = std::move(state.book);
    state.book = Book{};
    job->submitting = state.submit.has_value();
    job->submit = std::move(state.submit);
    state.submit.reset();
    job->forget = std::exchange(state.forget, false);
    job->cycle = due;
    job->listing = listing;
    job->kralove = kralove;
    if (listing || kralove) job->character = Character();
    if (kralove) {
        job->kraloveFromDay = KraloveFromDay();
        job->kraloveFrom = DateText(job->kraloveFromDay);
    }
    if (check) { job->check = state.known.file; state.check = false; }
    job->keyPath = state.keyPath;
    job->transport = state.transport;
    state.jobUnits = Units(job->book);
    state.job = activeJob = job;
    if (inlineJobs) { Work(*job); return; }
    // Only plain C++ data crosses to the worker: no IL2CPP call there.
    try { std::thread([job] { Work(*job); }).detach(); }
    catch (...) {
        // No thread: everything goes back and is tried again a little later.
        state.book = std::move(job->book);
        state.book.notBefore = now + BusyMs;
        state.submit = std::move(job->submit);
        state.forget = state.forget || job->forget;
        state.check = state.check || job->check.has_value();
        state.job.reset(); activeJob.reset(); state.jobUnits = 0;
        throw;
    }
}
}

void InstallSync(const std::filesystem::path& settingsPath, Transport transport, Notice notice, GameView game) {
    try {
        Cancel();
        state = State{};
        state.keyPath = KeyPath(settingsPath);
        state.transport = std::move(transport);
        state.notice = std::move(notice);
        state.game = std::move(game);
        state.installed = static_cast<bool>(state.transport);
        ++kraloveVersion;
        if (const auto file = Identity(state.keyPath)) Track(*file); // a key file there: Saved until checked
        Log(state.installed ? "METAMOB_SYNC_READY" : "METAMOB_SYNC_UNAVAILABLE");
    } catch (...) {
        state.installed = false;
        Error("METAMOB_SYNC_UNAVAILABLE");
    }
}
void TickSync(bool enabled) {
    try {
        if (!state.installed) return;
        if (!enabled && state.job) state.job->off.store(true); // a running cycle writes nothing more
        Collect();
        if (!enabled) Discard(true);
        Follow();
        if (!state.job) Start(enabled);
        Collect(); // a job run inline has already finished
        NoKeyTold();
        const auto notices = std::move(state.outbox);
        state.outbox.clear();
        for (const auto& text : notices) {
            try { if (state.notice) state.notice(text); }
            catch (...) { Error("METAMOB_NOTICE_ERROR"); }
        }
    } catch (...) { Error("METAMOB_SYNC_ERROR"); }
}
void RestoreSync() {
    try {
        Cancel();
        if (const int pending = Pending(); state.installed && pending > 0) Log("METAMOB_SYNC_STOP pending=" + std::to_string(pending));
        // A forget confirmed but not given to a worker yet is not lost to the Stop:
        // one local file, no request (a worker given one does it even when stopped).
        if (state.forget) {
            try { ForgetKey(state.keyPath); Log("METAMOB_KEY_FORGOTTEN"); } catch (...) { Log("METAMOB_KEY_FORGET_FAILED"); }
        }
        state = State{}; // a key not verified yet is wiped; a running worker ends on its own
        ++kraloveVersion;
    } catch (...) {}
}
void EnqueueCapture(Capture capture) {
    try {
        if (!state.installed) return;
        if ((capture.type != TypeBoss && capture.type != TypeArchimonster) || capture.quantity < 1 || capture.quantity > MaxQuantity ||
            capture.monsterId <= 0 || capture.name.empty()) {
            Log("METAMOB_CAPTURE_IGNORED monster=" + std::to_string(capture.monsterId));
            return;
        }
        // A key being verified counts: if it is refused, the cycle says there is no key.
        const bool keyComing = state.submit.has_value() || (state.job && state.job->submitting);
        if (!keyComing && !HasKey(state.keyPath)) {
            // Told at the next pump, with the other bosses of the same soul.
            auto shown = Shown(capture.name);
            if (std::find(state.noKey.begin(), state.noKey.end(), shown) == state.noKey.end() && state.noKey.size() < MaxNotices)
                state.noKey.push_back(std::move(shown));
            Log("METAMOB_NO_KEY monster=" + std::to_string(capture.monsterId));
            return;
        }
        if (Pending() + capture.quantity > MaxPending) { Log("METAMOB_QUEUE_FULL monster=" + std::to_string(capture.monsterId)); return; }
        Merge(state.inbox, Group{std::move(capture.characterName), std::move(capture.name), capture.monsterId, capture.type, capture.quantity});
        state.lastCapture = timing.now();
    } catch (...) { Error("METAMOB_CAPTURE_ERROR"); }
}
void SubmitKey(std::string key) {
    try {
        if (state.installed) {
            const auto first = key.find_first_not_of(" \t\r\n");
            const auto trimmed = first == std::string::npos ? std::string_view() :
                std::string_view(key).substr(first, key.find_last_not_of(" \t\r\n") + 1 - first);
            if (!ValidKey(trimmed)) { Post(KeyInvalid); Log("METAMOB_KEY_INVALID"); }
            else state.submit.emplace(std::string(trimmed)); // built at its final size: no stray copy; replaces one not sent yet
        }
    } catch (...) { Error("METAMOB_KEY_ERROR"); }
    Wipe(key);
}
void ForgetStoredKey() {
    try {
        if (!state.installed) return;
        state.submit.reset(); // a key not verified yet is dropped as well
        state.forget = true;
        Unlist();             // and the chat part's list read with it
        state.check = false;  // and a check not started yet
        if (state.job) state.job->forgetting.store(true); // a running cycle sends nothing more with that key
    } catch (...) { Error("METAMOB_KEY_ERROR"); }
}
bool KeyStored() {
    try { return state.installed && HasKey(state.keyPath); } catch (...) { return false; }
}
KeyStatus StoredKeyStatus() {
    try {
        if (!state.installed || Forgetting()) return KeyStatus::None;
        if (const auto file = Identity(state.keyPath)) Track(*file); // a Windows error: what is known stays
        return state.known.status;
    } catch (...) { return KeyStatus::None; }
}
void CheckStoredKey(bool again) {
    try {
        if (!state.installed || Forgetting()) return;
        // « Clé non vérifiée » clicked: that key file is checked once more, shown Saved meanwhile.
        if (again && state.known.status == KeyStatus::Unverified) state.known.status = KeyStatus::Saved;
        // Once per key file and session: a check, answered or not, leaves its file out of Saved (Learn, Collect).
        if (state.known.status == KeyStatus::Saved) state.check = true;
    } catch (...) { Error("METAMOB_KEY_ERROR"); }
}
std::string MetamobChatCaption(int monsterId, bool colorStatus) {
    try {
        const auto part = ChatPart(monsterId);
        return part.shown ? ChatCaption(part, colorStatus) : std::string();
    } catch (...) { return {}; }
}
std::string MetamobChatDetail(int monsterId) {
    try {
        const auto part = ChatPart(monsterId);
        return part.shown ? ChatDetail(part) : std::string();
    } catch (...) { return {}; }
}
void WantMetamobCatalog() {
    if (!state.installed) return;
    state.wanted = true;
    state.wantedAt = timing.now();
}
OcreQuantity MetamobOcreQuantity(int monsterId) {
    try {
        const auto part = ChatPart(monsterId);
        if (!part.shown) return {};
        if (part.quantity >= 0) return {true, part.quantity, OcreList::Read};
        const auto list = part.problem == QuestProblem::NoQuest ? OcreList::NoQuest :
            part.problem == QuestProblem::Several ? OcreList::SeveralQuests :
            state.catalog.failed ? OcreList::Failed : OcreList::NotRead;
        return {true, -1, list};
    } catch (...) { return {}; }
}
void WantKralove() {
    if (!state.installed) return;
    state.kraloveWanted = true;
    state.kraloveWantedAt = timing.now();
}
KraloveView MetamobKralove() {
    try {
        KraloveView view;
        if (!state.installed || !Usable()) return view;
        const auto& kralove = state.kralove;
        view.usable = true;
        view.status = kralove.status;
        view.read = kralove.read;
        view.server = kralove.server;
        view.fromDay = kralove.fromDay;
        view.openings = kralove.openings;
        view.version = kraloveVersion;
        return view;
    } catch (...) { return {}; }
}
// Nothing of the game is held: Restore has nothing to wait for.
bool SyncCleanupPending() { return false; }

// Test seam, declared by metamob-sync-tests.cpp only (not part of the module's
// interface): a fake clock (all three null: the real one), jobs run inside
// TickSync on the calling thread, and whether a worker is still running.
namespace testing {
void SetClock(uint64_t (*now)(), int64_t (*epoch)(), void (*sleep)(uint64_t)) {
    timing = now && epoch && sleep ? Clock{now, epoch, sleep} : Clock{};
    lastRequest.store(0);
}
void RunJobsInline(bool value) { inlineJobs = value; }
bool WorkerRunning() { return activeJob && !activeJob->done.load(); }
}
}
