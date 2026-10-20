#include "treasure-launch.hpp"
#include "common.hpp"
#include "runtime.hpp"

void OpenTreasureOverlay(const std::filesystem::path& moduleFolder) {
    const auto executable = std::filesystem::absolute(moduleFolder / L"TreasureOverlay.exe");
    std::error_code error;
    if (!std::filesystem::is_regular_file(executable, error) || error) {
        Log("TREASURE_OVERLAY_ERROR helper missing");
        MessageBoxW(nullptr,
            L"TreasureOverlay.exe est absent du dossier du mod.\n"
            L"Extrayez l'archive complète de cette révision, puis réessayez.",
            L"Chasse au trésor — DofusDB", MB_OK | MB_ICONERROR);
        return;
    }
    std::wstring command = L"\"" + executable.native() + L"\"";
    STARTUPINFOW startup{sizeof(startup)};
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_SHOWNORMAL;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, 0,
        nullptr, moduleFolder.c_str(), &startup, &process)) {
        const auto code = GetLastError();
        Log("TREASURE_OVERLAY_ERROR CreateProcess=" + std::to_string(code));
        const auto message = L"Impossible d'ouvrir la fenêtre DofusDB.\nErreur Windows : " +
            std::to_wstring(code) + L".";
        MessageBoxW(nullptr, message.c_str(), L"Chasse au trésor — DofusDB", MB_OK | MB_ICONERROR);
        return;
    }
    Handle processHandle(process.hProcess), threadHandle(process.hThread);
    Log("TREASURE_OVERLAY_OPEN requested");
}
