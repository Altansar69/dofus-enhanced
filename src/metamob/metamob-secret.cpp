#include "metamob/metamob-secret.hpp"
#include <Windows.h>
#include <dpapi.h>
#include <atomic>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace metamob {
namespace {
// The file is this magic header then the DPAPI blob. The fixed entropy ties the
// blob to this use: another program's blob for the same Windows user never opens here.
constexpr char Magic[] = "DEMK1\n", Entropy[] = "DofusEnhanced/Metamob/v1";
constexpr size_t MagicBytes = sizeof(Magic) - 1, MaxBytes = 4096;
// Fixed texts only: never a path, a Windows error text or a byte of the key.
constexpr char Invalid[] = "Clé Metamob invalide.", Unreachable[] = "Emplacement de la clé Metamob inaccessible.",
    Link[] = "Lien de fichier non pris en charge pour la clé Metamob.",
    BadFile[] = "Fichier de la clé Metamob invalide ou trop volumineux.",
    Unreadable[] = "Clé Metamob illisible sur ce compte Windows.", ReadFailed[] = "Lecture de la clé Metamob impossible.";
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE handle) : value(handle) {}
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
};
// DPAPI output (the plain key for LoadKey): wiped, then released with LocalFree.
struct Blob {
    DATA_BLOB data{};
    Blob() = default;
    Blob(const Blob&) = delete;
    ~Blob() { if (data.pbData) { SecureZeroMemory(data.pbData, data.cbData); LocalFree(data.pbData); } }
};
DATA_BLOB EntropyBlob() { return {static_cast<DWORD>(sizeof(Entropy) - 1), reinterpret_cast<BYTE*>(const_cast<char*>(Entropy))}; }
void Wipe(std::string& text) noexcept {
    text.resize(text.capacity()); SecureZeroMemory(text.data(), text.size()); text.clear();
    // A string that grew past its inline buffer keeps its first bytes there,
    // beside the heap pointer: go back to that buffer and wipe it too.
    text.shrink_to_fit();
    text.resize(text.capacity()); SecureZeroMemory(text.data(), text.size()); text.clear();
}
// The rule of metamob::ValidKey (metamob-data.cpp), repeated so that the store
// stands alone: printable ASCII without space, 16 to 256 bytes.
bool Valid(std::string_view key) {
    if (key.size() < 16 || key.size() > 256) return false;
    for (const unsigned char c : key) if (c < 0x21 || c > 0x7E) return false;
    return true;
}
// As for the notebook: a link (junction, symbolic link) on the file or on a
// parent could send the key elsewhere. Parents must exist; the file may be
// absent but is never a folder.
void Plain(const std::filesystem::path& path, bool file) {
    const auto attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        if (file && GetLastError() == ERROR_FILE_NOT_FOUND) return;
        throw std::runtime_error(Unreachable);
    }
    if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) throw std::runtime_error(Link);
    if (file && (attrs & FILE_ATTRIBUTE_DIRECTORY)) throw std::runtime_error(BadFile);
}
std::filesystem::path Checked(const std::filesystem::path& file) {
    std::error_code error; // the throwing overload would put the path in its message
    const auto absolute = std::filesystem::absolute(file, error).lexically_normal();
    if (error || !absolute.has_parent_path()) throw std::runtime_error(Unreachable);
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
// Windows briefly refuses to open, replace or delete a file that another client
// is reading or replacing (as in settings.cpp): up to 16 tries, 5 ms apart.
template<class Action> DWORD Retried(Action action) {
    for (unsigned attempt = 0; ; ++attempt) {
        if (action()) return ERROR_SUCCESS;
        const auto error = GetLastError();
        if (attempt == 15 || (error != ERROR_SHARING_VIOLATION && error != ERROR_LOCK_VIOLATION && error != ERROR_ACCESS_DENIED))
            return error;
        Sleep(5);
    }
}
}
Secret::Secret(std::string input) : text(std::move(input)) { Wipe(input); }
Secret::Secret(Secret&& other) noexcept : text(std::move(other.text)) { Wipe(other.text); }
Secret& Secret::operator=(Secret&& other) noexcept {
    if (this != &other) { Wipe(text); text = std::move(other.text); Wipe(other.text); }
    return *this;
}
Secret::~Secret() { Wipe(text); }
std::filesystem::path KeyPath(const std::filesystem::path& settingsPath) {
    return settingsPath.parent_path() / L"DofusMiniatures-metamob.key";
}
void StoreKey(const std::filesystem::path& file, const Secret& key) {
    if (!Valid(key.value())) throw std::runtime_error(Invalid);
    const auto path = Checked(file);
    // Current Windows user only (no CRYPTPROTECT_LOCAL_MACHINE) and never a prompt.
    DATA_BLOB plain{static_cast<DWORD>(key.value().size()), reinterpret_cast<BYTE*>(const_cast<char*>(key.value().data()))};
    auto entropy = EntropyBlob();
    Blob sealed;
    if (!CryptProtectData(&plain, nullptr, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &sealed.data) ||
        !sealed.data.pbData || !sealed.data.cbData || sealed.data.cbData > MaxBytes - MagicBytes)
        throw std::runtime_error("Chiffrement de la clé Metamob impossible.");
    static std::atomic_uint64_t counter{0};
    auto temporary = path;
    temporary += L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(++counter);
    struct Cleanup {
        const std::filesystem::path& target;
        ~Cleanup() { std::error_code ignored; std::filesystem::remove(target, ignored); }
    } cleanup{temporary};
    {
        Handle output(CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
        DWORD header = 0, written = 0;
        if (output.value == INVALID_HANDLE_VALUE ||
            !WriteFile(output.value, Magic, static_cast<DWORD>(MagicBytes), &header, nullptr) || header != MagicBytes ||
            !WriteFile(output.value, sealed.data.pbData, sealed.data.cbData, &written, nullptr) || written != sealed.data.cbData ||
            !FlushFileBuffers(output.value))
            throw std::runtime_error("Enregistrement de la clé Metamob impossible.");
    }
    Plain(path, true);
    if (Retried([&] { return MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0; }) != ERROR_SUCCESS)
        throw std::runtime_error("Remplacement de la clé Metamob impossible. Réessaie.");
}
std::optional<Secret> LoadKey(const std::filesystem::path& file) {
    if (!HasKey(file)) return std::nullopt;
    const auto path = Checked(file);
    std::string bytes(MaxBytes + 1, '\0');
    {
        // The handle is the file itself, never a link put there after the check.
        // It is closed before decrypting, so other clients can replace the file.
        Handle input(INVALID_HANDLE_VALUE);
        const auto error = Retried([&] {
            input.value = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            return input.value != INVALID_HANDLE_VALUE;
        });
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return std::nullopt; // forgotten meanwhile by another client
        if (error != ERROR_SUCCESS) throw std::runtime_error(ReadFailed);
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(input.value, &info)) throw std::runtime_error(ReadFailed);
        if (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) throw std::runtime_error(Link);
        if ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || info.nFileSizeHigh || info.nFileSizeLow > MaxBytes)
            throw std::runtime_error(BadFile);
        DWORD total = 0, chunk = 0;
        do {
            if (!ReadFile(input.value, bytes.data() + total, static_cast<DWORD>(bytes.size()) - total, &chunk, nullptr))
                throw std::runtime_error(ReadFailed);
            total += chunk;
        } while (chunk && total < bytes.size());
        bytes.resize(total);
    }
    if (bytes.size() <= MagicBytes || bytes.size() > MaxBytes || bytes.compare(0, MagicBytes, Magic) != 0)
        throw std::runtime_error(BadFile);
    DATA_BLOB sealed{static_cast<DWORD>(bytes.size() - MagicBytes), reinterpret_cast<BYTE*>(bytes.data() + MagicBytes)};
    auto entropy = EntropyBlob();
    Blob plain;
    if (!CryptUnprotectData(&sealed, nullptr, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &plain.data) || !plain.data.pbData)
        throw std::runtime_error(Unreadable);
    std::string text(reinterpret_cast<const char*>(plain.data.pbData), plain.data.cbData);
    std::optional<Secret> key(std::in_place, std::move(text));
    Wipe(text);
    // DPAPI already checks integrity; this refuses a value the store never writes.
    if (!Valid(key->value())) throw std::runtime_error(Unreadable);
    return key;
}
// Present even when LoadKey then finds it unreadable: the user is told to save
// the key again rather than that there is none.
bool HasKey(const std::filesystem::path& file) {
    return GetFileAttributesW(file.c_str()) != INVALID_FILE_ATTRIBUTES;
}
void ForgetKey(const std::filesystem::path& file) {
    if (!HasKey(file)) return;
    const auto path = Checked(file);
    const auto error = Retried([&] { return DeleteFileW(path.c_str()) != 0; });
    if (error != ERROR_SUCCESS && error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
        throw std::runtime_error("Suppression de la clé Metamob impossible.");
}
}
