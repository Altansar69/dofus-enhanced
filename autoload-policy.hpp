#pragma once
#include <cstdint>
#include <map>
#include <utility>

// Pure policy: no registry, process enumeration, Windows API, or game code.
class AutoLoadPolicy {
public:
    enum class Decision { Ignore, Wait, Attempt, Expired, Disabled };
    static constexpr uint64_t GraceMs = 25000;
    static constexpr uint64_t DeadlineMs = 180000;
    explicit AutoLoadPolicy(uint64_t watcherCreationTime) : cutoff(watcherCreationTime) {}
    Decision Observe(uint32_t pid, uint64_t creationTime, uint64_t nowMs, bool alive, bool ready, bool enabled) {
        if (!enabled) return Decision::Disabled;
        // The time cutoff also ignores pre-existing clients that could not be
        // inspected in the first snapshot. PID reuse is a distinct identity.
        if (!alive || !pid || !creationTime || creationTime <= cutoff) return Decision::Ignore;
        auto [position, inserted] = states.try_emplace({pid, creationTime}, State{nowMs, false});
        auto& state = position->second;
        if (state.finished) return Decision::Ignore;
        const auto elapsed = nowMs - state.firstSeen;
        if (elapsed >= DeadlineMs) { state.finished = true; return Decision::Expired; }
        if (elapsed < GraceMs || !ready) return Decision::Wait;
        state.finished = true; // Reserve the only attempt before any load operation.
        return Decision::Attempt;
    }
private:
    struct State { uint64_t firstSeen; bool finished; };
    uint64_t cutoff;
    std::map<std::pair<uint32_t, uint64_t>, State> states;
};
