#include "treasure/treasure-hunt.hpp"
#include "core/module.hpp"
#include <stdexcept>

namespace treasure {
namespace {
using namespace managed;
constexpr const char* DataCenter = "Ankama.Dofus.Core.DataCenter.dll";
// Step kinds built by the hunt message constructor: 1 follow direction to a
// point of interest, 3 follow direction for N maps, 5 follow direction to an
// NPC hint. 0 is the synthetic start, 2/4 are unknown placeholders.
constexpr int PoiStep = 1, DirectionStep = 3, HintStep = 5;
constexpr int MaxSteps = 64;
Klass serviceClass{}, huntClass{}, stepClass{}, poiClass{};
Method hunts{}, poiRoot{}, poiName{};
bool installed = false;
void RequireField(Klass type, const char* name, size_t offset) {
    auto field = api.class_get_field_from_name(type, name);
    if (!field || api.field_get_offset(field) != offset) throw std::runtime_error(std::string("Treasure hunt field mismatch: ") + name);
}
bool Ready() {
    return installed && module && module->ready && module->servicesReady && !module->stopped && !module->stopRequested;
}
}
void InstallHunt() {
    if (installed) return;
    serviceClass = api.Class("Core.dll", "", "fdl");
    huntClass = api.Class("Core.dll", "", "ha");
    stepClass = api.Class("Core.dll", "", "gz");
    RequireField(serviceClass, "<dted>k__BackingField", 0x48);
    RequireField(huntClass, "dcmw", 0x20);
    RequireField(stepClass, "dcmj", 0x10); RequireField(stepClass, "dcmk", 0x14); RequireField(stepClass, "dcml", 0x18);
    RequireField(stepClass, "dcmn", 0x20); RequireField(stepClass, "dcmp", 0x28);
    hunts = api.FindVerified(serviceClass, "bdbm", 0, 0x69F1E0);
    poiClass = api.Class(DataCenter, "Core.DataCenter.Metadata.Quest.TreasureHunt", "PointOfInterestData");
    poiName = api.FindVerified(poiClass, "get_name", 0, 0x7C7FC0);
    poiRoot = api.FindVerified(api.Class(DataCenter, "Core.DataCenter", "DataCenterModule"), "get_pointsOfInterestDataRoot", 0, 0x7BFD80);
    installed = true;
}
Step CurrentStep() {
    Step step;
    if (!Ready()) return step;
    // The hunt UI depends on the fov interface; the compositor resolves it to
    // the fdl implementation whose hunt table layout is pinned above.
    Ref service(module->FindService("fov"));
    if (!service.get() || api.object_get_class(service.get()) != serviceClass) return step;
    Ref table(api.Invoke(hunts, service.get()));
    if (!table.get()) return step;
    Obj hunt = nullptr;
    for (int kind : {0, 2, 1}) {
        Obj found = nullptr;
        if (api.Value<bool>(api.Call(table.get(), "TryGetValue", {&kind, &found})) && found) { hunt = found; break; }
    }
    if (!hunt || api.object_get_class(hunt) != huntClass) return step;
    Ref steps(api.Field(hunt, "dcmw"));
    if (!steps.get()) return step;
    const int count = api.Value<int>(api.Call(steps.get(), "get_Count"));
    if (count < 0 || count > MaxSteps) throw std::runtime_error("Treasure hunt step bounds");
    for (int i = count; i-- > 0;) {
        Ref entry(api.Call(steps.get(), "get_Item", {&i}));
        if (!entry.get() || api.object_get_class(entry.get()) != stepClass) continue;
        const int type = Read<int>(entry.get(), 0x14);
        if (type != PoiStep && type != DirectionStep && type != HintStep) continue;
        step.active = true; step.index = int(Read<uint32_t>(entry.get(), 0x10)); step.type = type;
        step.direction = Read<int>(entry.get(), 0x18);
        if (type == PoiStep) step.poiId = Read<uint32_t>(entry.get(), 0x20);
        else if (type == HintStep) step.npcId = Read<uint32_t>(entry.get(), 0x28);
        else step.mapCount = Read<uint32_t>(entry.get(), 0x28);
        break;
    }
    if (step.active && step.poiId) {
        try {
            Ref root(api.Invoke(poiRoot, nullptr));
            int64_t id = step.poiId;
            Ref poi(root.get() ? api.Call(root.get(), "GetObjectById", {&id}) : nullptr);
            if (poi.get() && api.object_get_class(poi.get()) == poiClass) step.hint = api.String(api.Invoke(poiName, poi.get()));
        } catch (const std::exception&) { step.hint.clear(); }
    }
    return step;
}
}
