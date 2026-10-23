#include "autoload.hpp"
#include "common.hpp"
#include "release-policy.hpp"
#include <cstdint>
#include <cwctype>
#include <fstream>
#include <optional>
#include <stdexcept>

namespace {
constexpr wchar_t RunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t RunValue[] = L"DofusMiniatures";
struct RegistryKey {
    HKEY value = nullptr;
    ~RegistryKey() { if (value) RegCloseKey(value); }
};
struct ConfigurationLock {
    Handle mutex{CreateMutexW(nullptr, FALSE, L"Local\\DofusMiniatures.AutoLoadConfiguration")};
    bool locked = false;
    ConfigurationLock() {
        if (!mutex) throw std::runtime_error("Cannot lock auto-load configuration");
        const auto result = WaitForSingleObject(mutex.value, 10000);
        locked = result == WAIT_OBJECT_0 || result == WAIT_ABANDONED;
        if (!locked) throw std::runtime_error("Auto-load configuration is busy");
    }
    ~ConfigurationLock() { if (locked) ReleaseMutex(mutex.value); }
};
std::wstring NormalPath(const std::filesystem::path& path) {
    return std::filesystem::absolute(path).lexically_normal().wstring();
}
bool Equal(const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) == 0; }
bool SameInstallation(const std::filesystem::path& a, const std::filesystem::path& b) {
    if (Equal(NormalPath(a), NormalPath(b))) return true;
    return LocalRelease::FolderRevision(a.filename().wstring()) && LocalRelease::FolderRevision(b.filename().wstring()) &&
        Equal(NormalPath(a.parent_path()), NormalPath(b.parent_path()));
}
std::filesystem::path InstallationIdentity(const std::filesystem::path& folder) {
    return LocalRelease::FolderRevision(folder.filename().wstring()) ?
        folder.parent_path() / L"DofusMiniatures-3.7.11.11" : folder;
}
bool PlainPath(const std::filesystem::path& path, bool directory) {
    const auto flags = GetFileAttributesW(path.c_str());
    return flags != INVALID_FILE_ATTRIBUTES && !(flags & FILE_ATTRIBUTE_REPARSE_POINT) &&
        static_cast<bool>(flags & FILE_ATTRIBUTE_DIRECTORY) == directory;
}
std::optional<LocalRelease::Candidate> VerifiedRelease(const std::filesystem::path& folder,
    const std::string& gameHash = ExpectedGameHash, const std::string& metadataHash = ExpectedMetadataHash) {
    try {
        if (!LocalRelease::FolderRevision(folder.filename().wstring()) || !PlainPath(folder, true) || !PlainPath(folder / L"release.ini", false)) return {};
        const auto name = folder.filename().string(); // verified ASCII family name
        const auto size = std::filesystem::file_size(folder / L"release.ini");
        if (!size || size > LocalRelease::ManifestLimit) return {};
        std::ifstream stream(folder / L"release.ini", std::ios::binary);
        std::string text(static_cast<size_t>(size), '\0');
        stream.read(text.data(), static_cast<std::streamsize>(text.size()));
        if (!stream || stream.peek() != std::char_traits<char>::eof()) return {};
        const auto manifest = LocalRelease::Parse(text);
        if (!manifest || manifest->gameHash != gameHash || manifest->metadataHash != metadataHash) return {};
        LocalRelease::Candidate candidate{name, *manifest, {}};
        for (size_t i = 0; i != LocalRelease::BinaryNames.size(); ++i) {
            const auto binary = folder / LocalRelease::BinaryNames[i];
            if (!PlainPath(binary, false) || std::filesystem::file_size(binary) > 256ULL * 1024 * 1024) return {};
            candidate.actualHashes[i] = FileHash(binary);
        }
        if (!LocalRelease::Compatible(candidate, gameHash, metadataHash)) return {};
        if (manifest->revision >= 75) {
            // Also protect the updater and the overlay, which are launched as
            // separate processes. No remotely supplied executable names.
            const auto integrity = folder / L"update-files.ini";
            if (!PlainPath(integrity, false) || std::filesystem::file_size(integrity) > 4096) return {};
            std::ifstream file(integrity);
            std::map<std::string, std::string> hashes;
            for (std::string line; std::getline(file, line);) {
                const auto split = line.find('=');
                if (split == std::string::npos || !hashes.emplace(line.substr(0, split), line.substr(split + 1)).second) return {};
            }
            if (hashes.size() != 5) return {};
            for (const auto entry : {"DofusMiniatures.dll", "ModuleLoader.exe", "ModuleAutoLoader.exe",
                                    "TreasureOverlay.exe", "Update-GitHub.ps1"}) {
                const auto item = folder / entry;
                if (!PlainPath(item, false) || std::filesystem::file_size(item) > 64ULL * 1024 * 1024 ||
                    !LocalRelease::Hash(hashes[entry]) || FileHash(item) != hashes[entry]) return {};
            }
        }
        return candidate;
    } catch (...) { return {}; } // incomplete/corrupt sibling never becomes a release
}
std::vector<std::filesystem::path> ReleaseFolders(const std::filesystem::path& folder) {
    const auto absolute = std::filesystem::path(NormalPath(folder));
    if (!LocalRelease::FolderRevision(absolute.filename().wstring()) || !PlainPath(absolute.parent_path(), true))
        throw std::runtime_error("Use a packaged DofusMiniatures-3.7.11.11-rN folder for automatic updates");
    std::vector<std::filesystem::path> result;
    size_t entries = 0;
    // One directory level, only exact family names. Never recurse or follow a
    // junction into another installation. Cap work even in a crowded parent.
    for (const auto& entry : std::filesystem::directory_iterator(absolute.parent_path())) {
        if (++entries > 4096) throw std::runtime_error("Too many sibling entries to resolve a local release");
        if (LocalRelease::FolderRevision(entry.path().filename().wstring()) && PlainPath(entry.path(), true)) {
            if (result.size() == 1024) throw std::runtime_error("Too many local releases");
            result.push_back(entry.path());
        }
    }
    return result;
}
std::optional<std::wstring> ReadRegistration() {
    RegistryKey key;
    const auto opened = RegOpenKeyExW(HKEY_CURRENT_USER, RunKey, 0, KEY_QUERY_VALUE, &key.value);
    if (opened == ERROR_FILE_NOT_FOUND) return std::nullopt;
    if (opened != ERROR_SUCCESS) throw std::runtime_error("Cannot read auto-load startup entry");
    DWORD type = 0, bytes = 0;
    auto result = RegQueryValueExW(key.value, RunValue, nullptr, &type, nullptr, &bytes);
    if (result == ERROR_FILE_NOT_FOUND) return std::nullopt;
    if (result != ERROR_SUCCESS || type != REG_SZ || bytes < sizeof(wchar_t) || bytes > 65536 || bytes % sizeof(wchar_t))
        throw std::runtime_error("Unexpected DofusMiniatures startup entry; left unchanged");
    std::vector<wchar_t> text(bytes / sizeof(wchar_t));
    result = RegQueryValueExW(key.value, RunValue, nullptr, &type, reinterpret_cast<BYTE*>(text.data()), &bytes);
    if (result != ERROR_SUCCESS || type != REG_SZ || text.back() != L'\0')
        throw std::runtime_error("Cannot read auto-load startup entry");
    std::wstring command(text.data(), text.size() - 1);
    if (command.find(L'\0') != std::wstring::npos) throw std::runtime_error("Invalid auto-load startup entry");
    return command;
}
void WriteRegistration(const std::optional<std::wstring>& command) {
    RegistryKey key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, RunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key.value, nullptr) != ERROR_SUCCESS)
        throw std::runtime_error("Cannot open auto-load startup entry for writing");
    if (command) {
        if (RegSetValueExW(key.value, RunValue, 0, REG_SZ, reinterpret_cast<const BYTE*>(command->c_str()),
                static_cast<DWORD>((command->size() + 1) * sizeof(wchar_t))) != ERROR_SUCCESS)
            throw std::runtime_error("Cannot save auto-load startup entry");
    } else {
        const auto result = RegDeleteValueW(key.value, RunValue);
        if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND)
            throw std::runtime_error("Cannot remove auto-load startup entry");
    }
}
bool ParseOwnedCommand(const std::wstring& command, std::filesystem::path& loader, std::filesystem::path* clientPath = nullptr) {
    const std::wstring separator = L"\" --watch \"";
    if (command.empty() || command.front() != L'\"' || command.back() != L'\"') return false;
    const auto split = command.find(separator, 1);
    if (split == std::wstring::npos) return false;
    loader = command.substr(1, split - 1);
    const auto client = command.substr(split + separator.size(), command.size() - split - separator.size() - 1);
    const bool valid = loader.is_absolute() && Equal(loader.filename().wstring(), L"ModuleAutoLoader.exe") &&
        loader.wstring().find(L'\"') == std::wstring::npos && client.find(L'\"') == std::wstring::npos &&
        std::filesystem::path(client).is_absolute() && Equal(std::filesystem::path(client).filename().wstring(), L"Dofus.exe");
    if (valid && clientPath) *clientPath = client;
    return valid;
}
bool OwnedForClient(const std::wstring& command, const std::filesystem::path& folder,
    const std::filesystem::path& clientExe, bool requireResolver) {
    std::filesystem::path loader, client;
    if (!ParseOwnedCommand(command, loader, &client) || !SameInstallation(loader.parent_path(), folder) ||
        !Equal(NormalPath(client), NormalPath(clientExe))) return false;
    if (!requireResolver || Equal(NormalPath(loader.parent_path()), NormalPath(folder))) return true;
    // r4-r7 helpers load only their own DLL. Never display their registration as
    // a working latest-release watcher in a newer module. A one-time r8 opt-in
    // replaces that old entry; recovery can still remove it by family.
    const auto registeredRevision = LocalRelease::FolderRevision(loader.parent_path().filename().wstring());
    return registeredRevision && *registeredRevision >= 8;
}
void SpawnWatcher(const std::filesystem::path& folder, const std::wstring& command) {
    auto mutableCommand = command;
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const auto loader = std::filesystem::path(NormalPath(folder)) / L"ModuleAutoLoader.exe";
    if (!CreateProcessW(loader.c_str(), mutableCommand.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
            nullptr, folder.c_str(), &startup, &process))
        throw std::runtime_error("Cannot start DofusMiniatures auto-loader; startup entry restored");
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
}
}

std::wstring AutoLoadCommand(const std::filesystem::path& folder, const std::filesystem::path& clientExe) {
    return L"\"" + NormalPath(folder / L"ModuleAutoLoader.exe") + L"\" --watch \"" + NormalPath(clientExe) + L"\"";
}
std::wstring AutoLoadMutexName(const std::filesystem::path& folder) {
    // Stable case-insensitive FNV-1a identity; the mutex only avoids duplicate
    // helpers for this installation and carries no security boundary.
    uint64_t hash = 14695981039346656037ULL;
    for (auto character : NormalPath(InstallationIdentity(folder))) { hash ^= static_cast<uint64_t>(towlower(character)); hash *= 1099511628211ULL; }
    return L"Local\\DofusMiniatures.AutoLoader." + std::to_wstring(hash);
}
bool AutoLoadEnabled(const std::filesystem::path& folder, const std::filesystem::path& clientExe) {
    const auto current = ReadRegistration();
    return current && OwnedForClient(*current, folder, clientExe, true);
}
bool AutoLoadOwnedByFolder(const std::filesystem::path& folder) {
    const auto current = ReadRegistration();
    std::filesystem::path loader;
    return current && ParseOwnedCommand(*current, loader) && SameInstallation(loader.parent_path(), folder);
}
void ConfigureAutoLoad(bool enabled, const std::filesystem::path& folder, const std::filesystem::path& clientExe) {
    ConfigurationLock lock;
    const auto command = AutoLoadCommand(folder, clientExe);
    const auto previous = ReadRegistration();
    if (!enabled) {
        if (previous && OwnedForClient(*previous, folder, clientExe, false)) WriteRegistration(std::nullopt);
        return;
    }
    std::filesystem::path previousLoader;
    if (previous && !ParseOwnedCommand(*previous, previousLoader))
        throw std::runtime_error("An unrelated DofusMiniatures startup entry exists; left unchanged");
    if (!std::filesystem::is_regular_file(folder / L"ModuleLoader.exe") ||
        !std::filesystem::is_regular_file(folder / L"ModuleAutoLoader.exe") ||
        !std::filesystem::is_regular_file(folder / L"DofusMiniatures.dll"))
        throw std::runtime_error("Keep ModuleLoader.exe, ModuleAutoLoader.exe and DofusMiniatures.dll together before enabling auto-load");
    if (!VerifiedRelease(folder)) throw std::runtime_error("This release.ini or its binary fingerprints are invalid; auto-load not enabled");
    if (!SupportedClient(clientExe)) throw std::runtime_error("Unsupported client: auto-load not enabled");
    // Run entries have a documented 260-character command line limit.
    if (command.size() >= 260) throw std::runtime_error("Auto-load folder path is too long; move the mod to a shorter path");
    WriteRegistration(command);
    try { SpawnWatcher(folder, command); }
    catch (...) {
        const auto current = ReadRegistration();
        if (current && Equal(*current, command)) WriteRegistration(previous);
        throw;
    }
}
void DisableAutoLoadFromFolder(const std::filesystem::path& folder) {
    ConfigurationLock lock;
    if (AutoLoadOwnedByFolder(folder)) WriteRegistration(std::nullopt);
}

LocalReleaseSelection LatestLocalRelease(const std::filesystem::path& folder) {
    const auto folders = ReleaseFolders(folder);
    std::vector<LocalRelease::Candidate> candidates;
    std::vector<std::filesystem::path> verifiedFolders;
    for (const auto& candidate : folders) if (auto verified = VerifiedRelease(candidate)) {
        candidates.push_back(std::move(*verified));
        verifiedFolders.push_back(candidate);
    }
    const auto latest = LocalRelease::Latest(candidates, ExpectedGameHash, ExpectedMetadataHash);
    if (!latest) throw std::runtime_error("No complete compatible local release with verified fingerprints; nothing loaded");
    return {verifiedFolders[*latest], candidates[*latest].manifest.revision};
}

LocalReleaseSelection LatestReleaseForClient(const std::filesystem::path& folder,
    const std::filesystem::path& clientExe) {
    if (!clientExe.is_absolute() || !Equal(clientExe.filename().wstring(), L"Dofus.exe"))
        throw std::runtime_error("Exact Dofus.exe path required");
    const auto game = FileHash(clientExe.parent_path() / L"GameAssembly.dll");
    const auto metadata = FileHash(clientExe.parent_path() / L"Dofus_Data/il2cpp_data/Metadata/global-metadata.dat");
    LocalReleaseSelection latest;
    for (const auto& candidate : ReleaseFolders(folder)) {
        const auto verified = VerifiedRelease(candidate, game, metadata);
        if (verified && verified->manifest.revision > latest.revision)
            latest = {candidate, verified->manifest.revision};
    }
    if (!latest.revision)
        throw std::runtime_error("NO_COMPATIBLE_RELEASE: cette mise a jour de Dofus attend une version adaptee du mod; jeu laisse intact");
    return latest;
}

bool VerifiedReleaseForClient(const std::filesystem::path& folder,
    const std::filesystem::path& clientExe) {
    try {
        return VerifiedRelease(folder, FileHash(clientExe.parent_path() / L"GameAssembly.dll"),
            FileHash(clientExe.parent_path() / L"Dofus_Data/il2cpp_data/Metadata/global-metadata.dat")).has_value();
    } catch (...) { return false; }
}

size_t RequestStopLocalReleases(const std::filesystem::path& folder) {
    std::vector<std::filesystem::path> destinations;
    for (const auto& candidate : ReleaseFolders(folder)) {
        if (!VerifiedRelease(candidate)) continue;
        const auto request = candidate / L"stop.request";
        if (GetFileAttributesW(request.c_str()) != INVALID_FILE_ATTRIBUTES && !PlainPath(request, false))
            throw std::runtime_error("Invalid stop.request path; no requests written");
        destinations.push_back(request);
    }
    if (destinations.empty()) throw std::runtime_error("No verified local release found; no stop request written");
    // Validate every destination first so a non-file/junction never receives an
    // overwrite. Modules poll these explicit requests through their normal loop.
    for (const auto& request : destinations) {
        std::ofstream file(request, std::ios::binary | std::ios::trunc);
        file << "stop\n";
        file.flush();
        if (!file) throw std::runtime_error("Cannot write a local stop request; inspect the selected release logs");
    }
    return destinations.size();
}

std::filesystem::path ReleaseSettingsPath(const std::filesystem::path& folder) {
    const auto fallback = folder / L"settings.ini";
    std::filesystem::path temporary;
    try {
        const auto folders = ReleaseFolders(folder);
        if (!VerifiedRelease(folder)) return fallback;
        const auto shared = std::filesystem::path(NormalPath(folder)).parent_path() / L"DofusMiniatures-3.7.11.11-settings.ini";
        if (std::filesystem::exists(shared) && !PlainPath(shared, false)) return fallback;
        // Check the parent without replacing preferences. Atomic Settings::Save
        // needs permission to create a temporary file in this same directory.
        temporary = shared;
        temporary += L".migration-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
        {
            Handle probe(CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr));
            if (!probe) return fallback;
        }
        if (std::filesystem::exists(shared)) {
            Handle writable(CreateFileW(shared.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
            return writable ? shared : fallback;
        }
        std::vector<LocalRelease::Preferences> preferences;
        std::vector<std::filesystem::path> paths;
        for (const auto& candidate : folders) {
            const auto path = candidate / L"settings.ini";
            if (!PlainPath(path, false) || std::filesystem::file_size(path) > 65536) continue;
            const auto verified = VerifiedRelease(candidate);
            if (!verified) continue;
            paths.push_back(path);
            preferences.push_back({verified->manifest.revision,
                std::filesystem::last_write_time(path).time_since_epoch().count(), true});
        }
        const auto source = LocalRelease::MigrationSource(false, preferences);
        if (source) std::filesystem::copy_file(paths[*source], temporary, std::filesystem::copy_options::none);
        else {
            std::ofstream empty(temporary, std::ios::binary);
            empty.flush();
            if (!empty) throw std::runtime_error("Cannot initialize shared preferences");
        }
        // No REPLACE_EXISTING: a concurrent first launch must never overwrite a
        // shared file that another module has already migrated or saved.
        if (!MoveFileExW(temporary.c_str(), shared.c_str(), MOVEFILE_WRITE_THROUGH)) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            if (!PlainPath(shared, false)) return fallback;
        }
        return shared;
    } catch (...) {
        if (!temporary.empty()) { std::error_code ignored; std::filesystem::remove(temporary, ignored); }
        return fallback;
    }
}
