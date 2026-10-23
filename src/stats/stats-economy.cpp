#include "stats/stats-economy.hpp"
#include "stats/stats.hpp"
#include "core/module.hpp"
#include <algorithm>
#include <atomic>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

// « Statistiques », economy (see stats-economy.hpp). Read on the game thread,
// inside the game's own message handlers as each message is applied, and at
// the tick: a few field reads, no inventory scan, no managed call inside a
// hook, nothing read at all while statistics are off. Classes and offsets are
// the pinned client's, checked at installation; a part whose contract fails
// stays off alone (STATS_ECONOMY_DISABLED).
//
// Kamas. PlayerInventory.SetKamas is the only writer of the character's
// kamas: KamasUpdateEvent (fef.bdmz), InventoryContentEvent (fef.bdmu) and
// CharacterCharacteristicsEvent (esg.zht, only when its value differs) all go
// through it, so one call is one exact change, the game's value before and
// after it, whichever of these messages comes first. A change counts only
// when the kamas on that inventory object are known to be this character's
// in this session:
// - the kamas a character has when it is loaded are a baseline, never
//   counted: its InventoryContentEvent, or AdoptMs with the same character and
//   inventory at the ticks (mod started in game, statistics turned on). The
//   ticks stop off the game (login, character selection, reconnection): after
//   such a gap only a new baseline counts again;
// - a change with the opposite change of the storage's kamas within WindowMs
//   (StorageKamasUpdateEvent, euk.zyv: the bank, the chests and the Havre-Sac
//   chest share one storage inventory) is a transfer, in either order, also
//   when one side came in several messages;
// - a change during a trade (faj.bcan names the partner; the exchange lasts
//   while esg.<dqai> is set, then TradeGraceMs) with another character played
//   with the mod on this PC (the clients share their characters' ids, see
//   SharedCharacters) is a transfer between one's own characters;
// - the rest is earned (more kamas) or spent (fewer), WindowMs after it.
// Harvests: fcp.bctq shows the harvested quantity above the character for
// both harvest events (with and without bonus): one harvest per call. Crafts:
// ezt.bbvb with the game's own success test and the multicraft crafter and
// customer it compares (ezt.dsef, ezt.dseg). Quests: fde.bcys
// (QuestValidatedEvent). Achievements: fcv.bcvg (AchievementFinishedEvent),
// achieved by this character.
namespace stats_economy {
namespace {
using namespace managed;
using stats::Counter;
constexpr const char* Core = "Core.dll";
constexpr const char* InventorySpace = "Core.UILogic.Inventory";
constexpr const char* Protocol = "Ankama.Dofus.Protocol.Game.dll";
constexpr uint64_t WindowMs = 2500;       // both sides of a transfer arrive within this time
constexpr uint64_t AdoptMs = 10000;       // the same character and inventory this long: their kamas are a baseline
constexpr uint64_t GapMs = 5000;          // ticks stopped this long: off the game, or a session change
constexpr uint64_t LookupMs = 2000;       // the character and inventory services are looked up again
constexpr uint64_t TradeGraceMs = 3000;   // a trade's kamas may follow the end of its exchange
constexpr uint64_t TradeMaxMs = 1800000;  // an exchange still open after 30 minutes is forgotten
constexpr uint64_t ListMs = 60000;        // this client lists its character again
constexpr int32_t PlayerTrade = 1;        // eim of a trade between players, as fai.bbzj and fai.bbzi call faj.bcan
constexpr int32_t NoObject = 0, NoObjectOther = 4; // kco results ezt.bbvb shows without any crafted object
constexpr int32_t WithObject = 2;         // kco oneof case of a crafted object's description (kco.ckgn)
constexpr int64_t MaxKamas = 1000000000000;
constexpr int32_t MaxCrafted = 100000;
constexpr size_t MaxChanges = 64, MaxTrades = 8, ListSlots = 64;
enum Seen : unsigned { SeenHarvest = 1, SeenCraft = 2, SeenQuest = 4, SeenAchievement = 8, SeenStorage = 16, SeenTrade = 32 };

using SetKamasFn = void(*)(Obj, int64_t, Method);
using HandlerFn = bool(*)(Obj, Obj, Method);
using TradeFn = void(*)(Obj, int32_t, int64_t, int64_t, int32_t, int32_t, int32_t, int32_t, Method);
using HarvestFn = bool(*)(Obj, int32_t, int32_t, int32_t, Method);
SetKamasFn originalSetKamas = nullptr;
HandlerFn originalContent = nullptr, originalStorage = nullptr, originalCraft = nullptr;
HandlerFn originalQuest = nullptr, originalAchievement = nullptr;
TradeFn originalTrade = nullptr;
HarvestFn originalHarvest = nullptr;

// Pinned 3.7.12.12 classes and offsets, each offset tied to its field name at installation.
struct Contract {
    Klass character{}, info{};                                      // esg, z
    size_t characterInfo = 0, characterId = 0, exchanging = 0;     // esg.dpzj, z.dbrl, esg.<dqai> (an exchange is open)
    Klass storageInventory{}, playerInventory{}, contentHandler{}, inventories{}, storageHandler{}, storageFrame{};
    size_t kamas = 0, inventoryCharacter = 0;                       // Inventory.<kamas>, PlayerInventory.m_playedCharacterService
    size_t handlerInventories = 0, player = 0, storage = 0;         // fef.dthz, feh.<dtjh> (the character's), feh.dtji (storage)
    size_t frame = 0, frameInventories = 0, frameCharacter = 0;     // euk.dqpb, eul.dqpe, eul.dqpg
    Klass exchangeFrame{};
    size_t exchangeCharacter = 0;                                   // faj.dsgo
    Klass craftFrame{}, craftParty{}, craftResult{}, craftDescription{}, craftObject{};
    size_t craftCharacter = 0, crafter = 0, customer = 0, partyId = 0;     // ezt.dsdy, ezt.dsef, ezt.dseg, bgw.dhrz
    size_t result = 0, outcome = 0, outcomeCase = 0;                       // kco: result, oneof object, oneof case
    size_t optional = 0, object = 0, quantity = 0, uid = 0;                // kcm has-bits, kcm object, lef quantity and uid
    Klass questFrame{}, questValidated{};
    size_t questCharacter = 0, questId = 0;                                // fde.dtcf, ibc.ehbe
    Klass achievementFrame{}, achievementFinished{}, achievement{};
    size_t achievementCharacter = 0, finished = 0, achievementId = 0, achievedBy = 0; // fcv.dtaa, mdi.fgqo, mdl.fgrh, mdl.fgrj
    bool base = false, kamasReady = false, tradeReady = false, harvestReady = false;
    bool craftReady = false, questReady = false, achievementReady = false;
} contract;

// One change of kamas, the character's own or the storage's.
struct Change { uint64_t at = 0; int64_t character = 0; int64_t amount = 0; bool paired = false; };
// A trade with another of one's own characters.
struct Trade { int64_t character = 0; uint64_t start = 0, end = 0; bool open = true; };
struct State {
    bool counting = false;               // statistics were on at the last tick
    Ref character, inventories;          // esg and feh, looked up again every LookupMs
    uint64_t nextLookup = 0, lastTick = 0;
    // The character whose kamas the player inventory holds in this session,
    // that inventory, its kamas after the last change seen and since when.
    int64_t owner = 0, last = 0;
    Ref ownerInventory;
    uint64_t ownerAt = 0;
    // What the ticks saw, for the AdoptMs baseline (the inventory: identity only).
    int64_t seenCharacter = 0;
    Obj seenInventory = nullptr;
    uint64_t seenSince = 0;
    int content = 0;                     // inside an InventoryContentEvent's handler
    std::vector<Change> player, storage;
    std::vector<Trade> trades;
    int64_t listed = 0;
    uint64_t nextListing = 0;
    unsigned seen = 0;                   // kinds already logged once (Seen)
    std::map<std::string, uint64_t> nextLog;
} state;
// The game thread, as the ticks run on it: the hooks act there only.
std::atomic<DWORD> gameThread{0};
std::atomic_bool otherThread{false};
uint64_t (*scriptedClock)() = nullptr;

// The characters played with the mod on this PC since the first of its game
// clients started, shared by these clients (one per character) in a small
// named memory block: a ring of ids, so that a trade between two of them is
// recognized by both. Ids only; the block goes with the last client.
struct SharedList { volatile LONG64 ids[ListSlots]; volatile LONG next; };
struct SharedCharacters {
    std::wstring name = L"Local\\DofusMiniatures-stats-characters-1";
    HANDLE mapping = nullptr;
    SharedList* list = nullptr;
    bool failed = false;
    bool Open() {
        if (list) return true;
        if (failed) return false;
        mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(SharedList), name.c_str());
        if (mapping) list = static_cast<SharedList*>(MapViewOfFile(mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(SharedList)));
        if (!list) {
            Close();
            failed = true;
            try { Log("STATS_ECONOMY_LIST_UNAVAILABLE"); } catch (...) {}
        }
        return list != nullptr;
    }
    bool Has(int64_t id) {
        if (id <= 0 || !Open()) return false;
        for (const auto& slot : list->ids) if (slot == id) return true;
        return false;
    }
    void Add(int64_t id) {
        // Has opened the list: none when it is unavailable (STATS_ECONOMY_LIST_UNAVAILABLE).
        if (id <= 0 || Has(id) || !list) return;
        const auto slot = static_cast<unsigned long>(InterlockedIncrement(&list->next) - 1) % ListSlots;
        InterlockedExchange64(&list->ids[slot], id);
    }
    void Close() {
        if (list) UnmapViewOfFile(list);
        if (mapping) CloseHandle(mapping);
        list = nullptr;
        mapping = nullptr;
    }
} ownCharacters;

uint64_t Now() { return scriptedClock ? scriptedClock() : GetTickCount64(); }
bool Active() {
    return module && module->ready.load() && module->servicesReady && !module->stopped.load() && !module->stopRequested.load();
}
bool Is(Obj object, Klass type) { return object && api.object_get_class(object) == type; }
size_t RequireField(Klass type, const char* name, size_t offset) {
    const auto field = api.class_get_field_from_name(type, name);
    if (!field || api.field_get_offset(field) != offset) throw std::runtime_error("Statistics field layout");
    return offset;
}
// Fixed codes and numbers only (never a name, an id or an exception text), at
// most once every 10 s per key.
bool Due(const std::string& key) {
    const auto now = Now();
    auto& next = state.nextLog[key];
    if (now < next) return false;
    next = now + 10000;
    return true;
}
void Error(const char* stage) {
    try { if (Due(std::string("error ") + stage)) Log(std::string("STATS_ECONOMY_ERROR stage=") + stage); } catch (...) {}
}
void Skipped(const char* reason) {
    try { if (Due(std::string("skip ") + reason)) Log(std::string("STATS_KAMAS_SKIPPED reason=") + reason); } catch (...) {}
}
// The first of each kind in a session, to confirm in game that its handler is reached.
void First(unsigned kind, const char* name) {
    if (state.seen & kind) return;
    state.seen |= kind;
    try { Log(std::string("STATS_ECONOMY_SEEN kind=") + name); } catch (...) {}
}
// The played character of a played-character service: esg.zcx without the
// call (its dpzj, then that z's id); 0 when there is none.
int64_t CharacterOf(Obj service) {
    if (!Is(service, contract.character)) return 0;
    const Obj info = Read<Obj>(service, contract.characterInfo);
    if (!Is(info, contract.info)) return 0;
    const int64_t id = Read<int64_t>(info, contract.characterId);
    return id > 0 ? id : 0;
}
// The hooks act on the game thread only, for a running module counting statistics.
// A handler reached from another thread is logged once: nothing is counted there.
bool Watching() {
    if (!contract.base) return false;
    const DWORD thread = gameThread.load();
    if (thread != GetCurrentThreadId()) {
        if (thread && !otherThread.exchange(true)) {
            try { Log("STATS_ECONOMY_THREAD other=1"); } catch (...) {}
        }
        return false;
    }
    return state.counting && Active() && stats::Counting();
}
bool Plausible(int64_t kamas) { return kamas >= 0 && kamas <= MaxKamas; }
void Queue(std::vector<Change>& changes, const Change& change) {
    if (changes.size() >= MaxChanges) { changes.erase(changes.begin()); Error("queue"); }
    changes.push_back(change);
}

// =================================================================== kamas
// The kamas this inventory holds now are the character's starting point.
void TakeBaseline(int64_t character, Obj inventory, const char* source) {
    if (character <= 0 || !inventory) { state.owner = 0; state.ownerInventory.reset(); return; }
    if (state.ownerInventory.get() != inventory) state.ownerInventory.reset(inventory);
    state.owner = character;
    state.last = Read<int64_t>(inventory, contract.kamas);
    state.ownerAt = Now();
    Log(std::string("STATS_KAMAS_BASELINE source=") + source);
}
struct Before { int64_t character = 0, kamas = 0; };
bool ReadBefore(Obj inventory, Before& before) {
    if (!Is(inventory, contract.playerInventory)) return false;
    before.character = CharacterOf(Read<Obj>(inventory, contract.inventoryCharacter));
    before.kamas = Read<int64_t>(inventory, contract.kamas);
    return true;
}
void Changed(Obj inventory, const Before& before) {
    if (state.content > 0) return; // InventoryContentEvent: its handler takes the baseline
    const int64_t after = Read<int64_t>(inventory, contract.kamas);
    const uint64_t now = Now();
    const bool owned = before.character > 0 && before.character == state.owner && inventory == state.ownerInventory.get();
    // Off the game since the last tick, only a baseline taken since counts.
    const bool current = now - state.lastTick <= GapMs || state.ownerAt > state.lastTick;
    if (!owned || !current) {
        if (after != before.kamas) Skipped(owned ? "session" : "owner");
        return;
    }
    if (before.kamas != state.last) {
        // Written without SetKamas: never counted, logged for an in-game check.
        try { if (Due("untracked")) Log("STATS_KAMAS_UNTRACKED amount=" + std::to_string(before.kamas - state.last)); } catch (...) {}
    }
    state.last = after;
    if (after == before.kamas) return;
    if (!Plausible(before.kamas) || !Plausible(after)) { Skipped("bounds"); return; }
    Queue(state.player, Change{now, before.character, after - before.kamas});
}
void SetKamasHook(Obj self, int64_t kamas, Method method) {
    Before before;
    bool watched = false;
    try { watched = contract.kamasReady && Watching() && ReadBefore(self, before); } catch (...) { Error("kamas"); }
    originalSetKamas(self, kamas, method);
    if (!watched) return;
    try { Changed(self, before); } catch (...) { Error("kamas"); }
}
// InventoryContentEvent's handler sets the kamas through SetKamas: not a change.
struct InContent {
    bool on;
    explicit InContent(bool watched) : on(watched) { if (on) ++state.content; }
    ~InContent() { if (on) --state.content; }
    InContent(const InContent&) = delete;
    InContent& operator=(const InContent&) = delete;
};
void Baseline(Obj handler) {
    const Obj service = Read<Obj>(handler, contract.handlerInventories);
    if (!Is(service, contract.inventories)) return;
    const Obj inventory = Read<Obj>(service, contract.player);
    if (!Is(inventory, contract.playerInventory)) return;
    TakeBaseline(CharacterOf(Read<Obj>(inventory, contract.inventoryCharacter)), inventory, "content");
}
bool ContentHook(Obj self, Obj message, Method method) {
    bool watched = false;
    try { watched = contract.kamasReady && Watching() && Is(self, contract.contentHandler); } catch (...) { Error("baseline"); }
    bool result = false;
    {
        InContent inside(watched);
        result = originalContent(self, message, method);
    }
    if (watched) {
        try { Baseline(self); } catch (...) { Error("baseline"); }
    }
    return result;
}
// The storage side: only a storage already open (the game creates an empty
// one otherwise), measured against the game's own previous value.
struct StorageBefore { Obj service = nullptr, storage = nullptr; int64_t character = 0, kamas = 0; };
bool ReadStorage(Obj handler, StorageBefore& before) {
    if (!Is(handler, contract.storageHandler)) return false;
    const Obj frame = Read<Obj>(handler, contract.frame);
    if (!Is(frame, contract.storageFrame)) return false;
    before.service = Read<Obj>(frame, contract.frameInventories);
    if (!Is(before.service, contract.inventories)) return false;
    before.storage = Read<Obj>(before.service, contract.storage);
    if (!Is(before.storage, contract.storageInventory)) return false;
    before.character = CharacterOf(Read<Obj>(frame, contract.frameCharacter));
    before.kamas = Read<int64_t>(before.storage, contract.kamas);
    return before.character > 0;
}
void Stored(const StorageBefore& before) {
    if (Read<Obj>(before.service, contract.storage) != before.storage) return;
    const int64_t after = Read<int64_t>(before.storage, contract.kamas);
    if (after == before.kamas) return;
    if (!Plausible(before.kamas) || !Plausible(after)) { Skipped("storage-bounds"); return; }
    First(SeenStorage, "storage");
    Queue(state.storage, Change{Now(), before.character, after - before.kamas});
}
bool StorageHook(Obj self, Obj message, Method method) {
    StorageBefore before;
    bool watched = false;
    try { watched = contract.kamasReady && Watching() && ReadStorage(self, before); } catch (...) { Error("storage"); }
    const bool result = originalStorage(self, message, method);
    if (watched) {
        try { Stored(before); } catch (...) { Error("storage"); }
    }
    return result;
}
// faj.bcan(kind, id, id, ...): as it does, the partner is the id that is not this character.
void TradeStarted(Obj frame, int64_t first, int64_t second) {
    if (!Is(frame, contract.exchangeFrame)) return;
    const int64_t character = CharacterOf(Read<Obj>(frame, contract.exchangeCharacter));
    const int64_t partner = first == character ? second : first;
    if (character <= 0 || partner <= 0 || partner == character || !ownCharacters.Has(partner)) return;
    if (state.trades.size() >= MaxTrades) state.trades.erase(state.trades.begin());
    state.trades.push_back(Trade{character, Now(), 0, true});
    First(SeenTrade, "own-trade");
    Log("STATS_KAMAS_TRADE own=1");
}
void TradeHook(Obj self, int32_t kind, int64_t first, int64_t second, int32_t a, int32_t b, int32_t c, int32_t d, Method method) {
    originalTrade(self, kind, first, second, a, b, c, d, method);
    try {
        if (contract.tradeReady && contract.kamasReady && kind == PlayerTrade && Watching()) TradeStarted(self, first, second);
    } catch (...) { Error("trade"); }
}
bool Near(uint64_t a, uint64_t b) { return (a > b ? a - b : b - a) <= WindowMs; }
// Transfers: a change of the character's kamas and one of the storage's, same
// character, opposite and equal, close in time; also when one side came in
// several messages (their sum).
void Pair() {
    for (auto& change : state.player) {
        if (change.paired) continue;
        for (auto& move : state.storage) {
            if (move.paired || move.character != change.character || !Near(change.at, move.at) || move.amount != -change.amount) continue;
            change.paired = move.paired = true;
            break;
        }
    }
    const auto sums = [](std::vector<Change>& ones, std::vector<Change>& others) {
        for (auto& one : ones) {
            if (one.paired) continue;
            const auto matches = [&](const Change& other) { return !other.paired && other.character == one.character && Near(one.at, other.at); };
            int64_t sum = 0;
            size_t count = 0;
            for (const auto& other : others) if (matches(other)) { sum += other.amount; ++count; }
            if (count < 2 || sum != -one.amount) continue;
            for (auto& other : others) if (matches(other)) other.paired = true;
            one.paired = true;
        }
    };
    sums(state.player, state.storage);
    sums(state.storage, state.player);
}
bool OwnTrade(const Change& change) {
    return std::any_of(state.trades.begin(), state.trades.end(), [&](const Trade& trade) {
        return trade.character == change.character && change.at >= trade.start && (trade.open || change.at <= trade.end + TradeGraceMs);
    });
}
void Commit(uint64_t now) {
    Pair();
    std::vector<Change> waiting;
    for (const auto& change : state.player) {
        if (now - change.at < WindowMs) { waiting.push_back(change); continue; }
        const bool own = !change.paired && OwnTrade(change);
        const char* kind = change.paired ? "transfer" : own ? "trade" : change.amount > 0 ? "earned" : "spent";
        if (!change.paired && !own) {
            stats::Add(change.character, change.amount > 0 ? Counter::KamasEarned : Counter::KamasSpent,
                       change.amount > 0 ? change.amount : -change.amount);
        }
        try { Log(std::string("STATS_KAMAS kind=") + kind + " amount=" + std::to_string(change.amount)); } catch (...) {}
    }
    state.player.swap(waiting);
    state.storage.erase(std::remove_if(state.storage.begin(), state.storage.end(),
        [&](const Change& move) { return now - move.at >= WindowMs; }), state.storage.end());
}
// A trade with one's own character lasts while the exchange is open: faj.bcak
// sets esg.<dqai>, the exchange's end clears it.
void Trades(uint64_t now) {
    const Obj service = state.character.get();
    const int64_t character = CharacterOf(service);
    const bool exchanging = character > 0 && Read<bool>(service, contract.exchanging);
    for (auto& trade : state.trades) {
        if (!trade.open || (exchanging && character == trade.character && now - trade.start < TradeMaxMs)) continue;
        trade.open = false;
        trade.end = now;
    }
    // Its last kamas (TradeGraceMs after its end) were committed WindowMs
    // later, by the Commit that runs before this at every tick.
    state.trades.erase(std::remove_if(state.trades.begin(), state.trades.end(),
        [&](const Trade& trade) { return !trade.open && now - trade.end >= TradeGraceMs + WindowMs; }), state.trades.end());
}
void Lookup(uint64_t now) {
    if (now < state.nextLookup) return;
    state.nextLookup = now + LookupMs;
    Ref character(module->FindService("fng"));
    const Obj played = Is(character.get(), contract.character) ? character.get() : nullptr;
    if (state.character.get() != played) state.character.reset(played);
    Ref inventories(contract.kamasReady ? module->FindService("fmo") : nullptr);
    const Obj found = Is(inventories.get(), contract.inventories) ? inventories.get() : nullptr;
    if (state.inventories.get() != found) state.inventories.reset(found);
}
// The character played and its inventory: their kamas become the baseline once
// both stayed the same for AdoptMs, unless its InventoryContentEvent set it.
// After a gap of the ticks, a baseline taken before it no longer counts.
void Follow(uint64_t now) {
    if (state.lastTick && now - state.lastTick > GapMs) {
        if (state.ownerAt <= state.lastTick) { state.owner = 0; state.ownerInventory.reset(); }
        state.seenSince = now;
    }
    state.lastTick = now;
    const int64_t character = CharacterOf(state.character.get());
    Obj inventory = nullptr;
    if (contract.kamasReady && state.inventories.get()) {
        inventory = Read<Obj>(state.inventories.get(), contract.player);
        if (!Is(inventory, contract.playerInventory)) inventory = nullptr;
    }
    if (character != state.seenCharacter || inventory != state.seenInventory) {
        state.seenCharacter = character;
        state.seenInventory = inventory;
        state.seenSince = now;
    }
    if (character > 0 && inventory && (state.owner != character || state.ownerInventory.get() != inventory) &&
        now - state.seenSince >= AdoptMs)
        TakeBaseline(character, inventory, "start");
    if (contract.tradeReady && character > 0 && (character != state.listed || now >= state.nextListing)) {
        ownCharacters.Add(character);
        state.listed = character;
        state.nextListing = now + ListMs;
    }
}
// Counting off, or on again: nothing pending carries over and the kamas need a new baseline.
void Forget() {
    if (!state.player.empty()) {
        try { Log("STATS_KAMAS_DROPPED count=" + std::to_string(state.player.size())); } catch (...) {}
    }
    state.player.clear(); state.storage.clear(); state.trades.clear();
    state.owner = 0; state.last = 0; state.ownerAt = 0; state.ownerInventory.reset();
    state.seenCharacter = 0; state.seenInventory = nullptr; state.seenSince = 0;
    state.lastTick = 0; state.listed = 0; state.nextListing = 0;
}

// ================================================================== events
bool HarvestHook(Obj self, int32_t item, int32_t quantity, int32_t bonus, Method method) {
    const bool result = originalHarvest(self, item, quantity, bonus, method);
    try {
        if (contract.harvestReady && item > 0 && quantity > 0 && Watching()) {
            // Shown above the harvesting character, in its own client only.
            const int64_t character = CharacterOf(state.character.get());
            if (character > 0) {
                stats::Add(character, Counter::Harvests, 1);
                First(SeenHarvest, "harvest");
            }
        }
    } catch (...) { Error("harvest"); }
    return result;
}
struct Craft { int64_t character = 0; int32_t quantity = 0; };
// The game's own success test (ezt.bbvb): a result other than 0 and 4 with a
// crafted object's description (oneof case 2, not the smithmagic variant with
// its optional magic field, an object uid). It tells the crafter alone
// « successSelf », a multicraft's crafter « successTarget » (counted for both)
// and its customer « successOther » (crafted by the other one: not counted).
bool Crafted(Obj frame, Obj message, Craft& craft) {
    if (!Is(frame, contract.craftFrame) || !Is(message, contract.craftResult)) return false;
    const int32_t result = Read<int32_t>(message, contract.result);
    if (result == NoObject || result == NoObjectOther || Read<int32_t>(message, contract.outcomeCase) != WithObject) return false;
    const Obj description = Read<Obj>(message, contract.outcome);
    if (!Is(description, contract.craftDescription) || (Read<int32_t>(description, contract.optional) & 1) != 0) return false;
    const Obj object = Read<Obj>(description, contract.object);
    if (!Is(object, contract.craftObject) || Read<int32_t>(object, contract.uid) == 0) return false;
    craft.quantity = Read<int32_t>(object, contract.quantity);
    craft.character = CharacterOf(Read<Obj>(frame, contract.craftCharacter));
    if (craft.character <= 0 || craft.quantity <= 0 || craft.quantity > MaxCrafted) return false;
    const auto is = [&](size_t slot) {
        const Obj party = Read<Obj>(frame, slot);
        return Is(party, contract.craftParty) && Read<int64_t>(party, contract.partyId) == craft.character;
    };
    return is(contract.crafter) || !is(contract.customer);
}
bool CraftHook(Obj self, Obj message, Method method) {
    Craft craft;
    bool counted = false;
    try { counted = contract.craftReady && Watching() && Crafted(self, message, craft); } catch (...) { Error("craft"); }
    const bool result = originalCraft(self, message, method);
    if (counted) {
        try {
            stats::Add(craft.character, Counter::Crafts, craft.quantity);
            First(SeenCraft, "craft");
        } catch (...) { Error("craft"); }
    }
    return result;
}
bool QuestHook(Obj self, Obj message, Method method) {
    int64_t character = 0;
    try {
        if (contract.questReady && Watching() && Is(self, contract.questFrame) && Is(message, contract.questValidated) &&
            Read<int32_t>(message, contract.questId) > 0)
            character = CharacterOf(Read<Obj>(self, contract.questCharacter));
    } catch (...) { Error("quest"); }
    const bool result = originalQuest(self, message, method);
    if (character > 0) {
        try {
            stats::Add(character, Counter::Quests, 1);
            First(SeenQuest, "quest");
        } catch (...) { Error("quest"); }
    }
    return result;
}
bool AchievementHook(Obj self, Obj message, Method method) {
    int64_t character = 0;
    try {
        if (contract.achievementReady && Watching() && Is(self, contract.achievementFrame) && Is(message, contract.achievementFinished)) {
            const Obj achievement = Read<Obj>(message, contract.finished);
            if (Is(achievement, contract.achievement) && Read<int32_t>(achievement, contract.achievementId) > 0) {
                // As the game adds the character's achievement points: achieved by this character.
                const int64_t local = CharacterOf(Read<Obj>(self, contract.achievementCharacter));
                if (local > 0 && Read<int64_t>(achievement, contract.achievedBy) == local) character = local;
            }
        }
    } catch (...) { Error("achievement"); }
    const bool result = originalAchievement(self, message, method);
    if (character > 0) {
        try {
            stats::Add(character, Counter::Achievements, 1);
            First(SeenAchievement, "achievement");
        } catch (...) { Error("achievement"); }
    }
    return result;
}

// ============================================================ installation
void InstallCharacter() {
    const auto character = api.Class(Core, "", "esg");
    const auto info = api.Class(Core, "", "z");
    contract.characterInfo = RequireField(character, "dpzj", 0x128);
    contract.exchanging = RequireField(character, "<dqai>k__BackingField", 0x1D8);
    contract.characterId = RequireField(info, "dbrl", 0x10);
    contract.character = character;
    contract.info = info;
}
// The three hooks together or none: without the storage side a bank deposit
// would count as spent, without the baseline a login as earned.
void InstallKamas() {
    const auto storageInventory = api.Class(Core, InventorySpace, "Inventory");
    const auto playerInventory = api.Class(Core, InventorySpace, "PlayerInventory");
    const auto contentHandler = api.Class(Core, "", "fef");
    const auto inventories = api.Class(Core, "", "feh");
    const auto storageHandler = api.Class(Core, "", "euk");
    const auto storageFrame = api.Class(Core, "", "eul");
    contract.kamas = RequireField(storageInventory, "<kamas>k__BackingField", 0x30);
    contract.inventoryCharacter = RequireField(playerInventory, "m_playedCharacterService", 0x60);
    contract.handlerInventories = RequireField(contentHandler, "dthz", 0x28);
    contract.player = RequireField(inventories, "<dtjh>k__BackingField", 0xF8);
    contract.storage = RequireField(inventories, "dtji", 0x100);
    contract.frame = RequireField(storageHandler, "dqpb", 0x10);
    contract.frameInventories = RequireField(storageFrame, "dqpe", 0x28);
    contract.frameCharacter = RequireField(storageFrame, "dqpg", 0x38);
    const auto setKamas = api.FindVerified(playerInventory, "SetKamas", 1, 0x2AF6F20);
    const auto content = api.FindVerified(contentHandler, "bdmu", 1, 0x1B07A50);
    const auto storage = api.FindVerified(storageHandler, "zyv", 1, 0x190B360);
    contract.storageInventory = storageInventory; contract.playerInventory = playerInventory;
    contract.contentHandler = contentHandler; contract.inventories = inventories;
    contract.storageHandler = storageHandler; contract.storageFrame = storageFrame;
    AddHook(setKamas, 0x2AF6F20, reinterpret_cast<void*>(&SetKamasHook), reinterpret_cast<void**>(&originalSetKamas));
    AddHook(content, 0x1B07A50, reinterpret_cast<void*>(&ContentHook), reinterpret_cast<void**>(&originalContent));
    AddHook(storage, 0x190B360, reinterpret_cast<void*>(&StorageHook), reinterpret_cast<void**>(&originalStorage));
}
void InstallTrade() {
    const auto exchangeFrame = api.Class(Core, "", "faj");
    contract.exchangeCharacter = RequireField(exchangeFrame, "dsgo", 0x38);
    const auto started = api.FindVerified(exchangeFrame, "bcan", 7, 0x1A05F60);
    contract.exchangeFrame = exchangeFrame;
    AddHook(started, 0x1A05F60, reinterpret_cast<void*>(&TradeHook), reinterpret_cast<void**>(&originalTrade));
}
void InstallHarvest() {
    const auto harvestFrame = api.Class(Core, "", "fcp");
    const auto shown = api.FindVerified(harvestFrame, "bctq", 3, 0x1A87B60);
    AddHook(shown, 0x1A87B60, reinterpret_cast<void*>(&HarvestHook), reinterpret_cast<void**>(&originalHarvest));
}
void InstallCraft() {
    const auto craftFrame = api.Class(Core, "", "ezt");
    const auto craftParty = api.Class(Core, "", "bgw");
    const auto craftResult = api.Class(Protocol, "", "kco");
    const auto craftObject = api.Class(Protocol, "", "lef");
    const auto craftDescription = api.NestedClass(api.NestedClass(craftResult, "kcn"), "kcm");
    contract.craftCharacter = RequireField(craftFrame, "dsdy", 0x30);
    contract.crafter = RequireField(craftFrame, "dsef", 0x68);
    contract.customer = RequireField(craftFrame, "dseg", 0x70);
    contract.partyId = RequireField(craftParty, "dhrz", 0x10);
    contract.result = RequireField(craftResult, "ettz", 0x18);
    contract.outcome = RequireField(craftResult, "etuc", 0x20);
    contract.outcomeCase = RequireField(craftResult, "etud", 0x28);
    contract.optional = RequireField(craftDescription, "ettm", 0x18);
    contract.object = RequireField(craftDescription, "ettv", 0x30);
    contract.quantity = RequireField(craftObject, "fade", 0x28);
    contract.uid = RequireField(craftObject, "fadg", 0x2C);
    const auto crafted = api.FindVerified(craftFrame, "bbvb", 1, 0x19D18D0);
    contract.craftFrame = craftFrame; contract.craftParty = craftParty; contract.craftResult = craftResult;
    contract.craftObject = craftObject; contract.craftDescription = craftDescription;
    AddHook(crafted, 0x19D18D0, reinterpret_cast<void*>(&CraftHook), reinterpret_cast<void**>(&originalCraft));
}
void InstallQuest() {
    const auto questFrame = api.Class(Core, "", "fde");
    const auto questValidated = api.Class(Protocol, "", "ibc");
    contract.questCharacter = RequireField(questFrame, "dtcf", 0x20);
    contract.questId = RequireField(questValidated, "ehbe", 0x18);
    const auto validated = api.FindVerified(questFrame, "bcys", 1, 0x1AA7380);
    contract.questFrame = questFrame;
    contract.questValidated = questValidated;
    AddHook(validated, 0x1AA7380, reinterpret_cast<void*>(&QuestHook), reinterpret_cast<void**>(&originalQuest));
}
void InstallAchievement() {
    const auto achievementFrame = api.Class(Core, "", "fcv");
    const auto achievementFinished = api.Class(Protocol, "", "mdi");
    const auto achievement = api.Class(Protocol, "", "mdl");
    contract.achievementCharacter = RequireField(achievementFrame, "dtaa", 0x28);
    contract.finished = RequireField(achievementFinished, "fgqo", 0x28);
    contract.achievementId = RequireField(achievement, "fgrh", 0x1C);
    contract.achievedBy = RequireField(achievement, "fgrj", 0x20);
    const auto unlocked = api.FindVerified(achievementFrame, "bcvg", 1, 0x1A90D70);
    contract.achievementFrame = achievementFrame;
    contract.achievementFinished = achievementFinished;
    contract.achievement = achievement;
    AddHook(unlocked, 0x1A90D70, reinterpret_cast<void*>(&AchievementHook), reinterpret_cast<void**>(&originalAchievement));
}
// One part: ready once its hooks are prepared; a failure leaves it off (its
// hooks, if any was prepared, only call the game's code).
void Part(const char* stage, bool& ready, void (*install)()) {
    if (ready) return;
    try {
        install();
        ready = true;
    } catch (...) {
        try { Log(std::string("STATS_ECONOMY_DISABLED stage=") + stage); } catch (...) {}
    }
}
}
}

void InstallStatsEconomy() {
    using namespace stats_economy;
    Part("character", contract.base, &InstallCharacter);
    if (!contract.base) return;
    Part("kamas", contract.kamasReady, &InstallKamas);
    Part("trade", contract.tradeReady, &InstallTrade);
    Part("harvest", contract.harvestReady, &InstallHarvest);
    Part("craft", contract.craftReady, &InstallCraft);
    Part("quest", contract.questReady, &InstallQuest);
    Part("achievement", contract.achievementReady, &InstallAchievement);
}
void TickStatsEconomy() {
    using namespace stats_economy;
    if (!contract.base || !Active()) return;
    try {
        gameThread.store(GetCurrentThreadId());
        const bool counting = stats::Counting();
        if (counting != state.counting) {
            Forget();
            state.counting = counting;
        }
        if (!counting) return;
        const uint64_t now = Now();
        try { Lookup(now); } catch (...) { Error("lookup"); }
        try { Follow(now); } catch (...) { Error("follow"); }
        // Commit first: kamas still within a trade's grace are committed
        // before that trade is forgotten, however far apart the ticks are.
        try { Commit(now); } catch (...) { Error("commit"); }
        try { Trades(now); } catch (...) { Error("trades"); }
    } catch (...) { Error("tick"); }
}
void RestoreStatsEconomy() {
    using namespace stats_economy;
    try {
        Forget();
        state.counting = false;
        state.content = 0;
        state.character.reset();
        state.inventories.reset();
        state.nextLookup = 0;
        state.seen = 0;
        state.nextLog.clear();
        gameThread.store(0);
        otherThread.store(false);
        ownCharacters.Close();
        ownCharacters.failed = false;
    } catch (...) {}
}
void UseStatsEconomyClock(uint64_t (*now)()) { stats_economy::scriptedClock = now; }
void UseStatsEconomyCharacterList(const wchar_t* name) {
    using namespace stats_economy;
    ownCharacters.Close();
    ownCharacters.failed = false;
    ownCharacters.name = name ? name : L"Local\\DofusMiniatures-stats-characters-1";
}
