#include "metamob-data.hpp"
#include <windows.h>
#include "third_party/nlohmann/json.hpp"
#include <algorithm>
#include <iterator>
#include <limits>
#include <set>
#include <stdexcept>

namespace metamob {
namespace {
using Json = nlohmann::json;
// The transport keeps 1 MiB of a body at most; Metamob answers nest 6 levels.
constexpr size_t MaxBody = 1024 * 1024;
constexpr int MaxDepth = 32;
// Every count, step, quantity and offset stays far below INT_MAX, so the
// sync's own arithmetic (offset + limit, quantity + n) cannot overflow.
constexpr int64_t MaxCount = 1000000;
// Text typed by the user on Metamob: the username and the free "Personnage à
// contacter" field (200 characters).
constexpr size_t MaxFreeText = 1024;
// NameKey only compares names: a longer input has no key and never matches.
constexpr size_t MaxKeyInput = 4096;
constexpr size_t MaxUsername = 40;
// A Kralamoure description as Metamob sends it: far more is no description.
constexpr size_t MaxDescriptionInput = 8192;

// Every answer goes through here. nlohmann's own messages quote the body
// ("last read: ..."), so each parse or type error becomes one fixed text.
template<class Reader> auto Read(std::string_view body, Reader reader) {
    if (body.size() > MaxBody) throw std::runtime_error("Réponse Metamob trop volumineuse.");
    try {
        const auto json = Json::parse(body.data(), body.data() + body.size(), [](int depth, Json::parse_event_t, Json&) {
            // Refused before it is built: 1 MiB of "[[[[" would cost tens of MiB.
            if (depth > MaxDepth) throw std::runtime_error("Réponse Metamob illisible.");
            return true;
        });
        return reader(json);
    } catch (const Json::exception&) { throw std::runtime_error("Réponse Metamob illisible."); }
}
const Json& Field(const Json& object, const char* name) {
    if (!object.is_object()) throw std::runtime_error("Réponse Metamob inattendue.");
    const auto found = object.find(name);
    if (found == object.end()) throw std::runtime_error("Réponse Metamob inattendue.");
    return *found;
}
// Fields the API documents as nullable (server, game_version, character_name,
// quest_type, monster type): absent and null read the same.
const Json* Nullable(const Json& object, const char* name) {
    if (!object.is_object()) throw std::runtime_error("Réponse Metamob inattendue.");
    const auto found = object.find(name);
    return found == object.end() || found->is_null() ? nullptr : &*found;
}
// Strict: no float, bool or string, and a huge unsigned value never wraps into
// a small signed one (get<int64_t> alone would).
int64_t Integer(const Json& value, int64_t minimum, int64_t maximum) {
    if (!value.is_number_integer() || (value.is_number_unsigned() &&
        value.get<uint64_t>() > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())))
        throw std::runtime_error("Nombre Metamob invalide.");
    const auto number = value.get<int64_t>();
    if (number < minimum || number > maximum) throw std::runtime_error("Nombre Metamob invalide.");
    return number;
}
int Count(const Json& value, int minimum) { return static_cast<int>(Integer(value, minimum, MaxCount)); }
int64_t Id(const Json& value) { return Integer(value, 1, std::numeric_limits<int64_t>::max()); }
// An id only a later reader needs (the quests' server.id, for the calendar):
// absent, null or unusable reads 0 and never refuses the answer.
int64_t OptionalId(const Json& object, const char* name) {
    const auto* value = Nullable(object, name);
    if (!value) return 0;
    try { return Id(*value); } catch (const std::runtime_error&) { return 0; }
}
// C0 and C1 control characters and DEL (U+0080..U+009F are C2 80..C2 9F).
bool HasControl(std::string_view text) {
    for (size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c < 32 || c == 127 || (c == 0xC2 && i + 1 < text.size() && static_cast<unsigned char>(text[i + 1]) < 0xA0))
            return true;
    }
    return false;
}
// Reference names (monster, server, quest type): never empty, bounded, no
// control character and no '<' '>' (the chat notices are rich text).
std::string Label(const Json& value, size_t maximum) {
    if (!value.is_string()) throw std::runtime_error("Texte Metamob invalide.");
    const auto& text = value.get_ref<const std::string&>();
    if (text.empty() || text.size() > maximum || HasControl(text) || text.find_first_of("<>") != std::string::npos)
        throw std::runtime_error("Texte Metamob invalide.");
    return text;
}
// Text the user typed on Metamob: only bounded. It is shown through
// SafeUsername or compared through NameKey, never as is, so an odd character
// in a contact name must not stop the sync of every quest.
std::string FreeText(const Json& value) {
    if (!value.is_string() || value.get_ref<const std::string&>().size() > MaxFreeText)
        throw std::runtime_error("Texte Metamob invalide.");
    return value.get<std::string>();
}
// name.en and name.es only serve the match: absent, null or unusable, the
// translation stays empty and never refuses the page (name.fr stays required).
std::string Translation(const Json& names, const char* language) {
    const auto* value = Nullable(names, language);
    if (!value) return {};
    try { return Label(*value, 256); } catch (const std::runtime_error&) { return {}; }
}
// The slug goes into request paths: refuse the whole answer rather than skip
// a quest (skipping could leave a wrong "only" Ocre quest).
std::string Slug(const Json& value) {
    if (!value.is_string() || !ValidSlug(value.get_ref<const std::string&>()))
        throw std::runtime_error("Quête Metamob invalide.");
    return value.get<std::string>();
}

std::wstring Wide(std::string_view text) {
    const auto size = static_cast<int>(text.size());
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), size, nullptr, 0);
    if (length <= 0) return {};
    std::wstring wide(static_cast<size_t>(length), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), size, wide.data(), length) != length) return {};
    return wide;
}
std::string Utf8(const std::wstring& text) {
    const auto size = static_cast<int>(text.size());
    const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), size, nullptr, 0, nullptr, nullptr);
    if (length <= 0) return {};
    std::string utf8(static_cast<size_t>(length), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), size, utf8.data(), length, nullptr, nullptr) != length)
        return {};
    return utf8;
}
// NFD splits "é" into "e" + U+0301. The first size is only an estimate: retry
// with the size NormalizeString asks for, as its documentation does.
std::wstring Decomposed(const std::wstring& text) {
    const auto length = static_cast<int>(text.size());
    int size = NormalizeString(NormalizationD, text.data(), length, nullptr, 0);
    for (int attempt = 0; attempt < 4 && size > 0; ++attempt) {
        std::wstring result(static_cast<size_t>(size), L'\0');
        const int written = NormalizeString(NormalizationD, text.data(), length, result.data(), size);
        if (written > 0) { result.resize(static_cast<size_t>(written)); return result; }
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) break;
        size = -written;
    }
    return {};
}
std::wstring Lower(const std::wstring& text) {
    const auto length = static_cast<int>(text.size());
    for (const auto* locale : {L"fr-FR", LOCALE_NAME_INVARIANT}) {
        const int size = LCMapStringEx(locale, LCMAP_LOWERCASE, text.data(), length, nullptr, 0, nullptr, nullptr, 0);
        if (size <= 0) continue;
        std::wstring lower(static_cast<size_t>(size), L'\0');
        if (LCMapStringEx(locale, LCMAP_LOWERCASE, text.data(), length, lower.data(), size, nullptr, nullptr, 0) == size)
            return lower;
    }
    return {};
}
// Tab, line breaks, no-break, thin and other typographic spaces.
bool Space(wchar_t c) {
    return c == L' ' || (c >= L'\t' && c <= L'\r') || c == 0x00A0 || (c >= 0x2000 && c <= 0x200A) ||
        c == 0x202F || c == 0x205F || c == 0x3000;
}
// "Bob", "Bob, Alice", "Bob/Alice", "Mon perso": the whole contact field or
// one of its comma, slash or space separated parts names the character.
bool Names(const std::string& field, const std::string& character) {
    const auto key = NameKey(field);
    if (key.empty()) return false;
    if (key == character) return true;
    for (size_t start = 0; start <= key.size();) {
        const auto end = std::min(key.find_first_of(",/ ", start), key.size());
        if (end > start && key.compare(start, end - start, character) == 0) return true;
        start = end + 1;
    }
    return false;
}
// FindMonster's key: NameKey with a comma read as a space, so Metamob's Spanish
// "Colmillazaqui, el Inagotable" is the game's "Colmillazaqui el Inagotable".
// NameKey itself keeps commas: ChooseQuest splits the contact field on them.
// Both are single bytes, never inside a UTF-8 sequence.
std::string MatchKey(std::string_view name) {
    std::string key;
    for (const char c : NameKey(name)) {
        if (c != ',' && c != ' ') key += c;
        else if (!key.empty() && key.back() != ' ') key += ' '; // one space, never leading
    }
    if (!key.empty() && key.back() == ' ') key.pop_back(); // nor trailing
    return key;
}
struct CodePoint { char32_t value = 0; size_t length = 0; };
// One UTF-8 sequence at text[at]; length 0 when it is not valid UTF-8.
CodePoint Decode(std::string_view text, size_t at) {
    const auto lead = static_cast<unsigned char>(text[at]);
    if (lead < 0x80) return {static_cast<char32_t>(lead), 1};
    const size_t length = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : 2;
    if (lead < 0xC2 || lead > 0xF4 || at + length > text.size()) return {};
    auto value = static_cast<char32_t>(lead & (0x7F >> length));
    for (size_t i = 1; i < length; ++i) {
        const auto next = static_cast<unsigned char>(text[at + i]);
        if ((next & 0xC0) != 0x80) return {};
        value = static_cast<char32_t>((value << 6) | (next & 0x3Fu));
    }
    // Overlong forms, surrogates and values past U+10FFFF are not UTF-8.
    constexpr char32_t Minimum[]{0, 0, 0x80, 0x800, 0x10000};
    if (value < Minimum[length] || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) return {};
    return {value, length};
}
// Controls, markup and the invisible characters that reorder or break a chat
// line (bidi overrides and isolates, line and paragraph separators).
bool Hidden(char32_t c) {
    return c < 32 || (c >= 0x7F && c <= 0x9F) || c == U'<' || c == U'>' || c == 0x061C ||
        (c >= 0x200B && c <= 0x200F) || (c >= 0x2028 && c <= 0x202E) || (c >= 0x2060 && c <= 0x206F) || c == 0xFEFF;
}
// Also dropped from free text: Unicode tag characters, interlinear annotation
// marks and the Mongolian vowel separator (all invisible).
bool Invisible(char32_t c) { return (c >= 0xE0000 && c <= 0xE007F) || (c >= 0xFFF9 && c <= 0xFFFB) || c == 0x180E; }
// Two or four decimal digits, nothing else (from_chars would accept a sign).
bool Digits(std::string_view text, int& value) {
    value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') return false;
        value = value * 10 + (c - '0');
    }
    return !text.empty();
}
// Days from 1970-01-01 to a proleptic Gregorian date (H. Hinnant's days_from_civil).
int64_t Days(int64_t year, int64_t month, int64_t day) {
    year -= month <= 2 ? 1 : 0;
    const int64_t era = (year >= 0 ? year : year - 399) / 400, yearOfEra = year - era * 400;
    const int64_t dayOfYear = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
    return era * 146097 + yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear - 719468;
}
int MonthLength(int year, int month) {
    constexpr int Lengths[]{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    return Lengths[month - 1] + (month == 2 && leap ? 1 : 0);
}
int64_t FloorDiv(int64_t value, int64_t divisor) { return value / divisor - (value % divisor < 0 ? 1 : 0); }
// A Windows time zone by its key name (EnumDynamicTimeZoneInformation, Windows 8+).
bool Zone(std::wstring_view key, DYNAMIC_TIME_ZONE_INFORMATION& info) {
    for (DWORD index = 0; index < 4096; ++index) {
        DYNAMIC_TIME_ZONE_INFORMATION candidate{};
        const DWORD result = EnumDynamicTimeZoneInformation(index, &candidate);
        if (result == ERROR_NO_MORE_ITEMS) return false;
        if (result == ERROR_SUCCESS && key == std::wstring_view(candidate.TimeZoneKeyName)) {
            info = candidate;
            return true;
        }
    }
    return false;
}
// IMF-fixdate (RFC 9110 5.6.7), the date form servers send:
// "Sun, 06 Nov 1994 08:49:37 GMT". The obsolete forms are not read.
std::optional<int64_t> HttpDate(std::string_view text) {
    static constexpr std::string_view Weekdays[]{"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    static constexpr std::string_view Months[]{"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    if (text.size() != 29 || text.substr(3, 2) != ", " || text[7] != ' ' || text[11] != ' ' || text[16] != ' ' ||
        text[19] != ':' || text[22] != ':' || text.substr(25) != " GMT" ||
        std::find(std::begin(Weekdays), std::end(Weekdays), text.substr(0, 3)) == std::end(Weekdays))
        return std::nullopt;
    const auto month = std::find(std::begin(Months), std::end(Months), text.substr(8, 3));
    int day = 0, year = 0, hour = 0, minute = 0, second = 0;
    if (month == std::end(Months) || !Digits(text.substr(5, 2), day) || !Digits(text.substr(12, 4), year) ||
        !Digits(text.substr(17, 2), hour) || !Digits(text.substr(20, 2), minute) || !Digits(text.substr(23, 2), second))
        return std::nullopt;
    const auto index = static_cast<int>(month - std::begin(Months));
    constexpr int Lengths[]{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    if (day < 1 || day > Lengths[index] + (index == 1 && leap ? 1 : 0) || hour > 23 || minute > 59 || second > 60)
        return std::nullopt;
    return Days(year, index + 1, day) * 86400 + hour * 3600 + minute * 60 + second;
}
}

Account ParseMe(std::string_view body) {
    return Read(body, [](const Json& json) { return Account{FreeText(Field(Field(json, "data"), "username"))}; });
}
std::vector<Quest> ParseQuests(std::string_view body) {
    return Read(body, [](const Json& json) {
        const auto& data = Field(json, "data");
        // Five quests per account at most: far more is no list of quests.
        if (!data.is_array() || data.size() > 100) throw std::runtime_error("Réponse Metamob inattendue.");
        std::vector<Quest> quests;
        std::set<std::string> slugs;
        for (const auto& item : data) {
            Quest quest;
            quest.slug = Slug(Field(item, "slug"));
            if (!slugs.insert(quest.slug).second) throw std::runtime_error("Quête Metamob invalide.");
            if (const auto* name = Nullable(item, "character_name")) quest.characterName = FreeText(*name);
            quest.parallelQuests = Count(Field(item, "parallel_quests"), 0);
            if (const auto* server = Nullable(item, "server")) {
                quest.serverName = Label(Field(*server, "name"), 128);
                if (const auto* version = Nullable(*server, "game_version")) quest.gameVersion = Count(Field(*version, "id"), 1);
                quest.serverId = OptionalId(*server, "id");
            }
            if (const auto* type = Nullable(Field(item, "quest_template"), "quest_type"))
                quest.questType = Label(Field(*type, "slug"), 64);
            quests.push_back(std::move(quest));
        }
        return quests;
    });
}
MonsterPage ParseMonsters(std::string_view body) {
    return Read(body, [](const Json& json) {
        const auto& data = Field(json, "data");
        MonsterPage page;
        page.slug = Slug(Field(data, "slug"));
        const auto& pagination = Field(data, "pagination"); // inside data here (top level for /v1/monsters)
        page.total = Count(Field(pagination, "total"), 0);
        page.limit = Count(Field(pagination, "limit"), 1);
        page.offset = Count(Field(pagination, "offset"), 0);
        const auto& monsters = Field(data, "monsters");
        if (!monsters.is_array()) throw std::runtime_error("Réponse Metamob inattendue.");
        // An empty page past the end is a fair answer; monsters past the total are not.
        if (monsters.size() > static_cast<size_t>(page.limit) ||
            (!monsters.empty() && static_cast<size_t>(page.offset) + monsters.size() > static_cast<size_t>(page.total)))
            throw std::runtime_error("Pagination Metamob incohérente.");
        std::set<int64_t> ids;
        for (const auto& item : monsters) {
            QuestMonster monster;
            monster.id = Id(Field(item, "id"));
            const auto& names = Field(item, "name");
            monster.name = Label(Field(names, "fr"), 256); // first: a name that is no object refuses the page
            monster.nameEn = Translation(names, "en");
            monster.nameEs = Translation(names, "es");
            if (const auto* type = Nullable(item, "type")) monster.type = Count(Field(*type, "id"), 1);
            monster.step = Count(Field(item, "step"), 1);
            monster.quantity = Count(Field(item, "quantity"), 0); // the site itself goes past MaxQuantity
            if (!ids.insert(monster.id).second) throw std::runtime_error("Monstre Metamob en double.");
            page.monsters.push_back(std::move(monster));
        }
        return page;
    });
}
QuantityUpdate ParseQuantityUpdate(std::string_view body) {
    return Read(body, [](const Json& json) {
        const auto& data = Field(json, "data");
        return QuantityUpdate{Id(Field(data, "monster_id")), Count(Field(data, "quantity"), 0)};
    });
}
std::vector<Kralove> ParseKralove(std::string_view body) {
    return Read(body, [](const Json& json) {
        const auto& data = Field(json, "data");
        if (!data.is_array() || data.size() > MaxKralove) throw std::runtime_error("Réponse Metamob inattendue.");
        std::vector<Kralove> openings;
        std::set<int64_t> ids;
        for (const auto& item : data) {
            Kralove opening;
            opening.id = Id(Field(item, "id"));
            if (!ids.insert(opening.id).second) throw std::runtime_error("Ouverture Metamob en double.");
            const auto& when = Field(item, "event_datetime");
            const auto at = when.is_string() ? UtcSeconds(when.get_ref<const std::string&>()) : std::nullopt;
            if (!at) throw std::runtime_error("Date Metamob invalide.");
            opening.at = *at;
            if (const auto* text = Nullable(item, "description")) {
                if (!text->is_string() || text->get_ref<const std::string&>().size() > MaxDescriptionInput)
                    throw std::runtime_error("Texte Metamob invalide.");
                opening.description = SafeText(text->get_ref<const std::string&>(), MaxDescription);
            }
            if (const auto* creator = Nullable(item, "creator")) opening.creator = SafeUsername(FreeText(*creator));
            opening.participants = Count(Field(item, "participants_count"), 0);
            opening.characters = Count(Field(item, "character_count"), 0);
            opening.messages = Count(Field(item, "messages_count"), 0);
            if (const auto* server = Nullable(item, "server")) {
                opening.serverId = Id(Field(*server, "id"));
                opening.server = Label(Field(*server, "name"), 128);
            }
            openings.push_back(std::move(opening));
        }
        std::sort(openings.begin(), openings.end(), [](const Kralove& a, const Kralove& b) { return a.at != b.at ? a.at < b.at : a.id < b.id; });
        return openings;
    });
}
std::string QuantityBody(int quantity) {
    if (quantity < 0 || quantity > MaxQuantity) throw std::runtime_error("Quantité Metamob hors limites.");
    return Json{{"quantity", quantity}}.dump();
}
std::optional<int> RetryAfterSeconds(std::string_view header, int64_t nowUnix) {
    constexpr int Shortest = 1, Longest = 900;
    const auto first = header.find_first_not_of(" \t");
    if (first == std::string_view::npos) return std::nullopt;
    const auto text = header.substr(first, header.find_last_not_of(" \t") + 1 - first);
    if (std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; })) {
        // delay-seconds may have any number of digits: stop counting past the bound.
        int64_t seconds = 0;
        for (const char c : text) {
            seconds = seconds * 10 + (c - '0');
            if (seconds > Longest) return Longest;
        }
        return std::max(Shortest, static_cast<int>(seconds));
    }
    const auto date = HttpDate(text);
    if (!date) return std::nullopt;
    // Compared without subtracting nowUnix, which may be any value.
    if (nowUnix <= *date - Longest) return Longest;
    if (nowUnix >= *date - Shortest) return Shortest;
    return static_cast<int>(*date - nowUnix);
}
bool ValidKey(std::string_view key) {
    return key.size() >= 16 && key.size() <= 256 &&
        std::all_of(key.begin(), key.end(), [](unsigned char c) { return c >= 0x21 && c <= 0x7E; });
}
bool ValidSlug(std::string_view slug) {
    return !slug.empty() && slug.size() <= 64 && std::all_of(slug.begin(), slug.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    });
}
std::string SafeUsername(std::string_view username) {
    std::string safe;
    for (size_t at = 0; at < username.size();) {
        const auto point = Decode(username, at);
        if (!point.length) { ++at; continue; } // a stray byte is dropped
        const auto piece = username.substr(at, point.length);
        at += point.length;
        if (Hidden(point.value)) continue;
        if (safe.size() + piece.size() > MaxUsername) break; // never cut a character
        safe += piece;
    }
    return safe;
}
std::string SafeText(std::string_view text, size_t maximum) {
    std::string safe;
    bool space = false;
    for (size_t at = 0; at < text.size();) {
        const auto point = Decode(text, at);
        if (!point.length) { ++at; continue; } // a stray byte is dropped
        const auto piece = text.substr(at, point.length);
        at += point.length;
        const auto c = point.value;
        if (c == U' ' || (c >= U'\t' && c <= U'\r') || c == 0x2028 || c == 0x2029) { space = !safe.empty(); continue; }
        if (Hidden(c) || Invisible(c)) continue;
        if (space) { safe += ' '; space = false; }
        safe += piece;
    }
    if (safe.size() <= maximum) return safe;
    constexpr std::string_view Ellipsis = "…";
    if (maximum < Ellipsis.size()) return {};
    size_t end = maximum - Ellipsis.size();
    while (end > 0 && (static_cast<unsigned char>(safe[end]) & 0xC0) == 0x80) --end; // never half a character
    safe.resize(end);
    while (!safe.empty() && safe.back() == ' ') safe.pop_back();
    return safe + std::string(Ellipsis);
}
int64_t DayNumber(int year, int month, int day) { return Days(year, month, day); }
CivilTime CivilDay(int64_t dayNumber) {
    // H. Hinnant's civil_from_days; 1970-01-01 was a Thursday.
    const int64_t z = dayNumber + 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097, dayOfEra = z - era * 146097;
    const int64_t yearOfEra = (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
    const int64_t dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
    const int64_t shifted = (5 * dayOfYear + 2) / 153, month = shifted < 10 ? shifted + 3 : shifted - 9;
    CivilTime civil;
    civil.year = static_cast<int>(yearOfEra + era * 400 + (month <= 2 ? 1 : 0));
    civil.month = static_cast<int>(month);
    civil.day = static_cast<int>(dayOfYear - (153 * shifted + 2) / 5 + 1);
    civil.weekday = static_cast<int>(((dayNumber % 7) + 7 + 3) % 7);
    return civil;
}
std::optional<int64_t> UtcSeconds(std::string_view text) {
    if (text.size() < 20 || text[4] != '-' || text[7] != '-' || text[10] != 'T' || text[13] != ':' || text[16] != ':')
        return std::nullopt;
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    if (!Digits(text.substr(0, 4), year) || !Digits(text.substr(5, 2), month) || !Digits(text.substr(8, 2), day) ||
        !Digits(text.substr(11, 2), hour) || !Digits(text.substr(14, 2), minute) || !Digits(text.substr(17, 2), second))
        return std::nullopt;
    auto zone = text.substr(19);
    if (!zone.empty() && zone.front() == '.') {
        size_t digits = 1;
        while (digits < zone.size() && zone[digits] >= '0' && zone[digits] <= '9') ++digits;
        if (digits == 1 || digits > 10) return std::nullopt; // 1 to 9 digits of a second
        zone = zone.substr(digits);
    }
    if (zone != "Z" && zone != "+00:00") return std::nullopt;
    if (year < 2000 || year > 2199 || month < 1 || month > 12 || day < 1 || day > MonthLength(year, month) || hour > 23 ||
        minute > 59 || second > 59)
        return std::nullopt;
    return Days(year, month, day) * 86400 + hour * 3600 + minute * 60 + second;
}
std::optional<CivilTime> LocalTime(int64_t unixSeconds, std::wstring_view zone) {
    if (unixSeconds < 0 || unixSeconds >= Days(2200, 1, 1) * 86400) return std::nullopt;
    DYNAMIC_TIME_ZONE_INFORMATION info{};
    if (zone.empty()) {
        if (GetDynamicTimeZoneInformation(&info) == TIME_ZONE_ID_INVALID) return std::nullopt;
    } else if (!Zone(zone, info)) return std::nullopt;
    const auto date = CivilDay(FloorDiv(unixSeconds, 86400));
    const auto seconds = unixSeconds % 86400;
    SYSTEMTIME utc{};
    utc.wYear = static_cast<WORD>(date.year); utc.wMonth = static_cast<WORD>(date.month); utc.wDay = static_cast<WORD>(date.day);
    utc.wHour = static_cast<WORD>(seconds / 3600); utc.wMinute = static_cast<WORD>(seconds / 60 % 60);
    utc.wSecond = static_cast<WORD>(seconds % 60);
    SYSTEMTIME local{};
    if (!SystemTimeToTzSpecificLocalTimeEx(&info, &utc, &local)) return std::nullopt;
    auto civil = CivilDay(Days(local.wYear, local.wMonth, local.wDay));
    civil.hour = local.wHour; civil.minute = local.wMinute; civil.second = local.wSecond;
    return civil;
}
std::string NameKey(std::string_view utf8) {
    if (utf8.empty() || utf8.size() > MaxKeyInput) return {};
    const auto wide = Wide(utf8);
    if (wide.empty()) return {};
    const auto decomposed = Decomposed(wide);
    std::wstring folded;
    bool space = false;
    for (const wchar_t c : decomposed) {
        if (c >= 0x0300 && c <= 0x036F) continue; // the accents NFD split off
        if (c == 0x00AD || c == 0x200B || c == 0x2060 || c == 0xFEFF) continue; // soft hyphen, invisible
        if (Space(c)) { space = !folded.empty(); continue; } // collapsed, and trimmed at both ends
        if (space) { folded += L' '; space = false; }
        switch (c) {
        case 0x0152: case 0x0153: folded += L"oe"; break; // NFD leaves the ligatures whole
        case 0x00C6: case 0x00E6: folded += L"ae"; break;
        case 0x2019: case 0x2018: case 0x02BC: case 0x0060: case 0x00B4: case 0x2032: folded += L'\''; break;
        case 0x2212: folded += L'-'; break;
        default: folded += c >= 0x2010 && c <= 0x2015 ? L'-' : c;
        }
    }
    if (folded.empty()) return {};
    return Utf8(Lower(folded));
}
QuestChoice ChooseQuest(const std::vector<Quest>& quests, std::string_view characterName) {
    std::vector<const Quest*> ocre;
    for (const auto& quest : quests)
        if (quest.questType == "ocre" && quest.gameVersion == 1) ocre.push_back(&quest);
    if (ocre.empty())
        return {std::nullopt, "Metamob : aucune quête Ocre Dofus Unity sur ton compte, rien n’a été modifié.", QuestProblem::NoQuest};
    if (ocre.size() == 1) return {*ocre.front(), {}};
    const auto character = NameKey(characterName);
    const Quest* chosen = nullptr;
    size_t matches = 0;
    for (const auto* quest : ocre)
        if (!character.empty() && Names(quest->characterName, character) && ++matches == 1) chosen = quest;
    if (matches == 1) return {*chosen, {}};
    return {std::nullopt, "Metamob : plusieurs quêtes Ocre sur ton compte ; indique le nom de ce personnage dans "
        "« Personnage à contacter » de la bonne quête.", QuestProblem::Several};
}
std::optional<QuestMonster> FindMonster(const std::vector<QuestMonster>& monsters, std::string_view gameName, int type) {
    const auto key = MatchKey(gameName);
    if (key.empty()) return std::nullopt;
    // The game names a monster in the client's language: French, English and
    // Spanish match, the languages Metamob names in. The Unity Ocre list shares
    // no key between two of its monsters across them.
    const auto named = [&](const QuestMonster& monster) {
        for (const auto* name : {&monster.name, &monster.nameEn, &monster.nameEs})
            if (!name->empty() && MatchKey(*name) == key) return true;
        return false;
    };
    const QuestMonster* found = nullptr;
    for (const auto& monster : monsters) {
        if (monster.type != type || !named(monster)) continue;
        // The same entry twice (overlapping pages) is one monster; two ids are ambiguous.
        if (found && found->id != monster.id) return std::nullopt;
        if (!found) found = &monster;
    }
    if (!found) return std::nullopt;
    return *found;
}
MonsterIndex IndexMonsters(const std::vector<QuestMonster>& monsters) {
    MonsterIndex index;
    for (size_t i = 0; i < monsters.size(); ++i)
        for (const auto* name : {&monsters[i].name, &monsters[i].nameEn, &monsters[i].nameEs}) {
            if (name->empty()) continue;
            auto key = MatchKey(*name);
            if (key.empty()) continue;
            const auto [entry, added] = index.try_emplace({monsters[i].type, std::move(key)}, i);
            // The same monster again (another language, overlapping pages) keeps its
            // first entry; another monster under the same key leaves the key naming none.
            if (!added && entry->second && monsters[*entry->second].id != monsters[i].id) entry->second.reset();
        }
    return index;
}
std::optional<size_t> FindIndexed(const MonsterIndex& index, std::string_view gameName, int type) {
    const auto key = MatchKey(gameName);
    if (key.empty()) return std::nullopt;
    const auto found = index.find({type, key});
    return found == index.end() ? std::nullopt : found->second;
}
}
