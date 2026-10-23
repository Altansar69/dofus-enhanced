#include "core/common.hpp"
#include <bcrypt.h>
#include <array>
#include <stdexcept>
#include <fstream>

std::filesystem::path ModulePath(HMODULE module) {
    std::vector<wchar_t> name(32768);
    auto size = GetModuleFileNameW(module, name.data(), static_cast<DWORD>(name.size()));
    if (!size || size >= name.size()) throw std::runtime_error("Cannot resolve module path");
    return std::wstring(name.data(), size);
}
std::string FileHash(const std::filesystem::path& path) {
    Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file) throw std::runtime_error("Cannot read fingerprint input");
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    auto check = [](NTSTATUS status) { if (status < 0) throw std::runtime_error("SHA256 error"); };
    try {
        check(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0));
        check(BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0));
        std::array<UCHAR, 65536> buffer{};
        DWORD count = 0;
        for (;;) {
            if (!ReadFile(file.value, buffer.data(), static_cast<DWORD>(buffer.size()), &count, nullptr))
                throw std::runtime_error("Fingerprint read failed");
            if (!count) break;
            check(BCryptHashData(hash, buffer.data(), count, 0));
        }
        std::array<UCHAR, 32> result{};
        check(BCryptFinishHash(hash, result.data(), static_cast<ULONG>(result.size()), 0));
        BCryptDestroyHash(hash); hash = nullptr;
        BCryptCloseAlgorithmProvider(algorithm, 0); algorithm = nullptr;
        constexpr char digits[] = "0123456789abcdef";
        std::string text;
        for (auto byte : result) { text += digits[byte >> 4]; text += digits[byte & 15]; }
        return text;
    } catch (...) {
        if (hash) BCryptDestroyHash(hash);
        if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
        throw;
    }
}
bool SupportedClient(const std::filesystem::path& exe) {
    return _wcsicmp(exe.filename().c_str(), L"Dofus.exe") == 0 &&
        FileHash(exe.parent_path() / L"GameAssembly.dll") == ExpectedGameHash &&
        FileHash(exe.parent_path() / L"Dofus_Data/il2cpp_data/Metadata/global-metadata.dat") == ExpectedMetadataHash;
}
std::filesystem::path StartupStatusPath(const std::filesystem::path& folder, DWORD pid) {
    return folder / (L"startup-" + std::to_wstring(pid) + L".status");
}
void WriteStartupStatus(const std::filesystem::path& folder, DWORD pid, const std::string& status) {
    auto destination = StartupStatusPath(folder, pid);
    auto temporary = destination; temporary += L".tmp";
    {
        std::ofstream file(temporary, std::ios::trunc);
        file << status << '\n';
        file.flush();
        if (!file) throw std::runtime_error("Cannot write startup status");
    }
    if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot publish startup status");
}
std::string ReadStartupStatus(const std::filesystem::path& folder, DWORD pid) {
    Handle file(CreateFileW(StartupStatusPath(folder, pid).c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file) return {};
    std::array<char, 256> bytes{};
    DWORD length = 0;
    if (!ReadFile(file.value, bytes.data(), static_cast<DWORD>(bytes.size()), &length, nullptr)) return {};
    std::string status(bytes.data(), length);
    auto end = status.find_first_of("\r\n");
    return end == std::string::npos ? status : status.substr(0, end);
}
