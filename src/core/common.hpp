#pragma once
#include <windows.h>
#include <filesystem>
#include <string>
#include <vector>

inline constexpr char ExpectedGameHash[] = "af866f65f0d08eebe9008ffda54462e2814d994c81059fffd766f0d685503ab3";
inline constexpr char ExpectedMetadataHash[] = "781890cf890114c5231b5bf04595acbceaf8317f7998fd8053c017de1ae23206";
std::string FileHash(const std::filesystem::path& path);
std::filesystem::path ModulePath(HMODULE module);
bool SupportedClient(const std::filesystem::path& exe);
std::filesystem::path StartupStatusPath(const std::filesystem::path& folder, DWORD pid);
void WriteStartupStatus(const std::filesystem::path& folder, DWORD pid, const std::string& status);
std::string ReadStartupStatus(const std::filesystem::path& folder, DWORD pid);
struct Handle {
    HANDLE value = nullptr;
    explicit Handle(HANDLE h = nullptr) : value(h) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    explicit operator bool() const { return value && value != INVALID_HANDLE_VALUE; }
};
