#include "maps/map-preload.hpp"
#include "core/module.hpp"
#include <array>
#include <chrono>
#include <climits>
#include <deque>
#include <unordered_set>

namespace {
using namespace managed;
constexpr size_t MaxSprites = 512, MaxPending = 8;
constexpr int MaxStartsPerTick = 8;
constexpr int MaxScanStepsPerTick = 256;
constexpr ULONGLONG KeepMilliseconds = 30000;
Klass addressesClass{}, inputClass{}, mapClass{}, spriteClass{}, handleClass{};
Method locate{}, provide{}, release{}, getManager{}, isValid{}, isDone{}, status{}, result{};
Ref manager;
struct Asset { Ref handle; bool map = false, done = false; };
std::vector<Asset> assets;
std::deque<int> sprites;
Ref scanData, scanGroups, scanElements;
std::unordered_set<int> uniqueSprites;
int scanLayer = 0, scanGroup = 0, scanElement = 0;
size_t inspected = 0;
int64_t observed = -1, target = -1;
int pack = 0;
ULONGLONG started = 0, nextTick = 0;
bool faulted = false, reportedReady = false;
size_t loadedAssets = 0, missedSprites = 0;
const char* scanStop = "pending";

bool Exact(Obj object, Klass klass) { return object && api.object_get_class(object) == klass; }
int Count(Obj list, int maximum) {
    if (!list) return 0;
    const int count = api.Value<int>(api.Call(list, "get_Count"));
    if (count < 0 || count > maximum) throw std::runtime_error("Map preload collection bounds");
    return count;
}
Obj Item(Obj list, int index) { return api.Call(list, "get_Item", {&index}); }
bool HandleFlag(Method method, Obj handle) {
    if (!Exact(handle, handleClass)) throw std::runtime_error("Map preload handle type mismatch");
    return api.Value<bool>(api.Invoke(method, api.object_unbox(handle)));
}
void ReleaseAssets() {
    sprites.clear();
    scanData.reset(); scanGroups.reset(); scanElements.reset(); uniqueSprites.clear();
    scanLayer = scanGroup = scanElement = 0; inspected = 0;
    // Retain ownership on failure so shutdown can retry. Release only handles
    // returned to this module, never uu.Release or native map cache entries.
    for (auto& asset : assets) {
        if (!asset.handle.get()) continue;
        if (HandleFlag(isValid, asset.handle.get()))
            api.Invoke(release, manager.get(), {api.object_unbox(asset.handle.get())});
        asset.handle.reset();
    }
    assets.clear(); manager.reset(); target = -1; reportedReady = false;
    loadedAssets = missedSprites = 0; scanStop = "pending";
}
bool StartAsset(const std::string& key, Klass klass, bool map) {
    Ref implementation(api.StaticField(addressesClass, "m_AddressablesInstance"));
    if (!implementation.get()) return false; // Do not initialize/load catalogs speculatively.
    Ref type(api.type_get_object(api.class_get_type(klass)));
    Ref address(api.string_new(key.c_str()));
    Obj locations = nullptr;
    const bool found = api.Value<bool>(api.Invoke(locate, implementation.get(), {address.get(), type.get(), &locations}));
    Ref roots(locations);
    if (!found || Count(roots.get(), 32) != 1) return false; // Ambiguous address: native fallback.
    Ref location(Item(roots.get(), 0));
    if (!location.get()) return false;
    if (!manager.get()) manager.reset(api.Invoke(getManager, nullptr));
    if (!manager.get()) return false;
    bool releaseDependencies = true;
    Asset asset;
    asset.map = map;
    asset.handle.reset(api.Invoke(provide, manager.get(), {location.get(), type.get(), &releaseDependencies}));
    if (!HandleFlag(isValid, asset.handle.get())) throw std::runtime_error("Invalid map preload operation");
    assets.push_back(std::move(asset));
    return true;
}
void CollectSprites(Obj data) {
    if (!Exact(data, mapClass) || api.Value<int>(api.Field(data, "id")) != target)
        throw std::runtime_error("Preloaded map ID mismatch");
    scanData.reset(data);
}
void ScanSprites() {
    if (!scanData.get()) return;
    constexpr std::array<const char*, 3> layers{"backgroundMapElements", "middlegroundMapElements", "foregroundMapElements"};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2);
    // These are the same three static layers scanned by ue.Preload/qji.
    // Animated objects, actors, particles and scene construction stay native.
    // Dense maps repeat many gfx IDs. Scan more of those cheap entries before
    // yielding, still within the same cooperative 2ms budget.
    for (int steps = 0; steps < MaxScanStepsPerTick && std::chrono::steady_clock::now() < deadline; ++steps) {
        if (scanLayer == 3 || uniqueSprites.size() == MaxSprites || inspected >= 8192) {
            scanStop = scanLayer == 3 ? "complete" : uniqueSprites.size() == MaxSprites ? "sprite-limit" : "element-limit";
            Log("MAP_PRELOAD_SCAN map=" + std::to_string(target) + " uniqueSprites=" + std::to_string(uniqueSprites.size()) +
                " inspected=" + std::to_string(inspected) + " stop=" + scanStop);
            scanData.reset(); scanGroups.reset(); scanElements.reset(); return;
        }
        if (!scanGroups.get()) scanGroups.reset(api.Field(scanData.get(), layers[scanLayer]));
        if (scanGroup >= Count(scanGroups.get(), 4096)) {
            ++scanLayer; scanGroup = scanElement = 0; scanGroups.reset(); continue;
        }
        if (!scanElements.get()) {
            Ref group(Item(scanGroups.get(), scanGroup));
            if (group.get()) scanElements.reset(api.Field(group.get(), "mapElements"));
        }
        if (scanElement >= Count(scanElements.get(), 8192)) {
            ++scanGroup; scanElement = 0; scanElements.reset(); continue;
        }
        Ref element(Item(scanElements.get(), scanElement++)); ++inspected;
        if (!element.get()) continue;
        const int id = api.Value<int>(api.Field(element.get(), "gfxId"));
        if (id >= 0 && uniqueSprites.insert(id).second) sprites.push_back(id);
    }
}
void Advance(ULONGLONG now) {
    if (target <= 0) return;
    if (now - started >= KeepMilliseconds) { ReleaseAssets(); return; }
    if (observed != target) {
        // Cancelled walking and an arrival request both clear the native intent.
        // Stop speculative launches in either case; retain existing references
        // briefly so the real loader can consume them without a release gap.
        if (scanData.get() || !sprites.empty() || scanStop == std::string("pending")) {
            Log("MAP_PRELOAD_INTENT_CLEARED map=" + std::to_string(target) + " queuedNotStarted=" +
                std::to_string(sprites.size()) + " scanIncomplete=" + std::to_string(bool(scanData.get())));
            scanStop = "intent-cleared";
        }
        sprites.clear(); scanData.reset(); scanGroups.reset(); scanElements.reset();
    }
    size_t pending = 0;
    for (auto& asset : assets) {
        if (asset.done) continue;
        if (!HandleFlag(isDone, asset.handle.get())) { ++pending; continue; }
        asset.done = true;
        if (api.Value<int>(api.Invoke(status, api.object_unbox(asset.handle.get()))) != 1) {
            if (asset.map) throw std::runtime_error("Map data preload failed");
            ++missedSprites;
            continue;
        }
        ++loadedAssets;
        if (asset.map && observed == target) {
            Ref data(api.Invoke(result, api.object_unbox(asset.handle.get())));
            CollectSprites(data.get());
            Log("MAP_PRELOAD_DATA map=" + std::to_string(target) + " elapsedMs=" + std::to_string(now - started) +
                " phase=scan-decors");
        }
    }
    ScanSprites();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2);
    for (int attempts = 0; attempts < MaxStartsPerTick && !sprites.empty() && pending < MaxPending &&
            std::chrono::steady_clock::now() < deadline; ++attempts) {
        const int id = sprites.front(); sprites.pop_front();
        if (StartAsset(std::to_string(pack) + "x-" + std::to_string(id), spriteClass, false)) ++pending;
        else ++missedSprites;
    }
    if (!pending && sprites.empty() && !scanData.get() && !reportedReady) {
        reportedReady = true;
        Log("MAP_PRELOAD_READY map=" + std::to_string(target) + " elapsedMs=" + std::to_string(now - started) +
            " loaded=" + std::to_string(loadedAssets) + " missedSprites=" + std::to_string(missedSprites) +
            " retained=" + std::to_string(assets.size()) + " uniqueSprites=" + std::to_string(uniqueSprites.size()) +
            " scan=" + scanStop);
    }
}
void TickAt(ULONGLONG now) {
    if (!module || !module->ready || !module->servicesReady || module->stopped || module->stopRequested ||
        !module->settings.preloadNextMap) {
        ReleaseAssets(); observed = -1; faulted = false; nextTick = 0; return;
    }
    if (now < nextTick) return;
    nextTick = now + 16;
    if (module->InCombat()) { ReleaseAssets(); observed = -1; return; }
    if (faulted) { ReleaseAssets(); return; }
    Ref input(module->FindService("fnv"));
    if (!Exact(input.get(), inputClass)) { ReleaseAssets(); observed = -1; return; }
    // bcpw sets dsvp before walking. bcpx cancels it; the native async map
    // request resets it to -1 once sent. Never send or alter that request.
    const auto destination = api.Value<int64_t>(api.Field(input.get(), "dsvp"));
    if (destination != observed) {
        observed = destination;
        if (destination > 0 && destination <= INT_MAX && destination != target) {
            ReleaseAssets();
            Ref container(api.Call(module->cached.get(), "get_dofusOptionsContainer"));
            Ref setting(api.Field(container.get(), "currentTexturePack"));
            pack = api.Value<int>(api.Call(setting.get(), "get_property"));
            if (pack != 1 && pack != 2 && pack != 4) return;
            assets.reserve(MaxSprites + 1);
            started = now; target = destination;
            if (!StartAsset("map_" + std::to_string(target) + ".asset", mapClass, true)) {
                Log("MAP_PRELOAD_SKIP map=" + std::to_string(target) + " reason=location-unavailable-or-ambiguous");
                ReleaseAssets(); return;
            }
            Log("MAP_PRELOAD_BEGIN map=" + std::to_string(target) + " pack=" + std::to_string(pack) + " spriteLimit=" + std::to_string(MaxSprites) +
                " maxPending=" + std::to_string(MaxPending));
        }
        // -1 can mean arrival request or cancellation. Hold the one cached map
        // for at most30s, allowing the real loader to acquire its own references.
    }
    Advance(now);
}
}
void InstallMapPreload() {
    addressesClass = api.Class("Unity.Addressables.dll", "UnityEngine.AddressableAssets", "Addressables");
    inputClass = api.Class("Core.dll", "", "fce");
    mapClass = api.Class("AleCore.dll", "AleCore.Data.UnityClient", "ClientMapData");
    spriteClass = api.Class("UnityEngine.CoreModule.dll", "UnityEngine", "Sprite");
    handleClass = api.Class("Unity.ResourceManager.dll", "UnityEngine.ResourceManagement.AsyncOperations", "AsyncOperationHandle");
    auto implementation = api.Class("Unity.Addressables.dll", "UnityEngine.AddressableAssets", "AddressablesImpl");
    auto resourceManager = api.Class("Unity.ResourceManager.dll", "UnityEngine.ResourceManagement", "ResourceManager");
    locate = api.FindVerified(implementation, "GetResourceLocations", 3, 0x4D786C0);
    provide = api.FindVerified(resourceManager, "ProvideResource", 3, 0x527B010);
    release = api.FindVerified(resourceManager, "Release", 1, 0x527BFD0);
    getManager = api.FindVerified(addressesClass, "get_ResourceManager", 0, 0x4D83070);
    isValid = api.FindVerified(handleClass, "IsValid", 0, 0x526BC70);
    isDone = api.FindVerified(handleClass, "get_IsDone", 0, 0x526C320);
    status = api.FindVerified(handleClass, "get_Status", 0, 0x526C5C0);
    result = api.FindVerified(handleClass, "get_Result", 0, 0x526BE70);
}
void TickMapPreload() {
    try { TickAt(GetTickCount64()); }
    catch (const std::exception& error) {
        if (!faulted) Log(std::string("MAP_PRELOAD_ERROR ") + error.what());
        faulted = true;
        try { ReleaseAssets(); } catch (...) {} // Retry retained ownership on following ticks.
    }
}
void RestoreMapPreload() {
    try { ReleaseAssets(); observed = -1; }
    catch (const std::exception& error) { Log(std::string("MAP_PRELOAD_CLEANUP_ERROR ") + error.what()); }
}
bool MapPreloadCleanupPending() { return !assets.empty(); }
