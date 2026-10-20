#pragma once
#include <charconv>
#include <string_view>
#include <vector>

struct ChatCoordinateSpan { size_t start, length; };
inline std::vector<ChatCoordinateSpan> ChatCoordinateSpans(std::string_view text) {
    std::vector<ChatCoordinateSpan> spans;
    bool tag = false; char quote = 0;
    auto space = [&](size_t& p) { while (p < text.size() && (text[p] == ' ' || text[p] == '\t')) ++p; };
    auto number = [&](size_t& p) {
        space(p);
        if (p < text.size() && text[p] == '+') {
            ++p;
            if (p >= text.size() || text[p] < '0' || text[p] > '9') return false;
        }
        if (p >= text.size()) return false;
        int value = 0;
        auto parsed = std::from_chars(text.data()+p,text.data()+text.size(),value);
        if (parsed.ec != std::errc{}) return false;
        p = static_cast<size_t>(parsed.ptr-text.data()); space(p);
        return true;
    };
    for (size_t i = 0; i < text.size() && spans.size() < 16; ++i) {
        const char c = text[i];
        if (tag) {
            if (quote) { if (c == quote) quote = 0; }
            else if (c == '\'' || c == '"') quote = c;
            else if (c == '>') tag = false;
            continue;
        }
        if (c == '<') { tag = true; continue; }
        if (c != '[' || (i && text[i-1] == '[')) continue;
        size_t p = i+1;
        if (!number(p) || p >= text.size() || text[p++] != ',' || !number(p) || p >= text.size() || text[p] != ']') continue;
        if (p+1 < text.size() && text[p+1] == ']') continue;
        spans.push_back({i,p-i+1}); i = p;
    }
    return spans;
}
