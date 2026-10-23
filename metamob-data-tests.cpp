#include "metamob-data.hpp"
#include <windows.h>
#include <climits>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace metamob;
namespace {
// Not a real key (tests never hold one, CGU 5.3) and a marker planted in the
// bodies: no 4-byte piece of either may reach an error message.
const std::string FakeKey = "TEST-KEY-0123456789-NOT-REAL", Marker = "Zorglub-Corps-Prive";
// Every error is one of these fixed texts, never built from the input.
const std::set<std::string> Fixed{"Réponse Metamob trop volumineuse.", "Réponse Metamob illisible.",
    "Réponse Metamob inattendue.", "Nombre Metamob invalide.", "Texte Metamob invalide.", "Quête Metamob invalide.",
    "Pagination Metamob incohérente.", "Monstre Metamob en double.", "Quantité Metamob hors limites.", "Date Metamob invalide.",
    "Ouverture Metamob en double."};
const std::string NoQuest = "Metamob : aucune quête Ocre Dofus Unity sur ton compte, rien n’a été modifié.";
const std::string SeveralQuests = "Metamob : plusieurs quêtes Ocre sur ton compte ; indique le nom de ce personnage dans "
    "« Personnage à contacter » de la bonne quête.";
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
bool Leaks(const std::string& text, const std::string& secret) {
    for (size_t i = 0; i + 4 <= secret.size(); ++i) if (text.find(secret.substr(i, 4)) != std::string::npos) return true;
    return false;
}
template<class F> std::string Error(F action) {
    try { action(); } catch (const std::runtime_error& error) { return error.what(); }
    return {};
}
// Must throw one of the fixed texts, which never quotes the body or the key.
template<class F> void Reject(F action, const char* message) {
    const auto error = Error(action);
    Check(Fixed.contains(error), message);
    Check(!Leaks(error, FakeKey) && !Leaks(error, Marker), "An error message quotes the body or the key");
}
template<class F> void RejectAs(F action, const std::string& expected, const char* message) {
    Check(Error(action) == expected, message);
}
// One piece of a realistic answer changed.
std::string With(std::string text, const std::string& from, const std::string& to) {
    const auto at = text.find(from);
    Check(at != std::string::npos, "Test fixture: text to replace not found");
    return text.replace(at, from.size(), to);
}
std::string Chosen(const std::vector<Quest>& quests, std::string_view character) {
    const auto choice = ChooseQuest(quests, character);
    return choice.quest && choice.problem.empty() && choice.kind == QuestProblem::None ? choice.quest->slug : "-";
}
int64_t Found(const std::vector<QuestMonster>& monsters, std::string_view name, int type) {
    const auto monster = FindMonster(monsters, name, type);
    // The chat part's and the maps' index answers exactly as FindMonster does.
    const auto indexed = FindIndexed(IndexMonsters(monsters), name, type);
    Check(indexed.has_value() == monster.has_value() && (!indexed || monsters[*indexed].id == monster->id),
        "The list's index finds the monster FindMonster finds");
    return monster ? monster->id : -1;
}

const std::string MeBody = R"json({"data":{"username":"player1","bio":"Ma bio","avatar":{"id":123,
  "name":{"fr":"Tofu","en":"Tofu","es":"Tofu"},"image":"tofu.png"},"last_active":"2026-01-27T14:22:00Z"}})json";
const std::string QuestsBody = R"json({"data":[
 {"slug":"a1b2c3d4","character_name":"Mon perso","current_step":15,"parallel_quests":6,"show_trades":true,"is_favorite":true,
  "server":{"id":1,"name":"Draconiros","community":"France","game_version":{"id":1,"name":"Dofus (Unity)"}},
  "quest_template":{"id":1,"quest_type":{"id":1,"slug":"ocre","name":{"fr":"Ocre","en":"Ochre","es":"Ocre"}},
   "monster_count":337,"step_count":18},"future_field":{"nested":[1,2.5,null,"x"]}},
 {"slug":"Retro_77-x","character_name":null,"current_step":3,"parallel_quests":1,"show_trades":false,"is_favorite":false,
  "server":{"id":20,"name":"Boune","community":"France","game_version":{"id":2,"name":"Dofus Retro (1.29)"}},
  "quest_template":{"id":2,"quest_type":{"id":1,"slug":"ocre","name":{"fr":"Ocre","en":"Ochre","es":"Ocre"}},
   "monster_count":530,"step_count":34}},
 {"slug":"dok1","character_name":"Bob\u2019s \ud83d\ude00","current_step":1,"parallel_quests":2,"show_trades":true,
  "is_favorite":false,"server":null,"quest_template":{"id":4,"quest_type":{"id":2,"slug":"dokille",
  "name":{"fr":"Dokille","en":"Grofus","es":"Dokille"}},"monster_count":20,"step_count":4}},
 {"slug":"q4","parallel_quests":1,"server":{"id":13,"name":"Tal Kasha","game_version":null},
  "quest_template":{"id":1,"quest_type":null,"monster_count":337,"step_count":18}}]})json";
const std::string MonstersBody = R"json({"data":{"slug":"a1b2c3d4","monsters":[
 {"id":305,"name":{"fr":"Bouftou Royal","en":"Royal Gobball","es":"Jalató Real"},"image":"bouftou_royal.png",
  "level_min":12,"level_max":20,"type":{"id":2,"name":{"fr":"boss","en":"boss","es":"jefe"}},"step":1,"quantity":0,
  "want":6,"offer":0,"trade_offer":null,"trade_want":null},
 {"id":330,"name":{"fr":"Mob l\u0027\u00c9ponge","en":"Mob the Sponge","es":"Mob la Esponja"},"image":"mob.png",
  "level_min":80,"level_max":80,"type":{"id":2,"name":{"fr":"boss","en":"boss","es":"jefe"}},"step":2,"quantity":45,
  "want":0,"offer":39,"trade_offer":2,"trade_want":null},
 {"id":420,"name":{"fr":"Tofumanchou l'Empereur","en":"Tofumanchou the Emperor","es":"Tofumanchú el Emperador"},
  "image":"tofu.png","level_min":50,"level_max":50,"type":{"id":3,"name":{"fr":"archimonstre","en":"archmonster",
  "es":"archimonstruo"}},"step":4,"quantity":1,"want":5,"offer":0,"trade_offer":null,"trade_want":null,
  "reference":{"id":20,"name":{"fr":"Tofu","en":"Tofu","es":"Tofu"}}},
 {"id":421,"name":{"fr":"Abrakroc l'édenté","en":"x","es":"y"},"type":{"id":3},"step":4,"quantity":3},
 {"id":422,"name":{"fr":"Larvaloeil l'Émue","en":"x","es":"y"},"type":{"id":3},"step":5,"quantity":0},
 {"id":423,"name":{"fr":"Tiwa'Missou le Gateux","en":"x","es":"y"},"type":{"id":3},"step":9,"quantity":2},
 {"id":424,"name":{"fr":"Scapé l'Epée","en":"x","es":"y"},"type":null,"step":12,"quantity":0}],
 "pagination":{"total":337,"limit":200,"offset":0}},"meta":{"generated":"2026-10-22T12:00:00Z"}})json";
const std::string UpdateBody = R"json({"data":{"monster_id":123,"quantity":5,"owned":5,"status":2,
  "effective_offer":0,"effective_want":0}})json";
// One quest or one monster in an otherwise valid answer (single lines, so a
// fixture can change any piece of them).
const std::string OneQuest = R"json({"data":[{"slug":"a1b2c3d4","character_name":"Bob","parallel_quests":6,)json"
    R"json("server":{"name":"Draconiros","game_version":{"id":1}},"quest_template":{"quest_type":{"slug":"ocre"}}}]})json";
const std::string QuestObject = OneQuest.substr(9, OneQuest.size() - 11); // without {"data":[ and ]}
const std::string OneMonster = R"json({"data":{"slug":"a1b2c3d4","monsters":[{"id":420,"name":{"fr":"Tofumanchou l'Empereur",)json"
    R"json("en":"x","es":"y"},"type":{"id":3},"step":4,"quantity":1}],"pagination":{"total":1,"limit":200,"offset":0}}})json";

void Answers() {
    Check(ParseMe(MeBody).username == "player1", "GET /me username");
    Check(ParseMe(R"json({"data":{"username":"Cl\u00e9ment\u2019s"}})json").username == "Clément’s", "JSON escapes decode");
    Check(ParseMe(R"json({"data":{"username":"","avatar":null}})json").username.empty(), "Empty username is still an answer");
    {
        // A control character in the username does not refuse the key: SafeUsername removes it.
        const auto odd = ParseMe(R"json({"data":{"username":"<i>Zed\u0007\u202e</i>"}})json").username;
        Check(odd.size() == 14 && SafeUsername(odd) == "iZed/i", "Username shown only through SafeUsername");
    }
    Check(ParseMe(R"json({"data":{"username":")json" + std::string(1024, 'u') + "\"}}").username.size() == 1024, "Username bound");
    for (const auto& body : std::vector<std::string>{R"json({"data":{"username":null}})json",
        R"json({"data":{"username":7}})json", R"json({"data":{"name":"player1"}})json", R"json({"username":"player1"})json",
        R"json({"data":["player1"]})json", R"json({"data":null})json",
        R"json({"data":{"username":")json" + std::string(1025, 'u') + "\"}}"})
        Reject([&] { ParseMe(body); }, "Bad /me answer accepted");

    const auto quests = ParseQuests(QuestsBody);
    Check(quests.size() == 4, "GET /quests count");
    Check(quests[0].slug == "a1b2c3d4" && quests[0].characterName == "Mon perso" && quests[0].serverName == "Draconiros" &&
        quests[0].questType == "ocre" && quests[0].gameVersion == 1 && quests[0].parallelQuests == 6, "Unity Ocre quest fields");
    Check(quests[1].characterName.empty() && quests[1].gameVersion == 2 && quests[1].questType == "ocre", "Null character_name");
    Check(quests[2].characterName == "Bob’s \U0001F600" && quests[2].serverName.empty() && quests[2].gameVersion == 0 &&
        quests[2].questType == "dokille", "Null server; surrogate pair escape");
    Check(quests[3].characterName.empty() && quests[3].serverName == "Tal Kasha" && quests[3].gameVersion == 0 &&
        quests[3].questType.empty(), "Absent character_name, null game_version and quest_type");
    Check(ParseQuests(R"json({"data":[]})json").empty(), "No quest at all");
    Check(QuestObject.front() == '{' && QuestObject.back() == '}' && ParseQuests(OneQuest).size() == 1, "Minimal quest");
    Check(ParseQuests(With(OneQuest, "\"Bob\"", "\"" + std::string(1024, 'b') + "\"")).size() == 1, "Contact field bound");
    {
        std::string many = "{\"data\":[";
        for (int i = 0; i < 100; ++i) many += (i ? "," : "") + With(QuestObject, "a1b2c3d4", "q" + std::to_string(i));
        Check(ParseQuests(many + "]}").size() == 100, "A hundred quests");
        Reject([&] { ParseQuests(many + "," + With(QuestObject, "a1b2c3d4", "q100") + "]}"); }, "Over a hundred quests accepted");
    }
    for (const auto& [from, to] : std::vector<std::pair<std::string, std::string>>{
        {"\"slug\":\"a1b2c3d4\"", "\"slug\":5"}, {"a1b2c3d4", "a b"}, {"a1b2c3d4", "../x"}, {"a1b2c3d4", std::string(65, 's')},
        {"a1b2c3d4", R"json(abc\r\nX-Evil: )json" + FakeKey}, {"a1b2c3d4", ""}, {"\"slug\":\"a1b2c3d4\",", ""},
        {"\"parallel_quests\":6", "\"parallel_quests\":\"6\""}, {"\"parallel_quests\":6", "\"parallel_quests\":6.0"},
        {"\"parallel_quests\":6", "\"parallel_quests\":-1"}, {"\"parallel_quests\":6", "\"parallel_quests\":true"},
        {"\"parallel_quests\":6", "\"parallel_quests\":18446744073709551615"}, {"\"parallel_quests\":6", "\"parallel_quests\":null"},
        {"\"parallel_quests\":6", "\"parallel_quests\":1000001"}, {"\"parallel_quests\":6,", ""},
        {"\"server\":{\"name\":\"Draconiros\",\"game_version\":{\"id\":1}}", "\"server\":\"Draconiros\""},
        {"\"Draconiros\"", "null"}, {"\"Draconiros\"", "\"\""}, {"Draconiros", Marker + R"json(\u0007)json"},
        {"Draconiros", "<b>Draco</b>"}, {"Draconiros", std::string(129, 'd')}, {"{\"id\":1}", "{\"id\":\"1\"}"},
        {"{\"id\":1}", "{\"id\":0}"}, {"{\"id\":1}", "{\"id\":1.5}"}, {"{\"id\":1}", "{}"},
        {"{\"quest_type\":{\"slug\":\"ocre\"}}", "null"}, {"\"slug\":\"ocre\"", "\"slug\":5"}, {"\"slug\":\"ocre\"", "\"slug\":\"\""},
        {",\"quest_template\":{\"quest_type\":{\"slug\":\"ocre\"}}", ""}, {"\"character_name\":\"Bob\"", "\"character_name\":7"},
        {"\"Bob\"", "\"" + std::string(1025, 'b') + "\""}})
        Reject([&] { ParseQuests(With(OneQuest, from, to)); }, "Bad /quests answer accepted");
    RejectAs([&] { ParseQuests(With(OneQuest, "[{", "[" + QuestObject + ",{")); }, "Quête Metamob invalide.", "Two quests, one slug");
    for (const auto& body : std::vector<std::string>{R"json({"data":{}})json", R"json({"data":null})json",
        R"json({"quests":[]})json", "{\"data\":{\"0\":" + QuestObject + "}}"})
        Reject([&] { ParseQuests(body); }, "/quests answer without a list accepted");

    const auto page = ParseMonsters(MonstersBody);
    Check(page.slug == "a1b2c3d4" && page.total == 337 && page.limit == 200 && page.offset == 0 && page.monsters.size() == 7,
        "Pagination inside data");
    Check(page.monsters[0].id == 305 && page.monsters[0].name == "Bouftou Royal" && page.monsters[0].type == TypeBoss &&
        page.monsters[0].step == 1 && page.monsters[0].quantity == 0, "Boss row");
    Check(page.monsters[1].name == "Mob l'Éponge" && page.monsters[1].quantity == 45, "Escaped name; quantity read above 30");
    Check(page.monsters[2].id == 420 && page.monsters[2].type == TypeArchimonster && page.monsters[2].step == 4 &&
        page.monsters[2].quantity == 1, "Archimonster row, unknown fields ignored");
    Check(page.monsters[3].name == "Abrakroc l'édenté" && page.monsters[6].type == 0, "Accents kept; null type");
    Check(ParseMonsters(With(OneMonster, ",\"type\":{\"id\":3}", "")).monsters.at(0).type == 0, "Absent type");
    Check(ParseMonsters(With(OneMonster, "\"quantity\":1", "\"quantity\":1000000")).monsters.at(0).quantity == 1000000, "Large quantity");
    Check(ParseMonsters(With(OneMonster, "\"id\":420", "\"id\":9223372036854775807")).monsters.at(0).id == INT64_MAX, "Largest id");
    {
        const auto end = ParseMonsters(R"json({"data":{"slug":"a1b2c3d4","monsters":[],
          "pagination":{"total":337,"limit":200,"offset":400}}})json");
        Check(end.monsters.empty() && end.offset == 400, "An empty page past the end is a fair answer");
    }
    for (const auto& [from, to] : std::vector<std::pair<std::string, std::string>>{
        {"\"id\":420", "\"id\":0"}, {"\"id\":420", "\"id\":-5"}, {"\"id\":420", "\"id\":\"420\""}, {"\"id\":420", "\"id\":420.0"},
        {"\"id\":420", "\"id\":9223372036854775808"}, {"\"id\":420", "\"id\":18446744073709551615"}, {"\"id\":420,", ""},
        {"{\"fr\":\"Tofumanchou l'Empereur\",\"en\":\"x\",\"es\":\"y\"}", "null"}, {"\"Tofumanchou l'Empereur\"", "null"},
        {"\"Tofumanchou l'Empereur\"", "\"\""}, {"Tofumanchou l'Empereur", Marker + FakeKey + R"json(\u0001)json"},
        {"Tofumanchou l'Empereur", R"json(Tofu\u0085manchou)json"}, {"Tofumanchou l'Empereur", "<b>Tofumanchou</b>"},
        {"Tofumanchou l'Empereur", std::string(257, 't')}, {"{\"id\":3}", "{\"id\":\"3\"}"}, {"{\"id\":3}", "{\"id\":0}"},
        {"{\"id\":3}", "\"3\""}, {"\"step\":4", "\"step\":0"}, {"\"step\":4", "\"step\":\"4\""}, {"\"step\":4,", ""},
        {"\"quantity\":1", "\"quantity\":-1"}, {"\"quantity\":1", "\"quantity\":1.5"}, {"\"quantity\":1", "\"quantity\":1000001"},
        {"\"quantity\":1", "\"quantity\":null"}, {"\"quantity\":1", "\"quantity\":\"" + FakeKey + "\""},
        {"\"slug\":\"a1b2c3d4\"", "\"slug\":\"a1b2/" + Marker + "\""}, {"\"slug\":\"a1b2c3d4\",", ""},
        {"\"total\":1", "\"total\":0"}, {"\"total\":1", "\"total\":-1"}, {"\"total\":1", "\"total\":\"1\""},
        {"\"limit\":200", "\"limit\":0"}, {"\"offset\":0", "\"offset\":-1"}, {"\"offset\":0", "\"offset\":1"},
        {"\"pagination\":{\"total\":1,\"limit\":200,\"offset\":0}}}", "\"q\":0},\"pagination\":{\"total\":1,\"limit\":200,\"offset\":0}}"},
        {"\"pagination\":{\"total\":1,\"limit\":200,\"offset\":0}", "\"pagination\":null"}})
        Reject([&] { ParseMonsters(With(OneMonster, from, to)); }, "Bad monster page accepted");
    RejectAs([&] { ParseMonsters(With(With(OneMonster, "\"monsters\":[{\"id\":420", "\"monsters\":{\"0\":{\"id\":420"),
        "\"quantity\":1}]", "\"quantity\":1}}")); }, "Réponse Metamob inattendue.", "Monsters as an object");
    const std::string second = R"json(,{"id":421,"name":{"fr":"Abrakroc l'édenté"},"type":{"id":3},"step":4,"quantity":0})json";
    const auto two = With(With(OneMonster, "\"quantity\":1}", "\"quantity\":1}" + second), "\"total\":1", "\"total\":2");
    Check(ParseMonsters(two).monsters.size() == 2, "Two rows");
    RejectAs([&] { ParseMonsters(With(two, "\"limit\":200", "\"limit\":1")); }, "Pagination Metamob incohérente.", "Rows over the limit");
    RejectAs([&] { ParseMonsters(With(two, "\"id\":421", "\"id\":420")); }, "Monstre Metamob en double.", "Two rows with one id");

    const auto update = ParseQuantityUpdate(UpdateBody);
    Check(update.monsterId == 123 && update.quantity == 5, "PATCH answer");
    for (const auto& body : std::vector<std::string>{With(UpdateBody, "\"monster_id\":123,", ""),
        With(UpdateBody, "\"quantity\":5", "\"quantity\":\"5\""), With(UpdateBody, "\"quantity\":5", "\"quantity\":null"),
        With(UpdateBody, "\"monster_id\":123", "\"monster_id\":0"), R"json({"data":[{"monster_id":123,"quantity":5}]})json",
        R"json({"data":{"updated_count":1,"monsters":[{"monster_id":123,"quantity":5}]}})json"})
        Reject([&] { ParseQuantityUpdate(body); }, "Bad PATCH answer accepted");

    Check(QuantityBody(0) == R"({"quantity":0})" && QuantityBody(5) == R"({"quantity":5})" &&
        QuantityBody(MaxQuantity) == R"({"quantity":30})", "Absolute quantity body");
    for (const int bad : {-1, MaxQuantity + 1, INT_MIN, INT_MAX})
        RejectAs([&] { QuantityBody(bad); }, "Quantité Metamob hors limites.", "Quantity outside 0..30 written");
}

void Hostile() {
    // Bodies a proxy, Cloudflare, an error or a broken answer could bring: one
    // fixed text each, whatever the parser, never a piece of the body or key.
    const std::vector<std::string> bodies{"", " ", "null", "[]", "\"" + FakeKey + "\"",
        "<!DOCTYPE html><html><head><title>Just a moment...</title></head><body>" + Marker + FakeKey + "</body></html>",
        "<html><body><h1>502 Bad Gateway</h1>" + Marker + "</body></html>",
        R"json({"data":{"username":")json" + FakeKey, R"json({"data":{"slug":")json" + Marker,
        R"json({"message":")json" + Marker + R"json("})json",
        R"json({"status":404,"error":404,"messages":{"error":")json" + Marker + R"json("}})json",
        R"json({"status":400,"error":400,"messages":{"quantity":"must be between 0 and 30"}})json",
        R"json({"data":{"username":"u"}} )json" + FakeKey, R"json({"data":{"username":")json" + Marker + "\xFF\"}}",
        R"json({"data":{"username":"\ud83d)json" + Marker + "\"}}", R"json({"data":{"username":)json" + FakeKey + "}}",
        R"json({"data":[)json" + FakeKey + "]}", R"json({"data":{"username":"u"},)json" + Marker + "}",
        R"json({"data":{"username":"u"} /* )json" + Marker + " */}", std::string(1024 * 1024 + 1, 'x'),
        R"json({"data":{"username":"u","x":)json" + std::string(40, '[') + std::string(40, ']') + "}}",
        std::string(1024 * 1024, '[')};
    for (const auto& body : bodies) {
        Reject([&] { ParseMe(body); }, "Hostile /me answer accepted");
        Reject([&] { ParseQuests(body); }, "Hostile /quests answer accepted");
        Reject([&] { ParseMonsters(body); }, "Hostile monster page accepted");
        Reject([&] { ParseQuantityUpdate(body); }, "Hostile PATCH answer accepted");
    }
    RejectAs([&] { ParseMe(std::string(1024 * 1024 + 1, ' ')); }, "Réponse Metamob trop volumineuse.", "Over 1 MiB");
    RejectAs([&] { ParseMe(R"json({"data":{"username":"u"})json"); }, "Réponse Metamob illisible.", "Truncated JSON");
    RejectAs([&] { ParseMe(std::string(1024 * 1024, '[')); }, "Réponse Metamob illisible.", "Deep nesting");
    std::string padded = MeBody;
    padded.resize(1024 * 1024, ' ');
    Check(ParseMe(padded).username == "player1", "Exactly 1 MiB is read");
    Check(ParseMe(R"json({"data":{"username":"u","x":[[[[[[[[[[[[{"y":[1]}]]]]]]]]]]]]}})json").username == "u",
        "Some nesting in an unknown field is fine");
    for (const auto& text : Fixed) Check(!Leaks(text, FakeKey) && !Leaks(text, Marker), "Fixed texts are fixed");
}

void Headers() {
    const int64_t now = 1792670400; // Thu, 22 Oct 2026 12:00:00 GMT
    const std::vector<std::pair<std::string, std::optional<int>>> cases{
        {"120", 120}, {" 30\t", 30}, {"0", 1}, {"1", 1}, {"900", 900}, {"901", 900}, {"007", 7},
        {"99999999999999999999999999999999", 900}, {"Thu, 22 Oct 2026 12:02:00 GMT", 120}, {" Thu, 22 Oct 2026 12:02:00 GMT ", 120},
        {"Thu, 22 Oct 2026 12:00:01 GMT", 1}, {"Thu, 22 Oct 2026 11:00:00 GMT", 1}, {"Thu, 22 Oct 2026 12:14:59 GMT", 899},
        {"Thu, 22 Oct 2026 12:15:00 GMT", 900}, {"Thu, 22 Oct 2026 14:00:00 GMT", 900},
        {"", std::nullopt}, {" \t ", std::nullopt}, {"abc", std::nullopt}, {"-5", std::nullopt}, {"+5", std::nullopt},
        {"1.5", std::nullopt}, {"30s", std::nullopt}, {"3 0", std::nullopt}, {"1e3", std::nullopt}, {"0x10", std::nullopt},
        {"Thu, 22 Oct 2026 12:02:00 UTC", std::nullopt}, {"Thu, 22 Oct 2026 12:02:00 gmt", std::nullopt},
        {"Thursday, 22-Oct-26 12:02:00 GMT", std::nullopt}, {"Thu Oct 22 12:02:00 2026", std::nullopt},
        {"Thu, 32 Oct 2026 12:02:00 GMT", std::nullopt}, {"Thu, 00 Oct 2026 12:02:00 GMT", std::nullopt},
        {"Sun, 29 Feb 2026 12:02:00 GMT", std::nullopt}, {"Thu, 22 Oct 2026 24:00:00 GMT", std::nullopt},
        {"Thu, 22 Oct 2026 12:60:00 GMT", std::nullopt}, {"Thu, 22 Oct 2026 12:00:61 GMT", std::nullopt},
        {"Xyz, 22 Oct 2026 12:02:00 GMT", std::nullopt}, {"Thu, 22 Okt 2026 12:02:00 GMT", std::nullopt},
        {"Thu, 2a Oct 2026 12:02:00 GMT", std::nullopt}, {"Thu, 22 Oct 2026 12:02:00 GMT x", std::nullopt},
        {"Thu, 22 Oct -026 12:02:00 GMT", std::nullopt}, {"Thu,  22 Oct 2026 12:02:00 GMT", std::nullopt},
        {"Thu, 22 Oct 2026 12:02:00 GMT\r\n", std::nullopt}, {FakeKey, std::nullopt}};
    for (const auto& [header, seconds] : cases) Check(RetryAfterSeconds(header, now) == seconds, "Retry-After");
    Check(RetryAfterSeconds("Sun, 06 Nov 1994 08:49:37 GMT", 784111777 - 42) == 42, "RFC example date");
    Check(RetryAfterSeconds("Tue, 29 Feb 2028 12:00:00 GMT", 1835438400 - 300) == 300, "Leap day");
    Check(RetryAfterSeconds("Thu, 22 Oct 2026 12:02:00 GMT", INT64_MIN) == 900 &&
        RetryAfterSeconds("Thu, 22 Oct 2026 12:02:00 GMT", INT64_MAX) == 1, "Any clock value, no overflow");
}

void Validators() {
    Check(ValidKey(FakeKey), "The fake key has a valid shape");
    Check(!ValidKey(std::string(15, 'k')) && ValidKey(std::string(16, 'k')) && ValidKey(std::string(256, 'k')) &&
        !ValidKey(std::string(257, 'k')), "Key length 16..256");
    std::string printable;
    for (char c = 0x21; c <= 0x7E; ++c) printable += c;
    Check(ValidKey(printable), "Every printable ASCII byte");
    for (const auto& bad : std::vector<std::string>{"", FakeKey + " ", " " + FakeKey, FakeKey + "\r\n", FakeKey + "\n",
        FakeKey.substr(0, 16) + "\r\nX-Evil: 1", FakeKey + "\t", FakeKey + "\x7f", FakeKey + "é", std::string(16, ' '),
        FakeKey.substr(0, 8) + std::string(1, '\0') + FakeKey.substr(9)})
        Check(!ValidKey(bad), "Key that could break a header accepted");
    Check(ValidSlug("a1b2c3d4") && ValidSlug("Retro_77-x") && ValidSlug("Z") && ValidSlug(std::string(64, 'z')), "Slugs");
    for (const auto& bad : std::vector<std::string>{"", std::string(65, 'z'), "a b", "ab\r\n", "ab\n", "a/b", "a?b", "a%2F",
        "a.b", "..", "é", "a#b", std::string("a\0b", 3)})
        Check(!ValidSlug(bad), "Slug that could change the request path accepted");

    Check(SafeUsername("player1") == "player1" && SafeUsername("").empty(), "Plain username");
    Check(SafeUsername("<b>Bob</b>") == "bBob/b", "Markup removed");
    Check(SafeUsername("Bo\x01" "b\x7f" "\t\r\nz") == "Bobz", "C0 controls and DEL removed");
    Check(SafeUsername("a\xC2\x85" "b") == "ab" && SafeUsername("a\xE2\x80\xA8" "b") == "ab", "C1 control and line separator removed");
    Check(SafeUsername("\xE2\x80\xAE" "evil" "\xE2\x81\xA6") == "evil", "Bidi override and isolate removed");
    Check(SafeUsername("abc\xFF" "def") == "abcdef" && SafeUsername("\xC0\xAF" "x") == "x" &&
        SafeUsername("\xED\xA0\x80" "x") == "x" && SafeUsername("x\xE2\x82") == "x", "Invalid UTF-8 dropped");
    Check(SafeUsername("Clément’s \U0001F600") == "Clément’s \U0001F600", "Accents, apostrophe and emoji kept");
    std::string accents;
    for (int i = 0; i < 30; ++i) accents += "é";
    Check(SafeUsername(accents) == accents.substr(0, 40), "At most 40 bytes");
    Check(SafeUsername(std::string(39, 'a') + "é") == std::string(39, 'a'), "A character is never cut");
    Check(SafeUsername(std::string(50, 'a')).size() == 40, "Long username shortened");
}

void Names() {
    // Real Metamob names (the game spells them the same way) and the variants
    // a game build, a font or a copy-paste can bring.
    Check(NameKey("Tofumanchou l'Empereur") == "tofumanchou l'empereur" &&
        NameKey("Tofumanchou l’Empereur") == "tofumanchou l'empereur", "Typographic apostrophe");
    Check(NameKey("Abrakroc l'édenté") == "abrakroc l'edente" && NameKey("ABRAKROC L’ÉDENTÉ") == "abrakroc l'edente", "Accents, case");
    Check(NameKey("Larvaloeil l'Émue") == "larvaloeil l'emue" && NameKey("Larvalœil l’Émue") == "larvaloeil l'emue" &&
        NameKey("LARVALŒIL L'ÉMUE") == "larvaloeil l'emue", "œ written oe");
    Check(NameKey("Tiwa'Missou le Gateux") == "tiwa'missou le gateux" && NameKey("Tiwa’Missou le Gâteux") == "tiwa'missou le gateux" &&
        NameKey("Tiwa\u02BCmissou\u00A0le\u202FGateux") == "tiwa'missou le gateux", "Modifier apostrophe, no-break spaces");
    for (const auto* name : {"Mob l'Éponge", "MOB L’ÉPONGE", "  Mob \t l`Éponge  ", "Mob l\u00B4Éponge", "Mob l\u2032Éponge",
        "Mob l\u2018Éponge", "Mob\u00A0l\u02BCE\u0301ponge", "Mob\u2009l'e\u0301ponge", "Mob l'\xC3\x89ponge"})
        Check(NameKey(name) == "mob l'eponge", "Mob l'Éponge variants");
    for (const auto* name : {"Jean-Luc", "Jean\u2010Luc", "Jean\u2011Luc", "Jean\u2012Luc", "Jean\u2013Luc", "Jean\u2014Luc",
        "Jean\u2015Luc", "Jean\u2212Luc"})
        Check(NameKey(name) == "jean-luc", "Dash variants");
    Check(NameKey("Tofu\u00ADmanchou") == "tofumanchou" && NameKey("Tofu\u200Bmanchou") == "tofumanchou", "Soft hyphen, zero width");
    Check(NameKey("Æther Cæsar") == "aether caesar" && NameKey("Ça") == "ca" && NameKey("Scapé l'Epée") == "scape l'epee" &&
        NameKey("Kilimanj’haro le Grimpeur") == "kilimanj'haro le grimpeur", "Ligatures and cedilla");
    Check(NameKey("").empty() && NameKey(" \t\u00A0 ").empty() && NameKey("Mob \xC3\x28").empty() && NameKey("\xFF").empty(),
        "No key for blank or invalid UTF-8");
    Check(NameKey(std::string(4096, 'a')) == std::string(4096, 'a') && NameKey(std::string(4097, 'a')).empty(), "Input bound");
    Check(NameKey("Crognan le Barbare") != NameKey("Crognan le Barbar") &&
        NameKey("Tofumanchou l'Empereur") != NameKey("Tofumanchou lEmpereur"), "Different names stay different");
    Check(NameKey("Colmillazaqui, el Inagotable") == "colmillazaqui, el inagotable",
        "Commas kept: ChooseQuest splits on them (only FindMonster reads one as a space)");
}

void Choices() {
    {
        const auto choice = ChooseQuest({}, "Bob");
        Check(!choice.quest && choice.problem == NoQuest && choice.kind == QuestProblem::NoQuest, "No quest");
    }
    const Quest unity{"a1", "Mon perso", "Draconiros", "ocre", 1, 6}, retro{"r2", "Bob", "Boune", "ocre", 2, 1},
        dokille{"d3", "Bob", "Draconiros", "dokille", 1, 2}, noServer{"n4", "Bob", "", "ocre", 0, 1};
    {
        const auto choice = ChooseQuest({retro, dokille, noServer}, "Bob");
        Check(!choice.quest && choice.problem == NoQuest && choice.kind == QuestProblem::NoQuest,
            "Only Dofus Unity Ocre quests count (Retro, Dokille, null server)");
    }
    Check(Chosen({retro, unity, dokille, noServer}, "Zed") == "a1", "The only Unity Ocre quest, whatever the name");
    Check(Chosen({unity}, "") == "a1", "One quest needs no character name");
    Check(Chosen(ParseQuests(QuestsBody), "Zed") == "a1b2c3d4", "Parsed answer");
    const Quest team1{"t1", "Bob, Alice", "Draconiros", "ocre", 1, 2}, team2{"t2", "Charlie/Élodie Dana", "Hell Mina", "ocre", 1, 3};
    Check(Chosen({team1, team2}, "alice") == "t1", "Comma part names the character");
    Check(Chosen({team1, team2}, "Charlie") == "t2", "Slash part names the character");
    Check(Chosen({team1, team2}, "ELODIE") == "t2" && Chosen({team1, team2}, "Dana") == "t2", "Space part, case and accents");
    Check(Chosen({team1, team2}, "Bob, Alice") == "t1", "Whole field");
    Check(Chosen({team1, {"t3", "Gus\u00A0Zed", "", "ocre", 1, 1}}, "Zed") == "t3", "No-break space separates");
    Check(Chosen({team1, {"t4", "", "", "ocre", 1, 1}}, "Bob") == "t1", "An empty contact field names nobody");
    Check(Chosen({team1, {"t5", "Bobby", "", "ocre", 1, 1}}, "Bob") == "t1", "No partial word match");
    for (const auto& [quests, character] : std::vector<std::pair<std::vector<Quest>, std::string>>{
        {{team1, team2}, "Zed"}, {{team1, team2}, ""}, {{team1, team2}, "\xFF"}, {{team1, {"t6", "bob/Gus", "", "ocre", 1, 1}}, "Bob"},
        {{team1, team1}, "Alice"}}) {
        const auto choice = ChooseQuest(quests, character);
        Check(!choice.quest && choice.problem == SeveralQuests && choice.kind == QuestProblem::Several,
            "Several quests and no single one naming the character: none");
    }
}

void Monsters() {
    const auto monsters = ParseMonsters(MonstersBody).monsters;
    Check(Found(monsters, "Tofumanchou l’Empereur", TypeArchimonster) == 420, "Archimonster by name");
    Check(Found(monsters, "Tofumanchou l'Empereur", TypeBoss) == -1, "Same name, other type");
    Check(Found(monsters, "Mob l’éponge", TypeBoss) == 330 && Found(monsters, "Bouftou Royal", TypeBoss) == 305, "Bosses");
    Check(Found(monsters, "Larvalœil l’Émue", TypeArchimonster) == 422 && Found(monsters, "Abrakroc l’Édenté", TypeArchimonster) == 421 &&
        Found(monsters, "Tiwa’Missou le Gâteux", TypeArchimonster) == 423, "Name variants");
    Check(Found(monsters, "Scapé l'Epée", TypeArchimonster) == -1, "A monster without a type never matches");
    Check(Found(monsters, "Grozilla", TypeArchimonster) == -1 && Found(monsters, "", TypeBoss) == -1 &&
        Found(monsters, "\xFF", TypeBoss) == -1 && Found({}, "Bouftou Royal", TypeBoss) == -1, "Unknown name: none");
    const auto found = FindMonster(monsters, "Mob l'Éponge", TypeBoss);
    Check(found && found->name == "Mob l'Éponge" && found->step == 2 && found->quantity == 45, "The whole row comes back");
    const std::vector<QuestMonster> twins{{500, "Tofumanchou l'Empereur", TypeArchimonster, 4, 0},
        {501, "Tofumanchou l’Empereur", TypeArchimonster, 4, 0}};
    Check(Found(twins, "Tofumanchou l'Empereur", TypeArchimonster) == -1, "Two monsters under one name: none");
    const std::vector<QuestMonster> overlap{{500, "Tofumanchou l'Empereur", TypeArchimonster, 4, 2},
        {500, "Tofumanchou l'Empereur", TypeArchimonster, 4, 2}};
    Check(Found(overlap, "Tofumanchou l'Empereur", TypeArchimonster) == 500, "One monster listed twice is still one");
    const std::vector<QuestMonster> kinds{{20, "Bouftou Royal", 1, 1, 0}, {305, "Bouftou Royal", TypeBoss, 1, 0}};
    Check(Found(kinds, "Bouftou Royal", TypeBoss) == 305 && Found(kinds, "Bouftou Royal", TypeArchimonster) == -1,
        "The type picks among equal names");
    Check(TypeMonster == 1 && Found(kinds, "Bouftou Royal", TypeMonster) == 20, "Any other monster: type 1");
}

// Client language: MonsterData.get_name is GetLocalized(nameId), so an English or
// Spanish client sends the English or Spanish name; Metamob names in fr, en and es.
void Languages() {
    const auto monsters = ParseMonsters(MonstersBody).monsters;
    Check(monsters[2].nameEn == "Tofumanchou the Emperor" && monsters[2].nameEs == "Tofumanchú el Emperador" &&
        monsters[0].nameEs == "Jalató Real" && monsters[1].nameEn == "Mob the Sponge", "English and Spanish names kept");
    Check(Found(monsters, "Tofumanchou the Emperor", TypeArchimonster) == 420, "English client name");
    Check(Found(monsters, "Tofumanchú el Emperador", TypeArchimonster) == 420 &&
        Found(monsters, "TOFUMANCHU EL EMPERADOR", TypeArchimonster) == 420, "Spanish client name");
    Check(Found(monsters, "Royal Gobball", TypeBoss) == 305 && Found(monsters, "Mob la Esponja", TypeBoss) == 330,
        "English and Spanish boss names");
    Check(Found(monsters, "Tofumanchou the Emperor", TypeBoss) == -1 && Found(monsters, "Royal Gobball", TypeArchimonster) == -1,
        "Translation, other type");
    Check(Found(monsters, "x", TypeArchimonster) == -1 && Found(monsters, "y", TypeArchimonster) == -1,
        "A translation shared by several monsters: none");
    // name.en and name.es only serve the match: unusable, they stay empty and never refuse the page.
    for (const auto& to : std::vector<std::string>{"null", "5", "true", "\"\"", "\"<b>y\"", "{\"x\":1}", "[\"x\"]",
        "\"" + std::string(257, 't') + "\"", R"json("Tofu\u0007manchou")json", "\"" + Marker + FakeKey + R"json(\u0085")json"}) {
        const auto row = ParseMonsters(With(OneMonster, "\"en\":\"x\"", "\"en\":" + to)).monsters.at(0);
        Check(row.name == "Tofumanchou l'Empereur" && row.nameEn.empty() && row.nameEs == "y", "Unusable English name ignored");
    }
    for (const auto* to : {"null", "\"<b>y\""}) {
        const auto row = ParseMonsters(With(OneMonster, "\"es\":\"y\"", std::string("\"es\":") + to)).monsters.at(0);
        Check(row.nameEn == "x" && row.nameEs.empty(), "Unusable Spanish name ignored");
    }
    Check(ParseMonsters(With(OneMonster, "\"en\":\"x\"", "\"en\":\"" + std::string(256, 't') + "\"")).monsters.at(0).nameEn.size() == 256,
        "English name bound");
    {
        const auto row = ParseMonsters(With(OneMonster, ",\"en\":\"x\",\"es\":\"y\"", "")).monsters.at(0);
        Check(row.name == "Tofumanchou l'Empereur" && row.nameEn.empty() && row.nameEs.empty(), "Absent translations");
    }
    Reject([&] { ParseMonsters(With(OneMonster, "\"fr\":\"Tofumanchou l'Empereur\",", "")); }, "A name without fr accepted");
    // The uniqueness rule spans the languages.
    const std::vector<QuestMonster> crossed{{500, "Alpha", TypeArchimonster, 4, 0, "Beta", ""},
        {501, "Beta", TypeArchimonster, 4, 0, "", ""}, {502, "Gamma", TypeArchimonster, 5, 0, "Gamma", "Gamma"}};
    Check(Found(crossed, "Beta", TypeArchimonster) == -1 && Found(crossed, "Alpha", TypeArchimonster) == 500,
        "One name for two monsters across languages: none");
    Check(Found(crossed, "Gamma", TypeArchimonster) == 502, "One monster named alike in several languages is one");
    // Metamob writes "Colmillazaqui, el Inagotable", the Spanish game "Colmillazaqui el
    // Inagotable" (real names of Metamob monster 416): FindMonster reads a comma as a space.
    const std::vector<QuestMonster> crognan{{416, "Crognan le Barbare", TypeArchimonster, 7, 0, "Lupisnockio the Woodwolf",
        "Colmillazaqui, el Inagotable"}};
    for (const auto* name : {"Colmillazaqui el Inagotable", "Colmillazaqui, el Inagotable", "colmillazaqui ,el  INAGOTABLE",
        "Colmillazaqui,el Inagotable,", "Crognan le Barbare", "Crognan, le Barbare", "Lupisnockio the Woodwolf"})
        Check(Found(crognan, name, TypeArchimonster) == 416, "A comma reads as a space");
    Check(Found(crognan, "Colmil,lazaqui el Inagotable", TypeArchimonster) == -1 &&
        Found(crognan, "Colmillazaqui el", TypeArchimonster) == -1, "A comma is a space, not nothing; whole names only");
    const std::vector<QuestMonster> commas{{600, "Bob, le Grand", TypeBoss, 1, 0, "", ""},
        {601, "Bob le Grand", TypeBoss, 1, 0, "", ""}, {602, ",", TypeBoss, 1, 0, ", ,", ","}};
    Check(Found(commas, "Bob le Grand", TypeBoss) == -1 && Found(commas, "Bob, le Grand", TypeBoss) == -1,
        "Names equal but for a comma: two monsters, none");
    Check(Found(commas, ",", TypeBoss) == -1 && Found(commas, " , ", TypeBoss) == -1, "Commas alone name nothing");
}
}

// GET /v1/kralove: Kralamoure openings, as the API documents them.
const std::string KraloveBody = R"json({"data":[
 {"id":42,"event_datetime":"2026-10-24T19:00:00Z","description":"Rendez-vous devant le zaap\nAmenez des pierres pleines !",
  "creator":"Zyx\u2019s","participants_count":4,"character_count":12,"messages_count":3,
  "server":{"id":11,"name":"Draconiros","community":"fr","game_version":{"id":1,"name":"Dofus"}},"future":{"x":[1,null,"y"]}},
 {"id":7,"event_datetime":"2026-10-24T18:00:00.000000Z","description":null,"creator":null,"participants_count":0,
  "character_count":0,"messages_count":0,"server":{"id":11,"name":"Draconiros","community":"fr","game_version":null}},
 {"id":9,"event_datetime":"2026-10-25T01:30:00+00:00","creator":"<b>Bob</b>\u202e","participants_count":1,
  "character_count":2,"messages_count":0,"server":null}],"meta":{"generated":"2026-10-22T12:00:00Z"}})json";
// One opening in an otherwise valid answer (one line, so a fixture can change any piece of it).
const std::string OneOpening = R"json({"data":[{"id":42,"event_datetime":"2026-10-24T19:00:00Z","description":"Rendez-vous",)json"
    R"json("creator":"Zyx","participants_count":4,"character_count":12,"messages_count":3,"server":{"id":11,"name":"Draconiros"}}]})json";
const std::string OpeningObject = OneOpening.substr(9, OneOpening.size() - 11); // without {"data":[ and ]}
int64_t At(int year, int month, int day, int hour, int minute, int second) {
    return DayNumber(year, month, day) * 86400 + hour * 3600 + minute * 60 + second;
}

void Kralamoure() {
    const auto openings = ParseKralove(KraloveBody);
    Check(openings.size() == 3 && openings[0].id == 7 && openings[1].id == 42 && openings[2].id == 9, "Openings by time");
    const auto& full = openings[1];
    Check(full.at == At(2026, 10, 24, 19, 0, 0) && full.at == 1792868400 && full.serverId == 11 && full.server == "Draconiros" &&
        full.creator == "Zyx’s" && full.participants == 4 && full.characters == 12 && full.messages == 3 &&
        full.description == "Rendez-vous devant le zaap Amenez des pierres pleines !", "Opening fields (a line break read as a space)");
    Check(openings[0].at == At(2026, 10, 24, 18, 0, 0) && openings[0].description.empty() && openings[0].creator.empty() &&
        openings[0].characters == 0, "Null description and creator; a fraction of a second");
    Check(openings[2].at == At(2026, 10, 25, 1, 30, 0) && openings[2].creator == "bBob/b" && openings[2].serverId == 0 &&
        openings[2].server.empty() && openings[2].description.empty(), "+00:00, absent description, a creator made safe, null server");
    Check(ParseKralove(R"json({"data":[]})json").empty(), "No opening at all");
    Check(OpeningObject.front() == '{' && OpeningObject.back() == '}' && ParseKralove(OneOpening).size() == 1, "Minimal opening");
    {
        // Same time: by id.
        const auto pair = ParseKralove("{\"data\":[" + With(OpeningObject, "\"id\":42", "\"id\":50") + "," + OpeningObject + "]}");
        Check(pair.size() == 2 && pair[0].id == 42 && pair[1].id == 50, "Same time: by id");
    }
    {
        // Bounds: 500 openings, a description of 8192 bytes (cut for the tooltip), a creator of 1024 bytes.
        std::string many = "{\"data\":[";
        for (int i = 0; i < 500; ++i) many += (i ? "," : "") + With(OpeningObject, "\"id\":42", "\"id\":" + std::to_string(i + 1));
        Check(ParseKralove(many + "]}").size() == 500, "500 openings");
        Reject([&] { ParseKralove(many + "," + With(OpeningObject, "\"id\":42", "\"id\":501") + "]}"); }, "Over 500 openings accepted");
        const auto longest = ParseKralove(With(OneOpening, "\"Rendez-vous\"", "\"" + std::string(8192, 'd') + "\""));
        Check(longest[0].description.size() == MaxDescription && longest[0].description.ends_with("…") &&
            longest[0].description.starts_with(std::string(MaxDescription - 3, 'd')), "Long description cut for the tooltip");
        Reject([&] { ParseKralove(With(OneOpening, "\"Rendez-vous\"", "\"" + std::string(8193, 'd') + "\"")); }, "Oversized description");
        Check(ParseKralove(With(OneOpening, "\"Zyx\"", "\"" + std::string(1024, 'z') + "\""))[0].creator == std::string(40, 'z'), "Creator bound");
        Reject([&] { ParseKralove(With(OneOpening, "\"Zyx\"", "\"" + std::string(1025, 'z') + "\"")); }, "Oversized creator");
        // The 1 MiB transport bound.
        Reject([&] { ParseKralove("{\"data\":[],\"x\":\"" + std::string(1024 * 1024, 'x') + "\"}"); }, "Oversized answer");
    }
    // Hostile text: never markup, never a control, bidi or invisible character, never a line break.
    {
        const auto hostile = ParseKralove(With(With(OneOpening, "\"Rendez-vous\"",
            R"json("<color=red>Viens</color>\u0007\u202e\u2066\u200b\ud834\udd1e\udb40\udc41 \r\n\t ici\u2028fin")json"),
            "\"Zyx\"", R"json("<i>Zed</i>\u0000\u200f")json"));
        Check(hostile[0].description == "color=redViens/color𝄞 ici fin" && hostile[0].creator == "iZed/i", "Hostile description and creator");
    }
    for (const auto& [from, to] : std::vector<std::pair<std::string, std::string>>{
        {"\"id\":42", "\"id\":0"}, {"\"id\":42", "\"id\":-1"}, {"\"id\":42", "\"id\":\"42\""}, {"\"id\":42", "\"id\":4.2"},
        {"\"id\":42", "\"id\":null"}, {"\"id\":42", "\"id\":18446744073709551615"}, {"\"id\":42,", ""},
        {"\"participants_count\":4", "\"participants_count\":-1"}, {"\"character_count\":12", "\"character_count\":\"12\""},
        {"\"messages_count\":3", "\"messages_count\":null"}, {"\"character_count\":12", "\"character_count\":1000001"},
        {"\"messages_count\":3,", ""}, {"\"participants_count\":4,", ""}, {"\"character_count\":12,", ""},
        {"\"Rendez-vous\"", "7"}, {"\"Rendez-vous\"", "{\"fr\":\"x\"}"}, {"\"Zyx\"", "5"}, {"\"Zyx\"", "[\"Zyx\"]"},
        {"{\"id\":11,\"name\":\"Draconiros\"}", "{\"name\":\"Draconiros\"}"}, {"{\"id\":11,\"name\":\"Draconiros\"}", "{\"id\":11}"},
        {"{\"id\":11,\"name\":\"Draconiros\"}", "\"Draconiros\""}, {"\"id\":11", "\"id\":0"}, {"\"Draconiros\"", "\"<b>Draco</b>\""},
        {"\"Draconiros\"", "\"\""}, {"Draconiros", Marker + R"json(\u0007)json"}, {"\"event_datetime\":\"2026-10-24T19:00:00Z\",", ""}}) {
        const auto body = With(OneOpening, from, to);
        Reject([&] { ParseKralove(body); }, ("Bad opening accepted: " + to).c_str());
    }
    // Bad dates: one fixed text.
    for (const std::string& date : {"\"2026-02-30T10:00:00Z\"", "\"2026-10-24T24:00:00Z\"", "\"2026-10-24T19:60:00Z\"",
        "\"2026-10-24T19:00:60Z\"", "\"2026-10-24 19:00:00Z\"", "\"2026-10-24T19:00:00\"", "\"2026-10-24T19:00:00+02:00\"",
        "\"2026-10-24T19:00:00-00:00\"", "\"2026-10-24T19:00:00z\"", "\"2026-10-24t19:00:00Z\"", "\"1999-12-31T23:00:00Z\"",
        "\"2200-01-01T00:00:00Z\"", "\"2026-1-24T19:00:00Z\"", "\"2026-10-24T19:00:00.Z\"", "\"2026-10-24T19:00:00.1234567890Z\"",
        "\"2026-10-24T19:00:00ZZ\"", "\"+2026-10-24T19:00:00Z\"", "\"\"", "null", "1792868400", "\"2026-10-24\"", "\"2100-02-29T00:00:00Z\""}) {
        const auto error = Error([&] { ParseKralove(With(OneOpening, "\"2026-10-24T19:00:00Z\"", date)); });
        Check(error == "Date Metamob invalide.", ("Bad date accepted or misreported: " + date).c_str());
    }
    RejectAs([&] { ParseKralove("{\"data\":[" + OpeningObject + "," + With(OpeningObject, "\"Zyx\"", "\"Other\"") + "]}"); },
        "Ouverture Metamob en double.", "The same id twice");
    for (const auto& body : std::vector<std::string>{"", "<html>" + Marker + "</html>", "{\"data\":", "{\"data\":{}}", "{\"data\":null}",
        "{\"openings\":[]}", "[]", "{\"data\":[7]}", "{\"data\":[null]}"})
        Reject([&] { ParseKralove(body); }, "Bad answer accepted");
    // The quests' server id, for the calendar: lenient (never refuses the quests the captures need).
    const auto quests = ParseQuests(QuestsBody);
    Check(quests[0].serverId == 1 && quests[1].serverId == 20 && quests[2].serverId == 0 && quests[3].serverId == 13, "Quests' server ids");
    Check(ParseQuests(OneQuest)[0].serverId == 0, "Absent server id: 0");
    for (const std::string& id : {"\"1\"", "0", "-5", "1.5", "null", "true"})
        Check(ParseQuests(With(OneQuest, "\"server\":{", "\"server\":{\"id\":" + id + ","))[0].serverId == 0, "Unusable server id: 0");
    Check(ParseQuests(With(OneQuest, "\"server\":{", "\"server\":{\"id\":23,"))[0].serverId == 23, "Server id");
}

void Texts() {
    Check(SafeText("  a\t\tb \r\n c  ", 100) == "a b c", "Spaces collapsed and trimmed");
    Check(SafeText("<script>alert(1)</script>", 100) == "scriptalert(1)/script", "No markup");
    Check(SafeText(std::string("a\xFF" "b\xC3"), 100) == "ab" && SafeText(std::string("x\0y", 3), 100) == "xy", "Invalid UTF-8 and NUL dropped");
    Check(SafeText("ligne\u2028suite\u2029fin\u0085.", 100) == "ligne suite fin.", "Unicode line breaks read as spaces, NEL dropped");
    Check(SafeText("a\u202eb\u2067c\u200dd\ufeffe\u061cf\U000E0041g\ufffah\u180ei", 100) == "abcdefghi", "Bidi, invisible and tag characters dropped");
    Check(SafeText("Clément’s « fête » 😀", 100) == "Clément’s « fête » 😀", "Ordinary text kept");
    Check(SafeText("abcdefghijklmno", 10) == "abcdefg…" && SafeText("abcdefghij", 10) == "abcdefghij", "Cut with « … » within the bound");
    Check(SafeText("ééééé", 8) == "éé…" && SafeText("abc defgh", 7) == "abc…", "Never half a character, no space before « … »");
    Check(SafeText("abcdef", 2).empty() && SafeText("", 10).empty() && SafeText(" \t\n", 10).empty(), "Tiny bounds, nothing left");
    for (size_t bound = 0; bound < 40; ++bound) {
        const auto cut = SafeText("éa😀b€c 𝄞 xyz «ok» ’", bound);
        Check(cut.size() <= bound && SafeUsername(cut) == cut, "Within the bound, whole characters only");
    }
}

void Times() {
    Check(DayNumber(1970, 1, 1) == 0 && DayNumber(2000, 1, 1) == 10957 && DayNumber(2026, 10, 24) == 20750, "Day numbers");
    for (int64_t day = DayNumber(2000, 1, 1); day <= DayNumber(2199, 12, 31); ++day) {
        const auto civil = CivilDay(day);
        if (DayNumber(civil.year, civil.month, civil.day) != day || civil.weekday != static_cast<int>(((day % 7) + 10) % 7))
            throw std::runtime_error("Civil day round trip");
    }
    Check(CivilDay(0).weekday == 3 && CivilDay(DayNumber(2026, 10, 19)).weekday == 0 && CivilDay(DayNumber(2026, 10, 25)).weekday == 6 &&
        CivilDay(DayNumber(2027, 3, 28)).weekday == 6, "Weekdays: Monday 0 to Sunday 6");
    Check(CivilDay(DayNumber(2024, 2, 29)).day == 29 && CivilDay(DayNumber(2100, 3, 1) - 1).day == 28 &&
        CivilDay(DayNumber(2000, 3, 1) - 1).day == 29, "Leap years");
    Check(UtcSeconds("2000-01-01T00:00:00Z") == 946684800 && UtcSeconds("2026-10-24T19:00:00Z") == 1792868400 &&
        UtcSeconds("2026-10-24T19:00:00.5Z") == 1792868400 && UtcSeconds("2026-10-24T19:00:00.123456789+00:00") == 1792868400 &&
        UtcSeconds("2024-02-29T12:00:00Z") == At(2024, 2, 29, 12, 0, 0) && UtcSeconds("2199-12-31T23:59:59Z") == At(2199, 12, 31, 23, 59, 59),
        "UTC dates");
    for (const char* bad : {"2026-02-29T00:00:00Z", "2100-02-29T00:00:00Z", "2026-04-31T00:00:00Z", "2026-13-01T00:00:00Z",
        "2026-00-10T00:00:00Z", "2026-10-00T00:00:00Z", "2026-10-24T19:00:00+01:00", "2026-10-24T19:00Z", "2026-10-24T19:00:00",
        "2026-10-24T19:00:00 Z", " 2026-10-24T19:00:00Z", "2026-10-24T19:00:00.Z", "2026-10-24T19:00:00.0000000000Z", ""})
        Check(!UtcSeconds(bad), (std::string("Bad UTC date accepted: ") + bad).c_str());
    // Local time with Windows' rules for the very date: Paris (Romance Standard Time) around both changes.
    const auto paris = [](int64_t at) { return LocalTime(at, L"Romance Standard Time"); };
    const auto shown = [](const std::optional<CivilTime>& t) {
        return t ? std::to_string(t->year) + "-" + std::to_string(t->month) + "-" + std::to_string(t->day) + " " + std::to_string(t->hour) +
            ":" + std::to_string(t->minute) + ":" + std::to_string(t->second) + " w" + std::to_string(t->weekday) : std::string("none");
    };
    Check(shown(paris(At(2026, 10, 24, 22, 0, 0))) == "2026-10-25 0:0:0 w6", "Saturday 22:00 UTC: Sunday 00:00 CEST");
    Check(shown(paris(At(2026, 10, 25, 0, 30, 0))) == "2026-10-25 2:30:0 w6", "02:30 CEST");
    Check(shown(paris(At(2026, 10, 25, 0, 59, 59))) == "2026-10-25 2:59:59 w6", "Last second of summer time");
    Check(shown(paris(At(2026, 10, 25, 1, 0, 0))) == "2026-10-25 2:0:0 w6", "Back to 02:00 CET");
    Check(shown(paris(At(2026, 10, 25, 1, 30, 0))) == "2026-10-25 2:30:0 w6", "02:30 CET, the second time");
    Check(shown(paris(At(2026, 10, 25, 22, 59, 59))) == "2026-10-25 23:59:59 w6" && shown(paris(At(2026, 10, 25, 23, 0, 0))) == "2026-10-26 0:0:0 w0",
        "Midnight in winter time");
    Check(shown(paris(At(2027, 3, 28, 0, 59, 59))) == "2027-3-28 1:59:59 w6" && shown(paris(At(2027, 3, 28, 1, 0, 0))) == "2027-3-28 3:0:0 w6",
        "02:00 CET is 03:00 CEST");
    Check(shown(paris(At(2027, 3, 27, 23, 0, 0))) == "2027-3-28 0:0:0 w6" && shown(paris(At(2027, 3, 28, 22, 0, 0))) == "2027-3-29 0:0:0 w0",
        "The day before and after in Paris");
    Check(shown(LocalTime(At(2026, 10, 25, 1, 30, 0), L"UTC")) == "2026-10-25 1:30:0 w6", "UTC");
    Check(shown(LocalTime(At(2026, 11, 1, 5, 30, 0), L"Eastern Standard Time")) == "2026-11-1 1:30:0 w6" &&
        shown(LocalTime(At(2026, 11, 1, 6, 30, 0), L"Eastern Standard Time")) == "2026-11-1 1:30:0 w6", "New York's own change");
    Check(!LocalTime(At(2026, 10, 25, 1, 30, 0), L"Nowhere Standard Time") && !LocalTime(-1, L"UTC") &&
        !LocalTime(At(2200, 1, 1, 0, 0, 0), L"UTC") && LocalTime(At(2199, 12, 31, 23, 59, 59), L"UTC"), "Unknown zone, out of range");
    // This PC's own zone: the same as its key's rules.
    DYNAMIC_TIME_ZONE_INFORMATION mine{};
    Check(GetDynamicTimeZoneInformation(&mine) != TIME_ZONE_ID_INVALID, "This PC's time zone");
    for (const auto at : {At(2026, 10, 25, 0, 30, 0), At(2026, 10, 25, 1, 30, 0), At(2027, 3, 28, 1, 0, 0), At(2026, 7, 14, 12, 0, 0)})
        Check(mine.DynamicDaylightTimeDisabled || !mine.TimeZoneKeyName[0] || shown(LocalTime(at)) == shown(LocalTime(at, mine.TimeZoneKeyName)), "This PC's local time");
}

int main() {
    try {
        Answers();
        Hostile();
        Headers();
        Validators();
        Names();
        Choices();
        Monsters();
        Languages();
        Kralamoure();
        Texts();
        Times();
        std::cout << "PASS: Metamob answers (accents, ’ and ', JSON escapes, null, unknown fields, wrong types, HTML, truncated JSON), "
            "fixed errors without body or key, Retry-After, key/slug bounds, NameKey, quest choice and the kind of its problem, "
            "monster match in fr/en/es; Kralamoure openings (strict: nulls, bad dates, duplicates, 500 openings, 1 MiB, "
            "description and creator bounds, the quests' lenient server id), display-safe text (markup, controls, bidi, "
            "invisible, cut on a character), UTC dates, civil days, local time with Windows' rules (Paris 2026-10-25 and "
            "2027-03-28, New York, UTC, this PC)\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
