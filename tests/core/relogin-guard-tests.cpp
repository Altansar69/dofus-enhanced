// « Reconnexion après une déconnexion » (relogin-guard.cpp), offline: the
// launcher's account-details reply after an inactivity disconnection, built by
// the game without details (cvg.dhzn null), reaches a replica of the game's
// handler (pw.phz) that, like the native one, reads the details without a
// check. Unguarded it fails as the game does (the login stops before the token
// request); guarded it takes the game's own « details incomplete » branch and
// goes on with the token request.
#include "core/module.hpp"
#include "appearance/features.hpp"
#include "ui/options-ui.hpp"
#include <cstddef>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <vector>
#include "core/relogin-guard.cpp"

namespace fake {
using managed::Obj;
std::vector<std::string> logs;
// The game's objects: cvg (reply) holds its error code at 0x18 and its details
// at 0x20; cvf (details) holds dhze, the field the handler tests first, at 0x20.
struct Reply { char header[0x18] = {}; int errorCode = 0; int padding = 0; void* details = nullptr; };
struct Details { char header[0x20] = {}; void* dhze = nullptr; char rest[0x38] = {}; };
static_assert(offsetof(Reply, errorCode) == 0x18 && offsetof(Reply, details) == 0x20 && offsetof(Details, dhze) == 0x20);
std::vector<std::unique_ptr<Details>> allocated;
int ctorCalls = 0, barrierWrites = 0, warnings = 0, tokenRequests = 0, nullReferences = 0, hooks = 0;
bool failAllocation = false, wrongLayout = false;
reloginGuard::HandlerFn installedHook = nullptr;
int handlerClass, replyClass, detailsClass, handlerMethod, ctorMethod, errorField, detailsField, selfStorage, methodStorage;
std::map<managed::GCHandle, Obj> handles;
managed::GCHandle serial = 0;
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
// pw.phz as the snapshot has it (0x19F22F0): the details read without a null
// check, then dhze tested: without it a warning; in both branches the token
// request (eog.xbs) follows.
void Original(Obj self, Obj reply, managed::Method method) {
    Check(self == &selfStorage && method == &methodStorage, "The game's own arguments reach its handler");
    auto details = static_cast<Details*>(static_cast<Reply*>(reply)->details);
    if (!details) { ++nullReferences; throw std::runtime_error("NullReferenceException at pw.phz"); }
    if (!details->dhze) ++warnings;
    ++tokenRequests;
}
}
namespace managed {
Api api{};
uintptr_t gameBase = 0x180000000;
Klass Api::Class(const char* image, const char* space, const char* name) const {
    fake::Check(!std::strcmp(image, "Core.dll") && !*space, "The login types live in Core.dll's global namespace");
    if (!std::strcmp(name, "pw")) return &fake::handlerClass;
    if (!std::strcmp(name, "cvg")) return &fake::replyClass;
    if (!std::strcmp(name, "cvf")) return &fake::detailsClass;
    throw std::runtime_error("Unexpected login type");
}
Method Api::FindVerified(Klass klass, const char* name, int count, uintptr_t rva) const {
    if (klass == &fake::handlerClass) {
        fake::Check(!std::strcmp(name, "phz") && count == 1 && rva == 0x19F22F0, "Verify the account-details handler");
        return &fake::handlerMethod;
    }
    fake::Check(klass == &fake::detailsClass && !std::strcmp(name, ".ctor") && count == 0 && rva == 0x65AD00, "Verify the details' empty constructor");
    return &fake::ctorMethod;
}
void* Api::NativeAt(uintptr_t rva) const { fake::Check(rva == 0x65AD00, "Guard the details' constructor"); return reinterpret_cast<void*>(gameBase + rva); }
Obj Api::Invoke(Method method, Obj self, std::initializer_list<void*> args) const {
    fake::Check(method == &fake::ctorMethod && self && args.size() == 0, "Only the details' constructor is invoked");
    ++fake::ctorCalls;
    return nullptr;
}
}
Module* module = nullptr;
Module::Module(std::filesystem::path) {}
void Log(const std::string& text) { fake::logs.push_back(text); }
void AddHook(managed::Method method, uintptr_t rva, void* replacement, void** original) {
    fake::Check(method == &fake::handlerMethod && rva == 0x19F22F0, "Only the guarded handler is hooked");
    fake::installedHook = reinterpret_cast<reloginGuard::HandlerFn>(replacement);
    *original = reinterpret_cast<void*>(&fake::Original);
    ++fake::hooks;
}
int main() {
    using namespace fake;
    using managed::api;
    try {
        api.class_get_field_from_name = [](managed::Klass klass, const char* name) -> void* {
            Check(klass == &replyClass, "The reply's own fields are checked");
            if (!std::strcmp(name, "dhzm")) return &errorField;
            if (!std::strcmp(name, "dhzn")) return &detailsField;
            return nullptr;
        };
        api.field_get_offset = [](void* field) -> size_t {
            if (field == &errorField) return 0x18;
            return wrongLayout ? 0x28 : 0x20;
        };
        api.object_new = [](managed::Klass klass) -> Obj {
            Check(klass == &detailsClass, "Only empty details are allocated");
            if (failAllocation) return nullptr;
            allocated.push_back(std::make_unique<Details>());
            return allocated.back().get();
        };
        api.gc_wbarrier_set_field = [](Obj owner, void** field, Obj value) {
            Check(field == &static_cast<Reply*>(owner)->details, "The details are written through the GC barrier, in their own field");
            *field = value; ++barrierWrites;
        };
        api.gchandle_new = [](Obj object, bool) { handles[++serial] = object; return serial; };
        api.gchandle_get_target = [](managed::GCHandle handle) { return handles.at(handle); };
        api.gchandle_free = [](managed::GCHandle handle) { Check(handles.erase(handle) == 1, "Invalid GC handle"); };
        Module owner{"offline-only"}; module = &owner;

        // A client whose reply layout differs: no hook, the game's login as it is.
        wrongLayout = true; InstallReloginGuard(); wrongLayout = false;
        Check(!hooks && !reloginGuard::installed && logs.back().rfind("RELOGIN_GUARD_UNAVAILABLE ", 0) == 0, "A different layout leaves the login untouched");
        InstallReloginGuard();
        Check(hooks == 1 && installedHook && logs.back() == "RELOGIN_GUARD_READY", "The guarded handler is hooked once");
        Check(owner.settings.reloginGuard, "The guard is on by default");

        // The inactivity case: the launcher answered with an error, no details.
        Reply failed; failed.errorCode = 7;
        installedHook(&selfStorage, &failed, &methodStorage);
        auto given = static_cast<Details*>(failed.details);
        Check(given && !given->dhze && ctorCalls == 1 && barrierWrites == 1 && !nullReferences, "Empty details, built by their own constructor, reach the handler");
        Check(warnings == 1 && tokenRequests == 1, "The game's own incomplete-details branch runs, then its token request: the login goes on");
        Check(logs.back() == "RELOGIN_GUARD launcher_reply_without_details errorCode=7 handed_to=game_warning_then_token_request",
            "One line tells it, with the launcher's error code only");
        Check(handles.empty(), "No GC handle kept");

        // A reply with the account's details: untouched.
        Details real; int text = 0; real.dhze = &text; Reply succeeded; succeeded.details = &real;
        const auto logged = logs.size();
        installedHook(&selfStorage, &succeeded, &methodStorage);
        Check(succeeded.details == &real && ctorCalls == 1 && barrierWrites == 1 && warnings == 1 && tokenRequests == 2 && logs.size() == logged,
            "A reply with details reaches the game as it is, without a line");
        Check(!reloginGuard::Repair(nullptr), "No reply: nothing to do");

        // Unticked: the game's own behaviour, its error included.
        owner.settings.reloginGuard = false;
        Reply unguarded;
        bool thrown = false;
        try { installedHook(&selfStorage, &unguarded, &methodStorage); } catch (const std::runtime_error&) { thrown = true; }
        Check(thrown && nullReferences == 1 && !unguarded.details && tokenRequests == 2 && logs.size() == logged,
            "Unticked, the reply reaches the game unchanged: the login stops as without the mod");
        owner.settings.reloginGuard = true;

        // A failed allocation: told once, the game's own handling as it is.
        failAllocation = true;
        Reply noMemory; thrown = false;
        try { installedHook(&selfStorage, &noMemory, &methodStorage); } catch (const std::runtime_error&) { thrown = true; }
        try { installedHook(&selfStorage, &noMemory, &methodStorage); } catch (const std::runtime_error&) {}
        Check(thrown && nullReferences == 3 && logs.size() == logged + 1 && logs.back().rfind("RELOGIN_GUARD_ERROR ", 0) == 0,
            "A failure is told once and leaves the game's own handling");
        failAllocation = false;

        // A stopped module touches nothing.
        owner.stopped = true;
        Reply afterStop;
        Check(!reloginGuard::Repair(&afterStop) && !afterStop.details, "A stopped module leaves the reply as it is");
        module = nullptr;
        std::cout << "PASS: relogin guard: a launcher reply without details takes the game's own incomplete-details branch and its token request; details, option off, failures and stop untouched\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        for (const auto& line : fake::logs) std::cerr << line << '\n';
        return 1;
    }
}
