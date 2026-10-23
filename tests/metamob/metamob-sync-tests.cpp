#include "metamob/metamob-sync.hpp"
#include "metamob/metamob-data.hpp"
#include "metamob/metamob-heap-probe.hpp"
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
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
std::string failingLog; // a log line starting with it throws: an unexpected failure inside the worker
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
struct Row { int64_t id; std::string name; int type, step, quantity; std::string en, es; }; // en, es: empty for the marker, null
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
    const auto es = row.es.empty() ? std::string("null") : "\"" + row.es + "\"";
    return "{\"id\":" + std::to_string(row.id) + ",\"name\":{\"fr\":\"" + row.name + "\",\"en\":\"" + (row.en.empty() ? Marker : row.en) +
        "\",\"es\":" + es + "},"
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
std::string ListPath(const std::string& slug, int offset) { return "/quests/" + slug + "/monsters?limit=200&offset=" + std::to_string(offset); }
// Both pages of a quest's list (the test catalog unless given).
void ExpectList(const std::string& slug, const std::vector<Row>& rows = Catalog()) {
    Expect("GET", ListPath(slug, 0), Json(Page(slug, {rows.begin(), rows.begin() + 200}, rows.size(), 0)));
    Expect("GET", ListPath(slug, 200), Json(Page(slug, {rows.begin() + 200, rows.end()}, rows.size(), 200)));
}
// The test catalog with some quantities changed (Metamob id, quantity).
std::vector<Row> CatalogWith(const std::vector<std::pair<int64_t, int>>& quantities) {
    auto rows = Catalog();
    for (auto& row : rows)
        for (const auto& [id, quantity] : quantities) if (row.id == id) row.quantity = quantity;
    return rows;
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
    KeyRefused = "Metamob : clé refusée (révoquée ou régénérée ?). Supprime-la avec la corbeille dans les options, puis enregistre une nouvelle clé.",
    KeyUnreadable = "Metamob : clé illisible sur ce compte Windows. Supprime-la avec la corbeille dans les options, puis enregistre-la à nouveau.",
    SavedUser = "Metamob : clé enregistrée pour le compte « Zyxwvut99 ».",
    NoQuest = "Metamob : aucune quête Ocre Dofus Unity sur ton compte, rien n’a été modifié.",
    SeveralQuests = "Metamob : plusieurs quêtes Ocre sur ton compte ; indique le nom de ce personnage dans « Personnage à contacter » de la bonne quête.";
std::string Added(const Row& row, int before, int after) {
    return "Metamob : +" + std::to_string(after - before) + " « " + row.name + " » (de " + std::to_string(before) + " à " + std::to_string(after) + ").";
}
std::string NoKey(const std::string& name) {
    return "Metamob : capture de « " + name + " » non synchronisée : aucune clé enregistrée (Options › Dofus Enhanced › Pierre d’âme).";
}
std::string Unmatched(const std::string& name) { return "Metamob : « " + name + " » introuvable dans ta quête Ocre, rien n’a été modifié."; }
std::string Capped(const Row& row) { return "Metamob : « " + row.name + " » est déjà à 30, la quantité maximale."; }
std::string Changed(const Row& row, int before, int after) {
    return "Metamob : « " + row.name + " » a changé pendant l’envoi (de " + std::to_string(before) + " à " + std::to_string(after) +
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

// « Metamob dans le chat »: the chat part's game view. Game ids from the Ocre
// goal table; their names and kinds are labels matching the test catalog (the
// game's own pairs are capture detection's business).
constexpr int GameBouftou = 147, GameMob = 797, GameTofu = 2270, GameTiwa = 2271, GameAbrakroc = 2272, GameLarva = 2273,
    GameMissing = 2275,   // an Ocre goal whose name is not in the user's quest
    GameOtherKind = 2276, // Tofumanchou's name as a boss: not that monster
    GamePlain = 2277,     // neither boss nor archimonster
    GameNameless = 2278,  // unknown to the game view
    GameStranger = 31;    // outside L’éternelle moisson
std::map<int, GameMonster> bestiary;
std::map<int, int> lookedUp; // game view reads per monster
std::string playing;         // the local character
void Bestiary() {
    bestiary = {{GameBouftou, {"Bouftou Royal", TypeBoss}}, {GameMob, {"Mob l’Éponge", TypeBoss}},
        {GameTofu, {"Tofumanchou l'Empereur", TypeArchimonster}}, {GameTiwa, {"Tiwa’Missou le Gâteux", TypeArchimonster}},
        {GameAbrakroc, {"Abrakroc l'édenté", TypeArchimonster}}, {GameLarva, {"Larvalœil l’Émue", TypeArchimonster}},
        {GameMissing, {"Krokette la Croustillante", TypeArchimonster}}, {GameOtherKind, {"Tofumanchou l'Empereur", TypeBoss}},
        {GamePlain, {"Bouftou", TypeMonster}}, {GameStranger, {"Bouftou Royal", TypeBoss}}};
    lookedUp.clear();
    playing = Character;
}
GameView View() {
    return {[](int id) -> std::optional<GameMonster> {
        ++lookedUp[id];
        const auto found = bestiary.find(id);
        return found == bestiary.end() ? std::nullopt : std::optional<GameMonster>(found->second);
    }, [] { return playing; }};
}
// The visible texts of the chat part (DESIGN r110), written here independently of the module.
const std::string NotRead = "Metamob : ?.\nTa quête Ocre sur Metamob n’a pas encore été lue ou n’a pas pu l’être ; nouvel essai automatique.",
    NoQuestDetail = "Metamob : ?.\nAucune quête Ocre Dofus Unity sur ton compte Metamob : crée-la sur metamob.fr, puis elle sera lue automatiquement.",
    SeveralDetail = "Metamob : ?.\nPlusieurs quêtes Ocre Dofus Unity sur ton compte Metamob, et aucune n’indique ce personnage dans "
        "« Personnage à contacter » : ajoute son nom dans la bonne quête sur metamob.fr.";
std::string Counted(int quantity) {
    return "Metamob : " + std::to_string(quantity) + ".\nTa quête Ocre sur Metamob compte " + std::to_string(quantity) +
        (quantity > 1 ? " exemplaires" : " exemplaire") + " de ce monstre ; elle est relue au plus toutes les 10 minutes et mise à jour "
        "aussitôt après une capture synchronisée par le mod.";
}
std::string Colored(int quantity) {
    return std::string(quantity > 0 ? "<color=#8DDB91>" : "<color=#F18C8C>") + "Metamob : " + std::to_string(quantity) + "</color>";
}
std::string Rich(int monster) { return MetamobChatCaption(monster, true); }
std::string Plain(int monster) { return MetamobChatCaption(monster, false); }
// The maps' view of the same cache.
bool Ocre(int monster, int quantity, OcreList list) {
    const auto value = MetamobOcreQuantity(monster);
    return value.usable && value.quantity == quantity && value.list == list;
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
    Bestiary();
    InstallSync(settings, Scripted, Noted, View());
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
// A chat pump: a label shows the Metamob part (it asks for the list), then the sync ticks.
void Ask(uint64_t advance = 0) { fakeNow += advance; WantMetamobCatalog(); TickSync(syncOn); }
void AskAt(uint64_t at) { if (fakeNow.load() < at) fakeNow = at; WantMetamobCatalog(); TickSync(syncOn); }
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
    Said({"Metamob : +1 « Tofumanchou l'Empereur » (de 2 à 3)."}, "Happy path");
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
    Said({"Metamob : +1 « Mob l'Éponge » (de 0 à 1)."}, "Cached quest and list");
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
    Said({"Metamob : +2 « Tofumanchou l'Empereur » (de 2 à 4)."}, "Two captures, one write");
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

// A key Metamob lets read but not write: refused at its write, told once. Its
// retries 10 min later read with it, then are refused at the write again: never
// told again (a read accepted does not end the pause, only a confirmed write).
void ReadOnlyKey() {
    Begin(Folder("read-only"), true);
    ExpectQuests({Quest(Slug, Character.c_str(), "ocre", 1)});
    ExpectList(Slug);
    ExpectStep(Slug, Tofu, 2);
    ExpectWrite(Slug, Tofu, 3, Status(403));
    EnqueueCapture(Captured(Tofu));
    Tick(2000);
    Said({KeyRefused}, "Refused at its write");
    for (size_t round = 0; round < 2; ++round) {
        for (int i = 0; i < 19; ++i) Tick(30000);
        Check(Sent() == 5 + 2 * round, "Not tried again before 10 min");
        ExpectStep(Slug, Tofu, 2);
        ExpectWrite(Slug, Tofu, 3, Status(403));
        Tick(30000);
        Check(notices.empty() && LoggedCount("METAMOB_KEY_REFUSED") == 1 && StoredKeyStatus() == KeyStatus::Refused,
            "Read again 10 min later, refused again at its write: never told again");
    }
    Finish("read-only key");
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
    Check(!KeyStored() && StoredKeyStatus() == KeyStatus::None, "No key known after Restore");
    SubmitKey(FakeKey);
    ForgetStoredKey();
    CheckStoredKey(true);
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
std::atomic_bool meLost{false};       // GET /me sent, never answered (Metamob unreachable)
Response ModelAnswer(const Request& request) {
    const std::vector<Row> rows{Mob, Tofu, Tiwa};
    const auto list = "/quests/" + Slug + "/monsters";
    std::lock_guard<std::mutex> guard(modelLock);
    if (request.method == "GET" && request.path == "/me") return meLost.load() ? Lost(12029, false) : Json(MeBody(User));
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
    holdAt = hold; heldReply = std::move(reply); holding = false; letGo = false; holdAfter = 0; losePatches = false; meLost = false;
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

// « Oui » confirmed while a cycle runs: no request starts after it, a write
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
        if (request.path == "/me") ForgetStoredKey(); // « Oui » confirmed meanwhile
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

// The key row as the options show it (OptionsUi reads the status, then asks for
// a check while a Saved key is shown).
void RowShown() { if (StoredKeyStatus() == KeyStatus::Saved) CheckStoredKey(false); }
// Another game client of this PC saves a key: a later write time than the file it replaces.
void AnotherClientSaves(const Path& settings, const std::string& key) {
    Sleep(40); // past the resolution of file times
    StoreKey(KeyPath(settings), Secret(key));
}

// The key row's status follows what the capture sync learnt of the key file,
// never more: saved verified or not, a refused new key leaving the saved one as
// it was, forgotten (none at once, before the worker deletes the file), a failed
// forget, a key file there at start, another client's key saved, replaced or
// forgotten.
void KeyStatusFollowsTheKey() {
    const auto settings = Folder("status");
    const auto file = KeyPath(settings);
    Begin(settings, false);
    Check(StoredKeyStatus() == KeyStatus::None, "No key file: none");
    Expect("GET", "/me", Json(MeBody(User)));
    SubmitKey(FakeKey);
    Check(StoredKeyStatus() == KeyStatus::None, "A key being checked is not stored yet");
    Tick();
    Said({SavedUser}, "A verified key");
    Check(StoredKeyStatus() == KeyStatus::Verified, "Saved with its account named: valid");
    keyNow = OtherKey;
    Expect("GET", "/me", Html(503));
    SubmitKey(OtherKey);
    Tick();
    Said({KeyUnverified}, "A key saved while Metamob is unreachable");
    Check(StoredKeyStatus() == KeyStatus::Unverified && LoadKey(file)->value() == OtherKey, "Saved without its check: not verified");
    Expect("GET", "/me", Status(401));
    SubmitKey(OtherKey);
    Tick();
    Said({KeyRejected}, "A refused key");
    Check(StoredKeyStatus() == KeyStatus::Unverified && LoadKey(file)->value() == OtherKey, "A refused new key leaves the saved one as it was");
    ForgetStoredKey();
    Check(StoredKeyStatus() == KeyStatus::None && KeyStored(), "Forgotten: none at once, before the file is deleted");
    Tick();
    Said({KeyForgotten}, "Forgotten");
    Check(StoredKeyStatus() == KeyStatus::None && !KeyStored(), "Deleted: none");
    // A forget that fails (a folder in place of the key file): that file again, as it was known.
    std::filesystem::create_directory(file);
    Check(StoredKeyStatus() == KeyStatus::Saved, "Something in place of the key file: not checked yet");
    ForgetStoredKey();
    Check(StoredKeyStatus() == KeyStatus::None, "None while the forget runs");
    Tick();
    Said({"Metamob : impossible d’oublier la clé sur ce PC."}, "Forget refused");
    Check(StoredKeyStatus() == KeyStatus::Saved, "Not deleted: shown again as it was");
    std::filesystem::remove(file);
    Check(StoredKeyStatus() == KeyStatus::None, "Removed: none");
    Finish("key status of saved and forgotten keys");

    // The next session with a key file: Saved, never checked unless the key row
    // shows it. Another client's key saved, replaced, forgotten: not checked yet, then none.
    Begin(settings, true);
    InstallSync(settings, Scripted, Noted);
    Check(StoredKeyStatus() == KeyStatus::Saved, "A key file there at start: not checked yet");
    Tick(60000);
    Check(StoredKeyStatus() == KeyStatus::Saved && Sent() == 0, "Not checked unless the key row shows it");
    Expect("GET", "/me", Json(MeBody(User)));
    RowShown();
    Tick();
    Check(StoredKeyStatus() == KeyStatus::Verified && notices.empty(), "Checked once shown: valid, silently");
    AnotherClientSaves(settings, OtherKey);
    Check(StoredKeyStatus() == KeyStatus::Saved, "Replaced by another client: not checked yet");
    ForgetKey(file);
    Check(StoredKeyStatus() == KeyStatus::None, "Forgotten by another client: none");
    AnotherClientSaves(settings, FakeKey);
    Check(StoredKeyStatus() == KeyStatus::Saved, "Saved again by another client: not checked yet");
    Finish("key status at start and from other clients");
}

// A capture cycle tells the key row what Metamob answered the stored key: a 2xx
// makes it valid, its refusal refused until it accepts the key again, a page
// that is not Metamob's nothing, an unreadable file unreadable.
void KeyStatusFromCycles() {
    const auto settings = Folder("status-cycle");
    Begin(settings, true);
    Check(StoredKeyStatus() == KeyStatus::Saved, "A key file not checked yet");
    ExpectFirst(Tofu, 2, 3);
    EnqueueCapture(Captured(Tofu));
    Tick(2000);
    Said({Added(Tofu, 2, 3)}, "A capture sent");
    Check(StoredKeyStatus() == KeyStatus::Verified, "Answered with the stored key: valid");
    Expect("GET", StepPath(Slug, Mob.step), Status(401));
    EnqueueCapture(Captured(Mob));
    Tick(2000);
    Said({KeyRefused}, "Refused");
    Check(StoredKeyStatus() == KeyStatus::Refused, "Refused by Metamob: refused");
    RowShown();
    for (int i = 0; i < 19; ++i) Tick(30000);
    Check(StoredKeyStatus() == KeyStatus::Refused && Sent() == 6, "Refused during the pause, never checked by the key row");
    ExpectStep(Slug, Mob, 0);
    ExpectWrite(Slug, Mob, 1, Written(Mob, 1));
    Tick(30000);
    Said({Added(Mob, 0, 1)}, "Accepted again 10 min later");
    Check(StoredKeyStatus() == KeyStatus::Verified, "Accepted again: valid");
    Expect("GET", StepPath(Slug, Tiwa.step), Html(401));
    EnqueueCapture(Captured(Tiwa));
    Tick(2000);
    Check(StoredKeyStatus() == KeyStatus::Verified && notices.empty(), "A 401 page that is not Metamob's says nothing of the key");
    {
        std::ofstream output(KeyPath(settings), std::ios::binary);
        output << "DEMK1\n" << Marker;
    }
    Check(StoredKeyStatus() == KeyStatus::Saved, "Another key file: not checked yet");
    Tick(30000);
    Said({KeyUnreadable}, "An unreadable key");
    Check(StoredKeyStatus() == KeyStatus::Unreadable, "Unreadable");
    Finish("key status from cycles");
}

// The key row shows a key not checked in this session: one silent GET /v1/me,
// the sync on or off. Once per key file and session; « Clé non vérifiée »
// clicked checks it once more, after the pause of a failed check (30 s, or its
// Retry-After). Nothing is said in the chat; an answer about a key file
// replaced meanwhile is not taken for the new one.
void SilentCheck() {
    const auto settings = Folder("check");
    const auto file = KeyPath(settings);
    Begin(settings, true);
    syncOn = false;
    Expect("GET", "/me", Json(MeBody(User)));
    RowShown();
    RowShown();
    Tick();
    Check(StoredKeyStatus() == KeyStatus::Verified && notices.empty() && Sent() == 1, "Checked once, silently, the sync off: valid");
    Check(Logged("METAMOB_HTTP method=GET route=/me status=200") && Logged("METAMOB_KEY_CHECKED status=verified"), "Check logs");
    for (int i = 0; i < 5; ++i) { RowShown(); CheckStoredKey(false); CheckStoredKey(true); Tick(60000); }
    Check(Sent() == 1 && notices.empty(), "Never twice for one key file; a valid key is not checked again on request");
    AnotherClientSaves(settings, OtherKey);
    keyNow = OtherKey;
    Expect("GET", "/me", Status(401));
    RowShown();
    Tick();
    Check(StoredKeyStatus() == KeyStatus::Refused && notices.empty() && Sent() == 2, "Refused by Metamob: refused, silently");
    for (int i = 0; i < 3; ++i) { RowShown(); CheckStoredKey(true); Tick(60000); }
    Check(Sent() == 2 && Logged("METAMOB_KEY_CHECKED status=refused"), "A refused key is not checked again");
    // Unreachable: not verified; checked again only when asked, 30 s after the failure.
    AnotherClientSaves(settings, FakeKey);
    keyNow = FakeKey;
    Expect("GET", "/me", Lost(12029, false));
    RowShown();
    Tick();
    const auto failed = LastAt();
    Check(StoredKeyStatus() == KeyStatus::Unverified && notices.empty() && Sent() == 3, "Unreachable: not verified, silently");
    RowShown();
    Tick(10000);
    Check(Sent() == 3, "Not checked again by itself");
    CheckStoredKey(true);
    Check(StoredKeyStatus() == KeyStatus::Saved, "« Clé non vérifiée » clicked: being checked again");
    CheckStoredKey(true);
    RowShown();
    TickAt(failed + 29999);
    Check(Sent() == 3 && StoredKeyStatus() == KeyStatus::Saved, "Not before 30 s after the failed check, asked once however often");
    Expect("GET", "/me", Json(MeBody(User)));
    TickAt(failed + 30000);
    Check(StoredKeyStatus() == KeyStatus::Verified && notices.empty() && Sent() == 4, "Checked once more on request: valid");
    // Rate limited: not verified, checked again after its Retry-After only.
    AnotherClientSaves(settings, OtherKey);
    keyNow = OtherKey;
    Expect("GET", "/me", Status(429, "120"));
    RowShown();
    Tick();
    const auto limited = LastAt();
    Check(StoredKeyStatus() == KeyStatus::Unverified && notices.empty() && Sent() == 5, "Rate limited: not verified");
    CheckStoredKey(true);
    TickAt(limited + 119999);
    Check(Sent() == 5, "Not before its Retry-After");
    Expect("GET", "/me", Json(MeBody(User)));
    TickAt(limited + 120000);
    Check(StoredKeyStatus() == KeyStatus::Verified && Sent() == 6, "Checked after its Retry-After: valid");
    // An unreadable key file: unreadable, nothing sent nor said.
    {
        std::ofstream output(file, std::ios::binary);
        output << "DEMK1\n" << Marker;
    }
    RowShown();
    Tick();
    Check(StoredKeyStatus() == KeyStatus::Unreadable && Sent() == 6 && notices.empty() && Logged("METAMOB_KEY_CHECKED status=unreadable"),
        "Unreadable: nothing sent nor said");
    // Replaced by another client while it is checked: the answer about the old
    // key is not taken for the new one, which is checked in turn.
    AnotherClientSaves(settings, FakeKey);
    bool swapped = false;
    intercept = [&](const Request& request, const std::atomic_bool&) {
        if (request.path == "/me" && !swapped) { swapped = true; AnotherClientSaves(settings, OtherKey); }
        return Json(MeBody(User));
    };
    RowShown();
    Tick();
    Check(swapped && StoredKeyStatus() == KeyStatus::Saved, "An answer about a replaced key file is not taken for the new one");
    RowShown();
    Tick();
    Check(StoredKeyStatus() == KeyStatus::Verified, "The new key file is checked in turn");
    intercept = nullptr;
    // A check that fails unexpectedly (its log line throws here): not verified,
    // never « Vérification… » for good.
    AnotherClientSaves(settings, FakeKey);
    keyNow = FakeKey;
    Expect("GET", "/me", Json(MeBody(User)));
    failingLog = "METAMOB_KEY_CHECKED";
    RowShown();
    Tick();
    failingLog.clear();
    Check(StoredKeyStatus() == KeyStatus::Unverified && Logged("METAMOB_WORKER_ERROR") && notices.empty(), "A check that told nothing: not verified");
    // Forgotten while a check waits for its pause: never sent.
    AnotherClientSaves(settings, FakeKey);
    keyNow = FakeKey;
    Expect("GET", "/me", Lost(12029, false));
    RowShown();
    Tick();
    const auto sent = Sent();
    Check(StoredKeyStatus() == KeyStatus::Unverified, "Unreachable again");
    CheckStoredKey(true);
    ForgetStoredKey();
    Check(StoredKeyStatus() == KeyStatus::None, "Forgotten: none at once");
    Tick(30000);
    Said({KeyForgotten}, "Forgotten while a check waits");
    Check(Sent() == sent && StoredKeyStatus() == KeyStatus::None, "A check waiting when the key is forgotten is never sent");
    Finish("silent check");
}

// A failed check holds the captures of its own worker too: with Metamob rate
// limiting or unreachable, the cycle that was due with the key row's check waits
// for the check's pause (its Retry-After, or 30 s), then sends them.
void CheckHoldsTheCycle() {
    for (const bool limited : {true, false}) {
        Begin(Folder(limited ? "check-hold-429" : "check-hold-lost"), true);
        Expect("GET", "/quests", Status(429, "60"));
        EnqueueCapture(Captured(Tofu));
        Tick(2000);
        const auto paused = LastAt();
        RowShown();
        Check(StoredKeyStatus() == KeyStatus::Saved && Sent() == 1, "Rate limited: the capture and the key row's check wait");
        Expect("GET", "/me", limited ? Status(429, "60") : Lost(12029, false));
        TickAt(paused + 60000); // both due again: one worker, the check first
        const auto checked = LastAt();
        Check(Sent() == 2 && StoredKeyStatus() == KeyStatus::Unverified && notices.empty(), "The check fails: not verified, the cycle not sent");
        const uint64_t hold = limited ? 60000 : 30000;
        TickAt(checked + hold - 1);
        Check(Sent() == 2, "Nothing sent before the check's pause ends");
        ExpectFirst(Tofu, 2, 3);
        TickAt(checked + hold);
        Said({Added(Tofu, 2, 3)}, "The capture sent once the check's pause ends");
        Check(StoredKeyStatus() == KeyStatus::Verified, "and the key valid from that cycle");
        Finish(limited ? "a rate-limited check holds the cycle" : "an unreachable check holds the cycle");
    }
}

// The key row shown while a cycle runs with that key: the cycle's answers tell
// its status, and the check asked for meanwhile is never sent.
void CheckSkippedOnceKnown() {
    Gate(Folder("check-known"), "GET /quests");
    EnqueueCapture(Captured(Tofu));
    Tick(2000);
    Await([] { return holding.load(); }, 5000, "the cycle's first read");
    RowShown();
    Check(StoredKeyStatus() == KeyStatus::Saved, "Shown while the cycle waits for Metamob: not checked yet");
    letGo = true;
    AwaitWorker("the cycle's end");
    Said({Added(Tofu, 2, 3)}, "The capture written");
    AwaitWorker("anything started after it");
    Check(StoredKeyStatus() == KeyStatus::Verified && Requested("GET /me") == 0, "Valid from the cycle's answers: the check is never sent");
    Finish("check skipped once the status is known");
}

// « Oui » while the check is on its way: nothing is learnt from its answer.
void CheckWhileForgetting() {
    const auto settings = Folder("check-forget");
    Gate(settings, "GET /me");
    RowShown();
    Tick();
    Await([] { return holding.load(); }, 5000, "the key row's check");
    ForgetStoredKey();
    Check(StoredKeyStatus() == KeyStatus::None, "None at once");
    letGo = true;
    Await([] { return Noticed(KeyForgotten) && !metamob::testing::WorkerRunning(); }, 10000, "the forget");
    Tick();
    Said({KeyForgotten}, "Forgotten while checked");
    Check(StoredKeyStatus() == KeyStatus::None && !KeyStored() && Requested("GET /me") == 1 && LoggedCount("METAMOB_KEY_CHECKED") == 0,
        "Nothing learnt from the answer, the key gone");
    Finish("check while forgetting");
}

// « Clé non vérifiée » clicked while the worker of that check still runs (here
// on a key typed meanwhile, which Metamob refuses): the click is not undone when
// that worker ends, its check told the status. The key is checked once more
// after the failed check's pause.
void RecheckWhileTheWorkerRuns() {
    const auto settings = Folder("recheck-running");
    Gate(settings, "GET /me", Status(401));
    holdAfter = 1; // the key row's check goes through, the typed key's check is held
    meLost = true; // Metamob unreachable for that first check
    RowShown();
    SubmitKey(OtherKey);
    Tick(); // one worker: the check, then the typed key
    Await([] { return holding.load(); }, 5000, "the typed key's check");
    Await([] { return StoredKeyStatus() == KeyStatus::Unverified; }, 5000, "the check's answer, the worker still running");
    CheckStoredKey(true);
    Check(StoredKeyStatus() == KeyStatus::Saved, "« Clé non vérifiée » clicked: being checked again");
    meLost = false;
    letGo = true;
    AwaitWorker("the typed key refused");
    Said({KeyRejected}, "The typed key refused, nothing stored");
    Check(StoredKeyStatus() == KeyStatus::Saved && Requested("GET /me") == 2, "The click is not undone when that worker ends");
    Tick(30000);
    AwaitWorker("the check asked by the click");
    Check(StoredKeyStatus() == KeyStatus::Verified && Requested("GET /me") == 3 && notices.empty(), "Checked once more: valid");
    Finish("a check asked again while its worker runs");
}

// « Oui » while a cycle runs, then a new key saved at once: one worker deletes
// the old key file then stores the new key, the file barely missing. The key
// row says none until that worker is done, never waiting to see the file
// missing, then follows the file: the new key, valid.
void ForgetThenNewKey() {
    auto settings = Folder("forget-new");
    Gate(settings, "GET " + StepPath(Slug, Tofu.step));
    EnqueueCapture(Captured(Tofu));
    Tick(2000);
    Await([] { return holding.load(); }, 5000, "the worker's read");
    ForgetStoredKey();
    SubmitKey(OtherKey);
    Check(StoredKeyStatus() == KeyStatus::None && KeyStored(), "Forgotten, a new key typed: none while the old file waits");
    letGo = true; // the key row is not read again before the old file is deleted and the new key stored
    Await([] { return Noticed(SavedUser) && !metamob::testing::WorkerRunning(); }, 10000, "the forget then the new key");
    Tick();
    Said({KeyForgotten, NoKey("Tofumanchou l'Empereur"), SavedUser}, "Forgotten, then the new key saved");
    Check(StoredKeyStatus() == KeyStatus::Verified && LoadKey(KeyPath(settings))->value() == OtherKey, "The new key at once, valid");
    Finish("a new key right after a forget");
    // The same, the key row read while the new key is checked: none, the old file deleted.
    settings = Folder("forget-new-seen");
    Gate(settings, "GET " + StepPath(Slug, Tofu.step));
    EnqueueCapture(Captured(Tofu));
    Tick(2000);
    Await([] { return holding.load(); }, 5000, "the worker's read");
    ForgetStoredKey();
    SubmitKey(OtherKey);
    letGo = true;
    const auto end = GetTickCount64() + 10000;
    while (metamob::testing::WorkerRunning()) { Check(GetTickCount64() < end, "the cycle's end"); Sleep(5); }
    holdAt = "GET /me"; holding = false; letGo = false; // the next worker's check of the new key is held
    Await([] { return holding.load(); }, 5000, "the new key's check");
    Check(StoredKeyStatus() == KeyStatus::None && !KeyStored(), "The old file deleted, the new key not stored yet: none");
    letGo = true;
    Await([] { return Noticed(SavedUser) && !metamob::testing::WorkerRunning(); }, 10000, "the new key");
    Tick();
    Said({KeyForgotten, NoKey("Tofumanchou l'Empereur"), SavedUser}, "Forgotten, then the new key saved, watched");
    Check(StoredKeyStatus() == KeyStatus::Verified && LoadKey(KeyPath(settings))->value() == OtherKey, "The new key once stored, valid");
    Finish("a new key right after a forget, watched");
}

// « Metamob dans le chat »: the user's quest is read at once when a chat label
// first asks for it; each monster then shows its quantity from that copy: green
// from 1, red at 0, « ? » before; nothing for a monster outside L’éternelle
// moisson, unknown to the game or not in the quest. Silent, reads only (never
// the sender lock), the capture sync on or off.
void ChatFirstLoad() {
    Begin(Folder("chat"), true);
    syncOn = false;
    Check(Rich(GameTofu) == "Metamob : ?" && Plain(GameTofu) == "Metamob : ?" && MetamobChatDetail(GameTofu) == NotRead,
        "Before the quest is read: « ? »");
    Check(Ocre(GameTofu, -1, OcreList::NotRead) && !MetamobOcreQuantity(GameStranger).usable &&
        !MetamobOcreQuantity(GameNameless).usable, "The maps: not read yet; nothing outside L’éternelle moisson");
    Check(Rich(GameStranger).empty() && MetamobChatDetail(GameStranger).empty() && Rich(GameNameless).empty() &&
        MetamobChatDetail(GameNameless).empty() && Rich(0).empty() && Rich(-GameTofu).empty(),
        "Nothing outside L’éternelle moisson, nor for a monster the game does not name");
    for (int i = 0; i < 5; ++i) Tick(60000);
    Check(Sent() == 0 && Rich(GameTofu) == "Metamob : ?", "Nothing read until a chat label asks");
    ExpectQuests({Quest(Slug, Character.c_str(), "ocre", 1)});
    ExpectList(Slug);
    Ask();
    Check(Sent() == 3 && notices.empty() && Logged("METAMOB_CATALOG monsters=337") &&
        LoggedCount("METAMOB_HTTP method=GET route=/quests/*/monsters status=200") == 2, "Read at once: the quests, both pages, silently");
    {
        std::lock_guard<std::mutex> guard(scriptLock);
        for (size_t i = 0; i < calls.size(); ++i) {
            Check(calls[i].key == FakeKey && !calls[i].held && calls[i].thread == GetCurrentThreadId(), "The stored key, never the sender lock");
            if (i) Check(calls[i].at - calls[i - 1].at >= 1100, "At least 1.1 s between two requests");
        }
    }
    Check(Rich(GameTofu) == Colored(2) && Plain(GameTofu) == "Metamob : 2" && MetamobChatDetail(GameTofu) == Counted(2), "2: green");
    Check(Rich(GameMob) == Colored(0) && Plain(GameMob) == "Metamob : 0" && MetamobChatDetail(GameMob) == Counted(0), "0: red");
    Check(Ocre(GameTofu, 2, OcreList::Read) && Ocre(GameMob, 0, OcreList::Read) && Ocre(GameAbrakroc, 0, OcreList::Read) &&
        !MetamobOcreQuantity(GameMissing).usable && !MetamobOcreQuantity(GameOtherKind).usable,
        "The maps: the quantities read; nothing for a monster not in the quest");
    Check(Rich(GameLarva) == Colored(1) && MetamobChatDetail(GameLarva) == Counted(1) && Rich(GameBouftou) == Colored(5) &&
        Rich(GameTiwa) == Colored(3) && Rich(GameAbrakroc) == Colored(0), "Every quest monster the game names, accents and apostrophes aside");
    Check(Rich(GameMissing).empty() && MetamobChatDetail(GameMissing).empty() && Rich(GameOtherKind).empty() && Rich(GamePlain).empty(),
        "Not in the quest, nor under another type: nothing");
    Check(Rich(GameStranger).empty() && Rich(GameNameless).empty() && lookedUp[GameStranger] == 0,
        "A monster outside L’éternelle moisson is never even looked up");
    // Matched once per list: the chat asks at every pump without reading the game again.
    const auto views = lookedUp;
    for (int i = 0; i < 100; ++i)
        for (const int monster : {GameTofu, GameMob, GameMissing, GameOtherKind}) { Rich(monster); MetamobChatDetail(monster); }
    Check(lookedUp == views, "Each monster matched once per list read");
    for (int i = 0; i < 9; ++i) Ask(60000);
    Check(Sent() == 3, "Read once in 10 min, however often asked");
    Finish("chat first load");
}

// Asked at every chat pump, the list is read again 10 min after the last
// reading, only while a label asked within the last minute; what was read stays.
void ChatRefresh() {
    Begin(Folder("chat-refresh"), true);
    ExpectQuests({Quest(Slug, Character.c_str(), "ocre", 1)});
    ExpectList(Slug);
    Ask();
    auto read = LastAt();
    for (int i = 0; i < 19; ++i) Ask(30000);
    AskAt(read + 599999);
    Check(Sent() == 3 && Plain(GameTofu) == "Metamob : 2" && Plain(GameMob) == "Metamob : 0", "Not read again within 10 min");
    auto reordered = CatalogWith({{Tofu.id, 7}, {Mob.id, 4}});
    std::reverse(reordered.begin(), reordered.end()); // another order: every monster matched again
    ExpectList(Slug, reordered);
    AskAt(read + 600000);
    Check(Sent() == 5 && Plain(GameTofu) == "Metamob : 7" && Plain(GameMob) == "Metamob : 4" && Plain(GameLarva) == "Metamob : 1" &&
        notices.empty(), "Read again after 10 min: both pages, the quests kept, every monster matched again");
    read = LastAt();
    TickAt(read + 600000);
    TickAt(read + 1800000);
    Check(Sent() == 5 && Plain(GameTofu) == "Metamob : 7", "No label asking: not read again, what was read stays");
    ExpectList(Slug, CatalogWith({{Tofu.id, 8}}));
    Ask();
    Check(Sent() == 7 && Plain(GameTofu) == "Metamob : 8", "Asked again: read at once");
    // A label that asked 59.999 s ago still counts, one that asked a minute ago no longer does.
    read = LastAt();
    AskAt(read + 540001);
    ExpectList(Slug, CatalogWith({{Tofu.id, 9}}));
    TickAt(read + 600000);
    Check(Sent() == 9 && Plain(GameTofu) == "Metamob : 9", "Asked within the last minute");
    read = LastAt();
    AskAt(read + 540000);
    TickAt(read + 600000);
    TickAt(read + 700000);
    Check(Sent() == 9, "Asked a minute ago: not read");
    Finish("chat refresh");
}

// Nothing at all without a usable key: none, unreadable, refused, or a forget
// under way. Never a notice from the chat part.
void ChatWithoutKey() {
    Begin(Folder("chat-nokey"), false);
    for (int i = 0; i < 3; ++i) Ask(60000);
    Check(Sent() == 0 && Rich(GameTofu).empty() && MetamobChatDetail(GameTofu).empty(), "No key: nothing shown, nothing read");
    Finish("chat without key");

    const auto unreadable = Folder("chat-unreadable");
    Begin(unreadable, false);
    {
        std::ofstream output(KeyPath(unreadable), std::ios::binary);
        output << "DEMK1\n" << Marker;
    }
    Check(Rich(GameTofu) == "Metamob : ?", "A key file not checked yet: « ? »");
    Ask();
    Check(Sent() == 0 && notices.empty() && StoredKeyStatus() == KeyStatus::Unreadable && Rich(GameTofu).empty() &&
        Logged("METAMOB_CATALOG_FAILED reason=key"), "Unreadable: nothing sent, shown nor said");
    for (int i = 0; i < 3; ++i) Ask(60000);
    Check(Sent() == 0 && LoggedCount("METAMOB_CATALOG_FAILED") == 1, "Not tried again");
    Finish("chat with an unreadable key");

    // Refused by Metamob (its JSON 401): silent, the key row says it, nothing more is read with it.
    Begin(Folder("chat-refused"), true);
    Expect("GET", "/quests", Status(401));
    Ask();
    Check(notices.empty() && StoredKeyStatus() == KeyStatus::Refused && Rich(GameTofu).empty() && !Logged("METAMOB_KEY_REFUSED") &&
        Logged("METAMOB_CATALOG_FAILED reason=refused"), "Refused: silent, nothing shown");
    for (int i = 0; i < 25; ++i) Ask(60000);
    Check(Sent() == 1, "Never read again with a refused key");
    // A capture still tells it at its first request, then retries it 10 min later.
    Expect("GET", "/quests", Status(401));
    EnqueueCapture(Captured(Tofu));
    Tick(2000);
    Said({KeyRefused}, "The capture tells the refusal");
    for (int i = 0; i < 19; ++i) Ask(30000);
    Check(Sent() == 2 && Rich(GameTofu).empty(), "Paused");
    ExpectFirst(Tofu, 2, 3);
    Ask(30000);
    Said({Added(Tofu, 2, 3)}, "Accepted again");
    ExpectList(Slug, CatalogWith({{Tofu.id, 3}}));
    Ask();
    Check(StoredKeyStatus() == KeyStatus::Verified && Rich(GameTofu) == Colored(3) && Sent() == 9, "Accepted again: read for the chat");
    Finish("chat with a refused key");

    Begin(Folder("chat-forget"), true);
    ForgetStoredKey();
    Check(Rich(GameTofu).empty() && MetamobChatDetail(GameTofu).empty(), "Forgotten: nothing at once");
    Ask();
    Said({KeyForgotten}, "Forgotten");
    Ask(60000);
    Check(Sent() == 0 && Rich(GameTofu).empty(), "Nothing read once forgotten");
    Finish("chat while forgetting");
}

// A capture's refusal pauses the capture sync only. Accepted again 10 min later
// by a retry that writes nothing (its capture not in the quest), the key is valid
// again and the chat part reads with it. Saved again by another client (the sync
// turned off meanwhile), the same key is tried once by the chat part: refused
// again, silently, and never read with; saved again and accepted, read.
void ChatAfterRefusal() {
    Begin(Folder("chat-after-refusal"), true);
    Capture krok;
    krok.characterName = Character; krok.monsterId = 3180; krok.name = "Krokette la Croustillante"; krok.type = TypeArchimonster;
    Expect("GET", "/quests", Status(401));
    EnqueueCapture(krok);
    Tick(2000);
    Said({KeyRefused}, "A capture refused");
    for (int i = 0; i < 19; ++i) Ask(30000);
    Check(Sent() == 1 && Rich(GameTofu).empty() && StoredKeyStatus() == KeyStatus::Refused, "Paused: nothing read, nothing shown");
    ExpectQuests({Quest(Slug, Character.c_str(), "ocre", 1)});
    ExpectList(Slug);
    Tick(30000);
    Said({Unmatched("Krokette la Croustillante")}, "Accepted again 10 min later, its capture not in the quest: nothing written");
    Check(StoredKeyStatus() == KeyStatus::Verified && Rich(GameTofu) == "Metamob : ?", "Accepted again: valid, the list not read yet");
    ExpectList(Slug);
    Ask();
    Check(Rich(GameTofu) == Colored(2) && MetamobChatDetail(GameTofu) == Counted(2) && Sent() == 6 && notices.empty() &&
        Logged("METAMOB_CATALOG monsters=337") && !Logged("METAMOB_CATALOG_FAILED reason=key"),
        "Valid again without a confirmed write: the chat part reads with that key, the quests kept");
    Finish("chat after a refused key accepted again");

    const auto settings = Folder("chat-after-refusal-saved");
    Begin(settings, true);
    Expect("GET", "/quests", Status(401));
    EnqueueCapture(Captured(Tofu));
    Tick(2000);
    Said({KeyRefused}, "A capture refused");
    syncOn = false;
    Tick();
    Said({Dropped(1)}, "The capture sync turned off: its capture dropped");
    AnotherClientSaves(settings, FakeKey);
    Check(StoredKeyStatus() == KeyStatus::Saved, "The same key saved again by another client: not checked yet");
    Expect("GET", "/quests", Status(401));
    Ask(30000); // once the refusal's pause is over
    Check(notices.empty() && StoredKeyStatus() == KeyStatus::Refused && Rich(GameTofu).empty() && LoggedCount("METAMOB_KEY_REFUSED") == 1 &&
        Logged("METAMOB_CATALOG_FAILED reason=refused"), "Tried once by the chat part, refused again: silent");
    for (int i = 0; i < 5; ++i) Ask(60000);
    Check(Sent() == 2, "Never read again with it");
    AnotherClientSaves(settings, FakeKey);
    Check(StoredKeyStatus() == KeyStatus::Saved, "Saved again: not checked yet");
    ExpectQuests({Quest(Slug, Character.c_str(), "ocre", 1)});
    ExpectList(Slug);
    Ask();
    Check(StoredKeyStatus() == KeyStatus::Verified && Rich(GameTofu) == Colored(2) && Sent() == 5 && notices.empty() &&
        !Logged("METAMOB_CATALOG_FAILED reason=key"), "Accepted: read with it");
    Finish("chat after a refused key saved again");
}

// The chat part's failures are silent and spaced (30 s, then twice longer),
// never hold the captures, and show « ? » until a reading succeeds. Retry-After
// holds both; the captures go first. A quest gone is looked for once more.
void ChatFailures() {
    Begin(Folder("chat-fail"), true);
    Expect("GET", "/quests", Html(503));
    Ask();
    auto at = LastAt();
    Check(notices.empty() && Rich(GameTofu) == "Metamob : ?" && MetamobChatDetail(GameTofu) == NotRead &&
        Logged("METAMOB_CATALOG_FAILED reason=request") && Logged("METAMOB_HTTP method=GET route=/quests status=503"),
        "Unreachable: « ? », silently");
    ExpectFirst(Tofu, 2, 3);
    EnqueueCapture(Captured(Tofu));
    Tick(2000);
    Said({Added(Tofu, 2, 3)}, "A capture right after a failed reading goes out at once");
    Check(Rich(GameTofu) == "Metamob : ?", "A write is no reading of the list");
    AskAt(at + 29999);
    Check(Sent() == 6, "Not tried again before 30 s");
    Expect("GET", ListPath(Slug, 0), Lost(12029, false));
    AskAt(at + 30000);
    at = LastAt();
    AskAt(at + 59999);
    Check(Sent() == 7, "Then 60 s");
    Expect("GET", ListPath(Slug, 0), Html(200));
    AskAt(at + 60000);
    at = LastAt();
    AskAt(at + 119999);
    Check(Sent() == 8 && notices.empty(), "Then 120 s; never told, even at the third failure in a row");
    ExpectList(Slug, CatalogWith({{Tofu.id, 3}}));
    AskAt(at + 120000);
    Check(Rich(GameTofu) == Colored(3) && Sent() == 10 && notices.empty(), "Read once Metamob answers (the quests kept)");
    // Rate limited: neither the chat part nor a capture before Retry-After; then the capture first.
    auto read = LastAt();
    Expect("GET", ListPath(Slug, 0), Status(429, "120"));
    AskAt(read + 600000);
    at = LastAt();
    Check(Rich(GameTofu) == "Metamob : ?" && Logged("METAMOB_RATE_LIMIT seconds=120") && notices.empty(), "Rate limited: « ? »");
    AskAt(at + 30000);
    Check(Sent() == 11, "The chat part waits for Retry-After");
    EnqueueCapture(Captured(Mob));
    AskAt(at + 119999);
    Check(Sent() == 11, "And so does a capture");
    ExpectStep(Slug, Mob, 0);
    ExpectWrite(Slug, Mob, 1, Written(Mob, 1));
    AskAt(at + 120000);
    Said({Added(Mob, 0, 1)}, "The capture first");
    Check(Sent() == 13 && Rich(GameMob) == "Metamob : ?", "Alone in its worker; « ? » until the list is read");
    ExpectList(Slug, CatalogWith({{Tofu.id, 3}, {Mob.id, 1}}));
    Ask();
    Check(Rich(GameMob) == Colored(1) && Rich(GameTofu) == Colored(3) && Sent() == 15, "Then the list");
    // Its quest gone (404): looked for again once, then read there.
    read = LastAt();
    Expect("GET", ListPath(Slug, 0), Status(404));
    ExpectQuests({Quest(OtherSlug, Character.c_str(), "ocre", 1)});
    ExpectList(OtherSlug, CatalogWith({{Tofu.id, 12}}));
    AskAt(read + 600000);
    Check(Rich(GameTofu) == Colored(12) && notices.empty() && Sent() == 19, "Rediscovered after a 404");
    read = LastAt();
    Expect("GET", ListPath(OtherSlug, 0), Status(404));
    ExpectQuests({Quest(OtherSlug, Character.c_str(), "ocre", 1)});
    Expect("GET", ListPath(OtherSlug, 0), Status(404));
    AskAt(read + 600000);
    Check(Rich(GameTofu) == "Metamob : ?" && Logged("METAMOB_QUEST_GONE") && notices.empty() && Sent() == 22, "Looked for once per reading");
    Finish("chat failures");
}

// No Unity Ocre quest, or several naming none of this character: « ? »,
// silently, its tooltip saying what to fix on metamob.fr, and the quests asked
// again 10 min later only. A passing failure then shows the generic text again,
// and so does another character until its own quest is read.
void ChatQuestChoice() {
    Begin(Folder("chat-quest"), true);
    ExpectQuests({Quest(Slug, Character.c_str(), "ocre", 2), Quest(OtherSlug, nullptr, "dokille", 1)});
    Ask();
    auto at = LastAt();
    Check(notices.empty() && Rich(GameTofu) == "Metamob : ?" && Logged("METAMOB_CATALOG_FAILED reason=quest"), "No Unity Ocre quest: silent");
    Check(Plain(GameTofu) == "Metamob : ?" && MetamobChatDetail(GameTofu) == NoQuestDetail && MetamobChatDetail(GameMob) == NoQuestDetail,
        "No Unity Ocre quest: its tooltip says to create it on metamob.fr");
    Check(Ocre(GameTofu, -1, OcreList::NoQuest), "The maps: no Unity Ocre quest");
    for (int i = 0; i < 19; ++i) Ask(30000);
    AskAt(at + 599999);
    Check(Sent() == 1 && MetamobChatDetail(GameTofu) == NoQuestDetail, "Not asked again within 10 min");
    ExpectQuests({Quest(Slug, "Bob", "ocre", 1), Quest(OtherSlug, nullptr, "ocre", 1)});
    AskAt(at + 600000);
    at = LastAt();
    Check(Sent() == 2 && notices.empty() && Rich(GameTofu) == "Metamob : ?", "Several quests naming none of this character: silent");
    Check(Plain(GameTofu) == "Metamob : ?" && MetamobChatDetail(GameTofu) == SeveralDetail,
        "Several quests naming none of this character: its tooltip says to name it in the right quest");
    Check(Ocre(GameTofu, -1, OcreList::SeveralQuests), "The maps: several quests naming none of this character");
    ExpectQuests({Quest(Slug, "Bob", "ocre", 1), Quest(OtherSlug, (Character + ", Bob").c_str(), "ocre", 1)});
    ExpectList(OtherSlug, CatalogWith({{Tofu.id, 6}}));
    AskAt(at + 599999);
    Check(Sent() == 2, "Again 10 min later only");
    AskAt(at + 600000);
    Check(Rich(GameTofu) == Colored(6) && MetamobChatDetail(GameTofu) == Counted(6) && notices.empty(), "The quest naming this character");
    at = LastAt();
    Expect("GET", ListPath(OtherSlug, 0), Html(503));
    AskAt(at + 600000);
    Check(Rich(GameTofu) == "Metamob : ?" && MetamobChatDetail(GameTofu) == NotRead && Sent() == 6, "A passing failure once read: the generic text");
    Check(Ocre(GameTofu, -1, OcreList::Failed), "The maps: the last reading failed");
    Finish("chat quest choice");

    // A passing failure right after a quest problem: the generic text again.
    Begin(Folder("chat-quest-passing"), true);
    ExpectQuests({Quest(Slug, "Bob", "ocre", 1), Quest(OtherSlug, nullptr, "ocre", 1)});
    Ask();
    at = LastAt();
    Check(MetamobChatDetail(GameTofu) == SeveralDetail, "Several quests naming none of this character");
    Expect("GET", "/quests", Html(503));
    AskAt(at + 600000);
    at = LastAt();
    Check(Rich(GameTofu) == "Metamob : ?" && MetamobChatDetail(GameTofu) == NotRead && notices.empty() &&
        Logged("METAMOB_CATALOG_FAILED reason=request"), "A passing failure after a quest problem: the generic text again");
    ExpectQuests({Quest(Slug, "Bob", "ocre", 1), Quest(OtherSlug, nullptr, "ocre", 1)});
    AskAt(at + 600000);
    Check(MetamobChatDetail(GameTofu) == SeveralDetail && Sent() == 3, "The quest problem read again");
    // Another character: nothing of the previous one's problem; its own quest read.
    playing = "Bobinou";
    Tick(60000); // no label asked within the last minute: nothing read
    Check(Rich(GameTofu) == "Metamob : ?" && MetamobChatDetail(GameTofu) == NotRead && Sent() == 3,
        "Another character: the generic text until its quest is read");
    ExpectQuests({Quest(Slug, "Bob", "ocre", 1), Quest(OtherSlug, "Bobinou", "ocre", 1)});
    ExpectList(OtherSlug, CatalogWith({{Tofu.id, 4}}));
    Ask();
    Check(Rich(GameTofu) == Colored(4) && MetamobChatDetail(GameTofu) == Counted(4) && notices.empty(), "Its own quest");
    Finish("chat quest problem, then a passing failure and another character");
}

// A page that does not read as expected is refused (another offset, another
// quest, a page missing before the total, missing fields): never a shorter list.
void ChatPages() {
    Begin(Folder("chat-pages"), true);
    const auto rows = Catalog();
    const auto bad = [] { return LoggedCount("METAMOB_BAD_ANSWER route=/quests/*/monsters"); };
    ExpectQuests({Quest(Slug, Character.c_str(), "ocre", 1)});
    Expect("GET", ListPath(Slug, 0), Json(Page(Slug, {rows.begin(), rows.begin() + 200}, rows.size(), 0)));
    Expect("GET", ListPath(Slug, 200), Json(Page(Slug, {rows.begin(), rows.begin() + 200}, rows.size(), 0)));
    Ask();
    auto at = LastAt();
    Check(Rich(GameTofu) == "Metamob : ?" && bad() == 1 && notices.empty(), "A page of another offset");
    Expect("GET", ListPath(Slug, 0), Json(Page(OtherSlug, {rows.begin(), rows.begin() + 200}, rows.size(), 0)));
    AskAt(at + 30000);
    at = LastAt();
    Check(Rich(GameTofu) == "Metamob : ?" && bad() == 2, "Another quest's page");
    Expect("GET", ListPath(Slug, 0), Json(Page(Slug, {rows.begin(), rows.begin() + 200}, rows.size(), 0)));
    Expect("GET", ListPath(Slug, 200), Json(Page(Slug, {}, rows.size(), 200)));
    AskAt(at + 60000);
    at = LastAt();
    Check(Rich(GameTofu) == "Metamob : ?" && bad() == 3, "A page missing before the total");
    Expect("GET", ListPath(Slug, 0), Json("{\"data\":{\"slug\":\"" + Slug + "\",\"bio\":\"" + Marker + "\"}}"));
    AskAt(at + 120000);
    at = LastAt();
    Check(Rich(GameTofu) == "Metamob : ?" && bad() == 4 && notices.empty() && Sent() == 7, "Missing fields");
    // Overlapping pages (a total grown between them): each monster once.
    Expect("GET", ListPath(Slug, 0), Json(Page(Slug, {rows.begin(), rows.begin() + 200}, rows.size(), 0)));
    Expect("GET", ListPath(Slug, 200), Json(Page(Slug, {rows.begin() + 150, rows.end()}, rows.size() + 50, 200)));
    AskAt(at + 240000);
    Check(Rich(GameTofu) == Colored(2) && Rich(GameLarva) == Colored(1) && Logged("METAMOB_CATALOG monsters=337"),
        "Read once the pages read well, each monster once");
    Finish("chat pages");
}

// The captures go first: nothing is read while a capture waits; a due cycle
// runs alone in its worker, the list comes at the next pump.
void ChatCapturesFirst() {
    Begin(Folder("chat-captures"), true);
    EnqueueCapture(Captured(Tofu));
    Ask();
    Ask(1999);
    Check(Sent() == 0, "Not read while a capture waits for its 2 s");
    ExpectFirst(Tofu, 2, 3);
    Ask(1);
    Said({Added(Tofu, 2, 3)}, "The capture first");
    Check(Sent() == 5 && Rich(GameTofu) == "Metamob : ?", "Alone in its worker");
    ExpectList(Slug, CatalogWith({{Tofu.id, 3}}));
    Ask();
    Check(Sent() == 7 && Rich(GameTofu) == Colored(3), "Then the list, the quests kept");
    Finish("chat after the captures");
}

// The sync's own confirmed writes show at once, without reading the list again;
// a value read back from someone else is not one of them.
void ChatFollowsWrites() {
    Begin(Folder("chat-writes"), true);
    ExpectQuests({Quest(Slug, Character.c_str(), "ocre", 1)});
    ExpectList(Slug);
    Ask();
    Check(Rich(GameTofu) == Colored(2) && Rich(GameMob) == Colored(0), "Read");
    ExpectStep(Slug, Tofu, 2);
    ExpectWrite(Slug, Tofu, 3, Written(Tofu, 3));
    EnqueueCapture(Captured(Tofu));
    Ask(2000);
    Said({Added(Tofu, 2, 3)}, "Written");
    Check(Rich(GameTofu) == Colored(3) && MetamobChatDetail(GameTofu) == Counted(3) && Sent() == 5, "Confirmed by its answer: shown at once");
    ExpectStep(Slug, Mob, 0);
    ExpectWrite(Slug, Mob, 1, Lost(12002, true));
    ExpectStep(Slug, Mob, 1);
    EnqueueCapture(Captured(Mob));
    Ask(2000);
    Said({Added(Mob, 0, 1)}, "Confirmed by a read back");
    Check(Rich(GameMob) == Colored(1), "Shown once confirmed");
    ExpectStep(Slug, Abrakroc, 0);
    ExpectWrite(Slug, Abrakroc, 1, Written(Abrakroc, 4));
    EnqueueCapture(Captured(Abrakroc));
    Ask(2000);
    Said({Changed(Abrakroc, 0, 4)}, "Another value in the answer");
    Check(Rich(GameAbrakroc) == Colored(0) && Sent() == 10, "Not the sync's write: left as read until the next reading");
    Finish("chat follows the sync's writes");
}

// Forgotten: nothing at once; a new key, or another client's: « ? » until the
// list is read with it.
void ChatKeyChanges() {
    const auto settings = Folder("chat-keys");
    Begin(settings, true);
    ExpectQuests({Quest(Slug, Character.c_str(), "ocre", 1)});
    ExpectList(Slug);
    Ask();
    Check(Rich(GameTofu) == Colored(2), "Read");
    ForgetStoredKey();
    Check(Rich(GameTofu).empty() && MetamobChatDetail(GameTofu).empty(), "Forgotten: nothing at once");
    Ask();
    Said({KeyForgotten}, "Forgotten");
    Ask(60000);
    Check(Sent() == 3 && Rich(GameTofu).empty(), "Nothing read nor shown without a key");
    keyNow = OtherKey;
    Expect("GET", "/me", Json(MeBody(User)));
    SubmitKey(OtherKey);
    Ask();
    Said({SavedUser}, "A new key");
    Check(Rich(GameTofu) == "Metamob : ?" && Sent() == 4, "A new key: « ? », its list never read by the worker saving it");
    ExpectQuests({Quest(Slug, Character.c_str(), "ocre", 1)});
    ExpectList(Slug, CatalogWith({{Tofu.id, 9}}));
    Ask();
    Check(Rich(GameTofu) == Colored(9), "Read with the new key");
    {
        std::lock_guard<std::mutex> guard(scriptLock);
        Check(calls.back().key == OtherKey, "With the new key");
    }
    AnotherClientSaves(settings, FakeKey);
    keyNow = FakeKey;
    fakeNow += 1000;
    Check(Rich(GameTofu) == "Metamob : ?", "Another client's key, seen within a second: « ? »");
    ExpectList(Slug, CatalogWith({{Tofu.id, 4}}));
    Ask();
    Check(Rich(GameTofu) == Colored(4), "Read with that key");
    {
        std::lock_guard<std::mutex> guard(scriptLock);
        Check(calls.back().key == FakeKey, "With the other client's key");
    }
    // A key typed when the list is due: saved alone in its worker, the list read with it at the next pump.
    const auto read = LastAt();
    keyNow = OtherKey;
    Expect("GET", "/me", Json(MeBody(User)));
    SubmitKey(OtherKey);
    AskAt(read + 600000);
    Said({SavedUser}, "A key typed when the list is due");
    Check(Rich(GameTofu) == "Metamob : ?" && Sent() == 10, "Saved alone first: « ? » until its list is read");
    ExpectQuests({Quest(Slug, Character.c_str(), "ocre", 1)});
    ExpectList(Slug, CatalogWith({{Tofu.id, 5}}));
    Ask();
    Check(Rich(GameTofu) == Colored(5) && Sent() == 13, "Then read with it");
    Finish("chat key changes");
}

// Another character on this client: its own quest (several Ocre quests), read
// at once; a character unknown for a moment keeps what was read.
void ChatCharacters() {
    Begin(Folder("chat-characters"), true);
    ExpectQuests({Quest(Slug, Character.c_str(), "ocre", 1), Quest(OtherSlug, "Bobinou", "ocre", 1)});
    ExpectList(Slug);
    Ask();
    Check(Rich(GameTofu) == Colored(2), "This character's quest");
    playing = "Bobinou";
    ExpectList(OtherSlug, CatalogWith({{Tofu.id, 11}}));
    Ask();
    Check(Rich(GameTofu) == Colored(11) && Sent() == 5, "Another character: its quest at once (the quests kept)");
    // A capture of the first character goes to its own quest: the quest shown is unchanged.
    ExpectStep(Slug, Tofu, 2);
    ExpectWrite(Slug, Tofu, 3, Written(Tofu, 3));
    EnqueueCapture(Captured(Tofu));
    Tick(2000);
    Said({Added(Tofu, 2, 3)}, "A capture of the first character");
    Check(Rich(GameTofu) == Colored(11) && Sent() == 7, "Written to another quest than the one shown: not counted there");
    playing.clear();
    Ask(1000);
    Check(Rich(GameTofu) == Colored(11) && Sent() == 7, "A character unknown for a moment: what was read stays");
    Finish("chat characters");
}

// The game names monsters in the client's language: Metamob's English and
// Spanish names match as its French ones do, the type deciding among equal names.
void ChatLanguages() {
    Begin(Folder("chat-languages"), true);
    auto rows = Catalog();
    for (auto& row : rows) {
        if (row.id == Tofu.id) { row.en = "Tofumanchou the Emperor"; row.es = "Tofumanchú el Emperador"; }
        if (row.id == Mob.id) { row.en = "Mob the Sponge"; row.es = "Mob la Esponja"; }
        if (row.id == Bouftou.id) { row.en = "Royal Gobball"; row.es = "Jalató Real"; }
    }
    rows.back() = Row{900, "Bouftou", TypeMonster, 1, 4, "Gobball", "Jalató"};
    bestiary[GameTofu] = {"Tofumanchou the Emperor", TypeArchimonster};
    bestiary[GameMob] = {"Mob la Esponja", TypeBoss};
    bestiary[GameBouftou] = {"ROYAL GOBBALL", TypeBoss};
    bestiary[GamePlain] = {"Jalató", TypeMonster};
    bestiary[GameOtherKind] = {"Tofumanchú el Emperador", TypeBoss};
    ExpectQuests({Quest(Slug, Character.c_str(), "ocre", 1)});
    ExpectList(Slug, rows);
    Ask();
    Check(Rich(GameTofu) == Colored(2), "An English client's name");
    Check(Rich(GameMob) == Colored(0), "A Spanish client's name");
    Check(Rich(GameBouftou) == Colored(5), "Case aside");
    Check(Rich(GamePlain) == Colored(4), "A monster neither boss nor archimonster, by its type");
    Check(Rich(GameOtherKind).empty(), "The same name under another type: not that monster");
    Check(Rich(GameLarva) == Colored(1), "French names still match");
    Finish("chat languages");
}

// The real wrapper: the list read on a worker thread and kept on the game
// thread; a capture made meanwhile goes out after it; « Oui » meanwhile:
// nothing shown at once, nothing more read.
void ChatThreaded() {
    const auto list = "GET " + ListPath(Slug, 0);
    Gate(Folder("chat-thread"), list);
    Ask();
    Await([] { return holding.load(); }, 5000, "the chat part's list read");
    Check(Rich(GameTofu) == "Metamob : ?" && metamob::testing::WorkerRunning(), "« ? » while the worker reads");
    EnqueueCapture(Captured(Tofu));
    letGo = true;
    AwaitWorker("the list read");
    Check(Rich(GameTofu) == Colored(2) && notices.empty(), "Read on the worker, kept on the game thread");
    Tick(2000);
    Await([] { return Noticed(Added(Tofu, 2, 3)) && !metamob::testing::WorkerRunning(); }, 10000, "the capture made meanwhile");
    Tick();
    Said({Added(Tofu, 2, 3)}, "The capture made while the list was read");
    Check(Rich(GameTofu) == Colored(3) && Requested(list) == 1 && Requested("GET /quests") == 3, "Its write shown at once, the list read once");
    Finish("chat on the worker");

    // The capture sync turned off while the quests are read: reads only, the list is read all the same.
    Gate(Folder("chat-thread-off"), "GET /quests");
    Ask();
    Await([] { return holding.load(); }, 5000, "the chat part's quests read");
    syncOn = false;
    Tick();
    letGo = true;
    AwaitWorker("the list read");
    Check(Rich(GameTofu) == Colored(2) && Requested(list) == 1 && notices.empty(), "The capture sync turned off meanwhile: read all the same");
    Finish("chat read while the sync is turned off");

    Gate(Folder("chat-thread-forget"), list);
    Ask();
    Await([] { return holding.load(); }, 5000, "the chat part's list read");
    ForgetStoredKey();
    Check(Rich(GameTofu).empty() && MetamobChatDetail(GameTofu).empty(), "Forgotten while read: nothing at once");
    letGo = true;
    Await([] { return Noticed(KeyForgotten) && !metamob::testing::WorkerRunning(); }, 10000, "the forget");
    Tick();
    Said({KeyForgotten}, "Forgotten");
    Ask(60000);
    Check(Rich(GameTofu).empty() && Requested() == 2 && !KeyStored(), "Nothing more read, nothing shown");
    Finish("chat forgotten while read");

    // Another client saves a key while the list is read: that reading is dropped,
    // the list read again with the new key.
    const auto replaced = Folder("chat-thread-replaced");
    Gate(replaced, list);
    Ask();
    Await([] { return holding.load(); }, 5000, "the chat part's list read");
    AnotherClientSaves(replaced, OtherKey);
    letGo = true;
    Await([&] { return Requested(list) == 2 && !metamob::testing::WorkerRunning(); }, 10000, "the list read again with the new key");
    Tick();
    Check(Rich(GameTofu) == Colored(2) && Requested("GET /quests") == 3 && notices.empty() && StoredKeyStatus() == KeyStatus::Verified,
        "A reading made with a replaced key is dropped, then made with the new one");
    Finish("chat key replaced while read");
}

// The key the chat part's reading loads is wiped before its heap block is
// released (the transport answers through intercept, which never copies it).
void ChatWipedKey() {
    Begin(Folder("chat-wiped"), true);
    intercept = [](const Request& request, const std::atomic_bool&) {
        const auto rows = Catalog();
        if (request.method == "GET" && request.path == "/quests") return Json("{\"data\":[" + Quest(Slug, Character.c_str(), "ocre", 1) + "]}");
        if (request.method == "GET" && request.path == ListPath(Slug, 0)) return Json(Page(Slug, {rows.begin(), rows.begin() + 200}, rows.size(), 0));
        if (request.method == "GET" && request.path == ListPath(Slug, 200)) return Json(Page(Slug, {rows.begin() + 200, rows.end()}, rows.size(), 200));
        return Response{};
    };
    heapprobe::Arm();
    Ask(); // the key loaded from its file, sent, released
    Check(heapprobe::Disarm() == 0, "A heap block was released with a plaintext copy of the key");
    Check(Rich(GameTofu) == Colored(2) && notices.empty(), "Read while the heap is watched");
    Finish("chat wiped key");
}

// The real wrapper: a detached worker and the real clock, end to end.
// « Ouvertures Kralamoure dans le calendrier »: the openings of the server of
// the character's quest (Quest() names server 11, Draconiros), asked from a
// week before this PC's date.
int64_t KraloveFromDay() {
    const auto local = metamob::LocalTime(FakeEpochValue);
    return (local ? metamob::DayNumber(local->year, local->month, local->day) : FakeEpochValue / 86400) - 7;
}
std::string KralovePath(int64_t server = 11) {
    const auto date = metamob::CivilDay(KraloveFromDay());
    char text[16]{};
    std::snprintf(text, sizeof(text), "%04d-%02d-%02d", date.year, date.month, date.day);
    return "/kralove?server=" + std::to_string(server) + "&from=" + text;
}
// This character's Unity Ocre quest on another server, or on one without an id.
std::string ServerQuest(const std::string& slug, const char* character, int64_t server, const std::string& name) {
    auto quest = Quest(slug, character, "ocre", 1);
    const std::string from = "\"server\":{\"id\":11,\"name\":\"Draconiros\",";
    const auto at = quest.find(from);
    Check(at != std::string::npos, "Test fixture: quest server");
    return quest.replace(at, from.size(), "\"server\":{" + (server ? "\"id\":" + std::to_string(server) + "," : std::string()) +
        "\"name\":\"" + name + "\",");
}
// An opening as the API documents it, the private marker in its description and creator.
std::string Opening(int64_t id, const std::string& at, int64_t server, int characters) {
    return "{\"id\":" + std::to_string(id) + ",\"event_datetime\":\"" + at + "\",\"description\":\"Rendez-vous " + Marker +
        "\",\"creator\":\"" + Marker + "\",\"participants_count\":3,\"character_count\":" + std::to_string(characters) +
        ",\"messages_count\":1,\"server\":{\"id\":" + std::to_string(server) + ",\"name\":\"" + (server == 11 ? "Draconiros" : "Ombre") +
        "\",\"community\":\"fr\",\"game_version\":{\"id\":1,\"name\":\"Dofus\"}}}";
}
std::string Openings(const std::vector<std::string>& items) {
    std::string body;
    for (const auto& item : items) body += (body.empty() ? "" : ",") + item;
    return "{\"data\":[" + body + "]}";
}
void ExpectKralove(Response response, int64_t server = 11) { Expect("GET", KralovePath(server), std::move(response)); }
int64_t Utc(const char* text) { return *metamob::UtcSeconds(text); }
// A pump while the calendar row is shown: it asks for the openings, then the sync ticks.
void Want(uint64_t advance = 0) { fakeNow += advance; WantKralove(); TickSync(syncOn); }
void WantAt(uint64_t at) { if (fakeNow.load() < at) fakeNow = at; WantKralove(); TickSync(syncOn); }

// Read at once when the calendar asks, silently, then at most every 10 min while
// it asked within the last minute (the quests read again then: the server as it is).
void KraloveFirstLoad() {
    Begin(Folder("kralove"), true);
    syncOn = false;
    auto view = MetamobKralove();
    Check(view.usable && view.status == KraloveStatus::Loading && !view.read && view.openings.empty() && view.server.empty(),
        "Before any reading: loading");
    for (int i = 0; i < 5; ++i) Tick(60000);
    Check(Sent() == 0 && MetamobKralove().version == view.version, "Nothing read until the calendar asks");
    if (const auto local = metamob::LocalTime(FakeEpochValue); local && local->month == 10 && local->day == 22)
        Check(KralovePath() == "/kralove?server=11&from=2026-10-15", "From a week before this PC's date");
    ExpectQuests({Quest(Slug, Character.c_str(), "ocre", 1)});
    ExpectKralove(Json(Openings({Opening(902, "2026-10-24T19:00:00Z", 11, 12), Opening(901, "2026-10-23T18:00:00.000000Z", 11, 5),
        Opening(903, "2026-10-24T20:00:00Z", 12, 7)})));
    Want();
    Check(Sent() == 2 && notices.empty() && Logged("METAMOB_KRALOVE openings=2") && Logged("METAMOB_KRALOVE_OTHER_SERVER openings=1") &&
        Logged("METAMOB_HTTP method=GET route=/kralove status=200"), "Read at once, silently: the quests, then the openings of the quest's server");
    {
        std::lock_guard<std::mutex> guard(scriptLock);
        for (size_t i = 0; i < calls.size(); ++i) {
            Check(calls[i].key == FakeKey && !calls[i].held && calls[i].thread == GetCurrentThreadId(), "The stored key, never the sender lock");
            if (i) Check(calls[i].at - calls[i - 1].at >= 1100, "At least 1.1 s between two requests");
        }
    }
    view = MetamobKralove();
    Check(view.usable && view.status == KraloveStatus::Ready && view.read && view.server == "Draconiros" && view.fromDay == KraloveFromDay() &&
        view.openings.size() == 2 && view.openings[0].id == 901 && view.openings[1].id == 902 &&
        view.openings[0].at == Utc("2026-10-23T18:00:00Z") && view.openings[1].characters == 12 && view.openings[1].creator == Marker &&
        view.openings[1].description == "Rendez-vous " + Marker && view.openings[1].serverId == 11, "Server 11's openings by time, server 12's left out");
    const auto version = view.version;
    auto read = LastAt();
    for (int i = 0; i < 19; ++i) Want(30000);
    WantAt(read + 599999);
    Check(Sent() == 2 && MetamobKralove().version == version, "Not read again within 10 min");
    ExpectQuests({Quest(Slug, Character.c_str(), "ocre", 1)});
    ExpectKralove(Json(Openings({Opening(904, "2026-10-25T19:00:00Z", 11, 3)})));
    WantAt(read + 600000);
    view = MetamobKralove();
    Check(Sent() == 4 && view.openings.size() == 1 && view.openings[0].id == 904 && view.version != version,
        "Read again after 10 min, with the quests (the server as it is now)");
    // The calendar closed: not read again, what was read stays; opened again: read at once.
    read = LastAt();
    TickAt(read + 600000);
    TickAt(read + 1800000);
    Check(Sent() == 4 && MetamobKralove().openings[0].id == 904, "Closed: not read again, what was read stays");
    ExpectQuests({Quest(Slug, Character.c_str(), "ocre", 1)});
    ExpectKralove(Json(Openings({})));
    Want();
    view = MetamobKralove();
    Check(Sent() == 6 && view.read && view.openings.empty() && view.status == KraloveStatus::Ready, "Opened again: read at once; none planned");
    read = LastAt();
    WantAt(read + 599001);
    ExpectQuests({Quest(Slug, Character.c_str(), "ocre", 1)});
    ExpectKralove(Json(Openings({})));
    TickAt(read + 600000);
    Check(Sent() == 8, "Asked within the last second: read");
    read = LastAt();
    WantAt(read + 599000);
    TickAt(read + 600000);
    TickAt(read + 700000);
    Check(Sent() == 8, "Asked a second ago: the calendar is closed, not read");
    // A failure's retry never comes after the calendar closed.
    ExpectQuests({Quest(Slug, Character.c_str(), "ocre", 1)});
    ExpectKralove(Html(503));
    WantAt(read + 700000);
    read = LastAt();
    Check(Sent() == 10 && MetamobKralove().status == KraloveStatus::Unreachable, "A failed reading");
    TickAt(read + 30000);
    TickAt(read + 3600000);
    Check(Sent() == 10, "Closed: its retry is not sent");
    Finish("kralove first load");
}

// Nothing at all without a usable key: none, unreadable, refused, or a forget under way.
void KraloveWithoutKey() {
    Begin(Folder("kralove-nokey"), false);
    for (int i = 0; i < 3; ++i) Want(60000);
    Check(Sent() == 0 && !MetamobKralove().usable && MetamobKralove().openings.empty(), "No key: nothing read, nothing usable");
    Finish("kralove without key");

    const auto unreadable = Folder("kralove-unreadable");
    Begin(unreadable, false);
    {
        std::ofstream output(KeyPath(unreadable), std::ios::binary);
        output << "DEMK1\n" << Marker;
    }
    Check(MetamobKralove().usable, "A key file not checked yet: usable");
    Want();
    Check(Sent() == 0 && notices.empty() && StoredKeyStatus() == KeyStatus::Unreadable && !MetamobKralove().usable &&
        Logged("METAMOB_KRALOVE_FAILED reason=key"), "Unreadable: nothing sent nor said, no longer usable");
    for (int i = 0; i < 3; ++i) Want(60000);
    Check(Sent() == 0 && LoggedCount("METAMOB_KRALOVE_FAILED") == 1, "Not tried again");
    Finish("kralove with an unreadable key");

    // Refused by Metamob (its JSON 401) on the openings only, the quests accepted: the
    // calendar's own refusal, read again 10 min later; the key stays usable elsewhere.
    Begin(Folder("kralove-refused"), true);
    ExpectQuests({Quest(Slug, Character.c_str(), "ocre", 1)});
    ExpectKralove(Status(401));
    Want();
    Check(notices.empty() && StoredKeyStatus() == KeyStatus::Verified && MetamobKralove().usable &&
        MetamobKralove().status == KraloveStatus::Refused && !Logged("METAMOB_KEY_REFUSED") &&
        Logged("METAMOB_KRALOVE_FAILED reason=refused"), "Refused for the openings only: silent, the key still usable");
    for (int i = 0; i < 9; ++i) Want(60000);
    Check(Sent() == 2 && MetamobKralove().status == KraloveStatus::Refused, "Not read again within 10 min");
    Finish("kralove refused for the openings only");

    Begin(Folder("kralove-forget"), true);
    ForgetStoredKey();
    Check(!MetamobKralove().usable, "Forgotten: unusable at once");
    Want();
    Said({KeyForgotten}, "Forgotten");
    Want(60000);
    Check(Sent() == 0 && !MetamobKralove().usable, "Nothing read once forgotten");
    Finish("kralove while forgetting");
}

// The captures go first: nothing is read while a capture waits.
void KraloveCapturesFirst() {
    Begin(Folder("kralove-captures"), true);
    EnqueueCapture(Captured(Tofu));
    Want();
    Want(1999);
    Check(Sent() == 0, "Not read while a capture waits for its 2 s");
    ExpectFirst(Tofu, 2, 3);
    Want(1);
    Said({Added(Tofu, 2, 3)}, "The capture first");
    Check(Sent() == 5 && !MetamobKralove().read, "Alone in its worker");
    ExpectKralove(Json(Openings({Opening(905, "2026-10-24T19:00:00Z", 11, 2)})));
    Want();
    Check(Sent() == 6 && MetamobKralove().read && MetamobKralove().openings.size() == 1, "Then the openings, the fresh quests kept");
    Finish("kralove after the captures");
}

// Failures are silent and spaced (30 s, then twice longer), never pause the
// captures; Retry-After holds everything, the captures first; Metamob's own 404
// waits 10 min; what was read stays through passing failures.
void KraloveFailures() {
    Begin(Folder("kralove-fail"), true);
    ExpectQuests({Quest(Slug, Character.c_str(), "ocre", 1)});
    ExpectKralove(Html(503));
    Want();
    auto at = LastAt();
    auto view = MetamobKralove();
    Check(view.status == KraloveStatus::Unreachable && !view.read && notices.empty() && Logged("METAMOB_KRALOVE_FAILED reason=request") &&
        Logged("METAMOB_HTTP method=GET route=/kralove status=503"), "Unreachable: silent");
    ExpectList(Slug);
    ExpectStep(Slug, Tofu, 2);
    ExpectWrite(Slug, Tofu, 3, Written(Tofu, 3));
    EnqueueCapture(Captured(Tofu));
    Tick(2000);
    Said({Added(Tofu, 2, 3)}, "A capture right after a failed reading goes out at once");
    WantAt(at + 29999);
    Check(Sent() == 6, "Not tried again before 30 s");
    ExpectKralove(Lost(12029, false));
    WantAt(at + 30000);
    at = LastAt();
    WantAt(at + 59999);
    Check(Sent() == 7, "Then 60 s");
    ExpectKralove(Json("{\"data\":{\"id\":1,\"description\":\"" + Marker + "\"}}"));
    WantAt(at + 60000);
    at = LastAt();
    Check(MetamobKralove().status == KraloveStatus::Unreadable && Logged("METAMOB_BAD_ANSWER route=/kralove") &&
        Logged("METAMOB_KRALOVE_FAILED reason=answer") && notices.empty(), "An answer that does not read: silent");
    WantAt(at + 119999);
    Check(Sent() == 8, "Then 120 s, never told");
    ExpectKralove(Json(Openings({Opening(906, "2026-10-24T19:00:00Z", 11, 4)})));
    WantAt(at + 120000);
    view = MetamobKralove();
    Check(view.status == KraloveStatus::Ready && view.read && view.openings.size() == 1 && Sent() == 9, "Read once Metamob answers (the quests kept)");
    // Rate limited: neither the calendar nor a capture before Retry-After; then the capture first.
    auto read = LastAt();
    ExpectQuests({Quest(Slug, Character.c_str(), "ocre", 1)});
    ExpectKralove(Status(429, "120"));
    WantAt(read + 600000);
    at = LastAt();
    view = MetamobKralove();
    Check(view.status == KraloveStatus::Busy && view.read && view.openings.size() == 1 && Logged("METAMOB_RATE_LIMIT seconds=120") &&
        Logged("METAMOB_KRALOVE_FAILED reason=busy") && notices.empty(), "429: busy, what was read stays");
    EnqueueCapture(Captured(Mob));
    WantAt(at + 119999);
    Check(Sent() == 11, "Neither the calendar nor a capture before Retry-After");
    ExpectStep(Slug, Mob, 0);
    ExpectWrite(Slug, Mob, 1, Written(Mob, 1));
    WantAt(at + 120000);
    Said({Added(Mob, 0, 1)}, "The capture first");
    Check(Sent() == 13, "Alone in its worker");
    ExpectKralove(Json(Openings({Opening(906, "2026-10-24T19:00:00Z", 11, 4)})));
    Want();
    Check(Sent() == 14 && MetamobKralove().status == KraloveStatus::Ready, "Then the openings");
    // Metamob's JSON 404: no such list, the openings dropped, asked again 10 min later only.
    read = LastAt();
    ExpectQuests({Quest(Slug, Character.c_str(), "ocre", 1)});
    ExpectKralove(Status(404));
    WantAt(read + 600000);
    at = LastAt();
    view = MetamobKralove();
    Check(view.status == KraloveStatus::Unavailable && !view.read && view.openings.empty() && view.server.empty() &&
        Logged("METAMOB_KRALOVE_FAILED reason=unavailable"), "Metamob's 404: unavailable");
    WantAt(at + 599999);
    Check(Sent() == 16, "Asked again 10 min later only");
    // A 404 page that is not Metamob's: a passing failure.
    ExpectQuests({Quest(Slug, Character.c_str(), "ocre", 1)});
    ExpectKralove(Html(404));
    WantAt(at + 600000);
    at = LastAt();
    Check(MetamobKralove().status == KraloveStatus::Unreachable && Sent() == 18, "A 404 page: unreachable");
    WantAt(at + 29999);
    Check(Sent() == 18, "Then 30 s");
    ExpectKralove(Json(Openings({})));
    WantAt(at + 30000);
    Check(Sent() == 19 && MetamobKralove().status == KraloveStatus::Ready && MetamobKralove().read, "Read again");
    Finish("kralove failures");
}

// The server of the character's Unity Ocre quest: none, several naming none of
// this character, a quest naming no server, quests that do not read; then its own.
void KraloveQuests() {
    Begin(Folder("kralove-quest"), true);
    ExpectQuests({Quest(Slug, Character.c_str(), "ocre", 2), Quest(OtherSlug, nullptr, "dokille", 1)});
    Want();
    auto at = LastAt();
    auto view = MetamobKralove();
    Check(view.status == KraloveStatus::NoQuest && !view.read && notices.empty() && Logged("METAMOB_KRALOVE_FAILED reason=quest"),
        "No Unity Ocre quest: no server, silent");
    for (int i = 0; i < 19; ++i) Want(30000);
    WantAt(at + 599999);
    Check(Sent() == 1, "Asked again 10 min later only");
    ExpectQuests({Quest(Slug, "Bob", "ocre", 1), Quest(OtherSlug, nullptr, "ocre", 1)});
    WantAt(at + 600000);
    at = LastAt();
    Check(MetamobKralove().status == KraloveStatus::SeveralQuests && notices.empty(), "Several quests naming none of this character");
    ExpectQuests({ServerQuest(Slug, Character.c_str(), 0, "Draconiros")});
    WantAt(at + 600000);
    at = LastAt();
    Check(MetamobKralove().status == KraloveStatus::NoServer && Logged("METAMOB_KRALOVE_FAILED reason=server"), "Its quest names no server");
    Expect("GET", "/quests", Json("{\"data\":{\"slug\":\"" + Slug + "\",\"bio\":\"" + Marker + "\"}}"));
    WantAt(at + 600000);
    at = LastAt();
    Check(MetamobKralove().status == KraloveStatus::Unreadable && Logged("METAMOB_BAD_ANSWER route=/quests"), "Quests that do not read");
    ExpectQuests({ServerQuest(Slug, Character.c_str(), 23, "Ombre")});
    ExpectKralove(Json(Openings({Opening(907, "2026-10-24T19:00:00Z", 23, 4), Opening(908, "2026-10-24T20:00:00Z", 11, 4)})), 23);
    WantAt(at + 30000);
    view = MetamobKralove();
    Check(view.read && view.server == "Ombre" && view.openings.size() == 1 && view.openings[0].id == 907 && view.openings[0].serverId == 23 &&
        Sent() == 6 && notices.empty(), "The quest's own server, its openings only");
    Finish("kralove quests");
}

// Another character, another key file: what was read goes, read again for them.
void KraloveChanges() {
    const auto settings = Folder("kralove-changes");
    Begin(settings, true);
    ExpectQuests({Quest(Slug, Character.c_str(), "ocre", 1), ServerQuest(OtherSlug, "Bobinou", 23, "Ombre")});
    ExpectKralove(Json(Openings({Opening(908, "2026-10-24T19:00:00Z", 11, 3)})));
    Want();
    auto view = MetamobKralove();
    Check(view.read && view.server == "Draconiros" && view.openings[0].id == 908, "This character's quest: server 11");
    playing = "Bobinou";
    ExpectKralove(Json(Openings({Opening(909, "2026-10-24T19:00:00Z", 23, 2)})), 23);
    Want(1000);
    view = MetamobKralove();
    Check(view.read && view.server == "Ombre" && view.openings.size() == 1 && view.openings[0].id == 909 && Sent() == 3,
        "Another character: its quest's server at once (the fresh quests kept)");
    playing.clear();
    Want(1000);
    Check(Sent() == 3 && MetamobKralove().openings[0].id == 909, "A character unknown for a moment: what was read stays");
    playing = "Bobinou";
    // Another client saves another key: what was read with the previous one goes at once.
    AnotherClientSaves(settings, OtherKey);
    keyNow = OtherKey;
    fakeNow += 1000;
    view = MetamobKralove();
    Check(view.usable && !view.read && view.status == KraloveStatus::Loading && view.openings.empty(), "Another key file: read again");
    ExpectKralove(Json(Openings({Opening(910, "2026-10-24T19:00:00Z", 23, 1)})), 23);
    Want();
    view = MetamobKralove();
    Check(view.read && view.openings[0].id == 910 && Sent() == 4, "Read with that key");
    {
        std::lock_guard<std::mutex> guard(scriptLock);
        Check(calls.back().key == OtherKey, "With the other client's key");
    }
    Finish("kralove changes");
}

// The chat part's list and the calendar's openings asked together: one worker,
// the quests read once.
void KraloveWithChat() {
    Begin(Folder("kralove-chat"), true);
    ExpectQuests({Quest(Slug, Character.c_str(), "ocre", 1)});
    ExpectList(Slug);
    ExpectKralove(Json(Openings({Opening(911, "2026-10-24T19:00:00Z", 11, 6)})));
    WantMetamobCatalog();
    Want();
    Check(Sent() == 4 && Rich(GameTofu) == Colored(2) && MetamobKralove().read && MetamobKralove().openings[0].id == 911 && notices.empty(),
        "One worker: the quests once, the list, the openings");
    {
        std::lock_guard<std::mutex> guard(scriptLock);
        for (size_t i = 1; i < calls.size(); ++i) Check(calls[i].at - calls[i - 1].at >= 1100, "At least 1.1 s between two requests");
    }
    // A 429 on the chat part's list holds the calendar's reading of that same worker until its Retry-After.
    const auto read = LastAt();
    Expect("GET", ListPath(Slug, 0), Status(429, "120"));
    const auto both = [](uint64_t at) { if (fakeNow.load() < at) fakeNow = at; WantMetamobCatalog(); WantKralove(); TickSync(syncOn); };
    both(read + 600000);
    const auto at = LastAt();
    Check(Sent() == 5 && Logged("METAMOB_RATE_LIMIT seconds=120") && MetamobKralove().openings[0].id == 911,
        "The list rate limited: the openings not asked in that worker");
    both(at + 119999);
    Check(Sent() == 5, "Neither before Retry-After");
    ExpectList(Slug, CatalogWith({{Tofu.id, 4}}));
    ExpectQuests({Quest(Slug, Character.c_str(), "ocre", 1)});
    ExpectKralove(Json(Openings({Opening(914, "2026-10-24T19:00:00Z", 11, 2)})));
    both(at + 120000);
    Check(Sent() == 9 && Rich(GameTofu) == Colored(4) && MetamobKralove().openings[0].id == 914 && notices.empty(),
        "Then both, the quests read again for the server");
    Finish("kralove with the chat part");
}

// On a real worker thread: loading meanwhile, kept on the game thread; a forget
// while it reads leaves nothing usable.
void KraloveThreaded() {
    const auto path = "GET " + KralovePath();
    Gate(Folder("kralove-thread"), path, Json(Openings({Opening(912, "2026-10-24T19:00:00Z", 11, 6)})));
    Want();
    Await([] { return holding.load(); }, 5000, "the openings read");
    Check(!MetamobKralove().read && MetamobKralove().status == KraloveStatus::Loading && metamob::testing::WorkerRunning(),
        "Loading while the worker reads");
    letGo = true;
    AwaitWorker("the openings read");
    Check(MetamobKralove().read && MetamobKralove().openings[0].id == 912 && notices.empty() && Requested(path) == 1,
        "Read on the worker, kept on the game thread");
    Finish("kralove on the worker");

    Gate(Folder("kralove-thread-forget"), path, Json(Openings({Opening(913, "2026-10-24T19:00:00Z", 11, 6)})));
    Want();
    Await([] { return holding.load(); }, 5000, "the openings read");
    ForgetStoredKey();
    Check(!MetamobKralove().usable, "Forgotten while read: nothing usable at once");
    letGo = true;
    Await([] { return Noticed(KeyForgotten) && !metamob::testing::WorkerRunning(); }, 10000, "the forget");
    Tick();
    Said({KeyForgotten}, "Forgotten");
    Want(60000);
    Check(!MetamobKralove().usable && !MetamobKralove().read && Requested() == 2 && !KeyStored(), "Nothing kept, nothing more read");
    Finish("kralove forgotten while read");
}
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
    InstallSync(settings, Scripted, Noted); // the next session: that key not checked yet
    RowShown();
    Tick(); // the key row's silent check: the key loaded from the file, sent, released
    Check(heapprobe::Disarm() == 0, "A heap block was released with a plaintext copy of the key");
    Said({SavedUser, NoQuest}, "A key saved then used while the heap is watched");
    Check(LoadKey(KeyPath(settings))->value() == FakeKey && StoredKeyStatus() == KeyStatus::Verified, "The watched key is stored, then checked");
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
    if (!failingLog.empty() && text.rfind(failingLog, 0) == 0) throw std::runtime_error("Synthetic log failure");
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
        ReadOnlyKey();
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
        KeyStatusFollowsTheKey();
        KeyStatusFromCycles();
        SilentCheck();
        CheckHoldsTheCycle();
        CheckSkippedOnceKnown();
        CheckWhileForgetting();
        RecheckWhileTheWorkerRuns();
        ForgetThenNewKey();
        ChatFirstLoad();
        ChatRefresh();
        ChatWithoutKey();
        ChatAfterRefusal();
        ChatFailures();
        ChatQuestChoice();
        ChatPages();
        ChatCapturesFirst();
        ChatFollowsWrites();
        ChatKeyChanges();
        ChatCharacters();
        ChatLanguages();
        ChatThreaded();
        ChatWipedKey();
        KraloveFirstLoad();
        KraloveWithoutKey();
        KraloveCapturesFirst();
        KraloveFailures();
        KraloveQuests();
        KraloveChanges();
        KraloveWithChat();
        KraloveThreaded();
        Threaded();
        NoPrivateData();
        std::filesystem::remove_all(root);
        std::cout << "PASS: Metamob capture sync: key check and storage, debounce and grouping, quest choice, re-read before every "
            "absolute write, 30 cap, unmatched names, 401 pause and its 10 min retry (a read-only key told once), proxy 401/403 pages, "
            "404 rediscovery, Retry-After, "
            "backoff, unknown outcomes settled under the sender lock and announced to sibling clients (only that monster waits), "
            "resends never +n, unsettled writes given up as unconfirmed, sync toggle off, forget during a cycle and at Stop, no write "
            "left announced after a Stop, sender lock, worker thread, key copies wiped, no private data in logs or notices; the key row's "
            "status (valid, not verified, refused, unreadable) tied to the key file, none while a forget runs, one silent check per key file (its pause holds its worker's captures; asked again on request, never undone); "
            "« Metamob dans le chat »: the quest read at once when a chat label asks, again at most every 10 min while one asked within a minute, "
            "never while a capture waits nor without a usable key or during a forget, read with a refused key once valid or saved again, "
            "silent spaced failures, Retry-After, page checks, 404 rediscovery, the quest of each character, the sync's own writes shown "
            "at once, captions and details per state (what to fix after a lasting quest problem), fr/en/es names by "
            "type, on the worker, key copies wiped; « Ouvertures Kralamoure »: the openings of the quest's server read at once when "
            "the calendar asks, again at most every 10 min while it is shown (the quests read again then), from a "
            "week before this PC's date, never while a capture waits nor without a usable key or during a forget, silent spaced "
            "failures, Retry-After (captures first), Metamob's 404, the quest and server problems, another server's openings "
            "left out, another character or key read again, with the chat part in one worker, on the worker\n";
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
