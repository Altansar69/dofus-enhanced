#include "souls/soul-helper.hpp"
#include "souls/soul-combat.hpp"
#include "souls/soul-capture.hpp"
#include "souls/soul-inventory.hpp"
#include "souls/soul-panel.hpp"
#include "souls/soul-layout.hpp"
#include "core/module.hpp"
#include "appearance/features.hpp"
#include "ui/options-ui.hpp"
#include <iostream>
#include <unordered_map>

namespace fake {
using namespace managed;
Obj inventory = reinterpret_cast<Obj>(0x100), root = reinterpret_cast<Obj>(0x200);
soul::CombatSnapshot combat{soul::CombatState::Ready, 1, 51};
soul::InventorySnapshot contents{true, 9688, {{3,9687,2,true}}};
soul::CaptureState captureState = soul::CaptureState::Missing;
Obj knownSpell = nullptr;
soul::PanelView shown;
void (*click)() = nullptr;
void (*hideModule)() = nullptr;
void (*position)(int,int,uint64_t) = nullptr;
int requests = 0, renders = 0, hides = 0, saves = 0, inventoryReads = 0, captureReads = 0;
int layoutWrites = 0, renderedX = 0, renderedY = 0;
uint64_t renderedRevision = 0;
soul::LayoutPosition layout{24,180,1,true};
bool accepted = true, endDuringRead = false, failRender = false, reenter = false;
uint64_t expectedUid = 3;
int expectedId = 9687;
std::unordered_map<GCHandle,Obj> handles;
GCHandle serial = 100;
void Check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
}
namespace managed { Api api{}; uintptr_t gameBase = 0; }
Module* module = nullptr;
Module::Module(std::filesystem::path path) : folder(path), settingsPath(path / "settings.ini") {}
managed::Obj Module::FindService(const char*) const { return fake::inventory; }
void Module::Change(Settings next,bool save) { settings=next;if(save)settings.Save(settingsPath); }
void Settings::Save(const std::filesystem::path&, bool) const { ++fake::saves; }
void Log(const std::string&) {}
namespace soul {
void InstallCombat() {}
void ResetCombat() {}
void InstallCapture() {}
void ResetCapture() {}
CaptureState ReadCapture(managed::Obj,managed::Obj) { ++fake::captureReads; return fake::captureState; }
CombatSnapshot ReadCombat(managed::Obj,managed::Obj) { return fake::combat; }
InventorySnapshot ReadInventory(managed::Obj) {
    ++fake::inventoryReads;
    if (fake::endDuringRead) fake::combat.state = CombatState::Fighting;
    return fake::contents;
}
bool EquipExactStone(managed::Obj, uint64_t uid, int id) {
    fake::Check(uid == fake::expectedUid && id == fake::expectedId, "Wrong item dispatched");
    ++fake::requests;
    if (fake::reenter) { TickSoulHelper(); fake::click(); }
    return fake::accepted;
}
void RestoreInventoryBridge() {}
managed::Obj GetUiRoot() { return fake::root; }
void InstallLayout() {}
void ResetLayout() {}
LayoutPosition ReadLayout(managed::Obj) { return fake::layout; }
bool SaveLayoutPosition(managed::Obj root,int x,int y,uint64_t revision) {
    if (root!=fake::root || revision!=fake::layout.revision || !fake::layout.writable) return false;
    ++fake::layoutWrites;
    fake::layout.x=x; fake::layout.y=y; ++fake::layout.revision;
    return true;
}
void Panel::Install(void (*equip)(), void (*moved)(int,int,uint64_t),void (*hide)()) { fake::click = equip; fake::position = moved; fake::hideModule=hide; }
void Panel::Render(managed::Obj,const PanelView& value,int x,int y,uint64_t revision) {
    if (fake::failRender) throw std::runtime_error("panel fixture failure");
    fake::shown = value; ++fake::renders;
    fake::renderedX=x; fake::renderedY=y; fake::renderedRevision=revision;
}
void Panel::Hide() { ++fake::hides; }
bool PanelCleanupPending() { return false; }
}
int main() {
    using namespace fake;
    try {
        managed::api.gchandle_new = [](Obj o,bool) { handles[++serial]=o; return serial; };
        managed::api.gchandle_get_target = [](GCHandle h) { return handles.at(h); };
        managed::api.gchandle_free = [](GCHandle h) { Check(handles.erase(h)==1,"Invalid GC ref"); };
        Module fixture(L"offline-only"); module = &fixture;
        fixture.ready = true; fixture.servicesReady = true;
        fixture.settings.soulAutoEquip = false; // Preserve all manual-path regressions below.
        const auto oldStone = reinterpret_cast<Obj>(0x310);
        const auto newStone = reinterpret_cast<Obj>(0x320);
        contents.equippedItem = std::make_shared<Ref>(oldStone);
        contents.equippedName = "Grande pierre d’âme parfaite";
        InstallSoulHelper();
        TickSoulHelper();
        Check(requests==0 && shown.equipEnabled && !shown.equippedCorrect,"Tick must display wrong150 and offer100 without equipping");
        Check(shown.requirement.find("100")!=std::string::npos && shown.stock.find("2")!=std::string::npos,"Requirement/stock missing");
        Check(shown.equippedItem == oldStone && shown.equipped.find(contents.equippedName) != std::string::npos,
            "Panel must receive actual item and native localized name");
        const auto beforeIneligibleRenders = renders, beforeIneligibleReads = inventoryReads, beforeIneligibleHides = hides;
        combat.state = soul::CombatState::Hidden; // Capture forbidden since the last render.
        click();
        Check(requests == 0, "A stale equip button ignored newly forbidden capture");
        TickSoulHelper();
        Check(hides > beforeIneligibleHides && renders == beforeIneligibleRenders && inventoryReads == beforeIneligibleReads,
            "Noncapturable placement must hide the popup without inventory reads");
        combat.state = soul::CombatState::Ready;
        TickSoulHelper();
        click(); click();
        Check(requests==1,"Duplicate click must not dispatch twice");
        TickSoulHelper();
        Check(!shown.equipEnabled && !shown.equippedCorrect,"Request must await actual equipment");
        // Actual server confirmation in the SAME fight must replace the old item.
        contents.equippedGenericId=9687;
        contents.equippedItem = std::make_shared<Ref>(newStone);
        contents.equippedName = "Moyenne pierre d’âme parfaite";
        Sleep(230); // Cross the pending-confirmation refresh deadline, offline only.
        TickSoulHelper();
        Check(shown.equippedCorrect && !shown.equipEnabled,"Observed equipped stone must mark green and disable action");
        Check(shown.equippedItem == newStone && shown.equipped.find(contents.equippedName) != std::string::npos &&
            shown.status.empty() && !shown.showEquip,
            "Same-fight equipment confirmation must update name/icon and remove redundant status/action");
        contents.equippedGenericId=0; contents.equippedItem.reset(); contents.equippedName.clear();
        ++combat.fightIdentity; TickSoulHelper();
        Check(!shown.equippedItem && shown.equipped.find("Aucune pierre") != std::string::npos,
            "Removed equipment must clear item and old name");
        combat.highestLevel=101; click();
        Check(requests==1,"Group change after render must reject stale click");
        combat.highestLevel=51; TickSoulHelper();
        contents.bag.clear(); click();
        Check(requests==1,"Depleted inventory after render must reject click");
        contents.bag={{3,9687,2,true}}; ++combat.fightIdentity; TickSoulHelper();
        endDuringRead=true; click(); endDuringRead=false;
        Check(requests==1,"Placement ending during read must not dispatch");
        knownSpell=reinterpret_cast<Obj>(0x903);
        const auto readsBeforeFighting=inventoryReads;
        TickSoulHelper();
        Check(!shown.showEquipment && !shown.showEquip && !shown.equipEnabled && shown.statusTone == soul::StatusTone::Warning,
            "Combat start must keep panel, show missing effect in red and hide equipment");
        Check(shown.captureSpell==knownSpell,"Learned capture icon must be passed to the panel before the effect is present");
        contents.equippedItem = std::make_shared<Ref>(newStone);
        contents.equippedGenericId = 9687; contents.equippedName = "Moyenne pierre d’âme parfaite";
        inventory = reinterpret_cast<Obj>(0x101);
        TickSoulHelper();
        Check(!shown.equippedItem && !shown.equipmentChecked && !shown.equipEnabled && !shown.showEquipment &&
            shown.equipped.empty() && shown.stock.empty() && shown.requirement.empty() && inventoryReads>readsBeforeFighting,
            "Active combat checks equipment while keeping its presentation compact");
        click(); Check(requests==1,"Capture display must never equip");
        captureState=soul::CaptureState::Active; TickSoulHelper();
        Check(shown.statusTone==soul::StatusTone::Good && !shown.showEquip,"Actual capture effect must turn green");
        Check(shown.captureSpell==knownSpell,"Active effect must keep the same learned spell icon");
        captureState=soul::CaptureState::Missing; TickSoulHelper();
        Check(shown.statusTone==soul::StatusTone::Warning,"Removed capture effect must return to red");
        captureState=soul::CaptureState::Unknown; TickSoulHelper();
        Check(shown.statusTone==soul::StatusTone::Neutral && !shown.equippedCorrect,"Unknown effect must clear previous positive state");
        captureState=soul::CaptureState::Active;
        contents.complete=false; inventory=reinterpret_cast<Obj>(0x102); TickSoulHelper();
        Check(!shown.equippedItem && !shown.equipmentChecked && shown.equipped.empty() &&
            shown.statusTone==soul::StatusTone::Neutral && inventoryReads>readsBeforeFighting,
            "Unavailable equipment must never claim capture is ready");
        contents.complete=true; contents.equippedGenericId=9688; inventory=reinterpret_cast<Obj>(0x103);
        TickSoulHelper();
        Check(shown.statusTone==soul::StatusTone::Warning && shown.status.find("mauvaise pierre")!=std::string::npos,
            "Wrong stone must take priority even when capture effect is active");
        captureState=soul::CaptureState::Missing; TickSoulHelper();
        Check(shown.status.find("mauvaise pierre")!=std::string::npos,"Wrong stone must take priority over missing effect");
        contents.equippedGenericId=0; inventory=reinterpret_cast<Obj>(0x104);TickSoulHelper();
        Check(shown.status.find("aucune pierre")!=std::string::npos,"Missing stone needs the primary warning");
        contents.complete=true; contents.equippedGenericId=0; contents.equippedItem.reset(); contents.equippedName.clear();
        combat.state=soul::CombatState::Pending; TickSoulHelper();
        Check(!shown.showEquip && !shown.equipEnabled,"Incomplete participant data must not resurrect equipment controls");
        const auto beforeExit=hides, beforeHiddenRenders=renders;
        const auto beforeHiddenInventory=inventoryReads, beforeHiddenCapture=captureReads;
        combat.state=soul::CombatState::Hidden; TickSoulHelper();
        Check(hides>beforeExit,"Combat exit must hide status panel");
        // ReadCombat also returns Hidden for forbidden capture/PvP in both phases. No capture or
        // inventory UI should be evaluated after a previously visible PvM fight.
        TickSoulHelper(); click();
        Check(renders==beforeHiddenRenders && inventoryReads==beforeHiddenInventory && captureReads==beforeHiddenCapture && requests==1,
            "Hidden/PvP combat must never render, read inventory/capture, or keep an old equip action");
        combat.state=soul::CombatState::Ready; ++combat.fightIdentity; TickSoulHelper();
        Check(shown.showEquipment && shown.showEquip && inventoryReads>readsBeforeFighting &&
            shown.statusTone==soul::StatusTone::Neutral,"Next placement must restore equip UI from a fresh inventory read");
        fixture.settings.soulReminder=false; click(); TickSoulHelper();
        Check(requests==1,"Disabled preference must invalidate already-rendered action");
        fixture.settings.soulReminder=true; TickSoulHelper();
        accepted=false; click();
        Check(requests==2,"Fresh explicit click should reach native route");
        TickSoulHelper();
        Check(shown.status.find("refus")!=std::string::npos,"Dispatch refusal must not claim success");
        const auto draggedRevision=renderedRevision;
        position(900,400,draggedRevision); position(900,400,draggedRevision);
        Check(saves==0 && layoutWrites==1 && layout.x==900 && layout.y==400 && requests==2,
            "User drag must persist through the native layout bridge and never the global settings file");
        TickSoulHelper();
        Check(renderedX==900&&renderedY==400&&renderedRevision==layout.revision,
            "Panel must receive current native position and layout revision");
        const auto staleRevision=renderedRevision;
        layout.x=60;layout.y=80;++layout.revision;
        position(700,500,staleRevision);
        TickSoulHelper();
        Check(layoutWrites==1&&renderedX==60&&renderedY==80&&renderedRevision==layout.revision,
            "Applying a layout between render and mouse-up must reject the stale drag and restore the new layout");
        const auto beforeMenuHides=hides,beforeMenuRenders=renders;
        hideModule();
        Check(!fixture.settings.soulReminder&&saves==1&&hides>beforeMenuHides,"Header menu must persist shared disabled preference and hide immediately");
        hideModule();TickSoulHelper();click();
        Check(saves==1&&renders==beforeMenuRenders&&requests==2,"Hidden module must stay hidden without duplicate saves or actions");
        fixture.settings.soulReminder=true;TickSoulHelper();
        Check(renders>beforeMenuRenders&&renderedX==60&&renderedY==80,"Shared toggle reactivation restores panel at saved layout position");
        // Automatic requests use the same production validation and native action.
        fixture.settings.soulAutoEquip=true;
        ++combat.fightIdentity;
        accepted=true; reenter=true;
        int before=requests;
        TickSoulHelper(); TickSoulHelper();
        Check(requests==before+1 && !shown.equipEnabled && !shown.equippedCorrect,
            "Auto equip must dispatch once, await confirmation and withstand native reentry");
        reenter=false;
        Sleep(3050); // A real pending timeout must never create another automatic attempt.
        TickSoulHelper();
        Check(requests==before+1 && shown.equipEnabled && shown.status.find("non confirmé")!=std::string::npos,
            "Unconfirmed automatic request must leave a manual retry, not loop");
        click(); TickSoulHelper();
        Check(requests==before+2,"Manual retry must remain possible after automatic timeout");
        contents.equippedGenericId=9687; contents.equippedItem=std::make_shared<Ref>(newStone);
        inventory=reinterpret_cast<Obj>(0x110); TickSoulHelper();
        Check(shown.equippedCorrect && shown.equippedItem==newStone && !shown.showEquip,
            "Auto confirmation must use the observed item and update the panel");

        ++combat.fightIdentity; accepted=false; contents.equippedGenericId=0;
        before=requests; TickSoulHelper(); TickSoulHelper();
        Check(requests==before+1 && shown.status.find("refus")!=std::string::npos,
            "Refused automatic request must not retry or claim success");
        combat.state=soul::CombatState::Hidden; TickSoulHelper();
        combat.state=soul::CombatState::Pending; TickSoulHelper();
        combat.state=soul::CombatState::Ready;
        fixture.settings.soulAutoEquip=false; TickSoulHelper();
        fixture.settings.soulAutoEquip=true; TickSoulHelper();
        hideModule(); fixture.settings.soulReminder=true; TickSoulHelper();
        Check(requests==before+1,"Panel hiding, incomplete combat and option toggles must not reset attempted tiers");

        ++combat.fightIdentity; contents.equippedGenericId=9687; TickSoulHelper();
        contents.equippedGenericId=0; inventory=reinterpret_cast<Obj>(0x111); TickSoulHelper();
        Check(requests==before+1,"Do not override a later manual equipment change after this tier was already satisfied");

        ++combat.fightIdentity; before=requests;
        contents.complete=false; TickSoulHelper();
        contents.complete=true; contents.bag.clear(); inventory=reinterpret_cast<Obj>(0x112); TickSoulHelper();
        contents.bag={{3,9687,2,false}}; inventory=reinterpret_cast<Obj>(0x113); TickSoulHelper();
        contents.bag={{3,9687,2,true}}; contents.equipmentPending=true;
        inventory=reinterpret_cast<Obj>(0x114); TickSoulHelper();
        Check(requests==before,"Incomplete, absent, ineligible or pending inventory must never auto equip");
        contents.equipmentPending=false; accepted=true; inventory=reinterpret_cast<Obj>(0x115);
        TickSoulHelper();
        Check(requests==before+1,"Inventory becoming usable must allow the previously unattempted tier");
        combat.highestLevel=101; expectedId=9688; expectedUid=4; contents.bag={{4,9688,1,true}};
        TickSoulHelper();
        Check(requests==before+2,"A changed required tier permits its own single exact request");
        combat.highestLevel=51; expectedId=9687; expectedUid=3; contents.bag={{3,9687,2,true}};
        TickSoulHelper();
        Check(requests==before+2,"Returning to a previous tier in the same fight must not loop");

        ++combat.fightIdentity; before=requests; endDuringRead=true; TickSoulHelper(); endDuringRead=false;
        Check(requests==before,"Placement ending during automatic refresh must block dispatch");
        TickSoulHelper(); // Active fight never equips.
        combat.state=soul::CombatState::Hidden; TickSoulHelper(); // Includes PvP and noncapturable fights.
        combat.state=soul::CombatState::Unsupported; TickSoulHelper();
        combat.state=soul::CombatState::Ready; root=nullptr; TickSoulHelper();
        root=reinterpret_cast<Obj>(0x200); fixture.settings.soulReminder=false; TickSoulHelper();
        Check(requests==before,"Automatic action requires capturable placement, a visible module and a live UI root");
        fixture.settings.soulReminder=true; fixture.settings.soulAutoEquip=false; TickSoulHelper();
        Check(requests==before,"Automatic option off must retain manual-only operation");
        fixture.settings.soulAutoEquip=true; TickSoulHelper();
        Check(requests==before+1,"Previously blocked placement must get one attempt once valid and enabled");
        // 1.20.2: the game leaves the world (inactivity disconnection, logout): the
        // panel shown goes with the game's windows; its option stays on.
        fixture.settings.soulAutoEquip=false; TickSoulHelper();
        const auto shownHides=hides, shownRenders=renders;
        HideSoulHelper();
        Check(hides==shownHides+1 && fixture.settings.soulReminder,"Leaving the world hides the shown panel, its option kept");
        HideSoulHelper();
        Check(hides==shownHides+1,"Hidden, each further pump off the world does nothing");
        TickSoulHelper();
        Check(renders==shownRenders+1,"A capturable fight back in the world shows it again");
        fixture.settings.soulAutoEquip=true;
        ++combat.fightIdentity; before=requests;
        failRender=true; TickSoulHelper(); click();
        Check(requests==before,"UI failure must disable both manual and automatic action");
        RestoreSoulHelper(); RestoreSoulHelper();
        contents.equippedItem.reset();
        Check(handles.empty(),"Helper restore must release refs");
        std::cout << "PASS: actual helper validates automatic/manual equip, one attempt per fight/tier, refusals, timeouts, confirmation, reentry, hiding off the world and cleanup\n";
        module=nullptr;
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}

namespace soul { void InstallSpell() {} managed::Obj CaptureSpell(bool& enabled) { enabled=false; return fake::knownSpell; } }
