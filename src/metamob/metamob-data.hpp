#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
// Metamob API v1 (https://www.metamob.fr/help/api) data: strict parsers of the
// answers the capture sync reads, the bodies it writes, and the rules that pick
// the user's Ocre quest and the monster a capture belongs to. Pure code: no
// network, no files, no game objects. Every error is a fixed French message,
// never a fragment of a body, a header or the key.
namespace metamob {
// Metamob monster type ids (type.id): the sync writes bosses and archimonsters,
// the chat part matches any monster of the quest (TypeMonster: neither).
constexpr int TypeMonster = 1, TypeBoss = 2, TypeArchimonster = 3;
// Documented bound of a quest monster quantity (the site itself allows more).
constexpr int MaxQuantity = 30;

struct Account { std::string username; };
struct Quest {
    std::string slug, characterName, serverName, questType; // questType: quest_template.quest_type.slug ("ocre")
    int gameVersion = 0;    // server.game_version.id (1 = Dofus Unity)
    int parallelQuests = 0;
    // server.id: the calendar's Kralamoure openings are read for it. 0 when the
    // server is null or its id absent or unusable (never refuses the quests: the
    // capture sync does not need it). Last, so aggregate initializers stay valid.
    int64_t serverId = 0;
};
struct QuestMonster {
    int64_t id = 0;         // Metamob id (not the game id)
    std::string name;       // name.fr
    int type = 0, step = 0, quantity = 0;
    // name.en and name.es, empty when absent or unusable: the game names a monster
    // in the client's language (MonsterData.get_name is GetLocalized). Last, so the
    // {id, name, type, step, quantity} initializers stay valid.
    std::string nameEn, nameEs;
};
struct MonsterPage {
    std::string slug;
    std::vector<QuestMonster> monsters;
    int total = 0, limit = 0, offset = 0;
};
struct QuantityUpdate { int64_t monsterId = 0; int quantity = 0; };
// GET /v1/kralove: a Kralamoure opening planned on Metamob (the calendar row of
// calendar-kralove.cpp). Every text is display-safe once parsed and is only
// ever shown as plain text.
struct Kralove {
    int64_t id = 0;             // Metamob's event id: https://www.metamob.fr/kralove/event/<id>
    int64_t at = 0;             // event_datetime (UTC), Unix seconds
    int64_t serverId = 0;       // server.id; 0 when the opening names no server
    std::string server;         // server.name; empty when the server is null
    std::string creator;        // SafeUsername(creator); empty when null (a mutual block or a deleted account)
    std::string description;    // SafeText(description, MaxDescription); empty when null or absent
    int participants = 0, characters = 0, messages = 0; // participants_count, character_count, messages_count
};
// Openings in one answer (3 a day per server at most) and bytes of a
// description kept for the tooltip; a longer description is cut (with « … »).
constexpr size_t MaxKralove = 500, MaxDescription = 280;

// GET /v1/me, GET /v1/quests, GET /v1/quests/{slug}/monsters and
// PATCH /v1/quests/{slug}/monsters/{id} answers. Unknown fields are ignored,
// nullable fields accepted; anything else throws std::runtime_error.
Account ParseMe(std::string_view body);
std::vector<Quest> ParseQuests(std::string_view body);
MonsterPage ParseMonsters(std::string_view body);
QuantityUpdate ParseQuantityUpdate(std::string_view body);
// GET /v1/kralove: strict. Each opening needs an id (unique in the answer), an
// event_datetime read by UtcSeconds and its three counts; description, creator
// and server may be null. Openings by time (then id), the order the API does
// not document. Fixed errors as the other parsers.
std::vector<Kralove> ParseKralove(std::string_view body);
// Body of a single quantity update: {"quantity":N} (absolute value, 0..MaxQuantity).
std::string QuantityBody(int quantity);
// Retry-After as delay-seconds or as an HTTP date (relative to nowUnix),
// clamped to [1, 900]; nullopt when absent or unreadable.
std::optional<int> RetryAfterSeconds(std::string_view header, int64_t nowUnix);
// Personal key: trimmed by the caller, printable ASCII 0x21..0x7E, 16..256 bytes
// (no space, CR or LF can reach an HTTP header). Slug: [A-Za-z0-9_-]{1,64}.
bool ValidKey(std::string_view key);
bool ValidSlug(std::string_view slug);
// Display-safe username: control characters, '<' and '>' removed, at most 40 bytes.
std::string SafeUsername(std::string_view username);
// Display-safe free text (a Kralamoure description), never rich text: invalid
// UTF-8, controls, '<', '>', bidi controls and invisible characters removed,
// line breaks and tabs read as spaces, spaces collapsed and trimmed, at most
// `maximum` bytes (never half a character; « … » ends a text that was cut).
std::string SafeText(std::string_view text, size_t maximum);
// A civil date and time; weekday 0 is Monday, 6 Sunday.
struct CivilTime { int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0, weekday = 0; };
// Days since 1970-01-01 of a proleptic Gregorian date, and the date of a day.
int64_t DayNumber(int year, int month, int day);
CivilTime CivilDay(int64_t dayNumber);
// event_datetime as Metamob writes it: "YYYY-MM-DDTHH:MM:SSZ" (UTC), with an
// optional fraction of a second (ignored) and "+00:00" read as "Z". nullopt for
// anything else: another offset or none, an impossible date or time (February
// 30, 24:00, a leap second), a year outside 2000..2199.
std::optional<int64_t> UtcSeconds(std::string_view text);
// UTC Unix seconds as local civil time with Windows' time zone rules, daylight
// saving included for that very date (GetDynamicTimeZoneInformation, then
// SystemTimeToTzSpecificLocalTimeEx): the game's calendar shows this PC's local
// days. zone: a Windows time zone key ("Romance Standard Time") instead of this
// PC's, for the tests. nullopt when Windows cannot convert, or outside 1970..2199.
std::optional<CivilTime> LocalTime(int64_t unixSeconds, std::wstring_view zone = {});
// Comparison key of a monster name (UTF-8): accents removed, apostrophe, dash
// and space variants unified, lowercase, commas kept (ChooseQuest splits the
// contact field on them). Equal game and Metamob names give equal keys
// ("Tofumanchou l’Empereur" == "Tofumanchou l'Empereur").
std::string NameKey(std::string_view utf8);
// The quest to update: Dofus Unity Ocre quests only (quest type "ocre", game
// version 1). One left: that one. Several: the one whose character_name names
// the character (NameKey match of the whole field or of one of its
// comma/slash/space separated parts). Otherwise none, with the reason to show
// (problem: the sync's notice) and its kind (the chat part's tooltip says what
// to fix): NoQuest, no Unity Ocre quest; Several, none naming the character.
enum class QuestProblem { None, NoQuest, Several };
struct QuestChoice { std::optional<Quest> quest; std::string problem; QuestProblem kind = QuestProblem::None; };
QuestChoice ChooseQuest(const std::vector<Quest>& quests, std::string_view characterName);
// The quest monster a captured game monster is: same type, unique NameKey match
// of its client-language name with name.fr, name.en or name.es, a comma read as
// a space (Metamob's Spanish "Colmillazaqui, el Inagotable" is the game's
// "Colmillazaqui el Inagotable").
std::optional<QuestMonster> FindMonster(const std::vector<QuestMonster>& monsters, std::string_view gameName, int type);
// FindMonster for a whole list at once (the chat part and the maps ask for
// hundreds of monsters per reading): each (type, name key) of the list's names,
// to the first entry naming it; none when it names two monsters (two ids).
// FindIndexed(IndexMonsters(list), name, type) is the entry of the monster
// FindMonster(list, name, type) finds.
using MonsterIndex = std::map<std::pair<int, std::string>, std::optional<size_t>>;
MonsterIndex IndexMonsters(const std::vector<QuestMonster>& monsters);
std::optional<size_t> FindIndexed(const MonsterIndex& index, std::string_view gameName, int type);
}
