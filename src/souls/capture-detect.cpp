#include "souls/capture-detect.hpp"
#include "souls/chat-soul-data.hpp"
#include "metamob/metamob-data.hpp"
#include "core/module.hpp"
#include <algorithm>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

// Captures come from the game's own fight-end report, polled on the game
// thread: fxm.bhys builds a NEW report (fyz) for every FightEndEvent and
// fzf.bioj, its only writer, stores it in fzf.dwqq. A report object never seen
// before is a fight that has just ended. Read-only: no hook, no inventory scan,
// no network; the sink only queues.
namespace capture_detect {
namespace {
using namespace managed;
constexpr const char* DataCenter = "Ankama.Dofus.Core.DataCenter.dll";
constexpr uint32_t BossSoul = 327, ArchSoul = 328; // item types "Âme de gardien de donjon", "Âme d'archimonstre"
constexpr uint16_t SoulMonster = 4058;             // ActionId.CharacterSoulMonster: its value is the monster id
constexpr int16_t Victory = 3;                     // fze.dwpy (emw) of a winner, as UIFightResult reads it
constexpr int MaxFighters = 128, MaxLoot = 512, MaxEffects = 128;
constexpr size_t MaxSeen = 16, MaxMonsterName = 256, MaxCharacterName = 128, MaxMonsters = 512;
struct Contract {
    Klass service{}, fight{}, history{}, report{}, fighter{}, loot{}, character{}, characterInfo{};
    Klass item{}, itemData{}, integerEffect{}, monster{};
    Method localId{}, genericId{}, quantity{}, itemType{}, data{}, byId{}, isBoss{}, isArch{}, name{};
    bool installed = false;
} contract;
std::function<void(metamob::Capture)> deliver;
struct State {
    Module* owner = nullptr;
    Ref compositor, history;
    // Reports already taken as the baseline or processed, newest last. Holding
    // them also keeps a later report from reusing one of their addresses.
    std::vector<Ref> seen;
    int64_t localId = 0;
    bool known = false, fightLogged = false;
    std::map<std::string, uint64_t> nextLog;
    // The chat part's reads: each monster read once (bounded), and the
    // character's name, read again when another character is seen.
    std::map<int, std::optional<metamob::GameMonster>> monsters;
    std::string name;
} state;
struct Soul { int gid = 0; uint32_t type = 0; int quantity = 0; bool valid = true; Ref item; };
struct Snapshot { Ref fight, history, report, character, info; int64_t localId = 0; };

bool Active() {
    return module && module->ready.load() && module->servicesReady && !module->stopped.load() && !module->stopRequested.load();
}
bool Is(Obj object, Klass type) { return object && api.object_get_class(object) == type; }
template<class T> T Field(Obj object, const char* name) { Ref value(api.Field(object, name)); return api.Value<T>(value.get()); }
int Count(Obj list, int maximum) {
    const int count = api.Value<int>(api.Call(list, "get_Count"));
    if (count < 0 || count > maximum) throw std::runtime_error("Capture collection outside bounds");
    return count;
}
void Layout(Klass type, const char* name, size_t offset) {
    const auto field = api.class_get_field_from_name(type, name);
    if (!field || api.field_get_offset(field) != offset) throw std::runtime_error("Capture field layout");
}
// Fixed codes and numbers only: never an exception text (game strings), a name or a key.
void Error(const char* stage) {
    try {
        const auto now = GetTickCount64();
        auto& next = state.nextLog[stage];
        if (now < next) return;
        next = now + 10000;
        Log(std::string("CAPTURE_DETECT_ERROR stage=") + stage);
    } catch (...) {}
}
bool Printable(const std::string& text, size_t maximum) {
    return !text.empty() && text.size() <= maximum &&
        std::none_of(text.begin(), text.end(), [](char c) { return static_cast<unsigned char>(c) < 0x20 || c == 0x7F; });
}
// Releases every reference without allocating: also safe from Module::Stop.
void Reset() {
    state.owner = nullptr; state.compositor.reset(); state.history.reset(); state.seen.clear();
    state.localId = 0; state.known = state.fightLogged = false; state.nextLog.clear();
    state.monsters.clear(); state.name.clear();
}
bool Seen(Obj report) {
    return std::any_of(state.seen.begin(), state.seen.end(), [&](const Ref& known) { return known.get() == report; });
}
void Remember(Ref report) {
    if (!report.get() || Seen(report.get())) return;
    if (state.seen.size() >= MaxSeen) state.seen.erase(state.seen.begin());
    state.seen.push_back(std::move(report));
}
// False while the game has no fight or character service (loading, character
// selection): nothing is decided until a complete read.
bool Read(Snapshot& now) {
    Ref service(module->FindService("fsl"));
    if (!Is(service.get(), contract.service)) return false;
    // Resolved on every tick: module->fight is bound once, at discovery.
    now.fight.reset(api.Field(service.get(), "drih"));
    if (!Is(now.fight.get(), contract.fight)) return false;
    now.history.reset(api.Field(now.fight.get(), "<dwaq>k__BackingField"));
    if (!Is(now.history.get(), contract.history)) return false;
    now.report.reset(api.Field(now.history.get(), "dwqq"));
    if (now.report.get() && !Is(now.report.get(), contract.report)) throw std::runtime_error("Unexpected fight report");
    now.character.reset(module->FindService("fng"));
    if (!Is(now.character.get(), contract.character)) return false;
    // zcx dereferences esg.dpzj (pinned at 0x128): no character while it is null.
    now.info.reset(api.Field(now.character.get(), "dpzj"));
    if (!now.info.get()) return false;
    now.localId = api.Value<int64_t>(api.Invoke(contract.localId, now.character.get()));
    return now.localId > 0;
}
// esg.dpzj.dbrm, the name the z(Int64, String, ...) constructor receives. It
// only picks the right Metamob quest when there are several: empty if unreadable.
std::string CharacterName(Obj info) {
    try {
        if (!Is(info, contract.characterInfo)) return {};
        Ref text(api.Field(info, "dbrm"));
        auto name = api.String(text.get());
        return Printable(name, MaxCharacterName) ? name : std::string{};
    } catch (...) { Error("name"); return {}; }
}
// The monster a soul holds: its static effect 4058 (live game data), else the
// table generated from the pinned client's MonsterData.souls. 0: unknown.
// A dungeon-room soul lists every boss of its room (11 type-327 souls in
// 3.7.12.12, such as 33746 "Âmes capturées : Tynrils" = 1087, 1072, 1085, 1086)
// yet belongs to one of them: the table's monster when listed, else the first
// effect (its dropMonsterIds in all 11). Only that monster counts, never the
// room's other bosses: they often belong to other Ocre steps.
int MonsterId(Obj item, int gid) {
    const auto row = std::lower_bound(chat_souls::SoulItems.begin(), chat_souls::SoulItems.end(), gid,
        [](const chat_souls::SoulItem& entry, int wanted) { return entry.item < wanted; });
    const int table = row != chat_souls::SoulItems.end() && row->item == gid ? row->monster : 0;
    int first = 0;
    bool several = false, listed = false;
    try {
        Ref data(api.Invoke(contract.data, item));
        Ref effects(Is(data.get(), contract.itemData) ? api.Field(data.get(), "possibleEffects") : nullptr);
        for (int i = 0, count = effects.get() ? Count(effects.get(), MaxEffects) : 0; i < count; ++i) {
            Ref effect(api.Call(effects.get(), "get_Item", {&i}));
            if (!effect.get() || Field<uint16_t>(effect.get(), "actionId") != SoulMonster ||
                !api.class_is_assignable_from(contract.integerEffect, api.object_get_class(effect.get()))) continue;
            const int monster = Field<int>(effect.get(), "value");
            if (monster <= 0) continue;
            if (!first) first = monster;
            else if (monster != first) several = true;
            listed = listed || monster == table;
        }
    } catch (...) { first = 0; several = listed = false; Error("effects"); }
    if (several && listed) return table; // a room soul: the table names its own monster
    return first ? first : table;        // else the live (first) effect wins over the table
}
void Add(std::vector<Soul>& souls, Obj item, uint32_t type) {
    const int gid = api.Value<int>(api.Invoke(contract.genericId, item));
    const int quantity = api.Value<int>(api.Invoke(contract.quantity, item));
    const bool valid = quantity >= 1 && quantity <= metamob::MaxQuantity;
    // The game already merges equal items of one fighter (fzk.bipt); merge again
    // so a soul is queued once, with its whole quantity.
    for (auto& soul : souls) {
        if (soul.gid != gid) continue;
        soul.valid = soul.valid && valid;
        if (soul.valid) soul.quantity += quantity;
        return;
    }
    souls.push_back(Soul{gid, type, quantity, valid, Ref(item)});
}
void Emit(const Soul& soul, const std::string& characterName, int64_t localId) {
    const char* skipped = "error";
    try {
        const bool boss = soul.type == BossSoul;
        if (!soul.valid || soul.quantity < 1 || soul.quantity > metamob::MaxQuantity) skipped = "quantity";
        else if (const int id = MonsterId(soul.item.get(), soul.gid); !id) skipped = "unknown";
        else {
            int64_t key = id;
            Ref monster(api.Invoke(contract.byId, nullptr, {&key}));
            if (!Is(monster.get(), contract.monster)) skipped = "monster";
            // A boss soul must hold a boss, an archimonster soul an archimonster.
            else if (!api.Value<bool>(api.Invoke(boss ? contract.isBoss : contract.isArch, monster.get()))) skipped = "kind";
            else {
                Ref text(api.Invoke(contract.name, monster.get()));
                auto name = api.String(text.get());
                if (!Printable(name, MaxMonsterName)) skipped = "name";
                else {
                    Log("CAPTURE_SOUL monster=" + std::to_string(id) + " kind=" + (boss ? "boss" : "arch") +
                        " qty=" + std::to_string(soul.quantity));
                    metamob::Capture capture;
                    capture.characterId = localId;
                    capture.characterName = characterName;
                    capture.monsterId = id;
                    capture.name = std::move(name);
                    capture.type = boss ? metamob::TypeBoss : metamob::TypeArchimonster;
                    capture.quantity = soul.quantity;
                    if (!deliver) { Error("sink"); return; }
                    try { deliver(std::move(capture)); } catch (...) { Error("sink"); }
                    return;
                }
            }
        }
    } catch (...) { skipped = "error"; }
    try { Log("CAPTURE_SOUL_SKIPPED gid=" + std::to_string(soul.gid) + " reason=" + skipped); } catch (...) {}
}
void Process(Obj report, Obj info, int64_t localId) {
    std::vector<Soul> souls;
    int entries = 0, local = 0, loot = 0, soulItems = 0;
    bool winner = false;
    try {
        Ref fighters(api.Field(report, "dwou"));
        entries = fighters.get() ? Count(fighters.get(), MaxFighters) : 0;
        Ref mine;
        for (int i = 0; i < entries; ++i) {
            Ref entry(api.Call(fighters.get(), "get_Item", {&i}));
            if (!entry.get()) continue;
            if (!Is(entry.get(), contract.fighter)) throw std::runtime_error("Unexpected fighter result");
            // Every client receives the whole party's loot: each one counts only
            // its own character, so a party capture is counted once.
            if (Field<int64_t>(entry.get(), "dwpw") != localId) continue;
            if (!local++) mine = std::move(entry);
        }
        if (mine.get()) {
            // Logged, not required: some fight kinds decide victory otherwise.
            winner = Field<int16_t>(mine.get(), "dwpy") == Victory;
            Ref gains(api.Field(mine.get(), "dwqb"));
            if (gains.get() && !Is(gains.get(), contract.loot)) throw std::runtime_error("Unexpected fighter loot");
            Ref items(gains.get() ? api.Field(gains.get(), "dwqv") : nullptr);
            loot = items.get() ? Count(items.get(), MaxLoot) : 0;
            for (int i = 0; i < loot; ++i) {
                Ref item(api.Call(items.get(), "get_Item", {&i}));
                if (!item.get() || !api.class_is_assignable_from(contract.item, api.object_get_class(item.get()))) continue;
                const auto type = api.Value<uint32_t>(api.Invoke(contract.itemType, item.get()));
                if (type != BossSoul && type != ArchSoul) continue;
                ++soulItems;
                Add(souls, item.get(), type);
            }
        }
    } catch (...) {
        // Already remembered: the souls of this report are lost, never counted twice.
        Error("report");
        return;
    }
    try {
        Log("CAPTURE_REPORT entries=" + std::to_string(entries) + " local=" + std::to_string(local) + " loot=" +
            std::to_string(loot) + " souls=" + std::to_string(soulItems) + " winner=" + (winner ? "1" : "0"));
    } catch (...) {}
    if (souls.empty()) return;
    const auto name = CharacterName(info);
    for (const auto& soul : souls) Emit(soul, name, localId);
}
void Observe(bool enabled) {
    Snapshot now;
    if (!Read(now)) { if (!enabled) state.known = false; return; }
    // The chat part's character: its name read again only when another character is seen.
    if (now.localId != state.localId) state.name = CharacterName(now.info.get());
    if (enabled && !state.fightLogged && module->fight.get() && now.fight.get() != module->fight.get()) {
        state.fightLogged = true;
        Log("CAPTURE_FIGHT_CHANGED");
    }
    // Baseline (first complete read, another report owner or character, sync
    // off): the report shown then is never processed, so neither a mod start, a
    // character switch nor a fight ended while the sync was off counts.
    const bool baseline = !enabled || !state.known || now.history.get() != state.history.get() || now.localId != state.localId;
    const Obj report = now.report.get();
    const bool fresh = report && !Seen(report);
    state.history = std::move(now.history);
    state.localId = now.localId;
    state.known = true;
    // Remembered before processing: an error inside never reprocesses it.
    Remember(std::move(now.report));
    if (!baseline && fresh) Process(report, now.info.get(), now.localId);
}
}
}

void InstallCaptureDetect(std::function<void(metamob::Capture)> sink) {
    using namespace capture_detect;
    const char* stage = "sink";
    try {
        deliver = std::move(sink);
        if (contract.installed) return;
        // Fields are read by name; their pinned offsets tie the names to this
        // client's layout, so a port that reshuffles them stops here.
        stage = "fight";
        Contract next;
        next.service = api.Class("Core.dll", "", "exf"); Layout(next.service, "drih", 0x18);
        next.fight = api.Class("Core.dll", "", "fxc"); Layout(next.fight, "<dwaq>k__BackingField", 0xC8);
        next.history = api.Class("Core.dll", "", "fzf"); Layout(next.history, "dwqq", 0x18);
        next.report = api.Class("Core.dll", "", "fyz"); Layout(next.report, "dwou", 0x30);
        next.fighter = api.Class("Core.dll", "", "fze");
        Layout(next.fighter, "dwpw", 0x10); Layout(next.fighter, "dwpy", 0x20); Layout(next.fighter, "dwqb", 0x28);
        next.loot = api.Class("Core.dll", "", "fzg"); Layout(next.loot, "dwqv", 0x10);
        stage = "character";
        next.character = api.Class("Core.dll", "", "esg"); Layout(next.character, "dpzj", 0x128);
        next.characterInfo = api.Class("Core.dll", "", "z"); Layout(next.characterInfo, "dbrm", 0x18);
        next.localId = api.FindVerified(next.character, "zcx", 0, 0x18BB140);
        stage = "item";
        // The item accessors and RVAs of soul-inventory.cpp.
        next.item = api.Class("Core.dll", "", "mc");
        next.genericId = api.FindVerified(next.item, "myn", 0, 0xF20D40);
        next.quantity = api.FindVerified(next.item, "myo", 0, 0x861E70);
        next.itemType = api.FindVerified(next.item, "mzb", 0, 0x2AAC720);
        next.data = api.FindVerified(next.item, "myx", 0, 0x2AAC6B0);
        next.itemData = api.Class(DataCenter, "Core.DataCenter.Metadata.Item", "ItemData");
        Layout(next.itemData, "possibleEffects", 0x68);
        Layout(api.Class(DataCenter, "Core.DataCenter.Metadata.Effect", "EffectInstanceData"), "actionId", 0x1A);
        next.integerEffect = api.Class(DataCenter, "Core.DataCenter.Metadata.Effect.Instance", "EffectInstanceInteger");
        Layout(next.integerEffect, "value", 0x78);
        stage = "monster";
        next.monster = api.Class(DataCenter, "Core.DataCenter.Metadata.Monster", "MonsterData");
        next.byId = api.FindVerified(next.monster, "GetById", 1, 0x7E76B0);
        next.isBoss = api.FindVerified(next.monster, "get_isBoss", 0, 0x7C43C0);
        next.isArch = api.FindVerified(next.monster, "get_isMiniBoss", 0, 0x7C43B0);
        next.name = api.FindVerified(next.monster, "get_name", 0, 0x7E7A30);
        next.installed = true;
        contract = next;
    } catch (...) {
        // Only this feature stays off; the rest of the mod keeps running.
        try { Log(std::string("CAPTURE_DETECT_DISABLED stage=") + stage); } catch (...) {}
    }
}
void TickCaptureDetect() {
    using namespace capture_detect;
    if (!contract.installed || !Active()) return;
    const bool enabled = module->settings.metamobSync;
    try {
        if (state.owner != module || state.compositor.get() != module->compositor.get()) {
            Reset();
            state.owner = module;
            state.compositor.reset(module->compositor.get());
        }
        Observe(enabled);
    } catch (...) {
        // Nothing reaches Module::Pump. Sync on: the state is kept, so a report
        // shown meanwhile is processed once reads work again. Sync off: the
        // baseline is forgotten and retaken at the next complete read.
        if (enabled) Error("read");
        else state.known = false;
    }
}
void RestoreCaptureDetect() { capture_detect::Reset(); }
std::optional<metamob::GameMonster> CaptureDetectMonster(int monsterId) {
    using namespace capture_detect;
    try {
        if (monsterId <= 0 || !contract.installed || !Active()) return std::nullopt;
        if (const auto known = state.monsters.find(monsterId); known != state.monsters.end()) return known->second;
        std::optional<metamob::GameMonster> found;
        try {
            int64_t key = monsterId;
            Ref monster(api.Invoke(contract.byId, nullptr, {&key}));
            if (Is(monster.get(), contract.monster)) {
                Ref text(api.Invoke(contract.name, monster.get()));
                auto name = api.String(text.get());
                if (Printable(name, MaxMonsterName)) {
                    // Metamob's types: a boss, an archimonster, else any monster.
                    const int type = api.Value<bool>(api.Invoke(contract.isBoss, monster.get())) ? metamob::TypeBoss :
                        api.Value<bool>(api.Invoke(contract.isArch, monster.get())) ? metamob::TypeArchimonster : metamob::TypeMonster;
                    found = metamob::GameMonster{std::move(name), type};
                }
            }
        } catch (...) { found.reset(); Error("lookup"); }
        // Unknown and unreadable monsters are kept too: never read again at every chat pump.
        if (state.monsters.size() >= MaxMonsters) state.monsters.clear();
        state.monsters.emplace(monsterId, found);
        return found;
    } catch (...) { return std::nullopt; }
}
std::string CaptureDetectCharacter() {
    try { return capture_detect::state.name; } catch (...) { return {}; }
}
