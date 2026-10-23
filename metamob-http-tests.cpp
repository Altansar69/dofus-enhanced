// MetamobHttpTests (offline): the real metamob-http.cpp with every WinHTTP call
// renamed to a scripted fake. No socket is opened and no real key exists. It
// pins the rules that keep a write from counting twice (sent from the moment
// WinHttpSendRequest succeeded, a status only with the whole answer) and the
// guards on what may reach the request line and the headers.
#include <windows.h>
#include <winhttp.h> // read here first: its include guard makes the copy in metamob-http.cpp a no-op
#include <atomic>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace fake {
enum class Step { None, Open, Timeouts, Connect, OpenRequest, Option, Send, Receive, Status, Read, ThrowBeforeSend, ThrowAfterSend };
struct Script {
    Step fail = Step::None;
    DWORD error = ERROR_WINHTTP_TIMEOUT;
    DWORD status = 200;
    std::map<DWORD, std::wstring> headers;  // WINHTTP_QUERY_* -> value
    std::vector<std::string> chunks;        // each at most 16 KiB
    size_t failRead = 0;                    // Step::Read: this read fails
    bool cancelWhileReading = false;        // the call's flag is set during read number cancelRead
    size_t cancelRead = 0;
};
struct Seen {
    std::wstring agent, host, verb, object, headers;
    DWORD access = 0, flags = 0, disabled = 0, total = 0;
    INTERNET_PORT port = 0;
    int timeouts[4]{};
    std::string body;
    int opened = 0, closed = 0, sends = 0;
};
Script script;
Seen seen;
size_t reads = 0;
std::atomic_bool* cancel = nullptr;
void Reset(Script next = {}) { script = std::move(next); seen = {}; reads = 0; cancel = nullptr; }
bool Fails(Step step) {
    if (script.fail != step) return false;
    SetLastError(script.error);
    return true;
}
HINTERNET Handle() { return reinterpret_cast<HINTERNET>(static_cast<uintptr_t>(0x1000 + ++seen.opened)); }
}
HINTERNET WINAPI FakeOpen(LPCWSTR agent, DWORD access, LPCWSTR, LPCWSTR, DWORD) {
    fake::seen.agent = agent; fake::seen.access = access;
    return fake::Fails(fake::Step::Open) ? nullptr : fake::Handle();
}
BOOL WINAPI FakeSetTimeouts(HINTERNET, int resolve, int connect, int send, int receive) {
    fake::seen.timeouts[0] = resolve; fake::seen.timeouts[1] = connect; fake::seen.timeouts[2] = send; fake::seen.timeouts[3] = receive;
    return !fake::Fails(fake::Step::Timeouts);
}
HINTERNET WINAPI FakeConnect(HINTERNET, LPCWSTR host, INTERNET_PORT port, DWORD) {
    fake::seen.host = host; fake::seen.port = port;
    return fake::Fails(fake::Step::Connect) ? nullptr : fake::Handle();
}
HINTERNET WINAPI FakeOpenRequest(HINTERNET, LPCWSTR verb, LPCWSTR object, LPCWSTR, LPCWSTR, LPCWSTR*, DWORD flags) {
    fake::seen.verb = verb; fake::seen.object = object; fake::seen.flags = flags;
    return fake::Fails(fake::Step::OpenRequest) ? nullptr : fake::Handle();
}
BOOL WINAPI FakeSetOption(HINTERNET, DWORD option, LPVOID buffer, DWORD length) {
    if (fake::script.fail == fake::Step::ThrowBeforeSend) throw std::bad_alloc();
    if (option == WINHTTP_OPTION_DISABLE_FEATURE && length == sizeof(DWORD)) fake::seen.disabled = *static_cast<const DWORD*>(buffer);
    return !fake::Fails(fake::Step::Option);
}
BOOL WINAPI FakeSendRequest(HINTERNET, LPCWSTR headers, DWORD headersLength, LPVOID optional, DWORD optionalLength, DWORD total, DWORD_PTR) {
    ++fake::seen.sends;
    fake::seen.headers = headersLength == static_cast<DWORD>(-1L) ? std::wstring(headers) : std::wstring(headers, headersLength);
    fake::seen.body = optional ? std::string(static_cast<const char*>(optional), optionalLength) : std::string();
    fake::seen.total = total;
    return !fake::Fails(fake::Step::Send);
}
BOOL WINAPI FakeReceiveResponse(HINTERNET, LPVOID) { return !fake::Fails(fake::Step::Receive); }
BOOL WINAPI FakeQueryHeaders(HINTERNET, DWORD info, LPCWSTR, LPVOID buffer, LPDWORD length, LPDWORD) {
    if (info == (WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER)) {
        if (fake::Fails(fake::Step::Status)) return FALSE;
        if (*length < sizeof(DWORD)) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
        *static_cast<DWORD*>(buffer) = fake::script.status;
        *length = sizeof(DWORD);
        return TRUE;
    }
    const auto header = fake::script.headers.find(info);
    if (header == fake::script.headers.end()) { SetLastError(ERROR_WINHTTP_HEADER_NOT_FOUND); return FALSE; }
    const auto needed = static_cast<DWORD>((header->second.size() + 1) * sizeof(wchar_t));
    if (*length < needed) { *length = needed; SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
    std::memcpy(buffer, header->second.c_str(), needed);
    *length = needed - sizeof(wchar_t);
    return TRUE;
}
BOOL WINAPI FakeReadData(HINTERNET, LPVOID buffer, DWORD size, LPDWORD read) {
    const auto index = fake::reads++;
    if (fake::script.fail == fake::Step::ThrowAfterSend) throw std::bad_alloc();
    if (fake::script.cancelWhileReading && fake::cancel && index == fake::script.cancelRead) fake::cancel->store(true);
    if (fake::script.fail == fake::Step::Read && index == fake::script.failRead) { SetLastError(fake::script.error); return FALSE; }
    if (index >= fake::script.chunks.size()) { *read = 0; return TRUE; }
    const auto& chunk = fake::script.chunks[index];
    if (chunk.size() > size) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
    std::memcpy(buffer, chunk.data(), chunk.size());
    *read = static_cast<DWORD>(chunk.size());
    return TRUE;
}
BOOL WINAPI FakeCloseHandle(HINTERNET) { ++fake::seen.closed; return TRUE; }

#define WinHttpOpen FakeOpen
#define WinHttpSetTimeouts FakeSetTimeouts
#define WinHttpConnect FakeConnect
#define WinHttpOpenRequest FakeOpenRequest
#define WinHttpSetOption FakeSetOption
#define WinHttpSendRequest FakeSendRequest
#define WinHttpReceiveResponse FakeReceiveResponse
#define WinHttpQueryHeaders FakeQueryHeaders
#define WinHttpReadData FakeReadData
#define WinHttpCloseHandle FakeCloseHandle
#include "metamob-http.cpp"
#undef WinHttpOpen
#undef WinHttpSetTimeouts
#undef WinHttpConnect
#undef WinHttpOpenRequest
#undef WinHttpSetOption
#undef WinHttpSendRequest
#undef WinHttpReceiveResponse
#undef WinHttpQueryHeaders
#undef WinHttpReadData
#undef WinHttpCloseHandle

namespace {
int failures = 0;
void Check(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << "\n"; ++failures; }
}
constexpr char Key[] = "TEST-KEY-0123456789-NOT-REAL";
const std::wstring WideKey = L"TEST-KEY-0123456789-NOT-REAL";
metamob::Response Send(const metamob::Request& request, const char* key = Key, bool cancelled = false) {
    const metamob::Secret secret{std::string(key)};
    std::atomic_bool flag{cancelled};
    fake::cancel = &flag;
    auto response = metamob::SendWinHttp(request, secret, flag);
    fake::cancel = nullptr;
    return response;
}
metamob::Request Get(std::string path) { return {"GET", std::move(path), {}}; }
metamob::Request Patch() { return {"PATCH", "/quests/a1b2c3d4/monsters/123", R"({"quantity":3})"}; }
fake::Script Answer(DWORD status, std::vector<std::string> chunks, std::wstring type = L"application/json; charset=utf-8") {
    fake::Script script;
    script.status = status;
    script.chunks = std::move(chunks);
    if (!type.empty()) script.headers[WINHTTP_QUERY_CONTENT_TYPE] = std::move(type);
    return script;
}
std::vector<std::string> Blocks(size_t count, size_t size) { return std::vector<std::string>(count, std::string(size, 'x')); }
bool Closed() { return fake::seen.closed == fake::seen.opened; }
// A failure before or at WinHttpSendRequest: nothing reached Metamob.
void NotSent(fake::Step step, DWORD error, const char* message) {
    auto script = Answer(200, {"{}"});
    script.fail = step; script.error = error;
    fake::Reset(std::move(script));
    const auto response = Send(Patch());
    Check(!response.sent && response.status == 0 && response.error == error && Closed(), message);
}
// A failure after WinHttpSendRequest succeeded: the write may have been applied.
void SentUnknown(fake::Script script, DWORD error, const char* message) {
    fake::Reset(std::move(script));
    const auto response = Send(Patch());
    Check(response.sent && response.status == 0 && response.error == error && !response.json && response.body.empty() &&
        response.retryAfter.empty() && fake::seen.sends == 1 && Closed(), message);
}
void Refused(const metamob::Request& request, const char* key, const char* message) {
    fake::Reset(Answer(200, {"{}"}));
    const auto answer = Send(request, key);
    Check(!answer.sent && answer.status == 0 && answer.error == ERROR_INVALID_PARAMETER && fake::seen.opened == 0, message);
}
}

int main() {
    // A GET: the exact endpoint, TLS, no redirect/cookie/automatic authentication, the timeouts, the headers.
    fake::Reset(Answer(200, {R"({"data":)", R"({"username":"x"}})"}));
    auto response = Send(Get("/me"));
    Check(response.status == 200 && response.sent && response.json && response.error == 0, "GET answered");
    Check(response.body == R"({"data":{"username":"x"}})", "The whole body is kept");
    Check(fake::seen.agent.rfind(L"DofusEnhanced/", 0) == 0 && fake::seen.access == WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, "Session");
    Check(fake::seen.timeouts[0] == 5000 && fake::seen.timeouts[1] == 5000 && fake::seen.timeouts[2] == 10000 && fake::seen.timeouts[3] == 15000,
        "Timeouts 5/5/10/15 s");
    Check(fake::seen.host == L"www.metamob.fr" && fake::seen.port == INTERNET_DEFAULT_HTTPS_PORT, "The www host on 443 (no 308 to follow)");
    Check(fake::seen.verb == L"GET" && fake::seen.object == L"/api/v1/me" && fake::seen.flags == WINHTTP_FLAG_SECURE, "GET over TLS");
    Check(fake::seen.disabled == (WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_AUTHENTICATION),
        "No redirect, cookie or automatic authentication");
    Check(fake::seen.headers == L"Authorization: Bearer " + WideKey + L"\r\nAccept: application/json\r\n", "GET headers");
    Check(fake::seen.body.empty() && fake::seen.total == 0 && Closed(), "GET without body, handles closed");

    // A PATCH: its JSON body and Content-Type.
    fake::Reset(Answer(200, {R"({"data":{"monster_id":123,"quantity":3}})"}));
    response = Send(Patch());
    Check(response.status == 200 && response.sent && response.json, "PATCH answered");
    Check(fake::seen.verb == L"PATCH" && fake::seen.object == L"/api/v1/quests/a1b2c3d4/monsters/123", "PATCH line");
    Check(fake::seen.headers == L"Authorization: Bearer " + WideKey + L"\r\nAccept: application/json\r\nContent-Type: application/json\r\n",
        "PATCH headers");
    Check(fake::seen.body == R"({"quantity":3})" && fake::seen.total == fake::seen.body.size(), "PATCH body sent whole");

    // Not sent: every failure up to and including WinHttpSendRequest (the sync reads afresh, then writes).
    NotSent(fake::Step::Open, ERROR_NOT_ENOUGH_MEMORY, "WinHttpOpen failed: not sent");
    NotSent(fake::Step::Timeouts, ERROR_INVALID_PARAMETER, "WinHttpSetTimeouts failed: not sent");
    NotSent(fake::Step::Connect, ERROR_WINHTTP_INVALID_URL, "WinHttpConnect failed: not sent");
    NotSent(fake::Step::OpenRequest, ERROR_WINHTTP_UNRECOGNIZED_SCHEME, "WinHttpOpenRequest failed: not sent");
    NotSent(fake::Step::Option, ERROR_WINHTTP_INCORRECT_HANDLE_TYPE, "WinHttpSetOption failed: not sent");
    NotSent(fake::Step::Send, ERROR_WINHTTP_CANNOT_CONNECT, "Connection refused: not sent");
    NotSent(fake::Step::Send, ERROR_WINHTTP_NAME_NOT_RESOLVED, "Name not resolved: not sent");
    fake::Reset([] { auto s = Answer(200, {"{}"}); s.fail = fake::Step::ThrowBeforeSend; return s; }());
    response = Send(Patch());
    Check(!response.sent && response.status == 0 && response.error == ERROR_OUTOFMEMORY && fake::seen.sends == 0 && Closed(),
        "An exception before the send: not sent");

    // Sent, outcome unknown: every failure after WinHttpSendRequest succeeded (kept in flight, never +n again).
    auto script = Answer(200, {"{}"});
    script.fail = fake::Step::Receive; script.error = ERROR_WINHTTP_TIMEOUT;
    SentUnknown(script, ERROR_WINHTTP_TIMEOUT, "Receive timeout after a delivered PATCH: sent, no status");
    script.fail = fake::Step::Receive; script.error = ERROR_WINHTTP_CONNECTION_ERROR;
    SentUnknown(script, ERROR_WINHTTP_CONNECTION_ERROR, "Connection reset before the answer: sent, no status");
    script.fail = fake::Step::Status; script.error = ERROR_WINHTTP_INVALID_SERVER_RESPONSE;
    SentUnknown(script, ERROR_WINHTTP_INVALID_SERVER_RESPONSE, "Unreadable status: sent, no status");
    script = Answer(200, {R"({"data":)", "{}}"});
    script.headers[WINHTTP_QUERY_RETRY_AFTER] = L"5";
    script.fail = fake::Step::Read; script.failRead = 1; script.error = ERROR_WINHTTP_CONNECTION_ERROR;
    SentUnknown(script, ERROR_WINHTTP_CONNECTION_ERROR, "A partial answer counts as no answer");
    fake::Reset(Answer(200, Blocks(64, 16384), L"application/json"));
    response = Send(Patch());
    Check(response.status == 200 && response.sent && response.body.size() == 1024 * 1024, "Exactly 1 MiB is accepted");
    auto big = Blocks(64, 16384);
    big.push_back("x");
    SentUnknown(Answer(200, big), ERROR_INSUFFICIENT_BUFFER, "Over 1 MiB: sent, no status");
    script = Answer(200, {"{", "}"});
    script.fail = fake::Step::ThrowAfterSend;
    SentUnknown(script, ERROR_OUTOFMEMORY, "An exception after the send keeps sent");

    // Cancelled: before the send nothing goes out; while reading the answer the outcome is unknown.
    fake::Reset(Answer(200, {"{}"}));
    response = Send(Patch(), Key, true);
    Check(!response.sent && response.error == ERROR_CANCELLED && fake::seen.opened == 0 && fake::seen.sends == 0, "Cancelled before anything");
    script = Answer(200, {"{", "}", "x"});
    script.cancelWhileReading = true; script.cancelRead = 0;
    fake::Reset(std::move(script));
    response = Send(Patch());
    Check(response.sent && response.status == 0 && response.error == ERROR_CANCELLED && response.body.empty() && Closed(),
        "Cancelled while reading: sent, no status");

    // Retry-After and Content-Type: printable ASCII only; JSON only as application/json.
    auto limited = Answer(429, {R"({"message":"slow"})"});
    limited.headers[WINHTTP_QUERY_RETRY_AFTER] = L"120";
    fake::Reset(std::move(limited));
    response = Send(Get("/quests"));
    Check(response.status == 429 && response.retryAfter == "120" && response.json, "Retry-After as seconds");
    limited = Answer(429, {"{}"});
    limited.headers[WINHTTP_QUERY_RETRY_AFTER] = L"Wed, 21 Oct 2026 07:28:00 GMT";
    fake::Reset(std::move(limited));
    Check(Send(Get("/quests")).retryAfter == "Wed, 21 Oct 2026 07:28:00 GMT", "Retry-After as a date");
    for (const std::wstring& bad : {std::wstring(L"12\x0001"), std::wstring(L"\x00e9t\x00e9"), std::wstring(300, L'9'), std::wstring(L"1\r\n2")}) {
        limited = Answer(429, {"{}"});
        limited.headers[WINHTTP_QUERY_RETRY_AFTER] = bad;
        fake::Reset(std::move(limited));
        Check(Send(Get("/quests")).retryAfter.empty(), "An unusual Retry-After is dropped");
    }
    fake::Reset(Answer(200, {"<html>Just a moment...</html>"}, L"text/html; charset=UTF-8"));
    response = Send(Get("/quests"));
    Check(response.status == 200 && !response.json, "A Cloudflare page is not JSON");
    fake::Reset(Answer(200, {"{}"}, L""));
    Check(!Send(Get("/quests")).json, "No Content-Type: not JSON");
    for (const char* type : {"application/json", "Application/JSON; charset=utf-8", " application/json ", "application/json;charset=UTF-8"})
        Check(metamob::JsonType(type), "JSON type accepted");
    for (const char* type : {"", "text/html", "application/json-patch+json", "application/jsonx", "text/json", "application/problem+json"})
        Check(!metamob::JsonType(type), "Other type refused");

    // Nothing unusual reaches WinHTTP: method, path, body, key.
    Refused({"DELETE", "/me", {}}, Key, "Method refused");
    Refused({"POST", "/quests", "{}"}, Key, "POST refused");
    Refused({"GET", "/me", "{}"}, Key, "GET with a body refused");
    Refused({"PATCH", "/quests/a/monsters/1", std::string(4097, ' ')}, Key, "Oversized body refused");
    for (const char* path : {"", "me", "/me ", "/me\r\nX-Injected: 1", "/a%20b", "/a/../me", "/a#b", "/a.b", "/a:b", "/a@b", "/\xC3\xA9"})
        Refused(Get(path), Key, "Path refused");
    Refused(Get("/" + std::string(512, 'a')), Key, "Path over 512 refused");
    Check(metamob::PlainPath("/" + std::string(511, 'a')) && !metamob::PlainPath("/" + std::string(512, 'a')), "Path bound");
    Check(metamob::PlainPath("/quests/abc_DEF-1/monsters?step=4&limit=200&offset=200"), "Every built path accepted");
    Check(metamob::PlainPath("/kralove?server=11&from=2026-10-15"), "The calendar's Kralamoure path accepted");
    for (const char* key : {"short-key", "TEST KEY 0123456789 NOT REAL", "TEST-KEY-0123456789\r\nX: 1", "TEST-KEY-0123456789-\x7F"})
        Refused(Get("/me"), key, "Unusable key refused");

    if (failures) { std::cerr << failures << " failure(s)\n"; return 1; }
    std::cout << "MetamobHttpTests OK\n";
    return 0;
}
