#include "size-measurements.hpp"
#include <algorithm>
#include <cmath>

using namespace managed;

void SizeMeasurements::Install() {
    library = api.Class("Ankama.Dofus.Core.Characters.dll", "", "mhy");
    resolveKey = api.Find(library, "dcdf", 3);
    api.Native(resolveKey, 0x76F790);
    idle.reset(api.string_new("AnimStatique"));
}
std::optional<float> SizeMeasurements::Height(Obj look) {
    // Resolve the ORIGINAL look's idle asset, including breed mapping for bone 1.
    // Never call the library loader: only read definitions already in its cache.
    Ref cache(api.StaticField(library, "fhod"));
    if (!cache.get()) return std::nullopt;
    if (Read<int>(look, 0x1C) == 1 && (!api.StaticField(library, "fhns") ||
        !api.StaticField(library, "fhnr"))) return std::nullopt;
    Obj rawKey = nullptr;
    if (!api.Value<bool>(api.Invoke(resolveKey, nullptr, {look, idle.get(), &rawKey})) || !rawKey) return std::nullopt;
    Ref key(rawKey);
    const auto name = api.String(key.get());
    auto known = heights.find(name);
    float authoredHeight = 0;
    if (known != heights.end()) authoredHeight = known->second;
    else {
        // ValueTuple<AnimatedObjectDefinition,int> layout in the pinned x64 build.
        struct CachedDefinition { Obj definition = nullptr; int lifetime = 0; int padding = 0; } entry;
        static_assert(sizeof(CachedDefinition) == 16);
        if (!api.Value<bool>(api.Call(cache.get(), "TryGetValue", {key.get(), &entry})) || !entry.definition) return std::nullopt;
        Ref definition(entry.definition);
        Ref animations(api.Field(definition.get(), "animations"));
        if (!animations.get()) return std::nullopt;
        const int count = api.Value<int>(api.Call(animations.get(), "get_Length"));
        if (count < 0 || count > 4096) return std::nullopt;
        // The authored idle envelope is stable across frames. Prefer ordinary
        // idle over combat/exploration poses; take the maximum across directions.
        float alternate = 0;
        for (int i = 0; i < count; ++i) {
            auto animation = Read<Obj>(animations.get(), 0x20 + sizeof(Obj) * i);
            if (!animation) continue;
            const auto label = api.String(api.Field(animation, "name"));
            if (label.rfind("AnimStatique", 0) != 0) continue;
            const float height = Read<float>(animation, 0x34); // Rect at +0x28, height +12.
            if (!std::isfinite(height) || height <= 0) continue;
            if (label == "AnimStatique" || label.rfind("AnimStatique_", 0) == 0)
                authoredHeight = std::max(authoredHeight, height);
            else if (label == "AnimStatiqueCombat" || label.rfind("AnimStatiqueCombat_", 0) == 0 ||
                label == "AnimStatiqueExplo" || label.rfind("AnimStatiqueExplo_", 0) == 0)
                alternate = std::max(alternate, height);
        }
        if (authoredHeight == 0) authoredHeight = alternate;
        if (authoredHeight <= 0) return std::nullopt;
        if (heights.size() < 4096) heights.emplace(name, authoredHeight);
    }
    const float scaled = authoredHeight * std::abs(Read<float>(look, 0x30));
    if (!std::isfinite(scaled) || scaled <= 0) return std::nullopt;
    return scaled;
}
std::optional<float> SizeMeasurements::Measure(Obj look, bool player, bool defer) {
    const auto height = Height(look);
    // The root bone 1 is the unmounted player body. A mount's root is excluded.
    bool referencePlayer = height && player && Read<int>(look, 0x1C) == 1 && playerHeight == 0;
    if (referencePlayer) {
        Ref children(api.Field(look, "fhyu"));
        int riderSlot = 2;
        Obj rawRiders = nullptr;
        if (children.get() && api.Value<bool>(api.Call(children.get(), "TryGetValue", {&riderSlot, &rawRiders})) && rawRiders) {
            Ref riders(rawRiders);
            referencePlayer = api.Value<int>(api.Call(riders.get(), "get_Count")) == 0;
        }
    }
    if (referencePlayer) {
        playerHeight = *height;
        changed = true;
        Log("SIZE_REFERENCE_READY authoredIdleHeight=" + std::to_string(playerHeight));
    }
    if (!height && defer && pending.size() < 1024) {
        auto found = std::find_if(pending.begin(), pending.end(), [look](const Pending& item) { return item.look.get() == look; });
        if (found == pending.end()) pending.push_back({Ref(look), player, GetTickCount64() + 60000});
        else found->player = found->player || player;
    }
    return height;
}
bool SizeMeasurements::Tick() {
    const auto now = GetTickCount64();
    if (now >= nextRetry) {
        nextRetry = now + 1000;
        for (auto item = pending.begin(); item != pending.end();) {
            if (Measure(item->look.get(), item->player, false)) { changed = true; item = pending.erase(item); }
            else if (now >= item->expires) item = pending.erase(item);
            else ++item;
        }
    }
    const bool refresh = changed && playerHeight > 0;
    changed = false;
    return refresh;
}
