// Actual resolver and migration code, exercised only on generated local files.
// No test calls registry, autoload configuration, process discovery or loading.
#include "autoload.cpp"
#include <iostream>
#include <sstream>
#include <chrono>

namespace {
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
std::string ManifestText(int revision, const std::array<std::string, 3>& hashes,
    const std::string& game = ExpectedGameHash, const std::string& metadata = ExpectedMetadataHash) {
    std::ostringstream text;
    text << "format=1\nclientVersion=3.7.12.12\nrevision=" << revision << "\ngameHash=" << game
         << "\nmetadataHash=" << metadata << '\n';
    for (size_t i = 0; i != hashes.size(); ++i) text << LocalRelease::BinaryNames[i] << '=' << hashes[i] << '\n';
    return text.str();
}
void Write(const std::filesystem::path& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary);
    file << text;
    file.flush();
    Check(static_cast<bool>(file), "Fixture write failed");
}
std::string Read(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
}
std::filesystem::path MakeRelease(const std::filesystem::path& parent, int revision,
    const std::string& game = ExpectedGameHash) {
    const auto folder = parent / (std::string(LocalRelease::FolderPrefix) + std::to_string(revision));
    std::filesystem::create_directory(folder);
    std::array<std::string, 3> hashes;
    for (size_t i = 0; i != hashes.size(); ++i) {
        const auto binary = folder / LocalRelease::BinaryNames[i];
        Write(binary, "synthetic binary " + std::to_string(revision) + " " + std::to_string(i));
        hashes[i] = FileHash(binary);
    }
    Write(folder / "release.ini", ManifestText(revision, hashes, game));
    if (revision >= 75) {
        std::ostringstream integrity;
        for (size_t i = 0; i != hashes.size(); ++i)
            integrity << LocalRelease::BinaryNames[i] << '=' << hashes[i] << '\n';
        for (const auto name : {"TreasureOverlay.exe", "Update-GitHub.ps1"}) {
            Write(folder / name, "synthetic companion");
            integrity << name << '=' << FileHash(folder / name) << '\n';
        }
        Write(folder / "update-files.ini", integrity.str());
    }
    return folder;
}
struct Fixtures {
    std::filesystem::path root;
    Fixtures() {
        const auto source = std::filesystem::absolute(std::filesystem::path(__FILE__)).parent_path();
        Check(std::filesystem::is_regular_file(source / "release-policy-tests.cpp"), "Cannot identify test source directory");
        const auto build = source / "build";
        std::filesystem::create_directories(build);
        Check(PlainPath(build, true), "Fixture parent must be a plain local build directory");
        root = build / ("release-fixtures-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
        Check(std::filesystem::create_directory(root), "Fresh fixture directory required");
    }
    ~Fixtures() { std::error_code ignored; std::filesystem::remove_all(root, ignored); }
};
void PolicyTests() {
    const std::array<std::string, 3> hashes{std::string(64, 'a'), std::string(64, 'b'), std::string(64, 'c')};
    const auto text = ManifestText(8, hashes);
    const auto manifest = LocalRelease::Parse(text);
    Check(manifest && manifest->revision == 8, "Valid manifest rejected");
    Check(LocalRelease::Parse(text + "\r\n").has_value(), "Blank CRLF must be accepted");
    Check(!LocalRelease::Parse(text + "revision=9\n"), "Duplicate keys must fail");
    Check(!LocalRelease::Parse(text + "unexpected=1\n"), "Unknown schema keys must fail");
    auto future = text;
    future.replace(future.find("3.7.12.12"), 9, "9.9.9.9");
    Check(LocalRelease::Parse(future).has_value(), "Bootstrap must parse future client versions without granting compatibility");
    Check(!LocalRelease::Version("3.7.12") && !LocalRelease::Version("3..12.12") &&
        !LocalRelease::Version("https://example.com") && LocalRelease::Version("3.8.1.42"),
        "Client version field must remain bounded numeric data");
    Check(!LocalRelease::Parse(text + std::string(1, '\0')), "NUL must fail");
    Check(!LocalRelease::Parse(std::string(4097, 'x')), "Oversized manifest must fail");
    Check(!LocalRelease::Parse(ManifestText(8, hashes, std::string(64, 'X'))), "Malformed fingerprint must fail");
    Check(!LocalRelease::FolderRevision("DofusMiniatures-3.7.11.11-r08"), "Leading-zero revision ambiguous");
    Check(!LocalRelease::FolderRevision("DofusMiniatures-3.7.11.11-r8-old"), "Backup directory must not be scanned");
    Check(!LocalRelease::FolderRevision("DofusMiniatures-3.7.11.12-r9"), "Other client family must be excluded");
    Check(!LocalRelease::FolderRevision(L"DofusMiniatures-3.7.11.11-r\u00e9"), "Non-ASCII sibling must be ignored safely");
    Check(!LocalRelease::Revision("9999999999999999999999") && !LocalRelease::Revision("-1"), "Invalid revision must fail");
    std::vector<LocalRelease::Candidate> candidates;
    for (const int revision : {9, 8, 10, 11}) {
        auto value = *manifest; value.revision = revision;
        candidates.push_back({std::string(LocalRelease::FolderPrefix) + std::to_string(revision), value, hashes});
    }
    candidates[3].actualHashes[1] = std::string(64, 'd');
    auto selected = LocalRelease::Latest(candidates, ExpectedGameHash, ExpectedMetadataHash);
    Check(selected && *selected == 2, "Choose numeric newest, exclude damaged loader");
    candidates[2].manifest.metadataHash = std::string(64, 'd');
    selected = LocalRelease::Latest(candidates, ExpectedGameHash, ExpectedMetadataHash);
    Check(selected && *selected == 0, "Exclude incompatible metadata");
    candidates[0].regularFiles = false;
    selected = LocalRelease::Latest(candidates, ExpectedGameHash, ExpectedMetadataHash);
    Check(selected && *selected == 1, "Exclude reparse/nonregular files");
    candidates[1].manifest.revision = 9;
    Check(!LocalRelease::Latest(candidates, ExpectedGameHash, ExpectedMetadataHash), "Folder/manifest mismatch must fail");
    const std::vector<LocalRelease::Preferences> preferences{{8, 100, true}, {9, 100, true}, {10, 200, false}};
    Check(LocalRelease::MigrationSource(false, preferences) == 1, "Migration tie uses newest verified revision");
    Check(!LocalRelease::MigrationSource(true, preferences), "Never overwrite shared preferences");
}
void FileTests() {
    Fixtures files;
    const auto r8 = MakeRelease(files.root, 8);
    const auto r9 = MakeRelease(files.root, 9);
    const auto r10 = MakeRelease(files.root, 10);
    const auto r11 = MakeRelease(files.root, 11);
    MakeRelease(files.root, 12, std::string(64, '0'));
    Write(r11 / "ModuleAutoLoader.exe", "incomplete replacement");
    Check(LatestLocalRelease(r8).folder == r10, "Actual resolver must select verified newest sibling");
    Check(LatestLocalRelease(r10).revision == 10, "Selection must include current folder");
    Write(r10 / "release.ini", Read(r10 / "release.ini") + "revision=10\n");
    Check(LatestLocalRelease(r8).folder == r9, "Malformed newest release must retain valid prior release");

    Write(r8 / "settings.ini", "mode=2\nanimationMode=1\n");
    Write(r9 / "settings.ini", "mode=2\nanimationMode=3\nroleplayMode=0\n");
    const auto now = std::filesystem::file_time_type::clock::now();
    std::filesystem::last_write_time(r8 / "settings.ini", now - std::chrono::hours(1));
    std::filesystem::last_write_time(r9 / "settings.ini", now);
    const auto shared = ReleaseSettingsPath(r8);
    Check(shared == files.root / "DofusMiniatures-3.7.11.11-settings.ini", "Known releases must share preferences");
    Check(Read(shared) == Read(r9 / "settings.ini"), "Migration must choose most recently changed verified preferences");
    Write(r9 / "settings.ini", "mode=0\n");
    Check(ReleaseSettingsPath(r9) == shared && Read(shared).find("animationMode=3") != std::string::npos,
        "Existing shared preferences must survive future launches");
    Check(ReleaseSettingsPath(r10) == r10 / "settings.ini", "Unverified package must use own preferences");
    const auto custom = files.root / "custom";
    std::filesystem::create_directory(custom);
    Check(ReleaseSettingsPath(custom) == custom / "settings.ini", "Unpackaged folder fallback required");
    Check(SetFileAttributesW(shared.c_str(), FILE_ATTRIBUTE_READONLY) != 0, "Cannot make readonly fixture");
    const auto readonlyPath = ReleaseSettingsPath(r8);
    Check(SetFileAttributesW(shared.c_str(), FILE_ATTRIBUTE_NORMAL) != 0, "Cannot restore fixture flags");
    Check(readonlyPath == r8 / "settings.ini", "Unwritable shared preferences need local fallback");

    std::filesystem::path loader, client;
    const auto exe = files.root / "Dofus.exe";
    Check(ParseOwnedCommand(AutoLoadCommand(r8, exe), loader, &client) && loader.parent_path() == r8 && client == exe,
        "Exact owned command parser must preserve folder and client");
    Check(!ParseOwnedCommand(AutoLoadCommand(r8, exe) + L" --extra", loader), "Unrelated command must never be owned");
    Check(SameInstallation(r8, r9) && AutoLoadMutexName(r8) == AutoLoadMutexName(r9), "Revisions must share enable/disable and singleton identity");
    const auto r7 = files.root / "DofusMiniatures-3.7.11.11-r7";
    Check(!OwnedForClient(AutoLoadCommand(r7, exe), r8, exe, true), "Legacy watcher must not masquerade as latest-release watcher");
    Check(OwnedForClient(AutoLoadCommand(r7, exe), r8, exe, false), "Legacy watcher remains removable by family");
    Check(OwnedForClient(AutoLoadCommand(r8, exe), r9, exe, true), "New release must recognize shared opt-in");
    Check(!OwnedForClient(AutoLoadCommand(r8, exe), r9, files.root / "other" / "Dofus.exe", true), "Client path must remain exact");
    Check(!SameInstallation(r8, custom / r8.filename()), "Different parent must stay a separate installation");
    Check(!SameInstallation(r8, files.root / "DofusMiniatures-3.7.11.12-r8"), "Different game must stay separate");
    const auto other = files.root / "other";
    std::filesystem::create_directory(other);
    const auto unrelated = MakeRelease(other, 99);
    Check(LatestLocalRelease(r8).folder == r9, "Do not recurse into another installation");
    Check(ReleaseSettingsPath(unrelated).parent_path() == other, "Shared preferences must not cross parent boundary");
    Check(RequestStopLocalReleases(r8) == 2, "Stop must target all and only complete compatible sibling releases");
    Check(Read(r8 / "stop.request") == "stop\n" && Read(r9 / "stop.request") == "stop\n", "Stop requests missing for verified releases");
    Check(!std::filesystem::exists(r10 / "stop.request") && !std::filesystem::exists(r11 / "stop.request") &&
        !std::filesystem::exists(unrelated / "stop.request") && !std::filesystem::exists(custom / "stop.request"),
        "Stop must exclude malformed, incomplete, foreign or unpackaged folders");
    std::filesystem::remove(r8 / "stop.request");
    std::filesystem::create_directory(r8 / "stop.request");
    bool rejected = false;
    try { RequestStopLocalReleases(r8); } catch (const std::exception&) { rejected = true; }
    Check(rejected && std::filesystem::is_directory(r8 / "stop.request"), "Stop may not overwrite an unexpected object");
}
void CrossVersionTests() {
    Fixtures files;
    const auto old = MakeRelease(files.root, 8);
    const auto client = files.root / "Dofus.exe";
    Write(client, "synthetic client");
    Write(files.root / "GameAssembly.dll", "synthetic future game binary");
    std::filesystem::create_directories(files.root / "Dofus_Data/il2cpp_data/Metadata");
    const auto metadata = files.root / "Dofus_Data/il2cpp_data/Metadata/global-metadata.dat";
    Write(metadata, "synthetic future metadata");
    const auto next = MakeRelease(files.root, 76, FileHash(files.root / "GameAssembly.dll"));
    auto manifest = Read(next / "release.ini");
    manifest.replace(manifest.find(ExpectedMetadataHash), 64, FileHash(metadata));
    manifest.replace(manifest.find("3.7.12.12"), 9, "9.9.9.9");
    Write(next / "release.ini", manifest);
    Check(LatestReleaseForClient(old, client).folder == next,
        "Old bootstrap must select a future client's package by actual file hashes");
    Check(LatestLocalRelease(old).folder == old && !SupportedClient(client),
        "Current native module must retain its pinned-client compatibility");
    Check(VerifiedReleaseForClient(next, client), "Verified dispatcher candidate rejected");
    Write(next / "Update-GitHub.ps1", "tampered updater");
    Check(!VerifiedReleaseForClient(next, client), "Changed updater must never execute");
    bool rejected = false;
    try { LatestReleaseForClient(old, client); } catch (const std::exception&) { rejected = true; }
    Check(rejected, "Incompatible older release must never become a fallback");
}
}
int main() {
    try {
        PolicyTests(); FileTests(); CrossVersionTests();
        std::cout << "PASS: manifest, numeric latest, fingerprints, compatibility, bounded family, settings migration/fallback, command ownership; fixtures only\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
