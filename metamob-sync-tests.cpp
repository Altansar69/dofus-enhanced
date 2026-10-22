#include "metamob-sync.hpp"
#include "metamob-data.hpp"
#include "metamob-heap-probe.hpp"
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <ctime>
#include <deque>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

// Test seam of metamob-sync.cpp, not part of the module's interface: a fake
// clock (no real waiting), jobs run inside TickSync, and the worker state.
namespace metamob::testing {
void SetClock(uint64_t (*now)(), int64_t (*epoch)(), void (*sleep)(uint64_t));
void RunJobsInline(bool value);
bool WorkerRunning();
}

namespace {
using namespace metamob;
using Path = std::filesystem::path;
// Fake keys only (CGU 5.3: a real key never appears in a test). The slugs, the
// character, the account name and the marker planted in every body are private:
// no 4-byte piece of them may reach a log or a notice.
const std::string FakeKey = "TEST-KEY-0123456789-NOT-REAL", OtherKey = "TEST-KEY-9876543210-NOT-REAL";
const std::string Slug = "s1ugZq9xQw", OtherSlug = "s2ugRt7yUi", Character = "Altanzorg", User = "Zyxwvut99", Marker = "Corps-Prive-Zorglub";
constexpr wchar_t SenderName[] = L"Local\\DofusMiniatures.Metamob";
constexpr int64_t FakeEpochValue = 1792670000; // a fixed date for Retry-After

void Check(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }
bool Leaks(const std::string& text, const std::string& secret) {
    for (size_t i = 0; i + 4 <= secret.size(); ++i) if (text.find(secret.substr(i, 4)) != std::string::npos) return true;
    return false;
}

// Logs (the worker writes them too) and notices (posted by TickSync on this thread).
std::mutex logLock;
std::vector<std::string> logs, allLogs, notices, allNotices;
// The options' « Synchroniser les captures avec Metamob », passed at every TickSync.
bool syncOn = true;

std::atomic<uint64_t> fakeNow{10000000};
uint64_t FakeNow() { return fakeNow.load(); }
int64_t FakeEpoch() { return FakeEpochValue; }
std::function<void()> onSleep; // runs once, at the next fake sleep (inline jobs only)
void FakeSleep(uint64_t milliseconds) {
    fakeNow += milliseconds;
    if (onSleep) { auto hook = std::move(onSleep); onSleep = nullptr; hook(); }
}

// Scripted transport: each request must be the next expected one, with the
// expected key; it records when, from which thread and whether the sender lock
// was held by another thread at that moment.
struct Exchange { std::string method, path, body, key; Response response; bool raise = false; };
struct Call { std::string method, path, body, key; uint64_t at = 0, real = 0; DWORD thread = 0; bool held = false; };
std::mutex scriptLock;
std::deque<Exchange> script;
std::vector<Call> calls;
std::string surprise, keyNow = FakeKey;
std::function<Response(const Request&, const std::atomic_bool&)> intercept; // replaces the script (blocking requests)
std::atomic_bool entered{false};

bool SenderHeld() {
    return std::async(std::launch::async, [] {
        const auto handle = OpenMutexW(SYNCHRONIZE, FALSE, SenderName);
        if (!handle) return false;
        const auto result = WaitForSingleObject(handle, 0);
        if (result == WAIT_OBJECT_0 || result == WAIT_ABANDONED) ReleaseMutex(handle);
        CloseHandle(handle);
        return result == WAIT_TIMEOUT;
    }).get();
}
Response Scripted(const Request& request, const Secret& key, const std::atomic_bool& cancelled) {
    if (intercept) return intercept(request, cancelled);
    const bool held = SenderHeld();
    std::lock_guard<std::mutex> guard(scriptLock);
    calls.push_back({request.method, request.path, request.body, key.value(), fakeNow.load(), GetTickCount64(), GetCurrentThreadId(), held});
    // Anything unexpected is recorded and gets no answer (the script is not consumed).
    const auto seen = request.method + " " + request.path + " " + request.body;
    if (script.empty()) { if (surprise.empty()) surprise = seen; return Response{}; }
    if (script.front().method != request.method || script.front().path != request.path || script.front().body != request.body ||
        script.front().key != key.value()) {
        if (surprise.empty()) surprise = seen + " instead of " + script.front().method + " " + script.front().path + " " + script.front().body;
        return Response{};
    }
    auto next = std::move(script.front());
    script.pop_front();
    // A failing transport whose text quotes the key and the path: never logged.
    if (next.raise) throw std::runtime_error("transport failed with " + key.value() + " on " + request.path);
    return next.response;
}

Response Json(std::string body, int status = 200) {
    Response response;
    response.status = status; response.sent = true; response.json = true; response.body = std::move(body);
    return response;
}
// Error bodies as the community client documents them, with the private marker.
Response Status(int status, std::string retryAfter = {}) {
    auto response = Json(status == 401 ? "{\"message\":\"The access token is invalid. " + Marker + "\"}" :
        "{\"status\":" + std::to_string(status) + ",\"error\":" + std::to_string(status) + ",\"messages\":{\"error\":\"" + Marker + "\"}}", status);
    response.retryAfter = std::move(retryAfter);
    return response;
}
Response Html(int status) {
    Response response;
    response.status = status; response.sent = true; response.body = "<html><title>Cloudflare " + Marker + "</title></html>";
    return response;
}
Response Lost(unsigned long error, bool sent) {
    Response response;
    response.error = error; response.sent = sent;
    return response;
}
std::string HttpDate(int64_t seconds) {
    const __time64_t time = seconds;
    tm parts{};
    _gmtime64_s(&parts, &time);
    char text[64]{};
    std::strftime(text, sizeof(text), "%a, %d %b %Y %H:%M:%S GMT", &parts);
    return text;
}

std::string MeBody(const std::string& username) {
    return "{\"data\":{\"username\":\"" + username + "\",\"bio\":\"" + Marker + "\",\"avatar\":null,\"last_active\":\"2026-10-22T10:00:00Z\"}}";
}
std::string Quest(const std::string& slug, const char* character, const char* type, int version) {
    const std::string name = character ? "\"" + std::string(character) + "\"" : std::string("null");
    return "{\"slug\":\"" + slug + "\",\"character_name\":" + name + ",\"current_step\":4,\"parallel_quests\":6,\"show_trades\":true,"
        "\"is_favorite\":false,\"server\":{\"id\":11,\"name\":\"Draconiros\",\"community\":\"fr\",\"game_version\":{\"id\":" +
        std::to_string(version) + ",\"name\":\"Dofus\"}},\"quest_template\":{\"id\":1,\"quest_type\":{\"id\":1,\"slug\":\"" + type +
        "\",\"name\":{\"fr\":\"Ocre\",\"en\":\"" + Marker + "\",\"es\":\"Ocre\"}},\"monster_count\":337,\"step_count\":18}}";
}
// Real Unity Ocre monsters (Metamob ids, steps, types), and generated ones up to the 337 of the quest.
struct Row { int64_t id; std::string name; int type, step, quantity; };
const Row Bouftou{305, "Bouftou Royal", TypeBoss, 1, 5}, Mob{312, "Mob l'Éponge", TypeBoss, 1, 0},
    Tofu{370, "Tofumanchou l'Empereur", TypeArchimonster, 4, 2}, Tiwa{387, "Tiwa'Missou le Gateux", TypeArchimonster, 5, 3},
    Abrakroc{432, "Abrakroc l'édenté", TypeArchimonster, 8, 0}, Larva{565, "Larvaloeil l'Émue", TypeArchimonster, 14, 1};
std::vector<Row> Catalog() {
    std::vector<Row> rows{Bouftou, Mob, Tofu, Tiwa, Abrakroc, Larva};
    for (int64_t id = 301; rows.size() < 337; ++id) {
        if (std::any_of(rows.begin(), rows.end(), [&](const Row& row) { return row.id == id; })) continue;
        const int type = id < 352 ? TypeBoss : TypeArchimonster;
        rows.push_back({id, "Monstre d'essai " + std::to_string(id), type, type == TypeBoss ? 1 + static_cast<int>(id % 3) : 4 + static_cast<int>(id % 15), 1});
    }
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.id < b.id; });
    return rows;
}
std::string MonsterJson(const Row& row) {
    return "{\"id\":" + std::to_string(row.id) + ",\"name\":{\"fr\":\"" + row.name + "\",\"en\":\"" + Marker + "\",\"es\":null},"
        "\"image\":\"m.png\",\"level_min\":60,\"level_max\":60,\"type\":{\"id\":" + std::to_string(row.type) +
        ",\"name\":{\"fr\":\"Archimonstre\",\"en\":\"Archmonster\",\"es\":\"Archimonstruo\"}},\"step\":" + std::to_string(row.step) +
        ",\"quantity\":" + std::to_string(row.quantity) + ",\"want\":0,\"offer\":0,\"trade_offer\":null,\"trade_want\":null}";
}
std::string Page(const std::string& slug, const std::vector<Row>& rows, size_t total, size_t offset) {
    std::string items;
    for (const auto& row : rows) items += (items.empty() ? "" : ",") + MonsterJson(row);
    return "{\"data\":{\"slug\":\"" + slug + "\",\"monsters\":[" + items + "],\"pagination\":{\"total\":" + std::to_string(total) +
        ",\"limit\":200,\"offset\":" + std::to_string(offset) + "}}}";
}
std::string StepPath(const std::string& slug, int step) { return "/quests/" + slug + "/monsters?step=" + std::to_string(step) + "&limit=200"; }

void Expect(std::string method, std::string path, Response response, std::string body = {}, bool raise = false) {
    std::lock_guard<std::mutex> guard(scriptLock);
    script.push_back({std::move(method), std::move(path), std::move(body), keyNow, std::move(response), raise});
}
void ExpectQuests(const std::vector<std::string>& quests) {
    std::string items;
    for (const auto& quest : quests) items += (items.empty() ? "" : ",") + quest;
    Expect("GET", "/quests", Json("{\"data\":[" + items + "]}"));
}
void ExpectList(const std::string& slug) {
    const auto rows = Catalog();
    Expect("GET", "/quests/" + slug + "/monsters?limit=200&offset=0", Json(Page(slug, {rows.begin(), rows.begin() + 200}, rows.size(), 0)));
    Expect("GET", "/quests/" + slug + "/monsters?limit=200&offset=200", Json(Page(slug, {rows.begin() + 200, rows.end()}, rows.size(), 200)));
}
// The step read again before a write: the monster at `quantity`, and a neighbour.
void ExpectStep(const std::string& slug, const Row& row, int quantity) {
    auto current = row;
    current.quantity = quantity;
    const Row neighbour{999, "Voisin d'étape", row.type, row.step, 7};
    Expect("GET", StepPath(slug, row.step), Json(Page(slug, {neighbour, current}, 2, 0)));
}
void ExpectWrite(const std::string& slug, const Row& row, int target, Response response) {
    Expect("PATCH", "/quests/" + slug + "/monsters/" + std::to_string(row.id), std::move(response), "{\"quantity\":" + std::to_string(target) + "}");
}
Response Written(const Row& row, int quantity) {
    return Json("{\"data\":{\"monster_id\":" + std::to_string(row.id) + ",\"quantity\":" + std::to_string(quantity) + ",\"owned\":" +
        std::to_string(quantity) + ",\"status\":0,\"effective_offer\":0,\"effective_want\":0}}");
}
// A first sync of one capture: the quests, both list pages, the step, the write.
void ExpectFirst(const Row& row, int before, int after) {
    ExpectQuests({Quest(Slug, Character.c_str(), "ocre", 1)});
    ExpectList(Slug);
    ExpectStep(Slug, row, before);
    ExpectWrite(Slug, row, after, Written(row, after));
}
Capture Captured(const Row& row, int quantity = 1) {
    Capture capture;
    capture.characterId = 4242; capture.characterName = Character;
    capture.monsterId = 10000 + row.id; // the game's own id, logged; not Metamob's
    capture.name = row.name; capture.type = row.type; capture.quantity = quantity;
    return capture;
}

// The visible texts (DESIGN r106), written here independently of the module.
const std::string KeyInvalid = "Metamob : clé invalide (lettres, chiffres et symboles, sans espace).",
    KeyRejected = "Metamob : clé refusée par Metamob, rien n’a été enregistré.",
    KeyUnverified = "Metamob : clé enregistrée ; Metamob est injoignable pour la vérifier.",
    KeyNotStored = "Metamob : impossible d’enregistrer la clé sur ce PC.", KeyForgotten = "Metamob : clé oubliée.",
    KeyRefused = "Metamob : clé refusée (révoquée ou régénérée ?). Enregistre une nouvelle clé dans les options.",
    KeyUnreadable = "Metamob : clé illisible sur ce compte Windows, enregistre-la à nouveau.",
    SavedUser = "Metamob : clé enregistrée pour le compte « Zyxwvut99 ».",
    NoQuest = "Metamob : aucune quête Ocre Dofus Unity sur ton compte, rien n’a été modifié.",
    SeveralQuests = "Metamob : plusieurs quêtes Ocre sur ton compte ; indique le nom de ce personnage dans « Personnage à contacter » de la bonne quête.";
std::string Added(const Row& row, int before, int after) {
    return "Metamob : +" + std::to_string(after - before) + " « " + row.name + " » (" + std::to_string(before) + " → " + std::to_string(after) + ").";
}
std::string NoKey(const std::string& name) {
    return "Metamob : capture de « " + name + " » non synchronisée : aucune clé enregistrée (Options › Dofus Enhanced › Pierre d’âme).";
}
std::string Unmatched(const std::string& name) { return "Metamob : « " + name + " » introuvable dans ta quête Ocre, rien n’a été modifié."; }
std::string Capped(const Row& row) { return "Metamob : « " + row.name + " » est déjà à 30, la quantité maximale."; }
std::string Changed(const Row& row, int before, int after) {
    return "Metamob : « " + row.name + " » a changé pendant l’envoi (" + std::to_string(before) + " → " + std::to_string(after) +
        ") ; vérifie ta quête sur Metamob.";
}
std::string Refused(const Row& row) { return "Metamob : mise à jour refusée pour « " + row.name + " »."; }
std::string Unsure(const Row& row) { return "Metamob : envoi de « " + row.name + " » non confirmé ; vérifie ta quête sur Metamob."; }
std::string Dropped(int pending) {
    return "Metamob : synchronisation désactivée, " + std::to_string(pending) + " capture(s) en attente abandonnée(s), rien n’a été envoyé.";
}
std::string OffUnsure(const Row& row) {
    return "Metamob : synchronisation désactivée pendant l’envoi de « " + row.name + " » ; vérifie ta quête sur Metamob.";
}
std::string ForgetUnsure(const Row& row) {
    return "Metamob : clé oubliée pendant l’envoi de « " + row.name + " » ; vérifie ta quête sur Metamob.";
}

Path root;
Path Folder(const std::string& name) {
    const auto folder = root / name;
    std::filesystem::create_directories(folder);
    return folder / "DofusMiniatures-3.7.11.11-settings.ini";
}
void Noted(const std::string& text) { notices.push_back(text); allNotices.push_back(text); }
void Begin(const Path& settings, bool key) {
    {
        std::lock_guard<std::mutex> guard(scriptLock);
        script.clear(); calls.clear(); surprise.clear();
    }
    { std::lock_guard<std::mutex> guard(logLock); logs.clear(); }
    notices.clear(); keyNow = FakeKey; intercept = nullptr; onSleep = nullptr; syncOn = true;
    metamob::testing::SetClock(FakeNow, FakeEpoch, FakeSleep);
    metamob::testing::RunJobsInline(true);
    InstallSync(settings, Scripted, Noted);
    if (key) StoreKey(KeyPath(settings), Secret(FakeKey));
}
void Finish(const std::string& name) {
    RestoreSync();
    std::lock_guard<std::mutex> guard(scriptLock);
    Check(surprise.empty(), name + ": unexpected request " + surprise);
    Check(script.empty(), name + ": expected request never sent: " + (script.empty() ? std::string() : script.front().method + " " + script.front().path));
    Check(notices.empty(), name + ": notice left unchecked: " + (notices.empty() ? std::string() : notices.front()));
}
void Said(const std::vector<std::string>& expected, const std::string& what) {
    {
        std::lock_guard<std::mutex> guard(scriptLock);
        Check(surprise.empty(), what + ": unexpected request " + surprise);
    }
    if (notices != expected) {
        std::string seen;
        for (const auto& text : notices) seen += "\n  [" + text + "]";
        throw std::runtime_error(what + ": the notices were" + seen);
    }
    notices.clear();
}
bool Logged(const std::string& line) {
    std::lock_guard<std::mutex> guard(logLock);
    return std::find(logs.begin(), logs.end(), line) != logs.end();
}
size_t LoggedCount(const std::string& prefix) {
    std::lock_guard<std::mutex> guard(logLock);
    return static_cast<size_t>(std::count_if(logs.begin(), logs.end(), [&](const std::string& line) { return line.rfind(prefix, 0) == 0; }));
}
size_t Sent() { std::lock_guard<std::mutex> guard(scriptLock); return calls.size(); }
uint64_t LastAt() { std::lock_guard<std::mutex> guard(scriptLock); return calls.back().at; }
void Tick(uint64_t advance = 0) { fakeNow += advance; TickSync(syncOn); }
void TickAt(uint64_t at) { if (fakeNow.load() < at) fakeNow = at; TickSync(syncOn); }
template<class Ready> void Await(Ready ready, uint64_t limit, const std::string& what) {
    const auto end = GetTickCount64() + limit;
    while (!ready()) {
        Check(GetTickCount64() < end, "Timed out waiting for " + what);
        TickSync(syncOn);
        Sleep(10);
    }
}
// Another game client holding the sender lock until released.
struct Holder {
    HANDLE release = CreateEventW(nullptr, TRUE, FALSE, nullptr), held = CreateEventW(nullptr, TRUE, FALSE, nullptr),
        over = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::thread thread;
    bool released = false;
    Holder() {
        thread = std::thread([this] {
            const auto mutex = CreateMutexW(nullptr, FALSE, SenderName);
            WaitForSingleObject(mutex, INFINITE);
            SetEvent(held);
            WaitForSingleObject(release, 30000);
            ReleaseMutex(mutex);
            CloseHandle(mutex);
            SetEvent(over);
        });
        Check(WaitForSingleObject(held, 5000) == WAIT_OBJECT_0, "The other client holds the sender lock");
    }
    void Release() { SetEvent(release); WaitForSingleObject(over, 5000); released = true; }
    ~Holder() { SetEvent(release); if (thread.joinable()) thread.join(); CloseHandle(release); CloseHandle(held); CloseHandle(over); }
};

void KeySubmission() {
    const auto settings = Folder("key");
    const auto file = KeyPath(settings);
    Begin(settings, false);
    Check(!KeyStored() && !SyncCleanupPending(), "No key yet, nothing of the game held");
    // The format is checked first: such a text is never sent nor stored.
    for (const auto& bad : {std::string(), std::string(" \r\n"), std::string("court"), std::string("TEST KEY 0123456789"),
        std::string("TEST-KEY-0123456789-clé"), std::string(257, 'k'), std::string("TEST-KEY-0123\r\n456789-NOT-REAL")}) {
        SubmitKey(bad);
        Tick();
        Said({KeyInvalid}, "An invalid key is refused at once");
    }
    Check(Sent() == 0 && !KeyStored() && Logged("METAMOB_KEY_INVALID"), "An invalid key is neither sent nor stored");

    // Trimmed, verified with GET /me, stored, the account named.
    Expect("GET", "/me", Json(MeBody(User)));
    SubmitKey(" \t" + FakeKey + " \r\n");
    Tick();
    Said({SavedUser}, "A verified key");
    Check(KeyStored() && LoadKey(file)->value() == FakeKey, "The trimmed key is stored");
    Check(Logged("METAMOB_HTTP method=GET route=/me status=200") && Logged("METAMOB_KEY_SAVED verified=1"), "Key logs");
    // The account name is shown without markup nor control character (a JSON escape of BEL).
    Expect("GET", "/me", Json(MeBody("<b>Zyx</b>" + std::string(1, '\\') + "u0007wvut")));
    SubmitKey(FakeKey);
    Tick();
    Said({"Metamob : clé enregistrée pour le compte « bZyx/bwvut »."}, "Markup and controls removed from the account name");

    // Refused by Metamob: nothing stored, the stored key kept.
    keyNow = OtherKey;
    for (const int status : {401, 403}) {
        Expect("GET", "/me", Status(status));
        SubmitKey(OtherKey);
        Tick();
        Said({KeyRejected}, "A refused key");
        Check(LoadKey(file)->value() == FakeKey && Logged("METAMOB_KEY_REJECTED"), "A refused key is not stored");
    }
    // Metamob unreachable or answering something else (a 401/403 page that is not
    // Metamob's JSON, a Cloudflare challenge, says nothing about the key): stored, said unverified.
    for (const auto& answer : {Html(503), Lost(12002, false), Lost(12002, true), Html(200), Status(429, "30"), Json("{\"data\":[]}"), Status(404),
        Html(403), Html(401)}) {
        ForgetKey(file);
        Expect("GET", "/me", answer);
        SubmitKey(OtherKey);
        Tick();
        Said({KeyUnverified}, "An unverified key");
        Check(LoadKey(file)->value() == OtherKey && Logged("METAMOB_KEY_SAVED verified=0"), "An unverified key is stored");
    }
    // A key replaced before it was sent: only the last one is verified.
    keyNow = FakeKey;
    Expect("GET", "/me", Json(MeBody(User)));
    SubmitKey(OtherKey);
    SubmitKey(FakeKey);
    Tick();
    Said({SavedUser}, "The last key typed wins");
    Check(LoadKey(file)->value() == FakeKey, "The last key is stored");
    // Forgotten before it was sent: never sent.
    const auto before = Sent();
    SubmitKey(OtherKey);
    ForgetStoredKey();
    Tick();
    Said({KeyForgotten}, "Forget after an unsent key");
    Check(Sent() == before && !KeyStored(), "A key forgotten before its check is never sent");
    Finish("key submission");

    // The key cannot be stored on this PC (folder missing): said, nothing stored.
    Begin(root / "missing" / "settings.ini", false);
    Expect("GET", "/me", Json(MeBody(User)));
    SubmitKey(FakeKey);
    Tick();
    Said({KeyNotStored}, "A key that cannot be stored");
    Check(!KeyStored() && Logged("METAMOB_KEY_STORE_FAILED"), "Nothing stored");
    Finish("key store failure");
}

void CaptureWithoutKey() {
    Begin(Folder("nokey"), false);
    EnqueueCapture(Captured(Tofu));
    Tick();
    Said({"Metamob : capture de « Tofumanchou l'Empereur » non synchronisée : aucune clé enregistrée (Options › Dofus Enhanced › Pierre d’âme)."},
        "A capture without a key");
    // A name with markup is shown without it.
    auto odd = Captured(Mob);
    odd.name = "Mob <i>l'Éponge</i>";
    EnqueueCapture(odd);
    Tick(60000);
    Said({NoKey("Mob il'Éponge/i")}, "Markup removed from a game name");
    Check(Sent() == 0 && Logged("METAMOB_NO_KEY monster=10370"), "Nothing sent without a key");
    Finish("capture without key");
}

void CaptureWhileKeyChecked() {
    const auto settings = Folder("checking");
    Begin(settings, false);
    // A capture made while a key typed in the options is being checked waits for it.
    Expect("GET", "/me", Json(MeBody(User)));
    SubmitKey(FakeKey);
    EnqueueCapture(Captured(Tofu));
    Tick();
    Said({SavedUser}, "The key is checked first");
    ExpectFirst(Tofu, 2, 3);
    Tick(2000);
    Said({Added(Tofu, 2, 3)}, "The capture made meanwhile is sent with that key");
    // That key refused: the capture waiting for it is said unsynchronised, nothing sent.
    ForgetKey(KeyPath(settings));
    keyNow = OtherKey;
    Expect("GET", "/me", Status(401));
    SubmitKey(OtherKey);
    EnqueueCapture(Captured(Mob));
    Tick();
    Said({KeyRejected}, "The new key refused");
    Tick(2000);
    Said({NoKey("Mob l'Éponge")}, "No key for the waiting capture");
    Finish("capture while a key is checked");
}

void HappyPath() {
    Begin(Folder("happy"), true);
    ExpectFirst(Tofu, 2, 3);
    EnqueueCapture(Captured(Tofu));
    Tick();
    Tick(1999);
    Check(Sent() == 0 && notices.empty(), "Nothing is sent within 2 s of the last capture");
    Tick(1);
    Said({"Metamob : +1 « Tofumanchou l'Empereur » (2 → 3)."}, "Happy path");
    {
        std::lock_guard<std::mutex> guard(scriptLock);
        Check(calls.size() == 5, "Quests, two list pages, the step, the write");
        for (size_t i = 1; i < calls.size(); ++i) Check(calls[i].at - calls[i - 1].at >= 1100, "At least 1.1 s between two requests");
        Check(!calls[0].held && !calls[1].held && !calls[2].held && calls[3].held && calls[4].held,
            "The sender lock covers the read, the write and its check only");
        for (const auto& call : calls)
            Check(call.key == FakeKey && call.thread == GetCurrentThreadId(), "The stored key is used (inline job)");
    }
    Check(Logged("METAMOB_HTTP method=GET route=/quests status=200") && LoggedCount("METAMOB_HTTP method=GET route=/quests/*/monsters status=200") == 3 &&
        Logged("METAMOB_HTTP method=PATCH route=/quests/*/monsters/* status=200") && Logged("METAMOB_SYNC done=1 pending=0"), "Sync logs");
    // Kept for the session: the next capture reads its step and writes, nothing else.
    ExpectStep(Slug, Mob, 0);
    ExpectWrite(Slug, Mob, 1, Written(Mob, 1));
    EnqueueCapture(Captured(Mob));
    Tick(2000);
    Said({"Metamob : +1 « Mob l'Éponge » (0 → 1)."}, "Cached quest and list");
    // A typographic apostrophe in the game name: the same monster, shown with Metamob's name.
    auto curly = Captured(Tiwa);
    curly.name = "Tiwa’Missou le Gateux";
    ExpectStep(Slug, Tiwa, 3);
    ExpectWrite(Slug, Tiwa, 4, Written(Tiwa, 4));
    EnqueueCapture(curly);
    Tick(2000);
    Said({Added(Tiwa, 3, 4)}, "Apostrophe variants");
    Finish("happy path");
}

void Grouping() {
    Begin(Folder("group"), true);
    ExpectFirst(Tofu, 2, 4);
    EnqueueCapture(Captured(Tofu));
    Tick(1500);
    EnqueueCapture(Captured(Tofu));
    Tick(1999);
    Check(Sent() == 0, "A new capture restarts the 2 s wait");
    Tick(1);
    Said({"Metamob : +2 « Tofumanchou l'Empereur » (2 → 4)."}, "Two captures, one write");
    // A fight with two souls of one monster and one of another: one write each.
    ExpectStep(Slug, Abrakroc, 0);
    ExpectWrite(Slug, Abrakroc, 2, Written(Abrakroc, 2));
    ExpectStep(Slug, Larva, 1);
    ExpectWrite(Slug, Larva, 2, Written(Larva, 2));
    EnqueueCapture(Captured(Abrakroc, 2));
    EnqueueCapture(Captured(Larva));
    Tick(2000);
    Said({Added(Abrakroc, 0, 2), Added(Larva, 1, 2)}, "Grouped per monster");
    Finish("grouping");
}

void Cap() {
    Begin(Folder("cap"), true);
    ExpectQuests({Quest(Slug, Character.c_str(), "ocre", 1)});
    ExpectList(Slug);
    ExpectStep(Slug, Tofu, 30);
    EnqueueCapture(Captured(Tofu));
    Tick(2000);
    Said({"Metamob : « Tofumanchou l'Empereur » est déjà à 30, la quantité maximale."}, "Already at 30: nothing written");
    // 29 + 2: written up to 30, the rest said uncounted.
    ExpectStep(Slug, Tofu, 29);
    ExpectWrite(Slug, Tofu, 30, Written(Tofu, 30));
    EnqueueCapture(Captured(Tofu, 2));
    Tick(2000);
    Said({Added(Tofu, 29, 30), Capped(Tofu)}, "Written up to 30 only");
    // Above the documented bound (the site allows more): never written.
    ExpectStep(Slug, Tofu, 45);
    EnqueueCapture(Captured(Tofu));
    Tick(2000);
    Said({Capped(Tofu)}, "Above 30: nothing written");
    Finish("cap");
}

void NotInQuest() {
    Begin(Folder("unmatched"), true);
    ExpectQuests({Quest(Slug, Character.c_str(), "ocre", 1)});
    ExpectList(Slug);
    Capture krok;
    krok.characterName = Character; krok.monsterId = 3180; krok.name = "Krokette la Croustillante"; krok.type = TypeArchimonster;
    EnqueueCapture(krok);
    Tick(2000);
    Said({Unmatched("Krokette la Croustillante")}, "A Dokille archimonster is not in the Ocre quest");
    Check(Logged("METAMOB_UNMATCHED monster=3180"), "Unmatched log with the game id");
    // The same name under the other type is not that monster.
    auto boss = Captured(Tofu);
    boss.type = TypeBoss;
    EnqueueCapture(boss);
    Tick(2000);
    Said({Unmatched("Tofumanchou l'Empereur")}, "Another type never matches");
    Check(Sent() == 3, "Nothing read again nor written for a monster not in the quest");
    Finish("not in quest");
}

void ChoosingQuests() {
    Begin(Folder("choice"), true);
    // No Dofus Unity Ocre quest (a Retro Ocre one, a Unity Dokille one): nothing written, asked again next time.
    ExpectQuests({Quest(Slug, Character.c_str(), "ocre", 2), Quest(OtherSlug, nullptr, "dokille", 1)});
    EnqueueCapture(Captured(Tofu));
    Tick(2000);
    Said({NoQuest}, "No Unity Ocre quest");
    // Several, none naming this character: ambiguous, told once for the whole fight.
    ExpectQuests({Quest(Slug, "Bob", "ocre", 1), Quest(OtherSlug, nullptr, "ocre", 1)});
    EnqueueCapture(Captured(Tofu));
    EnqueueCapture(Captured(Mob));
    Tick(2000);
    Said({SeveralQuests}, "Several Unity Ocre quests");
    // "Personnage à contacter" naming this character picks its quest.
    ExpectQuests({Quest(Slug, "Bob", "ocre", 1), Quest(OtherSlug, "Bob, Altanzorg", "ocre", 1)});
    ExpectList(OtherSlug);
    ExpectStep(OtherSlug, Tofu, 2);
    ExpectWrite(OtherSlug, Tofu, 3, Written(Tofu, 3));
    EnqueueCapture(Captured(Tofu));
    Tick(2000);
    Said({Added(Tofu, 2, 3)}, "The character's quest");
    // Kept for the session once chosen.
    ExpectStep(OtherSlug, Mob, 0);
    ExpectWrite(OtherSlug, Mob, 1, Written(Mob, 1));
    EnqueueCapture(Captured(Mob));
    Tick(2000);
    Said({Added(Mob, 0, 1)}, "The chosen quest is kept");
    Finish("quest choice");
}

void AuthPause() {
    const auto settings = Folder("auth");
    Begin(settings, true);
    Expect("GET", "/quests", Status(401));
    EnqueueCapture(Captured(Tofu));
    Tick(2000);
    Said({KeyRefused}, "A refused key pauses the sync");
    Check(Logged("METAMOB_KEY_REFUSED"), "Pause log");
    // The same key: nothing sent, nothing said again, the captures kept.
    Tick(30000);
    Tick(30000);
    EnqueueCapture(Captured(Mob));
    Tick(2000);
    Tick(30000);
    Check(Sent() == 1 && notices.empty(), "Paused until a new key");
    // A new key saved in the options: verified, then the kept captures go out with it.
    keyNow = OtherKey;
    Expect("GET", "/me", Json(MeBody(User)));
    ExpectFirst(Tofu, 2, 3);
    ExpectStep(Slug, Mob, 0);
    ExpectWrite(Slug, Mob, 1, Written(Mob, 1));
    SubmitKey(OtherKey);
    Tick();
    Said({SavedUser}, "The new key");
    Tick();
    Said({Added(Tofu, 2, 3), Added(Mob, 0, 1)}, "Kept captures sent with the new key");
    // Refused again; then another game client saves a new key: found at the next check.
    Expect("GET", StepPath(Slug, Larva.step), Status(403));
    EnqueueCapture(Captured(Larva));
    Tick(2000);
    Said({KeyRefused}, "Refused again");
    StoreKey(KeyPath(settings), Secret(FakeKey));
    keyNow = FakeKey;
    ExpectQuests({Quest(Slug, Character.c_str(), "ocre", 1)});
    ExpectList(Slug);
    ExpectStep(Slug, Larva, 1);
    ExpectWrite(Slug, Larva, 2, Written(Larva, 2));
    Tick(30000);
    Said({Added(Larva, 1, 2)}, "A key saved by another client");
    Finish("auth pause");
}

// Metamob's own refusal (JSON) pauses the sync, said once; the same key is then
// tried again every 10 min with one request and no new notice. A write confirmed
// with it ends the pause: a later refusal is told again.
void RefusedKeyRetry() {
    Begin(Folder("retry"), true);
    Expect("GET", "/quests", Status(401));
    EnqueueCapture(Captured(Tofu));
    Tick(2000);
    Said({KeyRefused}, "Refused by Metamob");
    for (int i = 0; i < 19; ++i) Tick(30000);
    Check(Sent() == 1 && notices.empty(), "The same key is not tried again before 10 min");
    Expect("GET", "/quests", Status(401));
    Tick(30000);
    Check(Sent() == 2 && notices.empty() && LoggedCount("METAMOB_KEY_REFUSED") == 1, "Tried again once after 10 min, nothing said again");
    for (int i = 0; i < 19; ++i) Tick(30000);
    Check(Sent() == 2, "Then again 10 min later only");
    ExpectFirst(Tofu, 2, 3);
    Tick(30000);
    Said({Added(Tofu, 2, 3)}, "Accepted again: the kept capture is written");
    Expect("GET", StepPath(Slug, Mob.step), Status(403));
    EnqueueCapture(Captured(Mob));
    Tick(2000);
    Said({KeyRefused}, "A refusal after a confirmed write is told again");
    Finish("refused key retry");
}

// A 401/403 page that is not JSON (a Cloudflare challenge, a filtering proxy)
// says nothing about the key: a passing failure, never the refused-key pause.
void ProxyPages() {
    Begin(Folder("proxy"), true);
    Expect("GET", "/quests", Html(403));
    EnqueueCapture(Captured(Tofu));
    Tick(2000);
    Check(notices.empty() && !Logged("METAMOB_KEY_REFUSED") && Logged("METAMOB_HTTP method=GET route=/quests status=403"),
        "A challenge page on a read is no refusal of the key");
    const auto at = LastAt();
    TickAt(at + 29999);
    Check(Sent() == 1, "Nothing before the backoff ends");
    ExpectFirst(Tofu, 2, 3);
    TickAt(at + 30000);
    Said({Added(Tofu, 2, 3)}, "Written once the challenge is over");
    // On a write: maybe forwarded before the page replaced its answer, so read back
    // at once under the sender lock; found at its base, the same target once more.
    ExpectStep(Slug, Mob, 0);
    ExpectWrite(Slug, Mob, 1, Html(403));
    ExpectStep(Slug, Mob, 0);
    ExpectWrite(Slug, Mob, 1, Written(Mob, 1));
    EnqueueCapture(Captured(Mob));
    Tick(2000);
    Said({Added(Mob, 0, 1)}, "A challenge page on a write");
    Check(Logged("METAMOB_UNCONFIRMED monster=10312") && !Logged("METAMOB_KEY_REFUSED"), "Unconfirmed, no refusal of the key");
    Finish("proxy pages");
}

void Rediscovery() {
    Begin(Folder("gone"), true);
    ExpectFirst(Tofu, 2, 3);
    EnqueueCapture(Captured(Tofu));
    Tick(2000);
    Said({Added(Tofu, 2, 3)}, "First write");
    // The quest was deleted and created again (new slug): looked for once more, written there.
    Expect("GET", StepPath(Slug, Larva.step), Status(404));
    ExpectQuests({Quest(OtherSlug, Character.c_str(), "ocre", 1)});
    ExpectList(OtherSlug);
    ExpectStep(OtherSlug, Larva, 1);
    ExpectWrite(OtherSlug, Larva, 2, Written(Larva, 2));
    EnqueueCapture(Captured(Larva));
    Tick(2000);
    Said({Added(Larva, 1, 2)}, "Rediscovered after a 404 read");
    // A write answered 404: not written, the quest looked for again, written there.
    ExpectStep(OtherSlug, Abrakroc, 0);
    ExpectWrite(OtherSlug, Abrakroc, 1, Status(404));
    ExpectQuests({Quest(Slug, Character.c_str(), "ocre", 1)});
    ExpectList(Slug);
    ExpectStep(Slug, Abrakroc, 0);
    ExpectWrite(Slug, Abrakroc, 1, Written(Abrakroc, 1));
    EnqueueCapture(Captured(Abrakroc));
    Tick(2000);
    Said({Added(Abrakroc, 0, 1)}, "Rediscovered after a 404 write");
    // Still missing right after looking again: kept, tried after a pause.
    Expect("GET", StepPath(Slug, Mob.step), Status(404));
    ExpectQuests({Quest(Slug, Character.c_str(), "ocre", 1)});
    ExpectList(Slug);
    Expect("GET", StepPath(Slug, Mob.step), Status(404));
    EnqueueCapture(Captured(Mob));
    Tick(2000);
    Check(notices.empty() && Logged("METAMOB_QUEST_GONE"), "Looked for once per cycle");
    const auto at = LastAt();
    const auto sent = Sent();
    TickAt(at + 29999);
    Check(Sent() == sent, "Nothing before the pause ends");
    ExpectStep(Slug, Mob, 0);
    ExpectWrite(Slug, Mob, 1, Written(Mob, 1));
    TickAt(at + 30000);
    Said({Added(Mob, 0, 1)}, "Written after the pause");
    Finish("rediscovery");
}

void StaleAnswers() {
    Begin(Folder("stale"), true);
    ExpectFirst(Tofu, 2, 3);
    EnqueueCapture(Captured(Tofu));
    Tick(2000);
    Said({Added(Tofu, 2, 3)}, "First write");
    // Its step no longer lists the monster: the kept list is read again once, then it is found.
    Expect("GET", StepPath(Slug, Larva.step), Json(Page(Slug, {}, 0, 0)));
    ExpectList(Slug);
    ExpectStep(Slug, Larva, 1);
    ExpectWrite(Slug, Larva, 2, Written(Larva, 2));
    EnqueueCapture(Captured(Larva));
    Tick(2000);
    Said({Added(Larva, 1, 2)}, "Found again after reading the list once more");
    // Still missing after that: not in the quest, nothing written.
    Expect("GET", StepPath(Slug, Mob.step), Json(Page(Slug, {}, 0, 0)));
    ExpectList(Slug);
    Expect("GET", StepPath(Slug, Mob.step), Json(Page(Slug, {}, 0, 0)));
    EnqueueCapture(Captured(Mob));
    Tick(2000);
    Said({Unmatched("Mob l'Éponge")}, "Missing twice from its step");
    Check(Logged("METAMOB_UNMATCHED monster=10312"), "Unmatched log");
    // Answers that do not read as expected (another quest's page, missing fields): failures like any other.
    Expect("GET", StepPath(Slug, Abrakroc.step), Json(Page(OtherSlug, {Abrakroc}, 1, 0)));
    EnqueueCapture(Captured(Abrakroc));
    Tick(2000);
    Check(notices.empty() && Logged("METAMOB_BAD_ANSWER route=/quests/*/monsters"), "Another quest's page is refused");
    auto at = LastAt();
    Expect("GET", StepPath(Slug, Abrakroc.step), Json("{\"data\":{\"slug\":\"" + Slug + "\",\"bio\":\"" + Marker + "\"}}"));
    TickAt(at + 30000);
    at = LastAt();
    ExpectStep(Slug, Abrakroc, 0);
    ExpectWrite(Slug, Abrakroc, 1, Written(Abrakroc, 1));
    TickAt(at + 60000);
    Said({Added(Abrakroc, 0, 1)}, "Written once the answers read well");
    Finish("stale answers");

    // A list page echoing another offset (a server ignoring it): refused, never paged forever.
    Begin(Folder("pages"), true);
    const auto rows = Catalog();
    ExpectQuests({Quest(Slug, Character.c_str(), "ocre", 1)});
    Expect("GET", "/quests/" + Slug + "/monsters?limit=200&offset=0", Json(Page(Slug, {rows.begin(), rows.begin() + 200}, rows.size(), 0)));
    Expect("GET", "/quests/" + Slug + "/monsters?limit=200&offset=200", Json(Page(Slug, {rows.begin(), rows.begin() + 200}, rows.size(), 0)));
    EnqueueCapture(Captured(Tofu));
    Tick(2000);
    Check(notices.empty() && Logged("METAMOB_BAD_ANSWER route=/quests/*/monsters"), "A page of another offset is refused");
    Finish("list pages");
}

void RateLimit() {
    Begin(Folder("limit"), true);
    Expect("GET", "/quests", Status(429, "120"));
    EnqueueCapture(Captured(Tofu));
    Tick(2000);
    Check(notices.empty() && Logged("METAMOB_RATE_LIMIT seconds=120"), "Retry-After in seconds");
    auto at = LastAt();
    TickAt(at + 119999);
    Check(Sent() == 1, "Nothing before Retry-After");
    ExpectFirst(Tofu, 2, 3);
    TickAt(at + 120000);
    Said({Added(Tofu, 2, 3)}, "Sent after Retry-After");
    // As an HTTP date (5 min after the clock), then absent on a write (60 s).
    Expect("GET", StepPath(Slug, Mob.step), Status(429, HttpDate(FakeEpochValue + 300)));
    EnqueueCapture(Captured(Mob));
    Tick(2000);
    Check(Logged("METAMOB_RATE_LIMIT seconds=300"), "Retry-After as a date");
    at = LastAt();
    const auto sent = Sent();
    TickAt(at + 299999);
    Check(Sent() == sent, "Nothing before the date");
    ExpectStep(Slug, Mob, 0);
    ExpectWrite(Slug, Mob, 1, Status(429));
    TickAt(at + 300000);
    Check(Logged("METAMOB_RATE_LIMIT seconds=60") && notices.empty(), "60 s without Retry-After");
    at = LastAt();
    ExpectStep(Slug, Mob, 0);
    ExpectWrite(Slug, Mob, 1, Written(Mob, 1));
    TickAt(at + 60000);
    Said({Added(Mob, 0, 1)}, "A write refused by the rate limit is read again, then written");
    Finish("rate limit");
}

void NetworkThenResume() {
    Begin(Folder("network"), true);
    Expect("GET", "/quests", Lost(12029, false));
    EnqueueCapture(Captured(Tofu));
    Tick(2000);
    Check(Logged("METAMOB_NET method=GET route=/quests error=12029 sent=0"), "Network error log");
    auto at = LastAt();
    TickAt(at + 29999);
    Check(Sent() == 1, "30 s after a first failure");
    Expect("GET", "/quests", Html(503));
    TickAt(at + 30000);
    at = LastAt();
    TickAt(at + 59999);
    Check(Sent() == 2 && notices.empty(), "60 s after a second one");
    Expect("GET", "/quests", Response{}, {}, true); // a transport that throws: no answer
    TickAt(at + 60000);
    Said({"Metamob injoignable : 1 capture(s) en attente, nouvel essai automatique."}, "Told at the third failure in a row");
    Check(Logged("METAMOB_NET method=GET route=/quests error=1359 sent=0"), "A throwing transport is a network failure");
    at = LastAt();
    Expect("GET", "/quests", Html(200)); // a 2xx page that is not JSON (Cloudflare)
    TickAt(at + 119999);
    Check(Sent() == 3, "120 s after a third one");
    TickAt(at + 120000);
    Check(notices.empty(), "Told once, not at every failure");
    at = LastAt();
    // Back: sent, and the next failure waits 30 s again.
    ExpectFirst(Tofu, 2, 3);
    TickAt(at + 240000);
    Said({Added(Tofu, 2, 3)}, "Sent once Metamob answers");
    Expect("GET", StepPath(Slug, Mob.step), Lost(12002, false));
    EnqueueCapture(Captured(Mob));
    Tick(2000);
    at = LastAt();
    ExpectStep(Slug, Mob, 0);
    ExpectWrite(Slug, Mob, 1, Written(Mob, 1));
    TickAt(at + 30000);
    Said({Added(Mob, 0, 1)}, "The delay starts again from 30 s");
    Finish("network then resume");
}

// The name this PC's other clients see while a write is unsettled (built as the module does).
std::wstring FlightName(const std::string& slug, int64_t id) {
    uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char c : slug + "/" + std::to_string(id)) { hash ^= c; hash *= 1099511628211ULL; }
    std::wstring name = L"Local\\DofusMiniatures.Metamob.";
    for (int shift = 60; shift >= 0; shift -= 4) name += L"0123456789abcdef"[(hash >> shift) & 15];
    return name;
}
bool FlightAnnounced(const std::string& slug, const Row& row) {
    const auto event = OpenEventW(SYNCHRONIZE, FALSE, FlightName(slug, row.id).c_str());
    if (event) CloseHandle(event);
    return event != nullptr;
}

void UnknownOutcome() {
    Begin(Folder("unknown"), true);
    // Written although the answer was lost: read back at once, still under the
    // sender lock, found at its target, not sent again.
    ExpectQuests({Quest(Slug, Character.c_str(), "ocre", 1)});
    ExpectList(Slug);
    ExpectStep(Slug, Tofu, 2);
    ExpectWrite(Slug, Tofu, 3, Lost(12002, true));
    ExpectStep(Slug, Tofu, 3);
    EnqueueCapture(Captured(Tofu));
    Tick(2000);
    Said({Added(Tofu, 2, 3)}, "Found at its target");
    Check(Logged("METAMOB_UNCONFIRMED monster=10370") &&
        Logged("METAMOB_NET method=PATCH route=/quests/*/monsters/* error=12002 sent=1"), "An unconfirmed write is logged");
    {
        std::lock_guard<std::mutex> guard(scriptLock);
        const auto n = calls.size();
        Check(n >= 3 && calls[n - 3].held && calls[n - 2].held && calls[n - 1].held, "Read, write and check under the sender lock");
    }
    Check(!FlightAnnounced(Slug, Tofu), "A settled write is not announced");
    // Not written (502 from a proxy): found at its base, the same absolute target
    // sent again at once, never +1 on top.
    ExpectStep(Slug, Mob, 0);
    ExpectWrite(Slug, Mob, 1, Html(502));
    ExpectStep(Slug, Mob, 0);
    ExpectWrite(Slug, Mob, 1, Written(Mob, 1));
    EnqueueCapture(Captured(Mob));
    Tick(2000);
    Said({Added(Mob, 0, 1)}, "Found at its base: the same target sent again");
    // Changed by someone else meanwhile: told, never overwritten.
    ExpectStep(Slug, Larva, 1);
    ExpectWrite(Slug, Larva, 2, Html(200));
    ExpectStep(Slug, Larva, 5);
    EnqueueCapture(Captured(Larva));
    Tick(2000);
    Said({Changed(Larva, 1, 5)}, "Found at another value");
    Check(Logged("METAMOB_CONFLICT monster=10565"), "Conflict log");
    // Not written twice in a row: kept in flight (announced), read at the next cycle.
    ExpectStep(Slug, Bouftou, 5);
    ExpectWrite(Slug, Bouftou, 6, Status(500));
    ExpectStep(Slug, Bouftou, 5);
    ExpectWrite(Slug, Bouftou, 6, Status(503));
    ExpectStep(Slug, Bouftou, 5);
    EnqueueCapture(Captured(Bouftou));
    Tick(2000);
    Check(notices.empty() && FlightAnnounced(Slug, Bouftou), "Sent again once only, then kept and announced");
    auto at = LastAt();
    ExpectStep(Slug, Bouftou, 5);
    ExpectWrite(Slug, Bouftou, 6, Written(Bouftou, 6));
    TickAt(at + 30000);
    Said({Added(Bouftou, 5, 6)}, "Settled at the next cycle");
    Check(!FlightAnnounced(Slug, Bouftou), "A settled write is no longer announced");
    // Its check unanswered too, then a new capture of that monster: that write is
    // settled first.
    ExpectStep(Slug, Abrakroc, 0);
    ExpectWrite(Slug, Abrakroc, 1, Status(500));
    Expect("GET", StepPath(Slug, Abrakroc.step), Html(502));
    EnqueueCapture(Captured(Abrakroc));
    Tick(2000);
    Check(notices.empty() && FlightAnnounced(Slug, Abrakroc), "An unsettled write is announced");
    at = LastAt();
    EnqueueCapture(Captured(Abrakroc));
    Tick(2000);
    Check(notices.empty() && LastAt() == at, "Nothing sent during the pause");
    ExpectStep(Slug, Abrakroc, 1);
    ExpectStep(Slug, Abrakroc, 1);
    ExpectWrite(Slug, Abrakroc, 2, Written(Abrakroc, 2));
    TickAt(at + 30000);
    Said({Added(Abrakroc, 0, 1), Added(Abrakroc, 1, 2)}, "The new capture goes on top of the settled write");
    // A transport that throws on a write: maybe received, so read back at once.
    ExpectStep(Slug, Bouftou, 5);
    Expect("PATCH", "/quests/" + Slug + "/monsters/305", Response{}, "{\"quantity\":6}", true);
    ExpectStep(Slug, Bouftou, 6);
    EnqueueCapture(Captured(Bouftou));
    Tick(2000);
    Said({Added(Bouftou, 5, 6)}, "A throwing write found done");
    Check(Logged("METAMOB_NET method=PATCH route=/quests/*/monsters/* error=1359 sent=1"), "A throwing write is unconfirmed");
    // A resend refused or not sent while the first send is still unknown (it lands
    // meanwhile): kept in flight with its base and target, never +1 on a fresh read.
    ExpectStep(Slug, Tofu, 3);
    ExpectWrite(Slug, Tofu, 4, Lost(12002, true));
    Expect("GET", StepPath(Slug, Tofu.step), Html(502));
    EnqueueCapture(Captured(Tofu));
    Tick(2000);
    at = LastAt();
    ExpectStep(Slug, Tofu, 3);
    ExpectWrite(Slug, Tofu, 4, Status(429, "30"));
    TickAt(at + 30000);
    Check(notices.empty() && Logged("METAMOB_RATE_LIMIT seconds=30") && FlightAnnounced(Slug, Tofu), "A resend refused by the rate limit stays in flight");
    at = LastAt();
    ExpectStep(Slug, Tofu, 3);
    ExpectWrite(Slug, Tofu, 4, Lost(12029, false));
    TickAt(at + 30000);
    Check(notices.empty() && FlightAnnounced(Slug, Tofu), "A resend never sent stays in flight");
    at = LastAt();
    ExpectStep(Slug, Tofu, 4); // the first send landed meanwhile
    TickAt(at + 60000);
    Said({Added(Tofu, 3, 4)}, "Found at its target: never written on top");
    // A quest gone before the write could be checked: asked again once, then given up.
    ExpectStep(Slug, Tiwa, 3);
    ExpectWrite(Slug, Tiwa, 4, Lost(12030, true));
    Expect("GET", StepPath(Slug, Tiwa.step), Status(404));
    EnqueueCapture(Captured(Tiwa));
    Tick(2000);
    Check(notices.empty(), "The check finds no quest: kept");
    at = LastAt();
    Expect("GET", StepPath(Slug, Tiwa.step), Status(404));
    TickAt(at + 30000);
    Check(notices.empty(), "A first miss waits");
    at = LastAt();
    Expect("GET", StepPath(Slug, Tiwa.step), Status(404));
    TickAt(at + 60000);
    Said({Unsure(Tiwa)}, "Given up after a second miss");
    Check(Logged("METAMOB_LOST monster=10387"), "Lost write log");
    // A resend answered 404 (the quest refuses a monster it listed): asked again
    // after a pause, then given up with a notice, never recomputed afresh.
    ExpectQuests({Quest(Slug, Character.c_str(), "ocre", 1)});
    ExpectList(Slug);
    ExpectStep(Slug, Larva, 2);
    ExpectWrite(Slug, Larva, 3, Status(502));
    ExpectStep(Slug, Larva, 2);
    ExpectWrite(Slug, Larva, 3, Status(404));
    EnqueueCapture(Captured(Larva));
    Tick(2000);
    Check(notices.empty() && FlightAnnounced(Slug, Larva), "A resend answered 404 waits in flight");
    at = LastAt();
    ExpectStep(Slug, Larva, 2);
    ExpectWrite(Slug, Larva, 3, Status(404));
    TickAt(at + 30000);
    Said({Unsure(Larva)}, "Given up after a second 404");
    Check(Logged("METAMOB_LOST monster=10565") && !FlightAnnounced(Slug, Larva), "Dropped: no longer announced");
    Finish("unknown outcome");
}

// A party capture: every client of the team writes its own capture of the same
// monster as soon as the sender lock is free.
void SiblingClients() {
    Begin(Folder("siblings"), true);
    // This client's write is lost (not applied); another client would write its own
    // +1 as soon as the lock is released: settled before, that +1 cannot pass for it.
    ExpectQuests({Quest(Slug, Character.c_str(), "ocre", 1)});
    ExpectList(Slug);
    ExpectStep(Slug, Tofu, 2);
    ExpectWrite(Slug, Tofu, 3, Lost(12002, true));
    ExpectStep(Slug, Tofu, 2);
    ExpectWrite(Slug, Tofu, 3, Written(Tofu, 3));
    EnqueueCapture(Captured(Tofu));
    Tick(2000);
    Said({Added(Tofu, 2, 3)}, "Settled before the lock goes");
    Check(!FlightAnnounced(Slug, Tofu), "Nothing left announced");
    // Another client's write of this monster is not settled: nothing read nor
    // written until it is, then read afresh on top of it.
    const auto other = CreateEventW(nullptr, TRUE, FALSE, FlightName(Slug, Tofu.id).c_str());
    Check(other != nullptr, "Another client's unsettled write");
    const auto sent = Sent();
    EnqueueCapture(Captured(Tofu));
    Tick(2000);
    Check(Sent() == sent && notices.empty() && Logged("METAMOB_WAIT monster=10370"), "Waits for the other client's write");
    Tick(30000);
    Check(Sent() == sent && notices.empty(), "Still waiting while it is unsettled");
    CloseHandle(other);
    ExpectStep(Slug, Tofu, 4);
    ExpectWrite(Slug, Tofu, 5, Written(Tofu, 5));
    Tick(30000);
    Said({Added(Tofu, 4, 5)}, "Written once the other client's write is settled");
    // A write of another quest's monster with the same id holds nothing up.
    const auto elsewhere = CreateEventW(nullptr, TRUE, FALSE, FlightName(OtherSlug, Tofu.id).c_str());
    Check(elsewhere != nullptr, "Another quest's unsettled write");
    ExpectStep(Slug, Tofu, 5);
    ExpectWrite(Slug, Tofu, 6, Written(Tofu, 6));
    EnqueueCapture(Captured(Tofu));
    Tick(2000);
    Said({Added(Tofu, 5, 6)}, "Another quest's flight does not hold this one");
    CloseHandle(elsewhere);
    Finish("sibling clients");
}

// Another client's unsettled write holds back that monster only: the captures of
// other monsters queued behind it are written meanwhile.
void HeadOfLine() {
    Begin(Folder("headofline"), true);
    ExpectFirst(Tofu, 2, 3);
    EnqueueCapture(Captured(Tofu));
    Tick(2000);
    Said({Added(Tofu, 2, 3)}, "First write");
    const auto other = CreateEventW(nullptr, TRUE, FALSE, FlightName(Slug, Tofu.id).c_str());
    Check(other != nullptr, "Another client's unsettled write");
    EnqueueCapture(Captured(Tofu));
    EnqueueCapture(Captured(Mob));
    ExpectStep(Slug, Mob, 0);
    ExpectWrite(Slug, Mob, 1, Written(Mob, 1));
    Tick(2000);
    Said({Added(Mob, 0, 1)}, "The capture queued behind the waiting one is written meanwhile");
    Check(Logged("METAMOB_WAIT monster=10370"), "Tofu waits for the other client");
    // Every capture left waits: no new job (nor check) before the 30 s.
    const auto waits = LoggedCount("METAMOB_WAIT");
    auto sent = Sent();
    Tick(); Tick(1000);
    Check(Sent() == sent && LoggedCount("METAMOB_WAIT") == waits, "No busy loop while every capture waits");
    Tick(30000);
    Check(Sent() == sent && notices.empty() && LoggedCount("METAMOB_WAIT") == waits + 1, "Tofu checked again after 30 s, still waits");
    // A later capture of another monster while every one left waits: written at the next check.
    EnqueueCapture(Captured(Bouftou));
    ExpectStep(Slug, Bouftou, 5);
    ExpectWrite(Slug, Bouftou, 6, Written(Bouftou, 6));
    Tick(30000);
    Said({Added(Bouftou, 5, 6)}, "A later capture of another monster is not held either");
    CloseHandle(other);
    ExpectStep(Slug, Tofu, 3);
    ExpectWrite(Slug, Tofu, 4, Written(Tofu, 4));
    Tick(30000);
    Said({Added(Tofu, 3, 4)}, "Tofu written once the other client's write is settled");
    Finish("head of line");
}

// An earlier send that may still land: a refused resend never turns the capture
// back into +n on a fresh read.
void LateLanding() {
    Begin(Folder("late"), true);
    ExpectQuests({Quest(Slug, Character.c_str(), "ocre", 1)});
    ExpectList(Slug);
    ExpectStep(Slug, Tofu, 2);
    ExpectWrite(Slug, Tofu, 3, Lost(12002, true));
    ExpectStep(Slug, Tofu, 2);
    ExpectWrite(Slug, Tofu, 3, Status(429, "60"));
    EnqueueCapture(Captured(Tofu));
    Tick(2000);
    Check(notices.empty() && FlightAnnounced(Slug, Tofu) && Logged("METAMOB_RATE_LIMIT seconds=60"), "Kept as its target, announced");
    const auto at = LastAt();
    ExpectStep(Slug, Tofu, 3); // the first send landed meanwhile
    TickAt(at + 60000);
    Said({Added(Tofu, 2, 3)}, "Found at its target: never written as +1 on top");
    Check(!FlightAnnounced(Slug, Tofu), "Settled: no longer announced");
    Finish("late landing");
}

void WriteAnswers() {
    Begin(Folder("answers"), true);
    ExpectQuests({Quest(Slug, Character.c_str(), "ocre", 1)});
    ExpectList(Slug);
    ExpectStep(Slug, Tofu, 2);
    ExpectWrite(Slug, Tofu, 3, Status(422));
    EnqueueCapture(Captured(Tofu));
    Tick(2000);
    Said({"Metamob : mise à jour refusée pour « Tofumanchou l'Empereur »."}, "A refused write");
    Check(Logged("METAMOB_REFUSED monster=10370"), "Refused log");
    ExpectStep(Slug, Mob, 0);
    ExpectWrite(Slug, Mob, 1, Status(400));
    EnqueueCapture(Captured(Mob));
    Tick(2000);
    Said({Refused(Mob)}, "400 on a write");
    // A resend refused while an earlier send of it is still unknown: given up, but
    // never said refused (that send may have landed).
    ExpectStep(Slug, Bouftou, 5);
    ExpectWrite(Slug, Bouftou, 6, Lost(12002, true));
    ExpectStep(Slug, Bouftou, 5);
    ExpectWrite(Slug, Bouftou, 6, Status(409));
    EnqueueCapture(Captured(Bouftou));
    Tick(2000);
    Said({Unsure(Bouftou)}, "A resend refused after an unknown send");
    Check(Logged("METAMOB_REFUSED monster=10305") && !FlightAnnounced(Slug, Bouftou), "Refused resend: dropped, no longer announced");
    // The answer itself says another value: told, not written again.
    ExpectStep(Slug, Larva, 1);
    ExpectWrite(Slug, Larva, 2, Written(Larva, 7));
    EnqueueCapture(Captured(Larva));
    Tick(2000);
    Said({Changed(Larva, 1, 7)}, "An answer with another value");
    // Never sent (connection refused): read afresh and written next time.
    ExpectStep(Slug, Abrakroc, 0);
    ExpectWrite(Slug, Abrakroc, 1, Lost(12029, false));
    EnqueueCapture(Captured(Abrakroc));
    Tick(2000);
    Check(notices.empty(), "Not sent: nothing said yet");
    const auto at = LastAt();
    ExpectStep(Slug, Abrakroc, 0);
    ExpectWrite(Slug, Abrakroc, 1, Written(Abrakroc, 1));
    TickAt(at + 30000);
    Said({Added(Abrakroc, 0, 1)}, "A write never sent is read again then written");
    // A write refused as unauthorized: nothing written, the capture kept.
    ExpectStep(Slug, Tiwa, 3);
    ExpectWrite(Slug, Tiwa, 4, Status(401));
    EnqueueCapture(Captured(Tiwa));
    Tick(2000);
    Said({KeyRefused}, "401 on a write");
    RestoreSync();
    Check(Logged("METAMOB_SYNC_STOP pending=1"), "The kept capture is counted at Restore");
    Finish("write answers");
}

void SenderMutex() {
    Begin(Folder("sender"), true);
    ExpectFirst(Tofu, 2, 3);
    EnqueueCapture(Captured(Tofu));
    Tick(2000);
    Said({Added(Tofu, 2, 3)}, "First write");
    // Held by another game client: waited for, then read and written under it.
    {
        Holder other;
        onSleep = [&other] { other.Release(); };
        ExpectStep(Slug, Mob, 0);
        ExpectWrite(Slug, Mob, 1, Written(Mob, 1));
        EnqueueCapture(Captured(Mob));
        Tick(2000);
        Said({Added(Mob, 0, 1)}, "Written once the other client is done");
        std::lock_guard<std::mutex> guard(scriptLock);
        Check(other.released && calls.back().held && calls[calls.size() - 2].held, "Read and written under the lock, after waiting");
    }
    // Abandoned by a client that ended while holding it: owned all the same, then released.
    const auto keep = CreateMutexW(nullptr, FALSE, SenderName);
    std::thread([] {
        const auto mutex = OpenMutexW(SYNCHRONIZE, FALSE, SenderName);
        WaitForSingleObject(mutex, INFINITE);
        CloseHandle(mutex); // the thread ends owning it
    }).join();
    ExpectStep(Slug, Abrakroc, 0);
    ExpectWrite(Slug, Abrakroc, 1, Written(Abrakroc, 1));
    EnqueueCapture(Captured(Abrakroc));
    Tick(2000);
    Said({Added(Abrakroc, 0, 1)}, "An abandoned lock is taken");
    Check(!SenderHeld(), "The lock is released after use");
    CloseHandle(keep);
    // Held for more than 10 s: nothing read nor written, tried again 2 s later.
    {
        Holder other;
        const auto sent = Sent();
        EnqueueCapture(Captured(Larva));
        Tick(2000);
        Check(Sent() == sent && notices.empty() && Logged("METAMOB_BUSY"), "Another client sending: nothing read nor written");
        other.Release();
        ExpectStep(Slug, Larva, 1);
        ExpectWrite(Slug, Larva, 2, Written(Larva, 2));
        Tick(2000);
        Said({Added(Larva, 1, 2)}, "Tried again 2 s later");
    }
    Finish("sender mutex");
}

void KeyFileProblems() {
    // A key file another Windows account or PC wrote, or a corrupt one.
    const auto unreadable = Folder("unreadable");
    Begin(unreadable, false);
    {
        std::ofstream output(KeyPath(unreadable), std::ios::binary);
        output << "DEMK1\n" << Marker;
    }
    Check(KeyStored(), "A key file is there");
    EnqueueCapture(Captured(Tofu));
    Tick(2000);
    Said({KeyUnreadable}, "An unreadable key");
    Tick(60000);
    Check(Sent() == 0 && notices.empty() && Logged("METAMOB_KEY_UNREADABLE pending=1"), "Its captures are dropped, nothing sent");
    Finish("unreadable key");

    // Forgotten: the key file goes, and so do the captures waiting for it.
    const auto settings = Folder("forget");
    Begin(settings, true);
    EnqueueCapture(Captured(Tofu));
    Tick();
    ForgetStoredKey();
    Tick();
    Said({KeyForgotten, NoKey("Tofumanchou l'Empereur")}, "Forget");
    Check(!KeyStored() && !std::filesystem::exists(KeyPath(settings)) && Logged("METAMOB_KEY_FORGOTTEN"), "The key file is removed");
    Tick(60000);
    Check(Sent() == 0 && notices.empty(), "Dropped captures are never sent");
    // A folder in place of the key file: refused, said, left as it is.
    std::filesystem::create_directory(KeyPath(settings));
    ForgetStoredKey();
    Tick();
    Said({"Metamob : impossible d’oublier la clé sur ce PC."}, "Forget refused");
    Check(std::filesystem::is_directory(KeyPath(settings)) && Logged("METAMOB_KEY_FORGET_FAILED"), "Nothing removed");
    std::filesystem::remove(KeyPath(settings));
    // Forgotten by another client after the capture: said when the cycle finds no key.
    StoreKey(KeyPath(settings), Secret(FakeKey));
    EnqueueCapture(Captured(Mob));
    ForgetKey(KeyPath(settings));
    Tick(2000);
    Said({NoKey("Mob l'Éponge")}, "No key at sending time");
    Check(Sent() == 0, "Nothing sent");
    Finish("key file problems");
}

std::atomic_bool thrownOnce{false};
void Robustness() {
    const auto settings = Folder("robust");
    Begin(settings, true);
    // Captures the detection never sends: ignored.
    auto plain = Captured(Tofu); plain.type = 1;
    auto none = Captured(Tofu); none.quantity = 0;
    auto many = Captured(Tofu); many.quantity = 31;
    auto nameless = Captured(Tofu); nameless.name.clear();
    for (const auto& capture : {plain, none, many, nameless}) EnqueueCapture(capture);
    Check(LoggedCount("METAMOB_CAPTURE_IGNORED") == 4, "Invalid captures ignored");
    // At most 500 captures wait in memory.
    for (int i = 0; i < 16; ++i) EnqueueCapture(Captured(Tofu, 30));
    EnqueueCapture(Captured(Mob, 30));
    Check(Logged("METAMOB_QUEUE_FULL monster=10312"), "The 501st capture is refused");
    EnqueueCapture(Captured(Mob, 20));
    RestoreSync();
    Check(Logged("METAMOB_SYNC_STOP pending=500"), "500 captures pending at Restore");
    // Restored: everything is inert.
    Check(!KeyStored(), "No key known after Restore");
    SubmitKey(FakeKey);
    ForgetStoredKey();
    EnqueueCapture(Captured(Tofu));
    Tick(5000);
    Check(Sent() == 0 && notices.empty(), "Inert after Restore");
    // A notice that throws never reaches the game loop, and the next ones are still posted.
    thrownOnce = false;
    InstallSync(settings, Scripted, [](const std::string& text) {
        if (!thrownOnce.exchange(true)) throw std::runtime_error("chat failed " + FakeKey);
        Noted(text);
    });
    ForgetKey(KeyPath(settings));
    EnqueueCapture(Captured(Tofu));
    EnqueueCapture(Captured(Mob));
    TickSync(syncOn);
    Said({NoKey("Mob l'Éponge")}, "The next notice after a failing one");
    Check(Logged("METAMOB_NOTICE_ERROR"), "Notice error log");
    // Nothing installed or an empty transport: no sync at all.
    InstallSync(settings, Transport{}, Noted);
    EnqueueCapture(Captured(Tofu));
    Tick(5000);
    Check(Sent() == 0 && notices.empty() && Logged("METAMOB_SYNC_UNAVAILABLE"), "No transport, no sync");
    Finish("robustness");
}

void Budget() {
    Begin(Folder("budget"), true);
    std::vector<Row> fillers;
    for (const auto& row : Catalog())
        if (row.name.rfind("Monstre d'essai", 0) == 0 && fillers.size() < 20) fillers.push_back(row);
    ExpectQuests({Quest(Slug, Character.c_str(), "ocre", 1)});
    ExpectList(Slug);
    for (const auto& row : fillers) { ExpectStep(Slug, row, 1); ExpectWrite(Slug, row, 2, Written(row, 2)); }
    for (const auto& row : fillers) EnqueueCapture(Captured(row));
    Tick(2000);
    const auto first = notices.size();
    Check(first > 0 && first < fillers.size() && Sent() <= 32, "One cycle stops after about 30 requests");
    Tick();
    Check(notices.size() == fillers.size(), "The next tick goes on at once");
    for (size_t i = 0; i < fillers.size(); ++i) Check(notices[i] == Added(fillers[i], 1, 2), "Each capture written once, in order");
    notices.clear();
    Finish("budget");
}

// « Synchroniser les captures avec Metamob » unticked: nothing more is written,
// what waits is dropped and said once (the user may enter it by hand: never
// counted twice), a write without a known result is named; the key still works.
void SyncTurnedOff() {
    // Unreachable, then turned off: dropped, never written, not even once on again.
    Begin(Folder("off"), true);
    Expect("GET", "/quests", Lost(12029, false));
    for (int i = 0; i < 3; ++i) EnqueueCapture(Captured(Tofu));
    Tick(2000);
    auto at = LastAt();
    Expect("GET", "/quests", Lost(12029, false));
    TickAt(at + 30000);
    at = LastAt();
    Expect("GET", "/quests", Lost(12029, false));
    TickAt(at + 60000);
    Said({"Metamob injoignable : 3 capture(s) en attente, nouvel essai automatique."}, "Unreachable");
    syncOn = false;
    Tick();
    Said({Dropped(3)}, "Turned off: the pending captures are dropped, said once");
    Check(Logged("METAMOB_SYNC_OFF dropped=3 unsettled=0"), "Drop log");
    const auto sent = Sent();
    Tick(15 * 60 * 1000);
    Tick(15 * 60 * 1000);
    Check(Sent() == sent && notices.empty(), "Nothing written while off");
    syncOn = true;
    Tick(15 * 60 * 1000);
    Check(Sent() == sent && notices.empty(), "Nor once turned on again");
    // A capture still in its 2 s wait when the sync goes off: dropped too.
    EnqueueCapture(Captured(Mob));
    syncOn = false;
    Tick();
    Said({Dropped(1)}, "A capture in its 2 s wait is dropped");
    Tick(60000);
    Check(Sent() == sent && notices.empty(), "Never sent");
    Finish("sync turned off while unreachable");

    // A refused key, turned off, then a new key saved: checked and stored, nothing
    // kept from the pause is written. On again, a new capture goes out with it;
    // forgetting works while off.
    const auto settings = Folder("off-auth");
    Begin(settings, true);
    Expect("GET", "/quests", Status(401));
    EnqueueCapture(Captured(Tofu));
    Tick(2000);
    Said({KeyRefused}, "Paused");
    syncOn = false;
    Tick();
    Said({Dropped(1)}, "Turned off during the pause");
    keyNow = OtherKey;
    Expect("GET", "/me", Json(MeBody(User)));
    SubmitKey(OtherKey);
    Tick();
    Said({SavedUser}, "A key saved while off");
    Tick(60000);
    Check(Sent() == 2 && notices.empty(), "Only the key check is sent");
    syncOn = true;
    ExpectFirst(Tofu, 2, 3);
    EnqueueCapture(Captured(Tofu));
    Tick(2000);
    Said({Added(Tofu, 2, 3)}, "On again: a new capture goes out with the new key");
    syncOn = false;
    ForgetStoredKey();
    Tick();
    Said({KeyForgotten}, "Forgotten while off");
    Check(!KeyStored(), "No key left");
    Finish("sync turned off during a key pause");

    // A write without a known result (its check unanswered too): named, never read
    // nor written again; the other capture waiting is dropped.
    Begin(Folder("off-flight"), true);
    ExpectQuests({Quest(Slug, Character.c_str(), "ocre", 1)});
    ExpectList(Slug);
    ExpectStep(Slug, Tofu, 2);
    ExpectWrite(Slug, Tofu, 3, Lost(12002, true));
    Expect("GET", StepPath(Slug, Tofu.step), Html(502));
    EnqueueCapture(Captured(Tofu));
    EnqueueCapture(Captured(Mob));
    Tick(2000);
    Check(notices.empty() && Logged("METAMOB_UNCONFIRMED monster=10370") && FlightAnnounced(Slug, Tofu), "An unconfirmed write is kept");
    syncOn = false;
    Tick();
    Said({OffUnsure(Tofu), Dropped(1)}, "Turned off: the unconfirmed write is named, the rest dropped");
    Check(Logged("METAMOB_SYNC_OFF dropped=1 unsettled=1") && !FlightAnnounced(Slug, Tofu), "Dropped: no longer announced");
    const auto before = Sent();
    syncOn = true;
    Tick(60000);
    Check(Sent() == before && notices.empty(), "Never read nor written again");
    ExpectStep(Slug, Larva, 1);
    ExpectWrite(Slug, Larva, 2, Written(Larva, 2));
    EnqueueCapture(Captured(Larva));
    Tick(2000);
    Said({Added(Larva, 1, 2)}, "On again: new captures go out");
    Finish("sync turned off with a write in flight");
}

void StopWhileSending() {
    Begin(Folder("stop"), true);
    metamob::testing::RunJobsInline(false);
    entered = false;
    intercept = [](const Request&, const std::atomic_bool& cancelled) {
        entered = true;
        for (int i = 0; i < 2000 && !cancelled.load(); ++i) Sleep(5); // a slow network, until cancelled
        Response response;
        response.error = ERROR_OPERATION_ABORTED;
        return response;
    };
    EnqueueCapture(Captured(Tofu));
    Tick(2000);
    Await([] { return entered.load(); }, 5000, "the worker's request");
    const auto started = std::chrono::steady_clock::now();
    RestoreSync();
    Check(std::chrono::steady_clock::now() - started < std::chrono::milliseconds(200), "Restore never waits for the worker");
    Check(Logged("METAMOB_SYNC_STOP pending=1"), "Pending capture logged at Restore");
    const auto end = GetTickCount64() + 5000;
    while (metamob::testing::WorkerRunning()) { Check(GetTickCount64() < end, "The cancelled worker ends"); Sleep(5); }
    intercept = nullptr;
    TickSync(syncOn);
    Check(notices.empty(), "Nothing posted after Restore");
    Finish("stop while sending");
}

// The worker-thread tests' Metamob: answers from its own quantities, records each
// request, and holds the chosen one until the test lets it go (the test acts
// meanwhile). A held request given its own reply is not applied.
std::mutex modelLock;
std::map<int64_t, int> stock;
std::vector<std::string> requested;
std::string holdAt;                 // "METHOD path" of the request held (empty: none)
std::optional<Response> heldReply;  // its reply instead of the model's
std::atomic_bool holding{false}, letGo{false};
std::atomic<int> holdAfter{0};        // matches of holdAt let through before the one held
std::atomic_bool losePatches{false};  // every write sent, never applied nor answered
Response ModelAnswer(const Request& request) {
    const std::vector<Row> rows{Mob, Tofu, Tiwa};
    const auto list = "/quests/" + Slug + "/monsters";
    std::lock_guard<std::mutex> guard(modelLock);
    if (request.method == "GET" && request.path == "/quests") return Json("{\"data\":[" + Quest(Slug, Character.c_str(), "ocre", 1) + "]}");
    if (request.method == "GET" && request.path == list + "?limit=200&offset=0") {
        std::vector<Row> page;
        for (auto row : rows) { row.quantity = stock[row.id]; page.push_back(row); }
        return Json(Page(Slug, page, page.size(), 0));
    }
    for (auto row : rows) {
        row.quantity = stock[row.id];
        if (request.method == "GET" && request.path == StepPath(Slug, row.step)) return Json(Page(Slug, {row}, 1, 0));
        if (request.method == "PATCH" && request.path == list + "/" + std::to_string(row.id)) {
            stock[row.id] = std::stoi(request.body.substr(request.body.find(':') + 1)); // applied by Metamob
            return Written(row, stock[row.id]);
        }
    }
    return Response{};
}
Response Gated(const Request& request, const std::atomic_bool& cancelled) {
    const auto seen = request.method + " " + request.path;
    {
        std::lock_guard<std::mutex> guard(modelLock);
        requested.push_back(seen);
    }
    if (losePatches.load() && request.method == "PATCH") return Lost(12002, true);
    if (seen != holdAt || holdAfter-- > 0 || holding.exchange(true)) return ModelAnswer(request);
    for (int i = 0; i < 2000 && !letGo.load() && !cancelled.load(); ++i) Sleep(5);
    return heldReply ? *heldReply : ModelAnswer(request);
}
// A real worker thread (fake clock) on that Metamob, holding `hold`.
void Gate(const Path& settings, const std::string& hold, std::optional<Response> reply = std::nullopt) {
    Begin(settings, true);
    metamob::testing::RunJobsInline(false);
    {
        std::lock_guard<std::mutex> guard(modelLock);
        stock = {{Mob.id, Mob.quantity}, {Tofu.id, Tofu.quantity}, {Tiwa.id, Tiwa.quantity}};
        requested.clear();
    }
    holdAt = hold; heldReply = std::move(reply); holding = false; letGo = false; holdAfter = 0; losePatches = false;
    intercept = Gated;
}
size_t Requested(const std::string& prefix = {}) {
    std::lock_guard<std::mutex> guard(modelLock);
    return static_cast<size_t>(std::count_if(requested.begin(), requested.end(), [&](const std::string& seen) { return seen.rfind(prefix, 0) == 0; }));
}
int Stock(const Row& row) { std::lock_guard<std::mutex> guard(modelLock); return stock[row.id]; }
bool Noticed(const std::string& text) { return std::find(notices.begin(), notices.end(), text) != notices.end(); }
// Until the worker has ended, its last notices taken.
void AwaitWorker(const std::string& what) {
    Await([] { return !metamob::testing::WorkerRunning(); }, 10000, what);
    Tick();
}
const std::string TofuWrite = "PATCH /quests/" + Slug + "/monsters/" + std::to_string(Tofu.id);

// Turned off while a worker sends: its cycle writes nothing more.
void TurnedOffWhileSending() {
    // While it reads the step: the write that would follow is never sent. Turned
    // on again before the worker ends: what it held is dropped all the same.
    Gate(Folder("off-thread"), "GET " + StepPath(Slug, Tofu.step));
    EnqueueCapture(Captured(Tofu));
    Tick(2000);
    Await([] { return holding.load(); }, 5000, "the worker's step read");
    syncOn = false;
    Tick();
    syncOn = true;
    letGo = true;
    AwaitWorker("the worker's end");
    Said({Dropped(1)}, "Turned off while reading: dropped, said once");
    Check(Requested("PATCH") == 0 && Stock(Tofu) == 2 && Logged("METAMOB_SYNC_OFF dropped=1 unsettled=0"), "Nothing written once turned off");
    Tick(60000);
    Check(Requested("PATCH") == 0 && notices.empty(), "Not resumed once on again");
    Finish("sync turned off while reading");
    // While a write is answered (Metamob has applied it): still confirmed, then
    // nothing more is read nor written.
    Gate(Folder("off-answered"), TofuWrite);
    for (const auto& row : {Tofu, Mob}) EnqueueCapture(Captured(row));
    Tick(2000);
    Await([] { return holding.load(); }, 5000, "the worker's write");
    syncOn = false;
    Tick();
    const auto before = Requested();
    letGo = true;
    AwaitWorker("the worker's end");
    Said({Added(Tofu, 2, 3), Dropped(1)}, "A write answered meanwhile is still confirmed, the rest dropped");
    Check(Requested() == before && Stock(Tofu) == 3 && Stock(Mob) == 0, "Nothing more read nor written");
    Finish("sync turned off while a write is answered");
    // While its write is answered without a result: read back (it may have landed),
    // found at its base, never sent again: named, to be checked.
    Gate(Folder("off-unsure"), TofuWrite, Lost(12002, true));
    EnqueueCapture(Captured(Tofu));
    Tick(2000);
    Await([] { return holding.load(); }, 5000, "the worker's write");
    syncOn = false;
    Tick();
    letGo = true;
    AwaitWorker("the worker's end");
    Said({OffUnsure(Tofu)}, "Turned off with a write unsettled: named");
    Check(Requested("PATCH") == 1 && Requested("GET " + StepPath(Slug, Tofu.step)) == 2 && Stock(Tofu) == 2 &&
        Logged("METAMOB_SYNC_OFF dropped=0 unsettled=1") && !FlightAnnounced(Slug, Tofu), "Read back, never sent again");
    Finish("sync turned off while writing");
}

// Stopped while a write is unsettled (its check under way): once the stopped
// worker ends, that write is no longer announced (other clients would wait).
void StopWhileUnsettled() {
    Gate(Folder("stop-unsure"), "GET " + StepPath(Slug, Tofu.step));
    holdAfter = 1;      // the read before the write goes through, its check is held
    losePatches = true;
    EnqueueCapture(Captured(Tofu));
    Tick(2000);
    Await([] { return holding.load(); }, 5000, "the worker's check");
    Check(FlightAnnounced(Slug, Tofu), "An unsettled write is announced");
    RestoreSync();
    Await([] { return !metamob::testing::WorkerRunning(); }, 10000, "the stopped worker's end");
    Check(!FlightAnnounced(Slug, Tofu) && Requested("PATCH") == 1, "A stopped worker's write is no longer announced");
    Finish("stop while a write is unsettled");
}

// The worker ends with an unsettled write and the Stop comes before TickSync took
// that job back (the next pump stops first): its write is no longer announced.
void StopAfterUnsettled() {
    Begin(Folder("stop-ended"), true);
    metamob::testing::RunJobsInline(false);
    ExpectQuests({Quest(Slug, Character.c_str(), "ocre", 1)});
    ExpectList(Slug);
    ExpectStep(Slug, Tofu, 2);
    ExpectWrite(Slug, Tofu, 3, Lost(12002, true));
    Expect("GET", StepPath(Slug, Tofu.step), Html(502));
    EnqueueCapture(Captured(Tofu));
    Tick(2000);
    // Waited for without TickSync (Await would take the job back and hide the defect).
    const auto end = GetTickCount64() + 10000;
    while (metamob::testing::WorkerRunning()) { Check(GetTickCount64() < end, "the worker's end"); Sleep(5); }
    Check(FlightAnnounced(Slug, Tofu), "An unsettled write is announced");
    RestoreSync();
    Check(!FlightAnnounced(Slug, Tofu), "A job that ended before the Stop no longer announces its write");
    Finish("stop after a write left unsettled");
}

// « Supprimer » confirmed while a cycle runs: no request starts after it, a write
// whose answer is on its way is still confirmed, then the key goes and what is
// left is said unsynchronised.
void ForgetWhileSending() {
    // Between two captures of one cycle (the worker reading Mob's step).
    auto settings = Folder("forget-cycle");
    Gate(settings, "GET " + StepPath(Slug, Mob.step));
    for (const auto& row : {Tofu, Mob, Tiwa}) EnqueueCapture(Captured(row));
    Tick(2000);
    Await([] { return holding.load(); }, 5000, "the worker's second capture");
    ForgetStoredKey();
    const auto before = Requested();
    letGo = true;
    Await([] { return Noticed(KeyForgotten) && !metamob::testing::WorkerRunning(); }, 10000, "the forget");
    Tick();
    Said({Added(Tofu, 2, 3), KeyForgotten, NoKey("Mob l'Éponge"), NoKey("Tiwa'Missou le Gateux")}, "Forgotten between two captures");
    Check(Requested() == before && Stock(Mob) == 0 && Stock(Tiwa) == 3 && !KeyStored(), "No request after the forget, the key file gone");
    Finish("forget between two captures");
    // While it reads the quests: the monster list is never asked for.
    settings = Folder("forget-quests");
    Gate(settings, "GET /quests");
    EnqueueCapture(Captured(Tofu));
    Tick(2000);
    Await([] { return holding.load(); }, 5000, "the worker's first read");
    ForgetStoredKey();
    const auto first = Requested();
    letGo = true;
    Await([] { return Noticed(KeyForgotten) && !metamob::testing::WorkerRunning(); }, 10000, "the forget");
    Tick();
    Said({KeyForgotten, NoKey("Tofumanchou l'Empereur")}, "Forgotten during the first read");
    Check(Requested() == first && !KeyStored(), "Nothing read after the forget");
    Finish("forget during the first read");
    // While Tofu's write is answered (Metamob has applied it): still confirmed.
    settings = Folder("forget-write");
    Gate(settings, TofuWrite);
    for (const auto& row : {Tofu, Mob}) EnqueueCapture(Captured(row));
    Tick(2000);
    Await([] { return holding.load(); }, 5000, "the worker's write");
    ForgetStoredKey();
    const auto during = Requested();
    letGo = true;
    Await([] { return Noticed(KeyForgotten) && !metamob::testing::WorkerRunning(); }, 10000, "the forget");
    Tick();
    Said({Added(Tofu, 2, 3), KeyForgotten, NoKey("Mob l'Éponge")}, "A write answered meanwhile is still confirmed");
    Check(Requested() == during && Stock(Tofu) == 3 && Stock(Mob) == 0 && !KeyStored(), "Nothing sent after the forget");
    Finish("forget while a write is answered");
    // The mod stops right after the forget (the game closes): the forget is not lost.
    settings = Folder("forget-stop");
    Gate(settings, "GET " + StepPath(Slug, Tofu.step));
    EnqueueCapture(Captured(Tofu));
    Tick(2000);
    Await([] { return holding.load(); }, 5000, "the worker's read");
    ForgetStoredKey();
    RestoreSync();
    Check(!HasKey(KeyPath(settings)) && Logged("METAMOB_KEY_FORGOTTEN"), "Forgotten at Stop, the worker still running");
    letGo = true;
    Await([] { return !metamob::testing::WorkerRunning(); }, 10000, "the stopped worker's end");
    InstallSync(settings, Scripted, Noted);
    Check(!KeyStored() && Requested("PATCH") == 0, "The next session has no key");
    Finish("forget then stop");
    // Confirmed in the pump right before the Stop, no worker running: done all the same.
    settings = Folder("forget-pending");
    Begin(settings, true);
    ForgetStoredKey();
    RestoreSync();
    Check(!HasKey(KeyPath(settings)), "A forget not started yet is done at Stop");
    Finish("forget pending at stop");
    // While waiting for another client's sender lock: the wait ends at once.
    Begin(Folder("forget-wait"), true);
    ExpectFirst(Tofu, 2, 3);
    EnqueueCapture(Captured(Tofu));
    Tick(2000);
    Said({Added(Tofu, 2, 3)}, "First write");
    {
        Holder other;
        onSleep = [] { ForgetStoredKey(); }; // the first sleep is the wait for the lock
        const auto sent = Sent();
        EnqueueCapture(Captured(Mob));
        Tick(2000);
        Check(Sent() == sent && !Logged("METAMOB_BUSY"), "The wait for the lock ends with the forget");
    }
    Tick();
    Said({KeyForgotten, NoKey("Mob l'Éponge")}, "Forgotten while waiting for the lock");
    Finish("forget while waiting for the lock");
    // Confirmed while a key typed before is being checked: that key is never stored.
    Begin(Folder("forget-submit"), true);
    intercept = [](const Request& request, const std::atomic_bool&) {
        if (request.path == "/me") ForgetStoredKey(); // « Supprimer » confirmed meanwhile
        return Json(MeBody(User));
    };
    SubmitKey(OtherKey);
    Tick();
    Tick();
    Said({KeyForgotten}, "A key checked while the forget is confirmed is not stored");
    Check(!KeyStored(), "No key left");
    Finish("forget while a key is checked");
    // A write without a known result when the key goes: named, never said unsynchronised.
    Begin(Folder("forget-unsure"), true);
    ExpectQuests({Quest(Slug, Character.c_str(), "ocre", 1)});
    ExpectList(Slug);
    ExpectStep(Slug, Tofu, 2);
    ExpectWrite(Slug, Tofu, 3, Lost(12002, true));
    Expect("GET", StepPath(Slug, Tofu.step), Html(502));
    EnqueueCapture(Captured(Tofu));
    Tick(2000);
    Check(notices.empty() && FlightAnnounced(Slug, Tofu), "Kept in flight");
    ForgetStoredKey();
    Tick();
    Said({KeyForgotten, ForgetUnsure(Tofu)}, "The unsettled write is named when the key goes");
    Check(!FlightAnnounced(Slug, Tofu) && !KeyStored(), "Forgotten, no longer announced");
    Finish("forget with a write unsettled");
}

// The real wrapper: a detached worker and the real clock, end to end.
void Threaded() {
    Begin(Folder("thread"), false);
    metamob::testing::SetClock(nullptr, nullptr, nullptr);
    metamob::testing::RunJobsInline(false);
    Expect("GET", "/me", Json(MeBody(User)));
    ExpectFirst(Tofu, 2, 3);
    SubmitKey(FakeKey);
    Await([] { return !notices.empty(); }, 10000, "the key checked on the worker");
    Said({SavedUser}, "Key saved by the worker");
    EnqueueCapture(Captured(Tofu));
    Await([] { return !notices.empty(); }, 30000, "the capture sent by the worker");
    Said({Added(Tofu, 2, 3)}, "Capture sent by the worker");
    Await([] { return !metamob::testing::WorkerRunning(); }, 5000, "the worker's end");
    {
        std::lock_guard<std::mutex> guard(scriptLock);
        Check(calls.size() == 6, "/me, then quests, two pages, the step and the write");
        for (size_t i = 0; i < calls.size(); ++i) {
            Check(calls[i].thread != GetCurrentThreadId(), "Requests run on the worker, never on the game thread");
            if (i) Check(calls[i].real - calls[i - 1].real >= 1100, "1.1 s between two requests in real time");
        }
        Check(calls[4].held && calls[5].held && !calls[3].held, "The worker holds the sender lock for the read and the write");
    }
    TickSync(syncOn);
    Finish("threaded");
}

// Every plaintext copy of the key is wiped before its heap block is released:
// the text given to SubmitKey, the key checked then stored by the worker, the
// key a cycle loads. The transport answers through intercept, which never copies the key.
void WipedCopies() {
    const auto settings = Folder("wiped");
    Begin(settings, false);
    intercept = [](const Request& request, const std::atomic_bool&) {
        if (request.method == "GET" && request.path == "/me") return Json(MeBody(User));
        if (request.method == "GET" && request.path == "/quests") return Json("{\"data\":[]}");
        return Response{};
    };
    std::string typed = " " + FakeKey + " "; // the test's own copies are made before arming
    heapprobe::Arm();
    SubmitKey(std::move(typed));
    Tick();
    EnqueueCapture(Captured(Tofu));
    Tick(2000); // one cycle: the key loaded from the file, sent, released
    Check(heapprobe::Disarm() == 0, "A heap block was released with a plaintext copy of the key");
    Said({SavedUser, NoQuest}, "A key saved then used while the heap is watched");
    Check(LoadKey(KeyPath(settings))->value() == FakeKey, "The watched key is stored");
    Finish("wiped copies");
}

void NoPrivateData() {
    std::lock_guard<std::mutex> guard(logLock);
    Check(allLogs.size() > 150 && allNotices.size() > 60, "The scan covers the whole run");
    for (const auto& line : allLogs)
        Check(!Leaks(line, FakeKey) && !Leaks(line, OtherKey) && !Leaks(line, Slug) && !Leaks(line, OtherSlug) && !Leaks(line, Character) &&
            !Leaks(line, User) && !Leaks(line, Marker) && line.find("Bearer") == std::string::npos && line.find("transport failed") == std::string::npos,
            "A log line carries private data");
    for (const auto& text : allNotices) {
        const bool account = text.rfind("Metamob : clé enregistrée pour le compte « ", 0) == 0;
        Check(!Leaks(text, FakeKey) && !Leaks(text, OtherKey) && !Leaks(text, Slug) && !Leaks(text, OtherSlug) && !Leaks(text, Character) &&
            (account || !Leaks(text, User)) && !Leaks(text, Marker), "A notice carries private data");
    }
}
}

void Log(const std::string& text) {
    std::lock_guard<std::mutex> guard(logLock);
    logs.push_back(text);
    allLogs.push_back(text);
}

int main() {
    root = std::filesystem::temp_directory_path() / ("DofusMetamobSyncTests-" + std::to_string(GetCurrentProcessId()));
    try {
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
        KeySubmission();
        WipedCopies();
        CaptureWithoutKey();
        CaptureWhileKeyChecked();
        HappyPath();
        Grouping();
        Cap();
        NotInQuest();
        ChoosingQuests();
        AuthPause();
        RefusedKeyRetry();
        ProxyPages();
        Rediscovery();
        StaleAnswers();
        RateLimit();
        NetworkThenResume();
        UnknownOutcome();
        SiblingClients();
        HeadOfLine();
        LateLanding();
        WriteAnswers();
        SenderMutex();
        KeyFileProblems();
        Robustness();
        Budget();
        SyncTurnedOff();
        StopWhileSending();
        TurnedOffWhileSending();
        StopWhileUnsettled();
        StopAfterUnsettled();
        ForgetWhileSending();
        Threaded();
        NoPrivateData();
        std::filesystem::remove_all(root);
        std::cout << "PASS: Metamob capture sync: key check and storage, debounce and grouping, quest choice, re-read before every "
            "absolute write, 30 cap, unmatched names, 401 pause and its 10 min retry, proxy 401/403 pages, 404 rediscovery, Retry-After, "
            "backoff, unknown outcomes settled under the sender lock and announced to sibling clients (only that monster waits), "
            "resends never +n, unsettled writes given up as unconfirmed, sync toggle off, forget during a cycle and at Stop, no write "
            "left announced after a Stop, sender lock, worker thread, key copies wiped, no private data in logs or notices\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        {
            // The module's log lines of the failing scenario (fixed codes and numbers only).
            std::lock_guard<std::mutex> guard(logLock);
            for (const auto& line : logs) std::cerr << "  log: " << line << '\n';
        }
        RestoreSync();
        for (int i = 0; i < 500 && metamob::testing::WorkerRunning(); ++i) Sleep(10);
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
        return 1;
    }
}
