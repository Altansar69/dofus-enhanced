#pragma once
#include "travel/chat-tp-links.hpp"
#include <cstdint>
#include <vector>

namespace chat_travel {
struct Destination { int64_t map = 0; int x = 0, y = 0, world = 0, cost = 0; bool known = false; int type = 0; };
// PassageWalk: the game's autopilot walks to Bad Aboum, the Roublards' secret
// passage; Passage: the mod talks to him and waits for his teleport.
enum class Phase { Idle, Havenbag, Destinations, Planning, DirectQuote, Arrival, Onward, PassageWalk, Passage };
// The walk to Bad Aboum may cross a city; his dialog and teleport have their
// own shorter limits (roublard_passage).
constexpr uint64_t PassageWalkMs=600000, PassageMs=60000;
struct Journey {
    Phase phase=Phase::Idle;
    ChatTpTarget target{};
    int64_t sourceMap=0, arrivalMap=0;
    uint64_t deadline=0;
    unsigned readySamples=0, onwardAttempts=0, idleSamples=0;
    bool onwardAccepted=false;
    uint64_t onwardAt=0;
    void Begin(ChatTpTarget destination,int64_t currentMap,uint64_t now) {
        target=destination; sourceMap=currentMap; arrivalMap=0; phase=Phase::Havenbag; deadline=now+30000;
    }
    void Interact(int64_t map,uint64_t now) { sourceMap=map; phase=Phase::Destinations; deadline=now+30000; }
    void Quote(uint64_t now) { phase=Phase::DirectQuote; deadline=now+30000; }
    void Teleport(int64_t map,uint64_t now) { arrivalMap=map; phase=Phase::Arrival; deadline=now+30000; readySamples=0; }
    bool Settled(bool ready) { readySamples=ready?readySamples+1:0; return readySamples>=2; }
    void Walk(uint64_t now) { phase=Phase::Onward; onwardAt=now; ++onwardAttempts; readySamples=0; }
    // The game's autopilot walks to Bad Aboum's map: arrival there starts the passage.
    void WalkToPassage(int64_t map,uint64_t now) { arrivalMap=map; phase=Phase::PassageWalk; deadline=now+PassageWalkMs; readySamples=idleSamples=0; }
    void Pass(uint64_t now) { phase=Phase::Passage; deadline=now+PassageMs; }
    bool Expired(uint64_t now) const { return phase!=Phase::Idle && now>=deadline; }
    bool CanWalk(int64_t map) const { return phase==Phase::Arrival && map==arrivalMap; }
    void Clear() { *this={}; }
};
}
