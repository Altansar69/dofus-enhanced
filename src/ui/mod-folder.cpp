#include "ui/mod-folder.hpp"
#include "core/runtime.hpp"
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <atomic>
#include <string>
#include <system_error>
#include <thread>

namespace {
// A click while Explorer is still being started is ignored: never a pile of windows or threads.
std::atomic_bool opening{false};

void Open(const std::wstring& folder) {
    std::error_code error;
    const std::filesystem::path path(folder);
    if (!path.is_absolute() || !std::filesystem::is_directory(path, error)) {
        Log("MOD_FOLDER_OPEN_ERROR missing folder");
        return;
    }
    // ShellExecute may use COM: its own apartment on this thread.
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"explore", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    if (SUCCEEDED(com)) CoUninitialize();
    if (result <= 32) Log("MOD_FOLDER_OPEN_ERROR shell " + std::to_string(static_cast<long long>(result)));
}
}

void OpenModFolder(const std::filesystem::path& folder) {
    bool idle = false;
    if (!opening.compare_exchange_strong(idle, true)) return;
    try {
        std::thread([target = folder.wstring()] {
            try { Open(target); } catch (...) { Log("MOD_FOLDER_OPEN_ERROR"); }
            opening.store(false);
        }).detach();
    } catch (...) {
        opening.store(false);
        Log("MOD_FOLDER_OPEN_ERROR thread");
    }
}
