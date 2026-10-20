#pragma once
struct RenderSnapshot {
    void* animation = nullptr;
    void* renderer = nullptr;
    void* definition = nullptr;
    void* skin = nullptr;
    bool operator==(const RenderSnapshot&) const = default;
};
inline bool ShouldFreeze(bool timerCall, bool rendered, bool dirty, const RenderSnapshot& previous, const RenderSnapshot& current) {
    return timerCall && rendered && !dirty && current.animation && current.renderer && previous == current;
}
