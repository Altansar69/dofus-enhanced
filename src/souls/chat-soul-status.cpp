#include "souls/chat-soul-status.hpp"
#include "souls/chat-soul-data.hpp"
#include "core/module.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <map>
#include <set>

namespace chat_souls {
using namespace managed;
enum class Progress { Unknown, Missing, Done, NotStarted };
struct State {
    Module* owner = nullptr;
    Ref cached, compositor, service, inventory, content, iterator, quests;
    Klass itemClass = nullptr, maskClass = nullptr, integerClass = nullptr, questClass = nullptr, progressClass = nullptr;
    std::set<int> owned, scanning;
    std::map<int, Progress> progress;
    std::string questReason;
    int count = 0, seen = 0, version = 0;
    bool installed = false, inventoryKnown = false;
    uint64_t nextScan = 0, inventoryAt = 0, nextQuest = 0, nextLog = 0;
} state;
// What each client (SoulClient order) last asked for, and when: kept across
// reader resets, so one client giving up a reader never drops another's need.
struct Need { bool inventory = false, quest = false; uint64_t at = 0; };
std::array<Need, 2> needs;
// The first client (SoulClient order) that needs a reader and asked within
// this delay leads: only its calls advance the readers, once per pump.
constexpr uint64_t LeadMs = 1000;
uint64_t revision = 1;
bool Active() {
    return module && module->ready.load() && module->servicesReady && !module->stopped && !module->stopRequested.load();
}
bool SameSession() {
    return Active() && state.owner == module && state.cached.get() == module->cached.get() &&
        state.compositor.get() == module->compositor.get();
}
bool Instance(Obj object, Klass type) { return object && api.object_get_class(object) == type; }
template<class T> T Field(Obj object, const char* name) { Ref value(api.Field(object,name)); return api.Value<T>(value.get()); }
int Count(Obj object, int maximum) {
    const int count = api.Value<int>(api.Call(object,"get_Count"));
    if (count < 0 || count > maximum) throw std::runtime_error("Chat soul collection outside bounds");
    return count;
}
void CancelScan() {
    Ref iterator = std::move(state.iterator);
    state.content.reset(); state.inventory.reset(); state.scanning.clear(); state.count = state.seen = 0;
    if (iterator.get()) api.Call(iterator.get(),"Dispose");
}
void Error(const char* area, const std::exception& e) {
    const auto now = GetTickCount64();
    if (now >= state.nextLog) { Log(std::string("CHAT_SOUL_STATUS ") + area + ": " + e.what()); state.nextLog = now + 10000; }
}
void QuestReason(const std::string& reason) {
    if (state.questReason == reason) return;
    state.questReason = reason;
    Log("CHAT_SOUL_QUEST " + reason);
}
void Install() {
    if (state.installed) return;
    auto inventoryService = api.Class("Core.dll","","feh");
    api.FindVerified(inventoryService,"bdoq",0,0x11EDC80);
    state.itemClass = api.Class("Core.dll","","mc");
    state.maskClass = api.Class("Core.dll","Core.UILogic.Inventory","ItemMasks");
    state.integerClass = api.Class("Ankama.Dofus.Core.DataCenter.dll","Core.DataCenter.Metadata.Effect.Instance","EffectInstanceInteger");
    api.FindVerified(state.itemClass,"myn",0,0xF20D40);
    api.FindVerified(state.itemClass,"myo",0,0x861E70);
    api.FindVerified(state.itemClass,"nag",0,0x2AACF80);
    api.FindVerified(state.itemClass,"mzb",0,0x2AAC720);
    api.FindVerified(state.itemClass,"myt",0,0x2AAC3B0);
    state.questClass = api.Class("Core.dll","","fdi");
    state.progressClass = api.Class("Core.dll","","fcz");
    api.FindVerified(state.questClass,"bczk",0,0x9566A0); // Completed quest IDs, short.
    api.FindVerified(state.questClass,"bczg",0,0x74B820); // Current per-quest progress.
    state.installed = true;
}
void ReadItem(Obj mask) {
    if (!Instance(mask,state.maskClass)) throw std::runtime_error("Invalid chat inventory entry");
    Ref item(api.Field(mask,"item"));
    if (!Instance(item.get(),state.itemClass)) return;
    const int position = api.Value<short>(api.Call(item.get(),"nag"));
    if ((position != 63 && position != 31) || api.Value<int>(api.Call(item.get(),"myo")) <= 0) return;
    const int id = api.Value<int>(api.Call(item.get(),"myn"));
    const auto found = std::lower_bound(SoulItems.begin(),SoulItems.end(),id,
        [](const SoulItem& row,int wanted) { return row.item < wanted; });
    if (found != SoulItems.end() && found->item == id) { state.scanning.insert(found->monster); return; }
    const auto type = api.Value<uint32_t>(api.Call(item.get(),"mzb"));
    if (type != 85 && type != 220) return; // Legacy full soul stones, ordinary/heroic.
    Ref effects(api.Call(item.get(),"myt"));
    if (!effects.get()) throw std::runtime_error("Full soul stone effects unavailable");
    for (int i = 0, count = Count(effects.get(),128); i < count; ++i) {
        Ref effect(api.Call(effects.get(),"get_Item",{&i}));
        if (!effect.get()) throw std::runtime_error("Missing soul stone effect");
        // EffectInstanceData has no effectId: the effect kind is actionId (UInt16),
        // 623 = CharacterSummonMonsters, one per monster in a legacy full stone.
        if (Field<uint16_t>(effect.get(),"actionId") != 623) continue;
        if (!api.class_is_assignable_from(state.integerClass,api.object_get_class(effect.get())))
            throw std::runtime_error("Unsupported soul identity effect");
        const int monster = Field<int>(effect.get(),"value"); // Effect623 parameter2 (#3) is the monster ID.
        if (monster <= 0) throw std::runtime_error("Invalid captured monster ID");
        state.scanning.insert(monster);
    }
}
void ScanInventory(uint64_t now) {
    Ref service(module->FindService("fmo"));
    if (service.get() != state.service.get()) {
        CancelScan(); state.service = std::move(service); state.owned.clear(); state.inventoryKnown = false; state.nextScan = 0;
    }
    if (!state.service.get()) return;
    if (!state.iterator.get()) {
        if (now < state.nextScan) return;
        if (api.object_get_class(state.service.get()) != api.Class("Core.dll","","feh"))
            throw std::runtime_error("Unsupported chat inventory service");
        state.inventory.reset(api.Call(state.service.get(),"bdoq"));
        state.content.reset(api.Call(state.inventory.get(),"get_content"));
        state.count = Count(state.content.get(),32768);
        state.version = Field<int>(state.content.get(),"_version");
        Ref values(api.Call(state.content.get(),"get_Values"));
        state.iterator.reset(api.Call(values.get(),"GetEnumerator"));
        if (!state.iterator.get()) throw std::runtime_error("Missing inventory iterator");
    }
    const auto started = std::chrono::steady_clock::now();
    for (int batch = 0; batch < 128; ++batch) {
        if (Field<int>(state.content.get(),"_version") != state.version)
            throw std::runtime_error("Inventory changed during chat scan");
        if (!api.Value<bool>(api.Call(state.iterator.get(),"MoveNext"))) {
            if (state.seen != state.count || Count(state.content.get(),32768) != state.count || !SameSession() ||
                module->FindService("fmo") != state.service.get() ||
                api.Call(state.service.get(),"bdoq") != state.inventory.get() ||
                api.Call(state.inventory.get(),"get_content") != state.content.get())
                throw std::runtime_error("Inventory owner changed during chat scan");
            if (!state.inventoryKnown || state.owned != state.scanning) ++revision;
            state.owned = std::move(state.scanning);
            CancelScan(); state.inventoryKnown = true; state.inventoryAt = now; state.nextScan = now + 2000;
            return;
        }
        if (++state.seen > state.count) throw std::runtime_error("Inventory grew during chat scan");
        Ref mask(api.Call(state.iterator.get(),"get_Current")); ReadItem(mask.get());
        if (std::chrono::steady_clock::now()-started >= std::chrono::milliseconds(2)) break;
    }
}
void ReadQuest(uint64_t now) {
    Ref service(module->FindService("fnp"));
    if (service.get() != state.quests.get()) { state.quests = std::move(service); state.progress.clear(); state.nextQuest = 0; }
    if (!state.quests.get()) { QuestReason("service-unavailable"); return; }
    if (now < state.nextQuest) return;
    state.nextQuest = now + 1000;
    if (!Instance(state.quests.get(),state.questClass)) throw std::runtime_error("Unsupported quest service");
    Ref finished(api.Call(state.quests.get(),"bczk"));
    short questId = 439;
    const bool complete = api.Value<bool>(api.Call(finished.get(),"Contains",{&questId}));
    std::map<int,Progress> progress;
    std::string reason = "progress-unavailable";
    if (complete) {
        for (const auto& goal : OcreGoals) progress[goal.monster] = Progress::Done;
        reason = "completed";
    } else {
        Ref active(api.Call(state.quests.get(),"bczg")); Obj raw = nullptr;
        const bool found = api.Value<bool>(api.Call(active.get(),"TryGetValue",{&questId,&raw}));
        Ref current(raw);
        // On this pinned client, fde.bcyw replaces the completed-quest list,
        // then always AddRange's the server snapshot, including an empty one.
        // List.InsertRange increments _version even for zero entries. The
        // constructor leaves it at zero: absence alone is not enough evidence.
        if (!found && Field<int>(finished.get(),"_version") > 0) {
            for (const auto& goal : OcreGoals) progress[goal.monster] = Progress::NotStarted;
            reason = "not-started snapshot=received";
        } else if (!found) reason = "snapshot-unavailable";
        if (found && Instance(current.get(),state.progressClass) && Field<short>(current.get(),"dtbr") == questId) {
            const auto step = Field<short>(current.get(),"dtbs");
            const auto stepIndex = std::find(OcreSteps.begin(),OcreSteps.end(),step);
            Ref objectives(api.Field(current.get(),"dtbt"));
            if (stepIndex != OcreSteps.end()) reason = "in-progress step=" + std::to_string(step);
            if (stepIndex != OcreSteps.end()) for (const auto& goal : OcreGoals) {
                const auto goalIndex = std::find(OcreSteps.begin(),OcreSteps.end(),goal.step);
                if (goalIndex < stepIndex) progress[goal.monster] = Progress::Done;
                else if (goalIndex > stepIndex) progress[goal.monster] = Progress::Missing;
                else if (objectives.get()) {
                    short objective = static_cast<short>(goal.objective); bool done = false;
                    if (api.Value<bool>(api.Call(objectives.get(),"TryGetValue",{&objective,&done})))
                        progress[goal.monster] = done ? Progress::Done : Progress::Missing;
                }
            }
            if (Field<short>(current.get(),"dtbs") != step || api.Field(current.get(),"dtbt") != objectives.get())
                throw std::runtime_error("Quest changed during chat read");
        }
        if (api.Call(state.quests.get(),"bczg") != active.get()) throw std::runtime_error("Quest list replaced during chat read");
    }
    if (!SameSession() || module->FindService("fnp") != state.quests.get() ||
        api.Call(state.quests.get(),"bczk") != finished.get()) throw std::runtime_error("Quest owner changed");
    if (state.progress != progress) ++revision;
    state.progress = std::move(progress);
    QuestReason(reason);
}
// A hidden status reads nothing; shown again, it starts from unknown.
void ForgetInventory() {
    try { CancelScan(); } catch (const std::exception& e) { Error("cleanup",e); }
    if (state.inventoryKnown || !state.owned.empty()) ++revision;
    state.service.reset(); state.owned.clear(); state.inventoryKnown = false; state.nextScan = 0;
}
void ForgetQuest() { if (!state.progress.empty()) ++revision; state.quests.reset(); state.progress.clear(); state.nextQuest = 0; }
// Every reader root released; the clients' needs stay.
void ResetReaders() {
    try { CancelScan(); } catch (const std::exception& e) { Error("cleanup",e); }
    if (state.inventoryKnown || !state.progress.empty()) ++revision;
    state = {};
}
SoulState Public(Progress progress) {
    switch (progress) {
    case Progress::Missing: return SoulState::Missing;
    case Progress::Done: return SoulState::Done;
    case Progress::NotStarted: return SoulState::NotStarted;
    default: return SoulState::Unknown;
    }
}
const Goal* FindGoal(int monster) {
    const auto it = std::lower_bound(OcreGoals.begin(),OcreGoals.end(),monster,
        [](const Goal& row,int id) { return row.monster < id; });
    return it != OcreGoals.end() && it->monster == monster ? &*it : nullptr;
}
// The last complete scan stays valid while later scans are interrupted (the
// marketplace refreshes the inventory content faster than a scan completes);
// it only expires after a long silence, so a stuck scanner still reads "?".
constexpr uint64_t InventoryValidityMs = 90000;
Progress Inventory(int monster) {
    if (!SameSession() || !state.inventoryKnown || GetTickCount64() - state.inventoryAt > InventoryValidityMs) return Progress::Unknown;
    return state.owned.contains(monster) ? Progress::Done : Progress::Missing;
}
Progress Quest(int monster) {
    const auto found = state.progress.find(monster);
    return SameSession() && found != state.progress.end() ? found->second : Progress::Unknown;
}
}
void WantSoulStatus(SoulClient client, bool inventory, bool quest) {
    using namespace chat_souls;
    const auto index = static_cast<size_t>(client);
    if (index >= needs.size()) return;
    const auto now = GetTickCount64();
    needs[index] = {inventory, quest, now};
    inventory = quest = false;
    for (const auto& need : needs) { inventory = inventory || need.inventory; quest = quest || need.quest; }
    if ((!inventory && !quest) || !Active()) { ResetReaders(); return; }
    if (!SameSession()) {
        ResetReaders(); state.owner = module; state.cached.reset(module->cached.get()); state.compositor.reset(module->compositor.get());
    }
    // A reader nobody needs any more is forgotten at once, whoever calls.
    if (!inventory) ForgetInventory();
    if (!quest) ForgetQuest();
    // One read step per pump: a client needing nothing, or asking after a
    // fresher earlier one, only updates its needs (each client asks once per pump).
    if (!needs[index].inventory && !needs[index].quest) return;
    for (size_t i = 0; i < index; ++i)
        if ((needs[i].inventory || needs[i].quest) && now - needs[i].at < LeadMs) return;
    try { Install(); } catch (const std::exception& e) { Error("metadata",e); return; }
    if (inventory) try { ScanInventory(now); } catch (const std::exception& e) {
        // An interrupted scan (content changed, owner replaced) keeps the last
        // complete result and retries soon; only a service change forgets it.
        state.nextScan = now + 1000;
        try { CancelScan(); } catch (...) {} Error("inventory",e);
    }
    if (quest) try { ReadQuest(now); } catch (const std::exception& e) {
        if (!state.progress.empty()) ++revision;
        state.progress.clear(); QuestReason("read-failed"); Error("quest",e);
    }
}
void RestoreChatSoulStatus() {
    using namespace chat_souls;
    ResetReaders();
    needs = {};
}
SoulState OcreProgress(int monster) { using namespace chat_souls; return Public(Quest(monster)); }
SoulState BagProgress(int monster) { using namespace chat_souls; return Public(Inventory(monster)); }
uint64_t SoulStatusRevision() { return chat_souls::revision; }
std::string ChatSoulCaption(int monster, ChatSoulPart part, bool colorStatus) {
    using namespace chat_souls;
    if (monster <= 0 || !FindGoal(monster)) return {};
    if (part == ChatSoulPart::Inventory) switch (Inventory(monster)) {
    case Progress::Done:
        return colorStatus ? "<color=#8DDB91>Âme en sac</color>" : "Âme en sac";
    case Progress::Missing:
        return colorStatus ? "<color=#F18C8C>Pas en sac</color>" : "Pas en sac";
    default: return "Sac : ?";
    }
    switch (Quest(monster)) {
    case Progress::Done:
        return colorStatus ? "<color=#8DDB91>Ocre validé</color>" : "Ocre validé";
    case Progress::Missing:
        return colorStatus ? "<color=#F18C8C>Ocre à faire</color>" : "Ocre à faire";
    case Progress::NotStarted:
        return colorStatus ? "<color=#F18C8C>Ocre non commencée</color>" : "Ocre non commencée";
    default: return "Ocre : ?";
    }
}
std::string ChatSoulDetail(int monster, ChatSoulPart part) {
    using namespace chat_souls;
    if (monster <= 0 || !FindGoal(monster)) return {};
    const auto title = ChatSoulCaption(monster, part) + ".\n";
    if (part == ChatSoulPart::Inventory) {
        std::string detail;
        switch (Inventory(monster)) {
        case Progress::Done: detail = "Une âme de ce monstre est présente dans le sac de ce personnage. "; break;
        case Progress::Missing: detail = "Aucune âme de ce monstre n’a été trouvée dans le sac de ce personnage. "; break;
        default: detail = "Le contenu du sac n’est pas encore disponible ou doit être relu. "; break;
        }
        return title + detail + "La banque, le coffre et les autres personnages ne sont pas consultés.";
    }
    switch (Quest(monster)) {
    case Progress::Done: return title + "L’objectif de ce monstre a déjà été validé dans L’éternelle moisson.";
    case Progress::Missing: return title + "L’objectif de ce monstre reste à valider dans L’éternelle moisson.";
    case Progress::NotStarted: return title + "L’éternelle moisson n’est ni en cours ni terminée dans les données reçues pour ce personnage. Elle doit être commencée pour figurer dans le journal.";
    default: return title + "La progression de L’éternelle moisson n’est pas disponible. Ouvre le journal de quêtes pour la charger si nécessaire.";
    }
}
