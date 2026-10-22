#include "metamob-sync.hpp"
#include "metamob-data.hpp"
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

void Log(const std::string& text);

// The game thread queues captures and key operations; one detached worker at a
// time takes the whole state ("book"), runs Work (key operations then one sync
// cycle, plain C++ only: no IL2CPP call), and hands it back to TickSync, which
// posts its notices. Logs carry fixed codes and numbers only: never the key, a
// header, a body, the username, a slug or a character name.
namespace metamob {
namespace {
// Visible texts: local chat notices, prefix included.
constexpr char KeyInvalid[] = "Metamob : clé invalide (lettres, chiffres et symboles, sans espace).",
    KeyRejected[] = "Metamob : clé refusée par Metamob, rien n’a été enregistré.",
    KeyUnverified[] = "Metamob : clé enregistrée ; Metamob est injoignable pour la vérifier.",
    KeyNotStored[] = "Metamob : impossible d’enregistrer la clé sur ce PC.",
    KeyForgotten[] = "Metamob : clé oubliée.", KeyNotForgotten[] = "Metamob : impossible d’oublier la clé sur ce PC.",
    KeyRefused[] = "Metamob : clé refusée (révoquée ou régénérée ?). Enregistre une nouvelle clé dans les options.",
    KeyUnreadable[] = "Metamob : clé illisible sur ce compte Windows, enregistre-la à nouveau.";
std::string Quoted(const std::string& name) { return "« " + name + " »"; }
std::string Number(int value) { return std::to_string(value); }
std::string SavedText(const std::string& user) { return "Metamob : clé enregistrée pour le compte " + Quoted(user) + "."; }
std::string AddedText(const std::string& name, int before, int after) {
    return "Metamob : +" + Number(after - before) + " " + Quoted(name) + " (" + Number(before) + " → " + Number(after) + ").";
}
std::string NoKeyText(const std::string& name) {
    return "Metamob : capture de " + Quoted(name) + " non synchronisée : aucune clé enregistrée (Options › Dofus Enhanced › Pierre d’âme).";
}
std::string UnmatchedText(const std::string& name) { return "Metamob : " + Quoted(name) + " introuvable dans ta quête Ocre, rien n’a été modifié."; }
std::string CappedText(const std::string& name) {
    return "Metamob : " + Quoted(name) + " est déjà à " + Number(MaxQuantity) + ", la quantité maximale.";
}
std::string UnreachableText(int pending) {
    return "Metamob injoignable : " + Number(pending) + " capture(s) en attente, nouvel essai automatique.";
}
std::string ChangedText(const std::string& name, int before, int after) {
    return "Metamob : " + Quoted(name) + " a changé pendant l’envoi (" + Number(before) + " → " + Number(after) + ") ; vérifie ta quête sur Metamob.";
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
constexpr wchar_t SenderName[] = L"Local\\DofusMiniatures.Metamob";
constexpr char QuestsRoute[] = "/quests", ListRoute[] = "/quests/*/monsters", WriteRoute[] = "/quests/*/monsters/*";

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
// Everything a cycle reads and updates. The game thread owns it while no worker runs.
struct Book {
    std::vector<Group> groups;
    std::vector<Flight> flights;
    std::optional<std::vector<Quest>> quests;                   // GET /quests, kept while it picks a quest
    std::map<std::string, std::vector<QuestMonster>> monsters;  // per quest slug: ids and steps (quantities are read again)
    int failures = 0;           // consecutive transient failures
    uint64_t notBefore = 0;     // backoff, Retry-After, a busy sender, the refused key check
    bool refused = false;       // Metamob refused the key (its JSON 401/403): paused until it changes
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

struct Job {
    Book book;
    std::optional<Secret> submit;   // a key to verify with GET /me, then store
    bool forget = false, cycle = false, submitting = false;
    std::filesystem::path keyPath;
    Transport transport;
    std::atomic_bool cancelled{false}, done{false};
    // Asked by the game thread, read by the cycle only, never by the transport: a
    // request under way ends normally, so a write Metamob has applied is confirmed.
    std::atomic_bool off{false},    // the sync was turned off: nothing more is written
        forgetting{false};          // « Supprimer » confirmed: nothing more is sent with the key
    std::mutex lock;
    std::vector<std::string> notices; // taken by TickSync, posted on the game thread
    void Post(std::string text) {
        std::lock_guard<std::mutex> guard(lock);
        if (notices.size() < MaxNotices) notices.push_back(std::move(text));
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
    int requests = 0, done = 0;
    bool rediscovered = false, askQuestsAgain = false;
    std::vector<std::string> problems; // quest choice notices already given in this cycle
};
// Metamob's own refusal of the key is JSON ({"message":...}); a 401/403 page that
// is not (a Cloudflare challenge, a filtering proxy) says nothing about the key.
bool KeyRefusal(const Response& response) { return (response.status == 401 || response.status == 403) && response.json; }
// Network errors, 5xx, a page that is not JSON (Cloudflare), an unreadable answer:
// 30 s, then twice longer each time up to 15 min; told at the 3rd in a row.
[[noreturn]] void Backoff(Session& s) {
    auto& book = s.book;
    book.failures = std::min(book.failures + 1, 1000);
    book.notBefore = timing.now() + std::min<uint64_t>(BackoffCapMs, BackoffMs << std::min(book.failures - 1, 5));
    if (book.failures == 3) s.job.Post(UnreachableText(Units(book)));
    throw Halt{};
}
// The key refused: the captures are kept until it changes; said once. The same
// key is tried again every 10 min (refused again: no new notice).
[[noreturn]] void Refuse(Session& s) {
    if (!s.book.refused) { s.job.Post(KeyRefused); Log("METAMOB_KEY_REFUSED"); }
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
    throw Halt{};
}
[[noreturn]] void BadAnswer(Session& s, const char* route) {
    Log(std::string("METAMOB_BAD_ANSWER route=") + route);
    Backoff(s);
}
// The body of a 2xx JSON answer; anything else stops the cycle.
std::string Get(Session& s, std::string path, const char* route, bool questPath) {
    if (s.job.forgetting.load()) throw Halt{}; // nothing more is sent with a key being forgotten
    ++s.requests;
    auto response = Send(s.job, s.key, "GET", std::move(path), route);
    if (s.job.cancelled.load()) throw Halt{};
    if (response.status >= 200 && response.status < 300 && response.json) return std::move(response.body);
    if (KeyRefusal(response)) Refuse(s);
    if (response.status == 429) Limit(s, response);
    if (response.status == 404 && questPath) throw QuestGone{};
    Backoff(s);
}
// The parsers' errors are fixed texts, but none of them is logged either.
template<class Parse> auto Parsed(Session& s, const char* route, Parse parse) {
    try { return parse(); } catch (...) { BadAnswer(s, route); }
}
// The character's quest. No quest or an ambiguous choice: told once per cycle,
// and the list is asked again for the next capture (the user may fix it meanwhile).
std::optional<std::string> QuestSlug(Session& s, const std::string& character) {
    if (!s.book.quests) {
        const auto body = Get(s, "/quests", QuestsRoute, false);
        s.book.quests = Parsed(s, QuestsRoute, [&] { return ParseQuests(body); });
    }
    auto choice = ChooseQuest(*s.book.quests, character);
    if (choice.quest) return std::move(choice.quest->slug);
    if (std::find(s.problems.begin(), s.problems.end(), choice.problem) == s.problems.end()) {
        s.job.Post(choice.problem);
        s.problems.push_back(std::move(choice.problem));
    }
    s.askQuestsAgain = true;
    return std::nullopt;
}
// The quest's whole monster list (Unity Ocre: 337, two pages), kept for the session.
const std::vector<QuestMonster>& Monsters(Session& s, const std::string& slug) {
    if (const auto known = s.book.monsters.find(slug); known != s.book.monsters.end()) return known->second;
    std::vector<QuestMonster> monsters;
    for (int offset = 0, pages = 0;; ++pages) {
        if (pages == MaxPages) BadAnswer(s, ListRoute);
        const auto body = Get(s, "/quests/" + slug + "/monsters?limit=200&offset=" + std::to_string(offset), ListRoute, true);
        auto page = Parsed(s, ListRoute, [&] { return ParseMonsters(body); });
        if (page.slug != slug || page.offset != offset) BadAnswer(s, ListRoute);
        offset += static_cast<int>(page.monsters.size());
        monsters.insert(monsters.end(), std::make_move_iterator(page.monsters.begin()), std::make_move_iterator(page.monsters.end()));
        if (page.monsters.empty() || offset >= page.total) break;
    }
    return s.book.monsters.emplace(slug, std::move(monsters)).first->second;
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
    s.job.Post(AddedText(flight.shown, flight.base, flight.target));
    if (flight.base + flight.group.count > flight.target) s.job.Post(CappedText(flight.shown)); // the rest stays uncounted
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
    std::optional<Secret> key;
    try { key = LoadKey(job.keyPath); }
    catch (...) {
        // Another Windows account or PC, a corrupt file: the captures cannot be sent.
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
    Session s{job, book, *key};
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
// Options › « Enregistrer »: GET /v1/me with the candidate key. Refused by
// Metamob (its JSON 401/403): nothing stored. Otherwise stored, verified or not
// (Metamob unreachable, or a page that is not its answer).
void Submit(Job& job) {
    const Secret key = std::move(*job.submit);
    job.submit.reset();
    const auto response = Send(job, key, "GET", "/me", "/me");
    if (job.cancelled.load() || job.forgetting.load()) return; // stopped, or « Supprimer » confirmed since: not stored
    if (KeyRefusal(response)) { job.Post(KeyRejected); Log("METAMOB_KEY_REJECTED"); return; }
    std::optional<std::string> user;
    if (response.status >= 200 && response.status < 300 && response.json) {
        try { user = SafeUsername(ParseMe(response.body).username); } catch (...) {}
    }
    try { StoreKey(job.keyPath, key); }
    catch (...) { job.Post(KeyNotStored); Log("METAMOB_KEY_STORE_FAILED"); return; }
    Log(user ? "METAMOB_KEY_SAVED verified=1" : "METAMOB_KEY_SAVED verified=0");
    job.Post(user ? SavedText(*user) : std::string(KeyUnverified));
    Fresh(job.book);
    if (response.status == 429)
        job.book.notBefore = timing.now() + 1000 * static_cast<uint64_t>(RetryAfterSeconds(response.retryAfter, timing.epoch()).value_or(DefaultRetryAfter));
}
// Options › « Supprimer » (second click): the key file goes, and so do the
// captures that can no longer be sent.
void Forget(Job& job) {
    try { ForgetKey(job.keyPath); }
    catch (...) { job.Post(KeyNotForgotten); Log("METAMOB_KEY_FORGET_FAILED"); return; }
    Log("METAMOB_KEY_FORGOTTEN");
    job.Post(KeyForgotten);
    Abandon(job);
    job.book = Book{};
}
// The synchronous core: what one worker does, in the order the user asked.
void Work(Job& job) noexcept {
    try {
        // A confirmed forget is done even by a stopped job: one local file, no request.
        if (job.forget) Forget(job);
        // A key typed before a forget confirmed while this job runs is never sent nor stored.
        if (job.submit && !job.cancelled.load() && !job.forgetting.load()) Submit(job);
        if (job.cycle && !job.cancelled.load() && !Ending(job) && Units(job.book) > 0) Cycle(job);
    } catch (...) {
        try { Log("METAMOB_WORKER_ERROR"); } catch (...) {}
    }
    // A stopped job is never collected (Install, Restore): its book goes, and with
    // it the announcement of a write it holds. Its end and its cancel (Cancel) are
    // handed over under its lock: whichever comes second empties the book.
    try {
        std::lock_guard<std::mutex> guard(job.lock);
        if (job.cancelled.load()) Empty(job.book);
        job.done.store(true);
    } catch (...) { job.done.store(true); }
}

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
    std::vector<std::string> outbox;
    uint64_t lastCapture = 0, nextError = 0;
    int jobUnits = 0;
    std::shared_ptr<Job> job;
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

void Post(std::string text) { if (state.outbox.size() < MaxNotices) state.outbox.push_back(std::move(text)); }
int Pending() { return Units(state.book) + Count(state.inbox) + state.jobUnits; }
void Error(const char* code) noexcept {
    try {
        const auto now = timing.now();
        if (now < state.nextError) return;
        state.nextError = now + ErrorLogMs;
        Log(code);
    } catch (...) {}
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
    {
        std::lock_guard<std::mutex> guard(state.job->lock);
        for (auto& text : state.job->notices) Post(std::move(text));
        state.job->notices.clear();
    }
    if (!finished) return;
    const auto job = std::move(state.job);
    state.job.reset();
    if (activeJob == job) activeJob.reset();
    state.book = std::move(job->book);
    state.jobUnits = 0;
    if (job->off.load()) Discard(false); // turned off while it ran: nothing of it is resumed
}
void Start(bool enabled) {
    for (auto& group : state.inbox) Merge(state.book.groups, std::move(group));
    state.inbox.clear();
    const auto now = timing.now();
    const bool due = enabled && Units(state.book) > 0 && (now < state.lastCapture || now - state.lastCapture >= DebounceMs) && now >= state.book.notBefore;
    if (!due && !state.submit && !state.forget) return;
    if (activeJob && !activeJob->done.load()) return;
    auto job = std::make_shared<Job>();
    job->book = std::move(state.book);
    state.book = Book{};
    job->submitting = state.submit.has_value();
    job->submit = std::move(state.submit);
    state.submit.reset();
    job->forget = std::exchange(state.forget, false);
    job->cycle = due;
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
        state.job.reset(); activeJob.reset(); state.jobUnits = 0;
        throw;
    }
}
}

void InstallSync(const std::filesystem::path& settingsPath, Transport transport, Notice notice) {
    try {
        Cancel();
        state = State{};
        state.keyPath = KeyPath(settingsPath);
        state.transport = std::move(transport);
        state.notice = std::move(notice);
        state.installed = static_cast<bool>(state.transport);
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
        if (!state.job) Start(enabled);
        Collect(); // a job run inline has already finished
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
            Post(NoKeyText(Shown(capture.name)));
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
        if (state.job) state.job->forgetting.store(true); // a running cycle sends nothing more with that key
    } catch (...) { Error("METAMOB_KEY_ERROR"); }
}
bool KeyStored() {
    try { return state.installed && HasKey(state.keyPath); } catch (...) { return false; }
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
