#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
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

// GET /v1/me, GET /v1/quests, GET /v1/quests/{slug}/monsters and
// PATCH /v1/quests/{slug}/monsters/{id} answers. Unknown fields are ignored,
// nullable fields accepted; anything else throws std::runtime_error.
Account ParseMe(std::string_view body);
std::vector<Quest> ParseQuests(std::string_view body);
MonsterPage ParseMonsters(std::string_view body);
QuantityUpdate ParseQuantityUpdate(std::string_view body);
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
}
