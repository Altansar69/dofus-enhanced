#include "metamob-http.hpp"
#include "metamob-data.hpp"
#include "version.hpp"
#include <windows.h>
#include <winhttp.h>
#include <algorithm>
#include <memory>
#include <string_view>

namespace metamob {
namespace {
struct Close { void operator()(void* handle) const { if (handle) WinHttpCloseHandle(handle); } };
using Internet = std::unique_ptr<void, Close>;
constexpr size_t MaxBody = 1024 * 1024, MaxSent = 4096;
constexpr ULONGLONG RequestDeadline = 30000;
// The only paths the sync builds: /me, /quests and /quests/{slug}/monsters...
// with a validated slug and numbers. Nothing else may reach the request line.
bool PlainPath(std::string_view path) {
    return !path.empty() && path.front() == '/' && path.size() <= 512 && std::all_of(path.begin(), path.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '/' || c == '_' || c == '-' ||
            c == '?' || c == '=' || c == '&';
    });
}
std::wstring Wide(std::string_view ascii) {
    std::wstring wide;
    for (const char c : ascii) wide += static_cast<wchar_t>(static_cast<unsigned char>(c));
    return wide;
}
std::wstring UserAgent() {
    return L"DofusEnhanced/" + Wide(ModSemanticVersion) + L" (+https://github.com/Altansar69/dofus-enhanced)";
}
// The key travels in this buffer only: built at its final size (no stray copy
// left by a reallocation) and wiped once the request is over.
struct Headers {
    std::wstring text;
    Headers(const Secret& key, bool json) {
        constexpr std::wstring_view bearer = L"Authorization: Bearer ", accept = L"\r\nAccept: application/json\r\n",
            type = L"Content-Type: application/json\r\n";
        text.reserve(bearer.size() + key.value().size() + accept.size() + type.size());
        text += bearer;
        for (const char c : key.value()) text += static_cast<wchar_t>(static_cast<unsigned char>(c));
        text += accept;
        if (json) text += type;
    }
    Headers(const Headers&) = delete;
    ~Headers() { text.resize(text.capacity()); SecureZeroMemory(text.data(), text.size() * sizeof(wchar_t)); }
};
// A response header as printable ASCII (Retry-After, Content-Type); empty when
// absent, oversized or unusual.
std::string Header(HINTERNET request, DWORD query) {
    wchar_t buffer[256]{};
    DWORD size = sizeof(buffer);
    if (!WinHttpQueryHeaders(request, query, WINHTTP_HEADER_NAME_BY_INDEX, buffer, &size, WINHTTP_NO_HEADER_INDEX)) return {};
    std::string text;
    for (DWORD i = 0; i < size / sizeof(wchar_t) && buffer[i]; ++i) {
        if (buffer[i] != L'\t' && (buffer[i] < 0x20 || buffer[i] > 0x7E)) return {};
        text += static_cast<char>(buffer[i]);
    }
    return text;
}
// "application/json", any case, parameters (charset) allowed: a Cloudflare or
// proxy page is never read as an API answer.
bool JsonType(std::string type) {
    type = type.substr(0, type.find(';'));
    for (auto& c : type) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    const auto first = type.find_first_not_of(" \t"), last = type.find_last_not_of(" \t");
    return first != std::string::npos && type.substr(first, last + 1 - first) == "application/json";
}
Response Failed(Response& response) { response.error = GetLastError(); return response; }
}
Response SendWinHttp(const Request& request, const Secret& key, const std::atomic_bool& cancelled) {
    Response response;
    try {
        const bool write = request.method == "PATCH";
        // Checked before anything reaches the network: a key or a path that could
        // break a header or the request line is never sent.
        if ((!write && request.method != "GET") || !PlainPath(request.path) || !ValidKey(key.value()) ||
            (!write && !request.body.empty()) || request.body.size() > MaxSent) {
            response.error = ERROR_INVALID_PARAMETER;
            return response;
        }
        if (cancelled.load()) { response.error = ERROR_CANCELLED; return response; }
        const auto deadline = GetTickCount64() + RequestDeadline;
        Internet session(WinHttpOpen(UserAgent().c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
        if (!session || !WinHttpSetTimeouts(session.get(), 5000, 5000, 10000, 15000)) return Failed(response);
        // The www host itself: metamob.fr answers 308, and no redirect is ever followed.
        Internet connection(WinHttpConnect(session.get(), L"www.metamob.fr", INTERNET_DEFAULT_HTTPS_PORT, 0));
        if (!connection) return Failed(response);
        const auto path = L"/api/v1" + Wide(request.path);
        Internet handle(WinHttpOpenRequest(connection.get(), write ? L"PATCH" : L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
        // No redirect (the Bearer header never follows one), no cookie, no automatic Windows authentication.
        DWORD disable = WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_AUTHENTICATION;
        if (!handle || !WinHttpSetOption(handle.get(), WINHTTP_OPTION_DISABLE_FEATURE, &disable, sizeof(disable))) return Failed(response);
        const Headers headers(key, write);
        if (cancelled.load()) { response.error = ERROR_CANCELLED; return response; }
        const auto length = static_cast<DWORD>(request.body.size());
        if (!WinHttpSendRequest(handle.get(), headers.text.c_str(), static_cast<DWORD>(-1L),
            length ? static_cast<LPVOID>(const_cast<char*>(request.body.data())) : WINHTTP_NO_REQUEST_DATA, length, length, 0))
            return Failed(response);
        response.sent = true; // from here a write may have reached Metamob: its outcome is unknown until answered
        if (!WinHttpReceiveResponse(handle.get(), nullptr)) return Failed(response);
        DWORD status = 0, size = sizeof(status);
        if (!WinHttpQueryHeaders(handle.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
            &status, &size, WINHTTP_NO_HEADER_INDEX))
            return Failed(response);
        auto retryAfter = Header(handle.get(), WINHTTP_QUERY_RETRY_AFTER);
        const bool json = JsonType(Header(handle.get(), WINHTTP_QUERY_CONTENT_TYPE));
        std::string body;
        char buffer[16384];
        for (DWORD read = 1; read;) {
            if (cancelled.load() || GetTickCount64() > deadline) { response.error = cancelled.load() ? ERROR_CANCELLED : ERROR_TIMEOUT; return response; }
            if (!WinHttpReadData(handle.get(), buffer, sizeof(buffer), &read)) return Failed(response);
            if (body.size() + read > MaxBody) { response.error = ERROR_INSUFFICIENT_BUFFER; return response; }
            body.append(buffer, read);
        }
        // A status only with the whole answer: a partial one counts as no answer.
        response.status = static_cast<int>(status);
        response.retryAfter = std::move(retryAfter);
        response.json = json;
        response.body = std::move(body);
    } catch (...) {
        response.status = 0; response.json = false; response.body.clear(); response.error = ERROR_OUTOFMEMORY;
    }
    return response;
}
}
