#include "souls/soul-helper.hpp"
#include "souls/soul-policy.hpp"
#include "souls/soul-combat.hpp"
#include "souls/soul-capture.hpp"
#include "souls/soul-spell.hpp"
#include "souls/soul-inventory.hpp"
#include "souls/soul-panel.hpp"
#include "souls/soul-layout.hpp"
#include "core/module.hpp"
#include <algorithm>

namespace soul_helper_detail {
using namespace soul;
using namespace managed;
struct State {
    Panel panel;
    PendingEquip pending;
    AutoEquipAttempts autoAttempts;
    Ref inventory;
    InventorySnapshot contents;
    CombatSnapshot shown;
    uint64_t nextInventory = 0;
    bool installed = false, failed = false, clickable = false, busy = false;
    std::string actionStatus;
} state;
bool Available() {
    return state.installed && !state.failed && module && module->ready.load() &&
        module->servicesReady && !module->stopped && !module->stopRequested.load() && module->settings.soulReminder;
}
void Hide() {
    state.clickable = false;
    state.shown = {};
    state.pending.Clear();
    state.contents = {};
    state.inventory.reset();
    state.nextInventory = 0;
    state.actionStatus.clear();
    state.panel.Hide();
    RestoreInventoryBridge();
    ResetCapture();
}
void HideModuleClicked() {
    if (!Available()) return;
    auto next = module->settings;
    next.soulReminder = false;
    module->Change(next);
    Hide();
    Log("SOUL_PANEL_HIDDEN source=header-menu saved=1");
}
void Fail(const std::exception& error) {
    state.failed = true;
    state.clickable = false;
    Log(std::string("SOUL_HELPER_DISABLED ") + error.what());
    try { Hide(); } catch (const std::exception& e) { Log(std::string("SOUL_PANEL_CLEANUP_ERROR ") + e.what()); }
}
std::string StoneLabel(const StoneType& stone) {
    return std::string(stone.name) + " (" + std::to_string(stone.power) + ")";
}
void RefreshInventory(uint64_t now) {
    Ref inventory(module->FindService("fmo"));
    if (inventory.get() == state.inventory.get() && now < state.nextInventory) return;
    state.inventory = std::move(inventory);
    state.contents = state.inventory.get() ? ReadInventory(state.inventory.get()) : InventorySnapshot{};
    state.nextInventory = now + (state.pending.until ? 200 : 600);
}
void ShowEquipment(PanelView& view, const StoneType* required) {
    view.equipmentChecked = state.contents.complete && required;
    if (!state.contents.complete) {
        view.equipped = "Lecture de l’équipement…";
        return;
    }
    view.equippedItem = state.contents.equippedItem ? state.contents.equippedItem->get() : nullptr;
    const auto* stone = StoneById(state.contents.equippedGenericId);
    if (stone) {
        view.equipped = (state.contents.equippedName.empty() ?
            StoneLabel(*stone) : state.contents.equippedName + " (" + std::to_string(stone->power) + ")");
    } else if (view.equippedItem) {
        view.equipped = state.contents.equippedName.empty() ? "Consommable équipé" : "Consommable équipé : " + state.contents.equippedName;
    } else {
        view.equipped = "Aucune pierre d’âme équipée";
    }
    view.equippedCorrect = required && state.contents.equippedGenericId == required->genericId;
}
void PositionChanged(int x, int y, uint64_t revision) {
    if (!Available()) return;
    try {
        Ref root(soul::GetUiRoot());
        // The bridge revalidates the native layout and root against the
        // revision captured before the drag, including switches between ticks.
        soul::SaveLayoutPosition(root.get(), x, y, revision);
    } catch (const std::exception& e) { Log(std::string("SOUL_POSITION_SAVE_ERROR ") + e.what()); }
}
void TryEquip(bool automatic) {
    if (!Available() || !state.clickable || state.busy || (automatic && !module->settings.soulAutoEquip)) return;
    state.busy = true;
    struct ClearBusy { ~ClearBusy() { state.busy = false; } } clear;
    try {
        const auto now = GetTickCount64();
        const auto fresh = ReadCombat(module->fight.get(), module->cached.get());
        const auto* required = RequiredStone(fresh.highestLevel);
        if (fresh.state != CombatState::Ready || fresh.fightIdentity != state.shown.fightIdentity ||
            fresh.highestLevel != state.shown.highestLevel || !required) {
            state.clickable = false; state.nextInventory = 0; return;
        }
        if (automatic && !state.autoAttempts.Allowed(fresh.fightIdentity, required->genericId)) return;
        Ref inventory(module->FindService("fmo"));
        if (!inventory.get() || inventory.get() != state.inventory.get()) { state.clickable = false; return; }
        const auto contents = ReadInventory(inventory.get());
        const auto selected = SelectStone(*required, contents);
        if (!contents.complete || contents.equipmentPending || selected.equipped || !selected.uid ||
            state.pending.Active(fresh.fightIdentity, required->genericId, selected.equipped, now)) return;
        const auto confirmed = ReadCombat(module->fight.get(), module->cached.get());
        if (!Available() || (automatic && !module->settings.soulAutoEquip) || confirmed.state != CombatState::Ready || confirmed.fightIdentity != fresh.fightIdentity ||
            confirmed.highestLevel != fresh.highestLevel) { state.clickable = false; return; }
        // Commit both gates BEFORE dispatch: native events may reenter the UI.
        state.autoAttempts.Mark(fresh.fightIdentity, required->genericId);
        state.clickable = false;
        state.pending.Start(fresh.fightIdentity, required->genericId, now);
        state.actionStatus = "Demande envoyée…";
        const bool sent = EquipExactStone(inventory.get(), selected.uid, required->genericId);
        if (automatic) Log("SOUL_AUTO_EQUIP power=" + std::to_string(required->power) +
            " result=" + (sent ? "sent" : "refused"));
        if (!sent) {
            state.pending.Clear();
            state.actionStatus = "Équipement refusé ou indisponible.";
        }
        state.nextInventory = 0;
    } catch (const std::exception& e) { Fail(e); }
}
void EquipClicked() { TryEquip(false); }
}

void InstallSoulHelper() {
    using namespace soul_helper_detail;
    try {
        soul::InstallCombat();
        soul::InstallCapture();
        soul::InstallSpell();
        soul::InstallLayout();
        state.panel.Install(EquipClicked, PositionChanged, HideModuleClicked);
        state.installed = true;
    } catch (const std::exception& e) { Fail(e); }
}
void TickSoulHelper() {
    using namespace soul_helper_detail;
    if (state.busy) return;
    try {
        if (!Available()) { Hide(); return; }
        const auto combat = soul::ReadCombat(module->fight.get(), module->cached.get());
        if (combat.state == soul::CombatState::Hidden) { Hide(); return; }
        if (combat.fightIdentity != state.shown.fightIdentity || combat.highestLevel != state.shown.highestLevel || combat.state != state.shown.state) {
            state.pending.Clear(); state.actionStatus.clear(); state.nextInventory = 0;
        }
        state.clickable = false;
        state.shown = combat;
        soul::PanelView view;
        Ref captureSpell;
        view.showEquip = combat.state == soul::CombatState::Ready;
        const auto* required = soul::RequiredStone(combat.highestLevel);
        if (combat.state == soul::CombatState::Fighting) {
            state.pending.Clear(); state.actionStatus.clear();
            RefreshInventory(GetTickCount64());
            captureSpell.reset(soul::CaptureSpell(view.castEnabled));
            view.captureSpell = captureSpell.get();
            view.showEquipment = false;
            view.equipmentChecked = false;
            view.showEquip = false;
            switch (soul::ReadCapture(module->fight.get(), module->cached.get())) {
            case soul::CaptureState::Active:
                view.status = "Capture d’âme active";
                view.statusTone = soul::StatusTone::Good;
                break;
            case soul::CaptureState::Missing:
                view.status = "Capture d’âme absente";
                view.statusTone = soul::StatusTone::Warning;
                break;
            default:
                view.status = "État de capture indisponible";
                break;
            }
            if (!required) {
                view.status = "Pierre requise indéterminée";
                view.statusTone = soul::StatusTone::Neutral;
            } else if (!state.contents.complete) {
                view.status = "Vérification de la pierre équipée…";
                view.statusTone = soul::StatusTone::Neutral;
            } else if (state.contents.equippedGenericId != required->genericId) {
                view.status = "Pierre d’âme " + std::to_string(required->power) + " requise — " +
                    (soul::StoneById(state.contents.equippedGenericId) ? "mauvaise pierre équipée" : "aucune pierre adaptée équipée");
                view.statusTone = soul::StatusTone::Warning;
            }
        } else if (combat.state == soul::CombatState::Pending) {
            view.requirement = "Lecture des données de combat…";
        } else if (combat.state != soul::CombatState::Ready || !required) {
            view.requirement = "Capture spéciale ou groupe non pris en charge.";
        } else {
            view.requirement = "Requise : " + std::to_string(required->power) + " · Monstre : niv. " + std::to_string(combat.highestLevel);
            const auto now = GetTickCount64();
            RefreshInventory(now);
            ShowEquipment(view, required);
            if (!state.contents.complete) {
                view.stock = "Lecture de l’inventaire…";
            } else {
                const auto selected = soul::SelectStone(*required, state.contents);
                view.stock = "En sac : " + std::to_string(selected.bagCount);
                const bool waiting = state.pending.Active(combat.fightIdentity, required->genericId, selected.equipped, now);
                view.equipEnabled = selected.uid && !selected.equipped && !waiting && !state.contents.equipmentPending;
                view.showEquip = selected.uid && !selected.equipped;
                if (selected.equipped) { view.status.clear(); state.actionStatus.clear(); }
                else if (waiting) view.status = "Équipement en cours…";
                else if (!state.actionStatus.empty()) view.status = state.actionStatus == "Demande envoyée…" ?
                    "Changement non confirmé. Vérifie le message du jeu." : state.actionStatus;
                else if (!selected.bagCount) view.status = "Aucune pierre adaptée en sac";
                else if (!selected.uid) view.status = state.contents.equipmentPending ?
                    "Équipement en cours…" : "Conditions d’équipement non remplies";
                else view.status.clear(); // The current stone is already red and the equip action is visible.
            }
        }
        Ref root(soul::GetUiRoot());
        if (!root.get()) { Hide(); return; }
        const auto layout = soul::ReadLayout(root.get());
        state.panel.Render(root.get(), view, layout.x, layout.y, layout.revision);
        state.clickable = view.equipEnabled;
        if (module->settings.soulAutoEquip && combat.state == soul::CombatState::Ready && required) {
            // Do not fight a later manual equipment change if this tier was already satisfied.
            if (view.equippedCorrect) state.autoAttempts.Mark(combat.fightIdentity, required->genericId);
            else if (state.autoAttempts.Allowed(combat.fightIdentity, required->genericId)) TryEquip(true);
        }
    } catch (const std::exception& e) { Fail(e); }
}
void RestoreSoulHelper() {
    using namespace soul_helper_detail;
    state.clickable = false;
    try { Hide(); } catch (const std::exception& e) { Log(std::string("SOUL_RESTORE_ERROR ") + e.what()); }
    soul::RestoreInventoryBridge();
    state.autoAttempts = {};
    soul::ResetCombat();
    soul::ResetCapture();
    soul::ResetLayout();
}
bool SoulHelperCleanupPending() { return soul::PanelCleanupPending(); }
// The game left the world (logout, inactivity, character selection): the panel
// goes with the game's windows, its fight being over. The option stays on and
// the position stays in the game's layout: the next fight shows it again.
void HideSoulHelper() {
    using namespace soul_helper_detail;
    if (!state.installed || state.busy) return;
    if (state.shown.state == soul::CombatState::Hidden && !soul::PanelCleanupPending()) return;
    try { Hide(); } catch (const std::exception& e) { Log(std::string("SOUL_PANEL_CLEANUP_ERROR ") + e.what()); }
}
