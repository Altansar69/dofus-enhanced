#include "monster-glow.hpp"
#include "monster-glow-entities.hpp"
#include "monster-glow-interactives.hpp"
#include "monster-glow-render.hpp"
#include "monster-glow-ui.hpp"
#include "monster-glow-world.hpp"
#include "module.hpp"
#include "features.hpp"
#include "options-ui.hpp"
#include <array>
#include <iostream>
#include <unordered_map>
#include <unordered_set>

namespace fake {
using namespace managed;
ULONGLONG now = 1000;
ULONGLONG FakeTick() { return now; }
Obj Ptr(uintptr_t value) { return reinterpret_cast<Obj>(value); }
Obj context = Ptr(1), camera = Ptr(2);
bool valid = true, combat = true, uiValid = true, throwRender = false, failShow = false;
bool pendingCleanup = false, loadingMap = false;
bool worldValid = true, worldCovered = false, worldSelectionValid = true, worldThrows = false;
unsigned worldReads = 0, wallCaptures = 0, interactiveReads = 0;
bool interactiveValid = true;
std::vector<monsterGlow::ScreenRect> lastCoverage;
unsigned resetFailures = 0;
uint64_t generation = 1;
std::vector<int64_t> roster{10};
std::unordered_set<int64_t> missingRenderers;
bool occluded = true;
unsigned reads = 0, projects = 0, renders = 0, ends = 0, uiResets = 0, gpuResets = 0;
std::vector<int64_t> current, visible;
std::array<bool,8> slots{};
std::unordered_map<GCHandle,Obj> handles;
GCHandle nextHandle = 0x123456780000;
std::vector<std::string> log;
void Check(bool condition,const char* message) { if (!condition) throw std::runtime_error(message); }
void Step() { now += 33; TickMonsterGlow(); }
}
namespace managed { Api api{}; uintptr_t gameBase = 0; }
Module* module = nullptr;
bool MapTransitionLoading() { return fake::loadingMap; }
Module::Module(std::filesystem::path path) : folder(std::move(path)) {}
void Log(const std::string& text) { fake::log.push_back(text); }
namespace monsterGlow {
void InstallEntities() {}
void InstallGlowInteractives() {}
Snapshot ReadGlowInteractives() {
    ++fake::interactiveReads; Snapshot result;
    if (fake::combat || !fake::interactiveValid) return result;
    result.valid=true; result.context.reset(fake::context); result.interactiveCount=1;
    Target target; target.id=10; target.interactive=true;
    target.entity.reset(fake::Ptr(500)); target.renderers.emplace_back(fake::Ptr(1500));
    result.targets.push_back(std::move(target)); return result;
}
void InstallGlowRender() {}
void InstallGlowUi() {}
void InstallGlowWorld() {}
WorldFrame ReadGlowWorld() {
    ++fake::worldReads;
    if (fake::worldThrows) throw std::runtime_error("world unavailable");
    WorldFrame result; result.valid = fake::worldValid;
    result.reason = result.valid ? "ready" : "no-map"; return result;
}
WorldSelection ForegroundGlowDecor(const WorldFrame&,const UiFrame&,const std::vector<managed::Obj>&,ScreenRect) {
    WorldSelection result; result.valid = fake::worldSelectionValid;
    result.reason = result.valid ? "ready" : "stale";
    if (result.valid && fake::worldCovered) result.renderers.emplace_back(fake::Ptr(5000));
    return result;
}
Snapshot ReadEntities() {
    using namespace fake;
    ++reads;
    Snapshot result;
    result.valid = valid; result.combat = combat; result.context.reset(context);
    result.reason = valid ? "ready" : "registry-ownership";
    result.rosterCount = static_cast<int>(roster.size()); result.monsterCount = result.rosterCount;
    result.matchedRoots = result.rosterCount;
    for (auto id : roster) {
        Target target;
        target.id = id; target.entity.reset(Ptr(100 + static_cast<uintptr_t>(id)));
        if (!missingRenderers.contains(id)) target.renderers.emplace_back(Ptr(1000 + static_cast<uintptr_t>(id)));
        result.targets.push_back(std::move(target));
    }
    return result;
}
UiFrame BeginGlowUi(uint64_t) {
    UiFrame result;
    result.valid = fake::uiValid;
    result.reason = fake::uiValid ? "ready" : "camera-target-texture";
    result.paintCount = 1; result.visitedNodes = 12;
    result.camera.reset(fake::camera); result.generation = fake::generation;
    fake::current.clear();
    return result;
}
UiProjection ProjectGlowRenderers(const UiFrame&,const std::vector<managed::Obj>& renderers,bool everywhere) {
    ++fake::projects;
    fake::Check(!renderers.empty(),"Empty renderers must never reach projection");
    UiProjection result;
    result.valid = true;
    result.reason = fake::occluded ? "ready" : "no-ui-overlap";
    result.screenRect = {10,20,100,160};
    result.panelRect = {10,20,100,160};
    if (everywhere) result.clipRects.push_back(result.panelRect);
    else if (fake::occluded) result.clipRects.push_back({35,40,50,40});
    return result;
}
managed::Obj RenderGlow(size_t slot,managed::Obj camera,const std::vector<managed::Obj>&,ScreenRect rectangle,uint64_t,
                       const GlowCoverage* coverage) {
    ++fake::renders;
    fake::Check(slot<8 && camera == fake::camera && rectangle.width==100,"Wrong capture slot/camera/crop");
    if (fake::throwRender) throw std::runtime_error("offline fixture render failure");
    fake::lastCoverage.clear();
    if (coverage) {
        ++fake::wallCaptures;
        fake::Check(coverage->renderers==std::vector<managed::Obj>{fake::Ptr(5000)},"Exact foreground renderer must reach GPU mask");
        fake::lastCoverage=coverage->rectangles;
    }
    fake::slots.at(slot) = true;
    return fake::Ptr(10000+slot);
}
bool ShowGlowUi(const UiFrame&,int64_t id,const UiProjection&,managed::Obj texture,bool interactive) {
    fake::Check(texture != nullptr,"Cannot show null texture");
    if (fake::failShow) return false;
    fake::current.push_back(interactive ? -id : id); return true;
}
void EndGlowUi() { ++fake::ends; fake::visible = fake::current; }
void ResetGlowUi() {
    ++fake::uiResets;
    if (fake::resetFailures) { --fake::resetFailures; fake::pendingCleanup = true; return; }
    fake::pendingCleanup = false; fake::visible.clear(); fake::current.clear();
}
bool GlowUiCleanupPending() { return fake::pendingCleanup; }
std::string DescribeGlowUi() { return "overlay=fixture"; }
void ReleaseGlowSlot(size_t slot) { fake::slots.at(slot) = false; }
void ResetGlowRender() {
    fake::Check(!fake::pendingCleanup,"Textures released before pending UI bindings detached");
    ++fake::gpuResets; fake::slots.fill(false);
}
}
#define GetTickCount64 fake::FakeTick
#include "monster-glow.cpp"
#undef GetTickCount64

int main() {
    using namespace fake;
    try {
        api.gchandle_new = [](Obj obj,bool) { handles[++nextHandle]=obj; return nextHandle; };
        api.gchandle_get_target = [](GCHandle h) { return handles.at(h); };
        api.gchandle_free = [](GCHandle h) { Check(handles.erase(h)==1,"GC ownership mismatch"); };
        Module fixture(L"offline-only"); module=&fixture;
        fixture.ready=true; fixture.servicesReady=true;
        InstallMonsterGlow();
        fixture.settings.monsterGlowMode=false;
        TickMonsterGlow(); Check(reads==0 && renders==0,"Explicit opt-out must do no scene or GPU work");
        fixture.settings.monsterGlowMode=true; Step();
        Check(visible==std::vector<int64_t>{10} && slots[0],"Eligible covered monster must render");
        Check(fake::log.back().find("[DEBUG:glow] stage=frame")!=std::string::npos &&
              fake::log.back().find("shown=1")!=std::string::npos,"First enabled frame must diagnose completed stages");
        const auto diagnosticCount = fake::log.size();
        for(int i=0;i<310;++i) Step();
        Check(fake::log.size()==diagnosticCount,"Unchanged diagnostics must not spam log every frame or interval");
        uiValid=false; now+=10000; Step();
        Check(fake::log.back().find("ui=camera-target-texture")!=std::string::npos,"Invalid UI must report precise skip gate");
        uiValid=true; now+=10000; Step();
        Check(fake::log.back().find("stage=frame")!=std::string::npos,"Recovery must be visible in bounded diagnostics");
        const auto beforeLoadingReads=reads, beforeLoadingRenders=renders;
        loadingMap=true; Step();
        Check(visible.empty() && handles.empty(),"Loading must detach old map glow");
        Step();Step();
        Check(reads==beforeLoadingReads && renders==beforeLoadingRenders && fixture.settings.monsterGlowMode,
            "Loading must skip entity/HUD/GPU work while preserving glow preference");
        loadingMap=false;Step();Check(!visible.empty(),"Glow resumes after native arrival");
        resetFailures=1;loadingMap=true;Step();
        Check(MonsterGlowCleanupPending(),"Loading suspension preserves pending UI detach ownership");
        now+=100;Step();Check(!MonsterGlowCleanupPending(),"Loading does not prevent cleanup retries");
        loadingMap=false;Step();
        const auto beforeThrottle = reads;
        TickMonsterGlow(); Check(reads==beforeThrottle,"Duplicate frame must respect throttle");
        roster={10,10,11,12,13,14,15,16,17,18,19}; Step();
        Check(visible.size()==8 && visible[1]==11 && visible.back()==17,"Duplicates and max8 capture budget");
        roster={11}; Step();
        Check(visible==std::vector<int64_t>{11} && !slots[1] && !slots[7],"Removed entities and unused slots must disappear");
        occluded=false; Step();
        Check(visible.empty() && !slots[0],"Uncovered monsters must not retain glow");
        fixture.settings.monsterGlowMode=3;Step();
        Check(visible==std::vector<int64_t>{11},"Always scope renders uncovered visible monsters");
        fixture.settings.monsterGlowMode=1;Step();
        Check(visible.empty(),"Returning to UI scope removes uncovered contours immediately");
        Check(worldReads==0,"HUD and Always modes must not scan world decor");
        fixture.settings.monsterGlowMode=2;Step();
        Check(visible.empty(),"UI+walls must not alias Always when no obstacle overlaps");
        worldCovered=true;Step();
        Check(visible==std::vector<int64_t>{11} && wallCaptures==1 && lastCoverage.empty(),"Wall-only overlap requires actual decor mask");
        occluded=true;Step();
        Check(lastCoverage.size()==1 && lastCoverage[0].x==.25f && lastCoverage[0].y==.625f &&
            lastCoverage[0].width==.5f && lastCoverage[0].height==.25f,"HUD coverage must convert top-left pixels into normalized bottom-left capture coordinates");
        const auto beforeBadWorld=wallCaptures;
        worldValid=false;Step();
        Check(visible.size()==1 && wallCaptures==beforeBadWorld,"Unavailable world must preserve HUD-only fallback");
        occluded=false;Step();Check(visible.empty(),"Unavailable world must never broaden coverage");
        worldValid=true;worldSelectionValid=false;Step();Check(visible.empty(),"Stale world selection must fail closed");
        worldSelectionValid=true;worldThrows=true;occluded=true;Step();
        Check(visible.size()==1 && wallCaptures==beforeBadWorld,"World read exception must preserve existing HUD behavior");
        worldThrows=false;Step();Check(wallCaptures>beforeBadWorld,"World read recovery must not leave glow disabled");
        fixture.settings.monsterGlowMode=1;occluded=false;Step();
        Check(visible.empty(),"Mode change must remove wall contours immediately");
        worldCovered=false;
        fixture.settings.monsterGlowMode=4;Step();
        Check(visible.empty(),"Invalid glow mode fails closed");
        fixture.settings.monsterGlowMode=1;
        occluded=true; missingRenderers.insert(11); Step();
        Check(visible.empty(),"No supported source renderers means no overlay");
        missingRenderers.clear(); Step();
        const auto beforeContext = gpuResets;
        context=Ptr(3); combat=false; Step();
        Check(gpuResets>beforeContext && visible.size()==1,"Context switch must reset owned textures");
        const auto beforeCamera = gpuResets;
        camera=Ptr(4); ++generation; Step();
        Check(gpuResets>beforeCamera,"Camera/layout generation must invalidate textures");
        uiValid=false; Step();
        Check(visible.empty() && handles.empty(),"Detached UI must clean every held frame reference");
        uiValid=true; Step(); valid=false; Step();
        Check(visible.empty(),"Invalid scene ownership must hide all");
        valid=true; Step();
        fixture.settings.monsterGlowMode=false;
        TickMonsterGlow(); // Clearing is immediate, even inside throttled interval.
        Check(visible.empty() && handles.empty(),"Disable must bypass throttle and release references");
        fixture.settings.monsterGlowMode=true; Step();
        resetFailures=1; const auto beforePending=gpuResets;
        fixture.settings.monsterGlowMode=false; TickMonsterGlow();
        Check(MonsterGlowCleanupPending() && gpuResets==beforePending,"Pending UI detach must retain textures");
        Step(); Step(); Step();
        Check(MonsterGlowCleanupPending() && gpuResets==beforePending,"Cleanup retry must be bounded to100ms");
        Step(); Check(!MonsterGlowCleanupPending() && visible.empty() && handles.empty(),"Disabled cleanup must retry without reenable");
        fixture.settings.monsterGlowMode=true; throwRender=true; Step();
        Check(visible.empty() && handles.empty() && !fake::log.empty(),"Render error must clear and report");
        const auto beforeLatch=reads; Step(); Step();
        Check(reads==beforeLatch,"Failed backend must not spin/retry every frame");
        fixture.settings.monsterGlowMode=false; TickMonsterGlow();
        fixture.settings.monsterGlowMode=true; throwRender=false; Step();
        Check(visible.size()==1,"Explicit toggle must retry failed rendering");
        combat=false; roster={10};
        fixture.settings.glowInteractives=true; TickMonsterGlow();
        Check(visible==std::vector<int64_t>{10,-10},"Actor and interactive IDs must not collide");
        fixture.settings.glowInteractives=false; TickMonsterGlow();
        Check(visible==std::vector<int64_t>{10},"Category disable must bypass throttle and remove its contour");
        const auto beforeNpcs=gpuResets;
        fixture.settings.glowNpcs=true; TickMonsterGlow();
        Check(gpuResets>beforeNpcs,"NPC category changes must refresh immediately");
        roster.clear(); fixture.settings.glowInteractives=true; TickMonsterGlow();
        Check(visible==std::vector<int64_t>{-10},"Interactives must render without any actors");
        interactiveValid=false; Step(); Check(visible.empty(),"Invalid interactive snapshot clears old object contour");
        interactiveValid=true; fixture.settings.glowInteractives=false; fixture.settings.glowNpcs=false;
        roster.clear(); Step(); Check(visible.empty() && handles.empty(),"Empty scene clears all");
        roster.reserve(256); for (int i=0;i<256;++i) roster.push_back(i+1);
        occluded=false; const auto beforeProjection=projects; Step();
        Check(projects-beforeProjection==128,"Projection work must be bounded when none overlap");
        occluded=true; failShow=true; const auto beforeFailedShows=renders; Step();
        Check(renders-beforeFailedShows==8 && visible.empty(),"Rejected UI binding must still cap GPU capture attempts at8");
        failShow=false; roster={10}; Step();
        resetFailures=1; fixture.stopped=true; RestoreMonsterGlow();
        Check(MonsterGlowCleanupPending(),"Stop must expose pending cleanup before worker disables hooks");
        now+=100; TickMonsterGlow();
        Check(!MonsterGlowCleanupPending() && handles.empty(),"Stopped main-thread drain must finish pending cleanup");
        fixture.stopRequested=true; TickMonsterGlow();
        Check(visible.empty() && handles.empty(),"Stop request must hide immediately");
        RestoreMonsterGlow(); RestoreMonsterGlow();
        Check(handles.empty(),"Restore must be idempotent");
        module=nullptr;
        std::cout<<"PASS: production glow coordinator opt-out, clipping gate, budgets, throttle, context/disposal and error isolation\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
