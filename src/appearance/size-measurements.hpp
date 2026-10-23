#pragma once
#include "core/runtime.hpp"
#include <optional>
#include <unordered_map>
#include <vector>

struct SizeMeasurements {
    struct Pending { managed::Ref look; bool player; ULONGLONG expires; };
    managed::Klass library{};
    managed::Method resolveKey{};
    managed::Ref idle;
    std::unordered_map<std::string, float> heights;
    std::vector<Pending> pending;
    float playerHeight = 0;
    bool changed = false;
    ULONGLONG nextRetry = 0;
    void Install();
    std::optional<float> Height(managed::Obj look);
    std::optional<float> Measure(managed::Obj look, bool player, bool defer = true);
    bool Tick();
};
