#pragma once
#include "metamob-data.hpp"
#include "metamob-http.hpp"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>
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
// The stored key as the options' key row tells it. None: no key file (or one
// being forgotten); Saved: a key file not checked with Metamob in this session
// yet (at start, or saved or replaced by another client); Verified: Metamob
// accepted it; Unverified: saved or checked while Metamob could not confirm it;
// Refused: Metamob refused it (until it accepts it again or another key is
// saved); Unreadable: it cannot be decrypted on this Windows account.
enum class KeyStatus { None, Saved, Verified, Unverified, Refused, Unreadable };
// A game monster as the chat part matches it with the quest: its name in the
// client's language and its Metamob type (TypeBoss, TypeArchimonster or TypeMonster).
struct GameMonster { std::string name; int type = 0; };
// What the chat part reads of the game, on the game thread only (module.cpp
// gives capture detection's verified reads): a monster by game id (nullopt:
// unknown or unreadable), and the local character's name, which picks the quest
// when there are several (empty: unknown).
struct GameView {
    std::function<std::optional<GameMonster>(int monsterId)> monster;
    std::function<std::string()> character;
};
// Game-thread API. Never throws and never blocks: requests, DPAPI and files run
// on one worker thread at a time; notices ("Metamob : ...") are posted from
// TickSync on the game thread.
void InstallSync(const std::filesystem::path& settingsPath, Transport transport, Notice notice, GameView game = {});
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
// Options, the trash then "Oui": the key file goes, and a running cycle sends
// nothing more with that key (a request under way still ends normally).
void ForgetStoredKey();
bool KeyStored();
// The key row's status, on the game thread: what the workers reported for the
// key file, tied to that file's size and dates (read at each call; the key is
// never decrypted here). Another key file than the one known (saved, replaced
// or forgotten, here or by another client) is Saved, or None.
KeyStatus StoredKeyStatus();
// The key row shows a Saved key: one GET /v1/me with it, silent, once per key
// file and session, never before a pause ends (backoff, Retry-After). again:
// « Clé non vérifiée » clicked, an Unverified key is checked once more.
void CheckStoredKey(bool again);
// Chat « Metamob dans le chat », on the game thread: the label of a game
// monster from the user's Metamob Ocre quest (« Metamob : N », green from 1, red
// at 0; « Metamob : ? » while the quest is not read yet or its last reading
// failed), read from a cache the worker refreshes; never a request here. Empty
// for a monster outside L’éternelle moisson, not in the quest or unknown to the
// game view, or when no usable key is saved. colorStatus: rich-text colors.
std::string MetamobChatCaption(int monsterId, bool colorStatus);
// Plain-text tooltip of that label; empty when the caption is. After a reading
// that found no Dofus Unity Ocre quest, or several none of which names this
// character, its « ? » says what to fix on metamob.fr.
std::string MetamobChatDetail(int monsterId);
// A shown chat label holds a monster of L’éternelle moisson (one a Metamob part
// can follow): keep the quest cache fresh. It is read at once the first time,
// then at most every 10 min while a label asked within the last minute; never
// while a capture waits, a key is being saved or forgotten, nor without a
// usable key. Called at every chat pump while such a label is shown.
void WantMetamobCatalog();
// « Archimonstres manquants » on the maps, game thread, cache only: whether a
// game monster of the Ocre quest is in the user's Metamob quest (usable false:
// no usable key, not an Ocre goal, unknown to the game view or not in the
// quest), its quantity (-1 while the list is not read or its last reading
// failed) and why there is none. The caller keeps the list fresh with
// WantMetamobCatalog.
enum class OcreList {
    NotRead,       // not read yet
    Read,          // read: the quantity is the list's
    Failed,        // the last reading failed: read again automatically
    NoQuest,       // no Dofus Unity Ocre quest on the account (read again 10 min later)
    SeveralQuests, // several, none naming this character (read again 10 min later)
};
struct OcreQuantity { bool usable = false; int quantity = -1; OcreList list = OcreList::NotRead; };
OcreQuantity MetamobOcreQuantity(int monsterId);
// « Ouvertures Kralamoure dans le calendrier » (calendar-kralove.cpp), game
// thread, cache only: the openings Metamob lists (GET /v1/kralove) for the
// server of the user's Dofus Unity Ocre quest (the one ChooseQuest picks for
// this character), from a week before this PC's date on. Read at once the first
// time, then at most every 10 min while the calendar is shown (it asked within
// the last second); never while a capture waits, a key is being saved or
// forgotten, nor without a usable key. Failures are silent: tried again 30 s later, twice
// longer each time up to 15 min, 10 min after a lasting one (quest, server, no
// such list); a 429 also waits for its Retry-After, as the captures do.
enum class KraloveStatus {
    Loading,        // not read yet
    Ready,          // read: openings and server below
    Unreachable,    // no answer, 5xx, or a page that is not Metamob's JSON
    Busy,           // 429: read again after its Retry-After
    Refused,        // the key refused (Metamob's JSON 401/403): no longer usable
    KeyUnreadable,  // the key file cannot be decrypted on this Windows account
    Unavailable,    // Metamob's JSON 404: no such list on its API
    Unreadable,     // a JSON answer that does not read as the documented list
    NoQuest,        // no Dofus Unity Ocre quest on the account: no server to read
    SeveralQuests,  // several, none naming this character
    NoServer,       // the chosen quest names no server
};
struct KraloveView {
    bool usable = false;        // a usable key (saved, valid or not verified; no forget under way): nothing below otherwise
    KraloveStatus status = KraloveStatus::Loading; // the last reading's outcome
    bool read = false;          // the openings below come from a reading (kept through passing failures)
    std::string server;         // their server's name
    int64_t fromDay = 0;        // first day asked (DayNumber): the days before it are not read
    std::vector<Kralove> openings; // that server's only, by time
    uint64_t version = 0;       // changes whenever the reading kept here changes
};
// The calendar row is shown: keep its openings fresh (see above). Called at
// every pump while it is shown, and only then.
void WantKralove();
KraloveView MetamobKralove();
bool SyncCleanupPending();
}
