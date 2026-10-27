#include "core/relogin-guard.hpp"
#include "core/module.hpp"
#include <string>

namespace reloginGuard {
namespace {
using namespace managed;
// pw.phz(cvg): the login screen's handler of the launcher's account-details reply.
constexpr uintptr_t HandlerRva = 0x19F22F0;
// cvf..ctor: the shared empty constructor (no field initializer).
constexpr uintptr_t DetailsCtorRva = 0x65AD00;
// cvg: dhzm the launcher's error code, dhzn the account details parsed from its answer.
constexpr size_t ErrorCodeOffset = 0x18, DetailsOffset = 0x20;
using HandlerFn = void (*)(Obj, Obj, Method);
HandlerFn originalHandler = nullptr;
Klass detailsClass = nullptr;
Method detailsCtor = nullptr;
bool installed = false, reported = false;
void RequireField(Klass type, const char* name, size_t offset) {
    auto field = api.class_get_field_from_name(type, name);
    if (!field || api.field_get_offset(field) != offset)
        throw std::runtime_error(std::string("Relogin field layout: ") + name);
}
bool Enabled() { return installed && module && !module->stopped && module->settings.reloginGuard; }
void Handler(Obj self, Obj reply, Method method) {
    Repair(reply);
    originalHandler(self, reply, method);
}
}
bool Repair(Obj reply) {
    if (!reply || !Enabled()) return false;
    try {
        auto details = reinterpret_cast<void**>(static_cast<char*>(reply) + DetailsOffset);
        if (*details) return false;
        Ref empty(api.object_new(detailsClass));
        if (!empty.get()) throw std::runtime_error("Empty account details allocation failed");
        api.Invoke(detailsCtor, empty.get());
        api.gc_wbarrier_set_field(reply, details, empty.get());
        // The launcher's error code only: no account detail, token or name.
        Log("RELOGIN_GUARD launcher_reply_without_details errorCode=" + std::to_string(Read<int>(reply, ErrorCodeOffset)) +
            " handed_to=game_warning_then_token_request");
        return true;
    } catch (const std::exception& error) {
        if (!reported) Log(std::string("RELOGIN_GUARD_ERROR ") + error.what());
        reported = true;
        return false;
    }
}
}

void InstallReloginGuard() {
    using namespace reloginGuard;
    try {
        auto handlerClass = api.Class("Core.dll", "", "pw");
        auto replyClass = api.Class("Core.dll", "", "cvg");
        detailsClass = api.Class("Core.dll", "", "cvf");
        RequireField(replyClass, "dhzm", ErrorCodeOffset);
        RequireField(replyClass, "dhzn", DetailsOffset);
        detailsCtor = api.FindVerified(detailsClass, ".ctor", 0, DetailsCtorRva);
        api.NativeAt(DetailsCtorRva);
        AddHook(api.FindVerified(handlerClass, "phz", 1, HandlerRva), HandlerRva,
            reinterpret_cast<void*>(&Handler), reinterpret_cast<void**>(&originalHandler));
        installed = true;
        Log("RELOGIN_GUARD_READY");
    } catch (const std::exception& error) {
        Log(std::string("RELOGIN_GUARD_UNAVAILABLE ") + error.what());
    }
}
