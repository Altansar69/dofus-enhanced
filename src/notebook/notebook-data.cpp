#include "notebook/notebook-data.hpp"
#include <Windows.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <atomic>
#include <charconv>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>

namespace notebook {
namespace {
constexpr size_t MaxBytes = 4 * 1024 * 1024, MaxEntries = 256;
constexpr int ReplaceRetries = 5; // 20 ms apart.
using Json = nlohmann::json;
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE handle) : value(handle) {}
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
};
void Plain(const std::filesystem::path& path, bool missing) {
    const auto attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        if (missing && GetLastError() == ERROR_FILE_NOT_FOUND) return;
        throw std::runtime_error("Fichier du carnet inaccessible.");
    }
    if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) throw std::runtime_error("Lien de fichier non pris en charge pour le carnet.");
}
std::filesystem::path Checked(const std::filesystem::path& path) {
    const auto absolute = std::filesystem::absolute(path).lexically_normal();
    auto parent = absolute.parent_path();
    while (!parent.empty()) {
        Plain(parent, false);
        const auto next = parent.parent_path();
        if (next == parent) break;
        parent = next;
    }
    Plain(absolute, true);
    return absolute;
}
void Text(const std::string& text, size_t max, bool multiline, bool required) {
    if (text.size() > max || (required && text.find_first_not_of(" \t\r\n") == std::string::npos))
        throw std::runtime_error("Titre ou texte vide, ou trop long.");
    for (const unsigned char c : text) {
        if ((c < 32 && !(multiline && (c == '\n' || c == '\r' || c == '\t'))) || c == 127)
            throw std::runtime_error("Le texte contient un caractère de contrôle.");
    }
    // Also reject invalid UTF-8 without changing the user's existing document.
    try { (void)Json(text).dump(); }
    catch (const Json::exception&) { throw std::runtime_error("Le texte doit être en UTF-8 valide."); }
}
const char* Name(Kind kind) {
    switch (kind) {
    case Kind::Position: return "position";
    case Kind::Note: return "note";
    case Kind::Message: return "message";
    }
    throw std::runtime_error("Type de carnet invalide.");
}
Kind ParseKind(const std::string& text) {
    if (text == "position") return Kind::Position;
    if (text == "note") return Kind::Note;
    if (text == "message") return Kind::Message;
    throw std::runtime_error("Type de carnet inconnu.");
}
uint64_t Unsigned(const Json& value) {
    if (!value.is_number_unsigned()) throw std::runtime_error("Identifiant du carnet invalide.");
    return value.get<uint64_t>();
}
int Integer(const Json& value) {
    if (!value.is_number_integer()) throw std::runtime_error("Coordonnées du carnet invalides.");
    if (value.is_number_unsigned() && value.get<uint64_t>() > static_cast<uint64_t>(std::numeric_limits<int>::max()))
        throw std::runtime_error("Coordonnées du carnet invalides.");
    const auto number = value.get<int64_t>();
    if (number < std::numeric_limits<int>::min() || number > std::numeric_limits<int>::max())
        throw std::runtime_error("Coordonnées du carnet invalides.");
    return static_cast<int>(number);
}
// The file's bytes, read through a handle that lets another client replace the
// file meanwhile (FILE_SHARE_DELETE) and closed before anything is parsed;
// nothing when the file is gone.
std::optional<std::string> ReadBytes(const std::filesystem::path& path) {
    Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (file.value == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND) return std::nullopt;
        throw std::runtime_error("Lecture du carnet impossible.");
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file.value, &size) || size.QuadPart < 0 || static_cast<uint64_t>(size.QuadPart) > MaxBytes)
        throw std::runtime_error("Fichier du carnet invalide ou trop volumineux.");
    std::string bytes(static_cast<size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    if (!bytes.empty() && (!ReadFile(file.value, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) || read != bytes.size()))
        throw std::runtime_error("Lecture du carnet impossible.");
    return bytes;
}
Document LoadUnchecked(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) return {};
    if (!std::filesystem::is_regular_file(path)) throw std::runtime_error("Fichier du carnet invalide ou trop volumineux.");
    const auto bytes = ReadBytes(path);
    if (!bytes) return {};
    const auto json = Json::parse(*bytes);
    if (!json.is_object() || json.at("schema") != 1 || !json.at("entries").is_array() ||
        json.at("entries").size() > MaxEntries)
        throw std::runtime_error("Format du carnet inconnu. Le fichier est conservé.");
    Document result;
    result.nextId = Unsigned(json.at("nextId"));
    if (!result.nextId || result.nextId == UINT64_MAX) throw std::runtime_error("Compteur du carnet invalide.");
    std::set<uint64_t> ids;
    for (const auto& item : json.at("entries")) {
        Entry entry;
        entry.id = Unsigned(item.at("id")); entry.version = Unsigned(item.at("version"));
        entry.kind = ParseKind(item.at("kind").get<std::string>());
        entry.title = item.at("title").get<std::string>(); entry.body = item.at("body").get<std::string>();
        entry.x = Integer(item.at("x")); entry.y = Integer(item.at("y")); entry.world = Integer(item.at("world"));
        if (!item.at("map").is_number_integer() ||
            (item.at("map").is_number_unsigned() && item.at("map").get<uint64_t>() > static_cast<uint64_t>(INT64_MAX)))
            throw std::runtime_error("Carte du carnet invalide.");
        entry.map = item.at("map").get<int64_t>();
        entry.channel = item.contains("channel") ? Integer(item.at("channel")) : 0; // Absent before 1.4.0.
        if (!entry.id || entry.id >= result.nextId || !entry.version || entry.version == UINT64_MAX || !ids.insert(entry.id).second)
            throw std::runtime_error("Entrée du carnet invalide.");
        Validate(entry); result.entries.push_back(std::move(entry));
    }
    return result;
}
Document Load(const std::filesystem::path& path) {
    try { return LoadUnchecked(path); }
    catch (const Json::exception&) { throw std::runtime_error("Format du carnet invalide. Le fichier est conservé."); }
}
void Write(const std::filesystem::path& path, const Document& data) {
    Json json{{"schema", 1}, {"nextId", data.nextId}, {"entries", Json::array()}};
    for (const auto& entry : data.entries)
        json["entries"].push_back({{"id", entry.id}, {"version", entry.version}, {"kind", Name(entry.kind)},
            {"title", entry.title}, {"body", entry.body}, {"x", entry.x}, {"y", entry.y},
            {"world", entry.world}, {"map", entry.map}, {"channel", entry.channel}});
    const auto bytes = json.dump(2);
    if (bytes.size() > MaxBytes) throw std::runtime_error("Le carnet est trop volumineux.");
    static std::atomic_uint64_t counter{0};
    auto temporary = path;
    temporary += L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(++counter);
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ignored; std::filesystem::remove(path, ignored); }
    } cleanup{temporary};
    {
        Handle file(CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
        DWORD written = 0;
        if (file.value == INVALID_HANDLE_VALUE ||
            !WriteFile(file.value, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) ||
            written != bytes.size() || !FlushFileBuffers(file.value))
            throw std::runtime_error("Enregistrement du carnet impossible.");
    }
    Plain(path, true);
    // A reader of another client, a backup or a sync tool may hold the file for
    // a moment: the replacement is tried again for about a tenth of a second.
    for (int attempt = 0;; ++attempt) {
        if (MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return;
        const auto error = GetLastError();
        if ((error != ERROR_ACCESS_DENIED && error != ERROR_SHARING_VIOLATION) || attempt >= ReplaceRetries)
            throw std::runtime_error("Remplacement du carnet impossible. Réessaie.");
        Sleep(20);
    }
}
// Writers take the lock file, then reload, change and replace the notebook.
template<class Change> auto Locked(const std::filesystem::path& path, Change change) {
    auto lockPath = path; lockPath += L".lock"; Plain(lockPath, true);
    Handle lock(CreateFileW(lockPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (lock.value == INVALID_HANDLE_VALUE) throw std::runtime_error("Le carnet est utilisé par un autre client. Réessaie.");
    return change();
}
}
std::filesystem::path DataPath(const std::filesystem::path& settingsPath) {
    return settingsPath.parent_path() / L"DofusMiniatures-notebook.json";
}
bool Coordinate(const std::string& text, int& value) {
    if (text.empty() || text.size() > 5) return false;
    int parsed = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || parsed < -1000 || parsed > 1000) return false;
    value = parsed; return true;
}
void Validate(const Entry& entry) {
    (void)Name(entry.kind);
    Text(entry.title, 160, false, true);
    Text(entry.body, entry.kind == Kind::Note ? 16384 : 600, entry.kind == Kind::Note, entry.kind != Kind::Position);
    if (entry.x < -1000 || entry.x > 1000 || entry.y < -1000 || entry.y > 1000 ||
        entry.world < 0 || entry.world > 10000 || entry.map < 0)
        throw std::runtime_error("Coordonnées du carnet invalides.");
    if (!ValidChannel(entry.channel) || (entry.channel && entry.kind != Kind::Message))
        throw std::runtime_error("Canal du carnet invalide.");
}
bool ValidChannel(int channel) {
    for (const auto known : Channels) if (known == channel) return true;
    return false;
}
int NextChannel(int channel) {
    const auto count = sizeof(Channels) / sizeof(Channels[0]);
    for (size_t i = 0; i < count; ++i) if (Channels[i] == channel) return Channels[(i + 1) % count];
    return Channels[0];
}
const char* ChannelName(int channel) {
    switch (channel) {
    case 1: return "Général";
    case 2: return "Équipe";
    case 4: return "Guilde";
    case 8: return "Alliance";
    case 16: return "Groupe";
    case 32: return "Commerce";
    case 64: return "Recrutement";
    default: return "actuel";
    }
}
Document Read(const std::filesystem::path& path) { return Load(Checked(path)); }
Entry Save(const std::filesystem::path& input, Entry entry) {
    Validate(entry);
    const auto path = Checked(input);
    return Locked(path, [&] {
        auto data = Load(path);
        if (!entry.id) {
            if (entry.version || data.entries.size() >= MaxEntries || data.nextId >= UINT64_MAX - 1)
                throw std::runtime_error("Le carnet est plein (256 entrées).");
            entry.id = data.nextId++; entry.version = 1; data.entries.push_back(entry);
        } else {
            const auto found = std::find_if(data.entries.begin(), data.entries.end(), [&](const Entry& row) { return row.id == entry.id; });
            if (found == data.entries.end()) throw std::runtime_error("Cette entrée a été supprimée ailleurs.");
            if (found->version != entry.version || found->kind != entry.kind || entry.version >= UINT64_MAX - 1)
                throw std::runtime_error("Cette entrée a changé ailleurs. La resélectionner dans la liste charge sa nouvelle version et abandonne ton brouillon.");
            ++entry.version; *found = entry;
        }
        Write(path, data); return entry;
    });
}
Entry AddPosition(const std::filesystem::path& input, Entry entry, bool& created) {
    entry.kind = Kind::Position; entry.id = entry.version = 0; created = false;
    Validate(entry);
    const auto path = Checked(input);
    return Locked(path, [&] {
        auto data = Load(path);
        // Checked under the lock: two clients adding the same cell keep one entry.
        for (const auto& known : data.entries)
            if (known.kind == Kind::Position && known.x == entry.x && known.y == entry.y && known.world == entry.world) return known;
        if (data.entries.size() >= MaxEntries || data.nextId >= UINT64_MAX - 1) throw std::runtime_error("Le carnet est plein (256 entrées).");
        entry.id = data.nextId++; entry.version = 1; data.entries.push_back(entry);
        Write(path, data); created = true; return entry;
    });
}
void Delete(const std::filesystem::path& input, uint64_t id, uint64_t version) {
    const auto path = Checked(input);
    Locked(path, [&] {
        auto data = Load(path);
        const auto found = std::find_if(data.entries.begin(), data.entries.end(), [&](const Entry& row) { return row.id == id; });
        if (found == data.entries.end()) throw std::runtime_error("Cette entrée a déjà été supprimée ailleurs.");
        if (found->version != version) throw std::runtime_error("Cette entrée a changé ailleurs. Resélectionne-la dans la liste avant de la supprimer.");
        data.entries.erase(found); Write(path, data);
    });
}
}
