#pragma once
// The Roublards' secret passage between the Cité d'Astrub [5,-15] and the
// Roublard temple [10,16], for the mod's travel (option travelRoublardTemple).
//
// Bad Aboum (NPC 1422) takes a character from the crypt under the old chapel of
// Astrub to the Roublard temple, back, and to the Amakna village [3,3], through
// his dialog. The game's route graph has no NPC transitions: its autopilot never
// takes him. The crypt (map 67372032) is an ordinary map of that graph, reached
// through the Astrub sewers, so the game's autopilot walks there; the mod then
// talks to him and picks the reply the way a click does, and plans again from
// the arrival. Where he stands on the temple side, where each trip arrives and
// which trips he offers a character are learned from what the mod sees (its own
// passages and the player's), and kept beside the shared settings.
#include "travel/chat-route.hpp"
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace roublard_passage {
enum class Side : int { Astrub = 0, Temple = 1, Amakna = 2, None = 3 };
constexpr int SideCount = 3;
constexpr int BadAboum = 1422;
// His questions, one per side he stands on.
constexpr int AstrubMessage = 8722, TempleMessage = 8723, AmaknaMessage = 8746;
// Built-in maps: the crypt under the old chapel of Astrub (reached by the
// sewers), the room under the Amakna village well, and the temple's entrance
// (an arrival estimate until the real one is seen).
constexpr int64_t AstrubCrypt = 67372032, AmaknaWell = 67373056, TempleEntrance = 67108864;
// A passage costs two map changes in the route comparison (dialog and loading):
// it is taken only when it saves at least one map change over every other way.
constexpr int PassageCost = 2;
const char* SideName(Side side);
// The side at these coordinates: [5,-15] Astrub, [10,16] or [10,15] the temple,
// [3,3] the Amakna village; None elsewhere.
Side SideAt(int x, int y);
// The side of a map: the temple's own maps and the Roublards' lair (one of them
// is not at [10,16]), else by its coordinates.
Side SideOf(int64_t map, int x, int y);
// The side his question belongs to (8722 Astrub, 8723 temple, 8746 Amakna).
Side QuestionSide(int message);
// The replies leading to a side (one per side he stands on), and the replies that
// cost kamas (the chapel mercenary's visit): never chosen.
bool LeadsTo(int reply, Side destination);
bool Paid(int reply);
bool Travel(int reply);
bool Question(int message);
// Shortcut ids: 100 + 10 × origin + destination (astrub>temple is 101).
int LinkId(Side from, Side to);
bool LinkSides(int id, Side& from, Side& to);

// What the mod learned, shared by the characters of this Windows account.
struct Knowledge {
    int64_t npc[SideCount] = {};                 // where Bad Aboum was seen, per side
    std::vector<int64_t> doubtful;               // maps where he was looked for in vain once
    std::vector<int64_t> absent;                 // ... and a second time: no more looks there
    int64_t arrival[SideCount][SideCount] = {};  // arrival map per trip, origin × destination
    struct Character {
        int64_t id = 0;
        std::vector<int> offered;  // travel replies he offered this character
        unsigned refused = 0;      // RefusedBit per trip he did not offer from where he was asked
        bool operator==(const Character&) const = default;
    };
    std::vector<Character> characters;
    bool operator==(const Knowledge&) const = default;
};
// One bit per trip: what he offers depends on the side where he is asked.
unsigned RefusedBit(Side from, Side to);
// Where to find him on a side: the learned map, else the built-in one (Astrub,
// Amakna), unless he was found absent there twice.
int64_t NpcMap(const Knowledge& known, Side side);
// Where a trip arrives: the learned map, else his map on that side, else that
// side's built-in map (the temple's entrance), only as the route comparison's
// estimate.
int64_t ArrivalMap(const Knowledge& known, Side from, Side to);
// The shortcuts the route comparison may take for this character: every trip
// from a side where he is known that he did not refuse the character.
std::vector<chat_route::Shortcut> Links(const Knowledge& known, int64_t character);
// One learning step, applied to the file's current content when saved.
// Offered: from is the side where he was asked; Refused: the trip from > to.
struct Change {
    enum class Kind { Npc, Doubt, Absent, Arrival, Offered, Refused } kind = Kind::Npc;
    Side from = Side::None, to = Side::None;
    int64_t map = 0, character = 0;
    std::vector<int> replies;
};
// True when it changed what is known.
bool Apply(Knowledge& known, const Change& change);
// The file beside the shared settings (DofusMiniatures-roublard-passage.json).
std::filesystem::path DataPath(const std::filesystem::path& settingsPath);
// A file whose content is not the mod's: kept as it is, never rewritten.
struct InvalidFile : std::runtime_error { using std::runtime_error::runtime_error; };
// An absent file is an empty knowledge; invalid content throws InvalidFile,
// anything else (in use, inaccessible) another exception.
Knowledge Read(const std::filesystem::path& path);
// Applies the changes to the file's current content under its lock (two clients
// never lose each other's learning) and returns the merged knowledge.
Knowledge Update(const std::filesystem::path& path, const std::vector<Change>& changes);

// The game's NPC dialog as the mod reads it (Core.UILogic.NpcDialog.NpcDialog).
struct Dialog {
    // The dialog holds a question: its NPC entity is set and a button holds a
    // reply id. NpcDialog.Open shows six empty buttons, with the previous
    // question's entity, message and lock, until its own question arrives.
    bool shown = false;
    int npc = 0;               // NpcData.id of that NPC
    int64_t entity = 0;        // its entity on the map
    int message = 0;           // the question
    std::vector<int> replies;  // reply id per button; -1 closes, 0 is unused
    bool locked = false;       // a reply was sent and its answer is awaited
    bool fresh = false;        // a list of buttons displayed after the last Mark()
};
// The game actions the passage needs: native in the mod, scripted in tests.
struct Game {
    virtual ~Game() = default;
    virtual int64_t Character() = 0;
    virtual bool Locate(int64_t map, int& x, int& y) = 0;
    // Entities of the current map with this NPC id: their count and the first
    // one; -1 while the map's entity registry is unavailable.
    virtual int FindNpc(int npc, int64_t& entity) = 0;
    virtual void Mark() = 0;
    virtual Dialog Read() = 0;
    virtual bool Talk(int64_t entity) = 0;  // the NPC menu's « Parler »
    // That reply button's click, only when it still holds this reply.
    virtual bool Reply(int button, int reply) = 0;
    // The dialog's close button, only when it still is this entity's Bad Aboum dialog.
    virtual bool Close(int64_t entity) = 0;
};

// The end of one trip through him.
enum class Outcome { Running, Arrived, Failed, Aborted };
struct Result {
    Outcome outcome = Outcome::Running;
    int64_t map = 0;     // where the character is at the end
    std::string reason;  // for the log
    std::string notice;  // for the chat, without « TP : »
};

// A trip that failed without teaching anything is not planned again for that
// character for this long (this session only); a map where he was not found,
// for no character.
constexpr uint64_t PauseMs = 1800000;
// settingsPath: the shared settings file; empty keeps the learning in memory.
void Install(const std::filesystem::path& settingsPath);
bool Available();
// Ends any run and saves what is still unsaved.
void Restore();
// Tests: a scripted game (nullptr returns to the native one) and a fresh state.
void UseGame(Game* game);
const Knowledge& Known();
void ResetKnowledge();
// The shortcuts for the current character; empty when unavailable.
std::vector<chat_route::Shortcut> Shortcuts(uint64_t now);
// Bad Aboum's map for a shortcut of the route; 0 when it is not a passage.
int64_t Entrance(int link);
// Describes a shortcut for the chat: « d’Astrub au temple Roublard ».
std::string Describe(int link);
// Starts the trip on his map; false when the link or the map does not fit.
bool Start(int link, int64_t map, uint64_t now);
bool Running();
// One step on the main thread: map is the current map, ready its loaded scene.
Result Step(int64_t map, bool ready, uint64_t now);
// Ends a run without any game action (the journey ended).
void Stop();
// Learns from the player's own passages: read-only and cheap, every pump.
void Observe(int64_t map, bool ready, uint64_t now);
}
