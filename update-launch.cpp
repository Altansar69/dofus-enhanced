#include "update-launch.hpp"
#include "autoload.hpp"
#include "common.hpp"
#include "settings.hpp"
#include <stdexcept>

namespace {
std::wstring Quote(const std::wstring& value) {
    if (value.find(L'"') != std::wstring::npos || value.find_first_of(L"\r\n") != std::wstring::npos)
        throw std::runtime_error("Invalid helper argument");
    // All filesystem arguments name files; no trailing backslash ambiguity.
    return L"\"" + value + L"\"";
}
DWORD Child(const std::filesystem::path& executable, std::wstring arguments,
    const std::filesystem::path& working, const std::filesystem::path& logFile,
    DWORD timeout, bool killOnTimeout, const std::function<bool()>& enabled) {
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    Handle output(CreateFileW(logFile.c_str(), FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    Handle input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        &security, OPEN_EXISTING, 0, nullptr));
    if (!output || !input) throw std::runtime_error("Cannot open helper log");
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input.value;
    startup.hStdOutput = startup.hStdError = output.value;
    PROCESS_INFORMATION process{};
    auto command = Quote(executable.wstring()) + L" " + arguments;
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
        nullptr, working.c_str(), &startup, &process))
        throw std::runtime_error("Cannot start verified helper");
    Handle running(process.hProcess), thread(process.hThread);
    const auto start = GetTickCount64();
    for (;;) {
        if (WaitForSingleObject(running.value, 100) == WAIT_OBJECT_0) {
            DWORD result = 1;
            if (!GetExitCodeProcess(running.value, &result)) throw std::runtime_error("Cannot read helper result");
            return result;
        }
        if (GetTickCount64() - start >= timeout || (enabled && !enabled())) {
            // Only the network worker may be terminated. A loader may already
            // be waiting for native initialization, so it keeps its own guard.
            if (killOnTimeout) {
                TerminateProcess(running.value, ERROR_TIMEOUT);
                WaitForSingleObject(running.value, 3000);
            }
            throw std::runtime_error("Helper timeout or automatic loading disabled; no retry");
        }
    }
}
}

void CheckGithubUpdates(const std::filesystem::path& folder, const std::filesystem::path& client,
    std::ostream& log, const std::function<bool()>& enabled) {
    try {
        const auto settings = ReleaseSettingsPath(folder);
        if (!Settings::Read(settings).githubUpdates) { log << "UPDATE_DISABLED\n"; log.flush(); return; }
        const auto verified = LatestLocalRelease(folder);
        if (verified.revision < 75) throw std::runtime_error("Updater integrity manifest unavailable");
        wchar_t system[MAX_PATH]{};
        if (!GetSystemDirectoryW(system, MAX_PATH)) throw std::runtime_error("Cannot locate Windows PowerShell");
        const auto powershell = std::filesystem::path(system) / L"WindowsPowerShell/v1.0/powershell.exe";
        log << "UPDATE_CHECK GitHub Altansar69/dofus-miniatures\n"; log.flush();
        const auto active = [&] { return (!enabled || enabled()) && Settings::Read(settings).githubUpdates; };
        if (!active()) return;
        const auto result = Child(powershell, L"-NoProfile -NonInteractive -ExecutionPolicy Bypass -File " +
            Quote((verified.folder / L"Update-GitHub.ps1").wstring()) + L" -InstallRoot " +
            Quote(folder.parent_path().wstring()) + L" -ClientExe " + Quote(client.wstring()),
            verified.folder, folder / L"autoload.log", 45000, true, active);
        log << (result == 0 ? "UPDATE_CHECK_FINISHED\n" : "UPDATE_UNAVAILABLE using verified local release\n");
    } catch (const std::exception& error) {
        log << "UPDATE_UNAVAILABLE " << error.what() << "; using verified local release\n";
    }
    log.flush();
}

DWORD DispatchVerifiedLoader(const std::filesystem::path& folder, DWORD pid, uint64_t creation,
    const std::filesystem::path& client, std::ostream& log) {
    if (!VerifiedReleaseForClient(folder, client))
        throw std::runtime_error("Selected release changed; loader not started");
    log.flush();
    // The selected release's loader rechecks its own pinned native snapshot.
    // The stable watcher never injects using another version's addresses.
    return Child(folder / L"ModuleLoader.exe", L"--auto-load " + std::to_wstring(pid) + L" " +
        std::to_wstring(creation) + L" " + Quote(client.wstring()), folder,
        folder / L"autoload.log", 240000, false, {});
}
