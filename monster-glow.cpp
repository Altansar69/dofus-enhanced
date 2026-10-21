#include "monster-glow.hpp"
#include "monster-glow-entities.hpp"
#include "monster-glow-interactives.hpp"
#include "monster-glow-render.hpp"
#include "monster-glow-ui.hpp"
#include "monster-glow-world.hpp"
#include "module.hpp"
#include "map-transitions.hpp"
#include <algorithm>
#include <unordered_set>
#include <sstream>

namespace {
using namespace managed;
using namespace monsterGlow;
constexpr size_t MaxGlows = 16;
constexpr size_t MaxProjectedTargets = 128;
constexpr ULONGLONG FrameInterval = 33;
bool installed = false, enabled = false, failed = false;
bool worldAvailable = false, interactivesAvailable = false;
bool previousNpcs = false, previousInteractives = false;
int previousMode = 0;
bool hadResources = false, previousCombat = false, cleanupPending = false;
Ref previousContext, previousCamera;
uint64_t previousUiGeneration = 0;
ULONGLONG nextFrame = 0;
ULONGLONG nextCleanup = 0;
ULONGLONG nextDiagnostic = 0;
std::string lastDiagnostic;

void Trace(ULONGLONG now, const char* stage, const Snapshot& entities, const UiFrame* frame = nullptr,
           size_t projected = 0, size_t captures = 0, size_t textures = 0, size_t shown = 0,
           const char* projectionReason = "none", const char* worldReason = "unused", size_t walls = 0) {
    if (now < nextDiagnostic) return;
    nextDiagnostic = now + 10000;
    std::ostringstream text;
    text << "[DEBUG:glow] stage=" << stage << " combat=" << entities.combat
         << " entities=" << entities.reason << " roster=" << entities.rosterCount
         << " npcs=" << entities.npcCount << " interactives=" << entities.interactiveCount
         << " monsters=" << entities.monsterCount << " roots=" << entities.matchedRoots
         << " visible=" << entities.targets.size() << " rejectedPart=" << entities.lastRejectedPart
         << " rejectedAlpha=" << entities.lastRejectedAlpha;
    if (frame) text << " ui=" << frame->reason << " paint=" << frame->paintCount
                    << " nodes=" << frame->visitedNodes << " scanLimited=" << frame->scanLimited;
    text << " projected=" << projected << " captures=" << captures << " textures=" << textures
         << " shown=" << shown << " projection=" << projectionReason
         << " mode=" << (module ? module->settings.monsterGlowMode : 0)
         << " world=" << worldReason << " walls=" << walls;
    if (shown) text << " " << DescribeGlowUi();
    const auto message = text.str();
    if (message != lastDiagnostic) { Log(message); lastDiagnostic = message; }
}

void Clear() {
    // Detach images before releasing their textures. Each backend owns only
    // private resources; a failed cleanup must not prevent the other cleanup.
    try { ResetGlowUi(); } catch (const std::exception& e) { Log(std::string("GLOW_UI_RESET_ERROR ") + e.what()); }
    cleanupPending = GlowUiCleanupPending();
    if (cleanupPending) {
        // A failed UI detach still owns texture bindings. Keep them alive and
        // retry on the main thread even after disable or the error latch.
        hadResources = true;
        nextCleanup = GetTickCount64() + 100;
        return;
    }
    try { ResetGlowRender(); } catch (const std::exception& e) { Log(std::string("GLOW_RENDER_RESET_ERROR ") + e.what()); }
    previousContext.reset(); previousCamera.reset(); previousUiGeneration = 0;
    hadResources = false;
}
}

void InstallMonsterGlow() {
    // Optional rendering must not prevent the existing mod from loading if a
    // shader/API is unavailable. Resource creation happens on the UI thread.
    try {
        monsterGlow::InstallEntities();
        monsterGlow::InstallGlowRender();
        monsterGlow::InstallGlowUi();
        // A missing decor registry must not disable the existing HUD contour.
        try { monsterGlow::InstallGlowWorld(); worldAvailable = true; }
        catch (const std::exception& e) { Log(std::string("GLOW_WORLD_UNAVAILABLE ") + e.what()); }
        try { monsterGlow::InstallGlowInteractives(); interactivesAvailable = true; }
        catch (const std::exception& e) { Log(std::string("GLOW_INTERACTIVES_UNAVAILABLE ") + e.what()); }
        installed = true;
        Log("GLOW_READY configurable contour; default UI; max8 targets at30Hz");
    } catch (const std::exception& e) {
        installed = false;
        Log(std::string("GLOW_UNAVAILABLE ") + e.what());
    }
}

void TickMonsterGlow() {
    const auto now = GetTickCount64();
    if (cleanupPending) {
        if (now >= nextCleanup) Clear();
        if (cleanupPending) return;
    }
    if (!installed || !module || !module->ready.load() || !module->servicesReady ||
        module->stopped || module->stopRequested.load() || !Settings::ValidMonsterGlowMode(module->settings.monsterGlowMode) ||
        module->settings.monsterGlowMode == 0) {
        if (enabled || hadResources) Clear();
        enabled = false; failed = false; nextFrame = 0;
        return;
    }
    const int mode = module->settings.monsterGlowMode;
    if (!enabled || mode != previousMode || module->settings.glowNpcs != previousNpcs ||
        module->settings.glowInteractives != previousInteractives) {
        if (enabled) { Clear(); if (cleanupPending) return; }
        previousMode = mode;
        previousNpcs = module->settings.glowNpcs; previousInteractives = module->settings.glowInteractives;
        enabled = true; failed = false; nextFrame = 0; nextDiagnostic = 0; lastDiagnostic.clear();
        Log("GLOW_ENABLED mode=" + std::to_string(mode));
    }
    if (failed) return;
    if (MapTransitionLoading()) {
        // Avoid traversing the HUD and rendering private capture textures while
        // the native loader builds a replacement map. Keep the saved preference.
        if (hadResources) Clear();
        nextFrame = 0;
        return;
    }
    if (now < nextFrame) return;
    nextFrame = now + FrameInterval;
    try {
        auto entities = ReadEntities();
        if (interactivesAvailable && module->settings.glowInteractives) {
            auto interactive = ReadGlowInteractives();
            if (interactive.valid) {
                if (!entities.valid) entities = std::move(interactive);
                else if (!entities.combat && entities.context.get() == interactive.context.get()) {
                    entities.interactiveCount = interactive.interactiveCount;
                    for (auto& target : interactive.targets) entities.targets.push_back(std::move(target));
                }
            }
        }
        if (!entities.valid || !entities.context.get() || entities.targets.empty()) {
            Trace(now,"entities",entities); Clear(); return;
        }
        if (previousContext.get() &&
            (entities.context.get() != previousContext.get() || entities.combat != previousCombat)) {
            Clear();
            if (cleanupPending) return;
        }
        auto frame = BeginGlowUi();
        hadResources = true;
        if (!frame.valid) { Trace(now,"ui",entities,&frame); Clear(); return; }
        if (previousCamera.get() && (frame.camera.get() != previousCamera.get() ||
            frame.generation != previousUiGeneration)) {
            // BeginGlowUi owns reparenting/reset of its new frame. Drop only
            // renderer slots here so its fresh frame remains valid.
            ResetGlowRender();
        }
        previousContext.reset(entities.context.get()); previousCombat = entities.combat;
        previousCamera.reset(frame.camera.get()); previousUiGeneration = frame.generation;
        size_t slot = 0, projected = 0, captures = 0, textures = 0;
        size_t walls = 0;
        WorldFrame world;
        if (mode == 2 && worldAvailable) {
            try { world = ReadGlowWorld(); }
            catch (const std::exception&) { world.reason = "read-error"; }
        }
        const char* worldReason = mode == 2 ? world.reason : "unused";
        const char* projectionReason = "none";
        std::unordered_set<int64_t> seenActors, seenInteractives;
        for (const auto& target : entities.targets) {
            if (captures >= MaxGlows || projected >= MaxProjectedTargets) break;
            if (!target.id || !target.entity.get() || target.renderers.empty() || !(target.interactive ? seenInteractives : seenActors).insert(target.id).second) continue;
            ++projected;
            std::vector<Obj> renderers;
            renderers.reserve(target.renderers.size());
            for (const auto& renderer : target.renderers) if (renderer.get()) renderers.push_back(renderer.get());
            if (renderers.empty()) continue;
            auto projection = ProjectGlowRenderers(frame, renderers, mode != 1);
            projectionReason = projection.reason;
            if (!projection.valid || projection.clipRects.empty()) continue;
            GlowCoverage coverage;
            WorldSelection foreground;
            if (mode == 2) {
                const auto hud = ProjectGlowRenderers(frame,renderers);
                if (world.valid) {
                    try { foreground = ForegroundGlowDecor(world,frame,renderers,projection.screenRect); }
                    catch (const std::exception&) { foreground.reason = "selection-error"; }
                    worldReason = foreground.reason;
                }
                if (foreground.valid && !foreground.renderers.empty()) {
                    for (const auto& decor : foreground.renderers) coverage.renderers.push_back(decor.get());
                    walls += coverage.renderers.size();
                    const auto r = projection.panelRect;
                    if (hud.valid && r.width > 0 && r.height > 0) {
                        for (const auto c : hud.clipRects) {
                            const float x0 = std::clamp((c.x-r.x)/r.width,0.0f,1.0f);
                            const float x1 = std::clamp((c.x+c.width-r.x)/r.width,0.0f,1.0f);
                            const float y0 = std::clamp((r.y+r.height-c.y-c.height)/r.height,0.0f,1.0f);
                            const float y1 = std::clamp((r.y+r.height-c.y)/r.height,0.0f,1.0f);
                            if (x1 > x0 && y1 > y0) coverage.rectangles.push_back({x0,y0,x1-x0,y1-y0});
                        }
                    }
                } else {
                    // Unknown/ambiguous world ordering never becomes Always.
                    // The established HUD-only behavior remains available.
                    projection = hud;
                    if (!projection.valid || projection.clipRects.empty()) continue;
                }
            }
            ++captures;
            auto texture = RenderGlow(slot, frame.camera.get(), renderers, projection.screenRect, now,
                coverage.renderers.empty() ? nullptr : &coverage);
            if (!texture) continue;
            ++textures;
            if (!ShowGlowUi(frame, target.id, projection, texture, target.interactive)) continue;
            ++slot;
        }
        EndGlowUi();
        Trace(now,"frame",entities,&frame,projected,captures,textures,slot,projectionReason,worldReason,walls);
        if (GlowUiCleanupPending()) { Clear(); return; }
        for (; slot < MaxGlows; ++slot) ReleaseGlowSlot(slot);
    } catch (const std::exception& e) {
        Clear();
        failed = true;
        Log(std::string("GLOW_DISABLED_ERROR reenable option to retry: ") + e.what());
    }
}

void RestoreMonsterGlow() {
    Clear();
    enabled = false; failed = false; nextFrame = 0;
}
bool MonsterGlowCleanupPending() { return cleanupPending; }
