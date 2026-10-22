#pragma once
#include "metamob-http.hpp"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
// "Synchro des captures": after each archimonster or boss capture, +1 to that
// monster in the user's Metamob Ocre quest, nothing else. Metamob only takes
// absolute quantities, so each +1 is a fresh read then an absolute write, kept
// as an in-flight target until confirmed (a resend never counts twice).
namespace metamob {
struct Capture {
    int64_t characterId = 0;    // the local character (one mod instance per game client)
    std::string characterName;  // in-game name, used to pick the quest when there are several
    int64_t monsterId = 0;      // game monster id
    std::string name;           // game name, in the client's language
    int type = 0;               // TypeBoss or TypeArchimonster
    int quantity = 1;           // souls of this monster in the character's loot (1..30)
};
using Notice = std::function<void(const std::string&)>;
// Game-thread API. Never throws and never blocks: requests, DPAPI and files run
// on one worker thread at a time; notices ("Metamob : ...") are posted from
// TickSync on the game thread.
void InstallSync(const std::filesystem::path& settingsPath, Transport transport, Notice notice);
// enabled: the options' "Synchroniser les captures avec Metamob", at every pump.
// Off, nothing more is written: no cycle starts, a running one ends before its
// next write, the captures not sent yet are dropped with one notice (never kept
// for later: they may be entered by hand), a write without a known result is
// only reported. Saving, checking and forgetting the key go on.
void TickSync(bool enabled);
// Cancels a running worker without waiting for it; a forget confirmed just
// before is still done (one local file).
void RestoreSync();
void EnqueueCapture(Capture capture);
// Options: verify the key with GET /v1/me, then store it (refused keys are not
// stored); the text is wiped once used.
void SubmitKey(std::string key);
// Options, "Supprimer" confirmed: the key file goes, and a running cycle sends
// nothing more with that key (a request under way still ends normally).
void ForgetStoredKey();
bool KeyStored();
bool SyncCleanupPending();
}
