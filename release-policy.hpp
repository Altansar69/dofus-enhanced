#pragma once
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Pure local-release policy. No registry, process access or filesystem actions.
namespace LocalRelease {
inline constexpr std::string_view ClientVersion = "3.7.12.12";
// Historical installation family: retain shared preferences and owned startup identity.
inline constexpr std::string_view FolderPrefix = "DofusMiniatures-3.7.11.11-r";
inline constexpr std::array<std::string_view, 3> BinaryNames{
    "DofusMiniatures.dll", "ModuleLoader.exe", "ModuleAutoLoader.exe"};
inline constexpr size_t ManifestLimit = 4096;
struct Manifest {
    int revision = 0;
    std::string gameHash, metadataHash;
    std::array<std::string, 3> binaryHashes;
    std::string clientVersion;
};
inline bool Version(std::string_view value) {
    if (value.empty() || value.size() > 31) return false;
    unsigned parts = 1, digits = 0;
    for (char c : value) {
        if (c == '.') { if (!digits || ++parts > 4) return false; digits = 0; }
        else if (c >= '0' && c <= '9') { if (++digits > 6) return false; }
        else return false;
    }
    return parts == 4 && digits != 0;
}
inline bool Hash(std::string_view value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}
inline std::optional<int> Revision(std::string_view value) {
    if (value.empty() || value.front() == '0') return {};
    int revision = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), revision);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || revision < 1 || revision > 1000000)
        return {};
    return revision;
}
inline std::optional<int> FolderRevision(std::string_view name) {
    if (!name.starts_with(FolderPrefix)) return {};
    return Revision(name.substr(FolderPrefix.size()));
}
inline std::optional<int> FolderRevision(std::wstring_view name) {
    std::string ascii;
    for (const auto character : name) {
        if (static_cast<uint32_t>(character) > 127) return {};
        ascii.push_back(static_cast<char>(character));
    }
    return FolderRevision(std::string_view(ascii));
}
inline std::optional<Manifest> Parse(std::string_view text) {
    if (text.empty() || text.size() > ManifestLimit || text.find('\0') != std::string_view::npos) return {};
    std::map<std::string, std::string> fields;
    while (!text.empty()) {
        const auto newline = text.find('\n');
        auto line = text.substr(0, newline);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (!line.empty()) {
            const auto split = line.find('=');
            if (split == std::string_view::npos || !split || split + 1 == line.size()) return {};
            if (!fields.emplace(std::string(line.substr(0, split)), std::string(line.substr(split + 1))).second) return {};
        }
        if (newline == std::string_view::npos) break;
        text.remove_prefix(newline + 1);
    }
    // A stable bootstrap must be able to discover a later client's package.
    // Compatibility still requires both exact installed binary fingerprints.
    if (fields.size() != 8 || fields["format"] != "1" || !Version(fields["clientVersion"])) return {};
    const auto revision = Revision(fields["revision"]);
    if (!revision || !Hash(fields["gameHash"]) || !Hash(fields["metadataHash"])) return {};
    Manifest result{*revision, fields["gameHash"], fields["metadataHash"], {}, fields["clientVersion"]};
    for (size_t i = 0; i != BinaryNames.size(); ++i) {
        result.binaryHashes[i] = fields[std::string(BinaryNames[i])];
        if (!Hash(result.binaryHashes[i])) return {};
    }
    return result;
}
struct Candidate {
    std::string name;
    Manifest manifest;
    std::array<std::string, 3> actualHashes;
    bool regularFiles = true; // runtime rejects reparse points and non-files
};
inline bool Compatible(const Candidate& candidate, std::string_view gameHash, std::string_view metadataHash) {
    const auto revision = FolderRevision(candidate.name);
    return candidate.regularFiles && revision && *revision == candidate.manifest.revision &&
        candidate.manifest.gameHash == gameHash && candidate.manifest.metadataHash == metadataHash &&
        candidate.manifest.binaryHashes == candidate.actualHashes &&
        std::all_of(candidate.actualHashes.begin(), candidate.actualHashes.end(), [](const auto& hash) { return Hash(hash); });
}
inline std::optional<size_t> Latest(const std::vector<Candidate>& candidates,
    std::string_view gameHash, std::string_view metadataHash) {
    std::optional<size_t> selected;
    for (size_t i = 0; i != candidates.size(); ++i)
        if (Compatible(candidates[i], gameHash, metadataHash) &&
            (!selected || candidates[i].manifest.revision > candidates[*selected].manifest.revision)) selected = i;
    return selected;
}
struct Preferences {
    int revision = 0;
    int64_t modified = 0;
    bool verified = false;
};
inline std::optional<size_t> MigrationSource(bool sharedExists, const std::vector<Preferences>& candidates) {
    if (sharedExists) return {};
    std::optional<size_t> selected;
    for (size_t i = 0; i != candidates.size(); ++i) {
        const auto& value = candidates[i];
        if (!value.verified || value.revision < 1) continue;
        if (!selected || value.modified > candidates[*selected].modified ||
            (value.modified == candidates[*selected].modified && value.revision > candidates[*selected].revision)) selected = i;
    }
    return selected;
}
}
