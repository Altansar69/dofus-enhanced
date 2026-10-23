#pragma once
#include "core/runtime.hpp"
#include <string>

namespace soul {
enum class StatusTone { Neutral, Warning, Good };
struct PanelView {
    std::string requirement, stock, equipped, status;
    bool equippedCorrect = false;
    bool equipEnabled = false;
    bool showEquip = true;
    StatusTone statusTone = StatusTone::Neutral;
    managed::Obj equippedItem = nullptr; // Valid through Render; native slot roots its binding.
    bool equipmentChecked = true;
    bool showEquipment = true; // False after placement: capture status only.
    managed::Obj captureSpell = nullptr;
    bool castEnabled = false;
};

// All calls occur on the existing game UI pump. GetUiRoot only resolves the
// native UIDocument root; it performs no input or session discovery.
managed::Obj GetUiRoot();
bool PanelCleanupPending();
// The soul panel's native window while it is shown (null otherwise), so the
// shared header-size setting can be applied without the panel knowing it.
managed::Obj PanelWindow();
bool HandlePanelMenuCallback(managed::Obj callback, managed::Obj view);
// Shared native ListElement action hook. Adds each handler once at install;
// handlers return false for foreign controls. Null does not clear other panels.
void SetAuxiliaryPanelAction(bool (*handler)(managed::Obj));
void SetAuxiliaryPanelMenus(bool (*populate)(managed::Obj, managed::Obj),
    bool (*callback)(managed::Obj, managed::Obj));
// True once the panel's hook on the menus' populate event is installed: only
// then do auxiliary menus get their rows (without it a right-click would show
// an empty menu).
bool PanelMenusReady();
class Panel {
public:
    void Install(void (*equip)(), void (*positionChanged)(int, int, uint64_t), void (*hideModule)());
    void Render(managed::Obj root, const PanelView&, int x = 24, int y = 180, uint64_t layoutGeneration = 0);
    void Hide();
};
}
