#pragma once
#include <filesystem>
#include <optional>
#include <string>
// The user's personal Metamob API key (CGU 5.3: personal, never shared or
// shipped). It lives only in memory as a Secret and on disk encrypted with
// DPAPI for the current Windows user, next to the shared settings file. It is
// never written to settings.ini, never logged and never put in an exception.
namespace metamob {
class Secret {
public:
    Secret() = default;
    explicit Secret(std::string value);
    Secret(const Secret&) = delete;
    Secret& operator=(const Secret&) = delete;
    Secret(Secret&& other) noexcept;
    Secret& operator=(Secret&& other) noexcept;
    ~Secret();                      // wipes the bytes (SecureZeroMemory)
    const std::string& value() const { return text; }
    bool empty() const { return text.empty(); }
private:
    std::string text;
};
// DofusMiniatures-metamob.key next to the active settings file.
std::filesystem::path KeyPath(const std::filesystem::path& settingsPath);
// CryptProtectData (current user, no UI, fixed entropy), magic header, atomic
// replace through a temporary file; refuses reparse points and invalid keys.
// Errors are fixed French messages.
void StoreKey(const std::filesystem::path& file, const Secret& key);
// nullopt when no key file exists; throws a fixed message when the file is
// unreadable (other Windows account or PC, corrupt, oversized, reparse point).
std::optional<Secret> LoadKey(const std::filesystem::path& file);
bool HasKey(const std::filesystem::path& file);
void ForgetKey(const std::filesystem::path& file);
}
