#pragma once
#include <windows.h>
#include <filesystem>
#include <string>
#include <vector>

inline constexpr char ExpectedGameHash[] = "70b3861ded2ffa4a963bb6eaac8ff3c470c2f062e641601628c95f60411e8663";
inline constexpr char ExpectedMetadataHash[] = "ba1efbad5a8ecca48775fc20293f17e9dece72cc6134c4e7ecdd8071e7c67db6";
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
