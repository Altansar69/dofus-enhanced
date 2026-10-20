#pragma once
#include <filesystem>
#include <cstdint>
#include <functional>
#include <ostream>
#include <windows.h>

void CheckGithubUpdates(const std::filesystem::path& folder, const std::filesystem::path& client,
    std::ostream& log, const std::function<bool()>& enabled = {});
DWORD DispatchVerifiedLoader(const std::filesystem::path& folder, DWORD pid, uint64_t creation,
    const std::filesystem::path& client, std::ostream& log);
