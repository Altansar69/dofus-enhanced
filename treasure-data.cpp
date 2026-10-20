#include "treasure-data.hpp"
#include "third_party/nlohmann/json.hpp"
#include <windows.h>
#include <algorithm>
#include <charconv>
#include <map>
#include <stdexcept>

namespace treasure {
namespace {
int Integer(const nlohmann::json& value, int minimum, int maximum) {
    if (!value.is_number_integer()) throw std::runtime_error("Nombre DofusDB invalide.");
    const auto number = value.get<int64_t>();
    if (number < minimum || number > maximum) throw std::runtime_error("Nombre DofusDB hors limites.");
    return static_cast<int>(number);
}
}
bool Coordinate(const std::string& text, int& value) {
    const auto first = text.find_first_not_of(" \t");
    if (first == std::string::npos) return false;
    const auto last = text.find_last_not_of(" \t") + 1;
    int parsed = 0;
    auto result = std::from_chars(text.data()+first, text.data()+last, parsed);
    if (result.ec != std::errc{} || result.ptr != text.data()+last || parsed < -1000 || parsed > 1000) return false;
    value = parsed; return true;
}
bool Valid(const Query& q) {
    return q.x >= -1000 && q.x <= 1000 && q.y >= -1000 && q.y <= 1000 &&
        (q.direction == 0 || q.direction == 2 || q.direction == 4 || q.direction == 6);
}
std::wstring RequestPath(const Query& q, int skip) {
    if (!Valid(q) || skip < 0 || skip > 500) throw std::runtime_error("Coordonnées ou direction invalides.");
    return L"/treasure-hunt?x="+std::to_wstring(q.x)+L"&y="+std::to_wstring(q.y)+
        L"&direction="+std::to_wstring(q.direction)+L"&%24limit=50&%24skip="+std::to_wstring(skip)+L"&lang=fr";
}
Page ParsePage(const std::string& body, const Query& query, int skip) {
    if (body.size() > 1024*1024 || !Valid(query)) throw std::runtime_error("Réponse DofusDB trop volumineuse.");
    const auto json = nlohmann::json::parse(body);
    Page page;
    page.total = Integer(json.at("total"),0,500);
    page.skip = Integer(json.at("skip"),0,500);
    const auto& data = json.at("data");
    if (!data.is_array() || page.total < 0 || page.total > 500 || page.skip != skip ||
        data.size() > 50 || skip + static_cast<int>(data.size()) > page.total)
        throw std::runtime_error("Réponse DofusDB incohérente.");
    page.count = static_cast<int>(data.size());
    for (const auto& map : data) {
        const int x = Integer(map.at("posX"),-1000,1000), y = Integer(map.at("posY"),-1000,1000);
        const int dx = x-query.x, dy = y-query.y;
        const bool aligned = query.direction == 0 ? dy==0 && dx>0 :
            query.direction == 2 ? dx==0 && dy>0 :
            query.direction == 4 ? dy==0 && dx<0 : dx==0 && dy<0;
        const int distance = Integer(map.at("distance"),1,10);
        if (x < -1000 || x > 1000 || y < -1000 || y > 1000 || !aligned ||
            distance != std::abs(dx)+std::abs(dy) || distance > 10)
            throw std::runtime_error("Position d'indice DofusDB invalide.");
        const auto& pois = map.at("pois");
        if (!pois.is_array() || pois.size() > 128) throw std::runtime_error("Liste d'indices invalide.");
        for (const auto& poi : pois) {
            Clue clue{Integer(poi.at("id"),1,INT_MAX), x, y, distance, poi.at("name").at("fr").get<std::string>()};
            if (clue.id <= 0 || clue.name.empty() || clue.name.size() > 256 ||
                std::any_of(clue.name.begin(), clue.name.end(), [](unsigned char c){return c < 32 || c == '<' || c == '>'; }))
                throw std::runtime_error("Libellé d'indice invalide.");
            page.clues.push_back(std::move(clue));
        }
    }
    if (!page.count && skip < page.total) throw std::runtime_error("Pagination DofusDB interrompue.");
    return page;
}
std::string SearchKey(const std::string& text) {
    if (text.empty()) return {};
    auto length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (!length) return {};
    std::wstring wide(length, 0);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), wide.data(), length);
    auto count = NormalizeString(NormalizationD, wide.data(), length, nullptr, 0);
    if (count <= 0) return {};
    std::wstring normalized(count, 0);
    count = NormalizeString(NormalizationD, wide.data(), length, normalized.data(), count);
    if (count <= 0) return {};
    normalized.resize(count);
    std::wstring clean;
    for (wchar_t ch : normalized) {
        WORD type = 0;
        GetStringTypeW(CT_CTYPE3, &ch, 1, &type);
        if (!(type & C3_NONSPACING)) clean.push_back(ch);
    }
    if (clean.empty()) return {};
    std::wstring lower(clean.size(), 0);
    if (!LCMapStringEx(L"fr-FR", LCMAP_LOWERCASE, clean.data(), static_cast<int>(clean.size()),
        lower.data(), static_cast<int>(lower.size()), nullptr, nullptr, 0)) return {};
    count = WideCharToMultiByte(CP_UTF8, 0, lower.data(), static_cast<int>(lower.size()), nullptr, 0, nullptr, nullptr);
    std::string result(count, 0);
    WideCharToMultiByte(CP_UTF8, 0, lower.data(), static_cast<int>(lower.size()), result.data(), count, nullptr, nullptr);
    return result;
}
std::vector<Clue> Nearest(std::vector<Clue> clues) {
    std::map<int, Clue> nearest;
    for (auto& clue : clues) {
        auto found = nearest.find(clue.id);
        if (found == nearest.end() || clue.distance < found->second.distance) nearest[clue.id] = std::move(clue);
    }
    std::vector<Clue> result;
    for (auto& [id, clue] : nearest) result.push_back(std::move(clue));
    std::sort(result.begin(),result.end(),[](const Clue& a,const Clue& b){return SearchKey(a.name)<SearchKey(b.name);});
    return result;
}
std::vector<Clue> Filter(const std::vector<Clue>& clues, const std::string& text) {
    const auto key = SearchKey(text);
    std::vector<Clue> result;
    for (const auto& clue : clues)
        if (SearchKey(clue.name).find(key) != std::string::npos) result.push_back(clue);
    return result;
}
}
