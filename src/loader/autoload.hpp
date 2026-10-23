#pragma once
#include <filesystem>
#include <string>

// The per-user Windows startup entry is the source of truth. ConfigureAutoLoad
// is called by its checkbox/reset, or after the first successful module startup
// when no explicit preference has been saved yet.
bool AutoLoadEnabled(const std::filesystem::path& folder, const std::filesystem::path& clientExe);
bool AutoLoadOwnedByFolder(const std::filesystem::path& folder);
void ConfigureAutoLoad(bool enabled, const std::filesystem::path& folder, const std::filesystem::path& clientExe);
void DisableAutoLoadFromFolder(const std::filesystem::path& folder);
std::wstring AutoLoadCommand(const std::filesystem::path& folder, const std::filesystem::path& clientExe);
std::wstring AutoLoadMutexName(const std::filesystem::path& folder);

struct LocalReleaseSelection {
    std::filesystem::path folder;
    int revision = 0;
};
// Selects only verified releases in the same parent and exact client family.
LocalReleaseSelection LatestLocalRelease(const std::filesystem::path& folder);
// Version-independent bootstrap: select by the installed client's exact hashes.
LocalReleaseSelection LatestReleaseForClient(const std::filesystem::path& folder,
    const std::filesystem::path& clientExe);
bool VerifiedReleaseForClient(const std::filesystem::path& folder,
    const std::filesystem::path& clientExe);
// One-time migration to shared preferences. Falls back to folder/settings.ini
// if the parent is unwritable, unsafe, or this is not a packaged release.
std::filesystem::path ReleaseSettingsPath(const std::filesystem::path& folder);
// Writes stop.request only in verified sibling releases; never accesses a game.
size_t RequestStopLocalReleases(const std::filesystem::path& folder);
