#pragma once
#include "metamob/metamob-secret.hpp"
#include <atomic>
#include <functional>
#include <string>
// HTTPS requests to the Metamob API. The capture sync sees only this
// interface, so its tests replace WinHTTP by a scripted transport.
namespace metamob {
struct Request {
    std::string method;     // "GET" or "PATCH"
    std::string path;       // below /api/v1, e.g. "/me" or "/quests/abc/monsters?step=4&limit=200"
    std::string body;       // JSON for PATCH, empty for GET
};
struct Response {
    int status = 0;             // 0: no HTTP answer (see error)
    unsigned long error = 0;    // WinHTTP/Windows error code when status == 0
    bool sent = false;          // the request reached the server (an unknown outcome for a PATCH)
    std::string retryAfter;     // raw Retry-After header, empty when absent
    bool json = false;          // Content-Type application/json
    std::string body;           // at most 1 MiB
};
// One request to https://www.metamob.fr/api/v1<path> with "Authorization:
// Bearer <key>". Redirects, cookies and automatic authentication disabled;
// never logs, never throws text built from the key, the headers or the body.
using Transport = std::function<Response(const Request&, const Secret&, const std::atomic_bool& cancelled)>;
Response SendWinHttp(const Request& request, const Secret& key, const std::atomic_bool& cancelled);
}
