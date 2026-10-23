#include "metamob/metamob-secret.hpp"
#include "metamob/metamob-heap-probe.hpp"
#include <Windows.h>
#include <winioctl.h>
#include <dpapi.h>
#include <atomic>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <new>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {
using Path = std::filesystem::path;
// Fake keys only: a real key never appears in a test.
constexpr char FakeKey[] = "TEST-KEY-0123456789-NOT-REAL", OtherKey[] = "TEST-KEY-9876543210-NOT-REAL",
    Entropy[] = "DofusEnhanced/Metamob/v1";
// The store's fixed messages.
constexpr char Invalid[] = "Clé Metamob invalide.", Unreachable[] = "Emplacement de la clé Metamob inaccessible.",
    Link[] = "Lien de fichier non pris en charge pour la clé Metamob.",
    BadFile[] = "Fichier de la clé Metamob invalide ou trop volumineux.",
    Unreadable[] = "Clé Metamob illisible sur ce compte Windows.",
    Replace[] = "Remplacement de la clé Metamob impossible. Réessaie.", Removal[] = "Suppression de la clé Metamob impossible.";
std::string folder; // the test folder's name, which no message may contain
void Check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
// True when any `width`-byte slice of the secret appears in the text.
bool Leaks(std::string_view text, std::string_view secret, size_t width) {
    for (size_t i = 0; i + width <= secret.size(); ++i) if (text.find(secret.substr(i, width)) != std::string_view::npos) return true;
    return false;
}
// Exactly the expected fixed message: no path, no Windows text, no key fragment.
template<class Fn> void Reject(Fn action, const char* expected, const char* message) {
    std::string what;
    try { action(); } catch (const std::exception& error) { what = error.what(); }
    Check(what == expected, message);
    Check(!Leaks(what, FakeKey, 4) && !Leaks(what, OtherKey, 4) && what.find(folder) == std::string::npos &&
        what.find('\\') == std::string::npos, "An error message carries a key fragment or a path");
}
std::string Bytes(const Path& path) {
    std::ifstream input(path, std::ios::binary); return {std::istreambuf_iterator<char>(input), {}};
}
void Put(const Path& path, const std::string& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size())); output.flush();
    Check(output.good(), "Fixture file");
}
bool Temporaries(const Path& directory) {
    for (const auto& entry : std::filesystem::directory_iterator(directory))
        if (entry.path().filename().wstring().find(L".tmp-") != std::wstring::npos) return true;
    return false;
}
bool IsLink(const Path& path) {
    const auto attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_REPARSE_POINT);
}
DATA_BLOB Blob(const std::string& bytes) { return {static_cast<DWORD>(bytes.size()), reinterpret_cast<BYTE*>(const_cast<char*>(bytes.data()))}; }
// A blob sealed for this Windows user with the given entropy (nullptr: none).
std::string Seal(const std::string& plain, const char* entropy) {
    const std::string salt = entropy ? entropy : "";
    auto data = Blob(plain), extra = Blob(salt);
    DATA_BLOB sealed{};
    Check(CryptProtectData(&data, nullptr, entropy ? &extra : nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &sealed) != 0, "DPAPI fixture");
    std::string blob(reinterpret_cast<const char*>(sealed.pbData), sealed.cbData);
    LocalFree(sealed.pbData);
    return blob;
}
// Opens a blob with the given entropy and tells whether it carries a description.
bool Open(const std::string& blob, const char* entropy, std::string& plain, bool& described) {
    const std::string salt = entropy ? entropy : "";
    auto data = Blob(blob), extra = Blob(salt);
    DATA_BLOB opened{}; LPWSTR description = nullptr;
    if (!CryptUnprotectData(&data, &description, entropy ? &extra : nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &opened)) return false;
    described = description && *description; LocalFree(description);
    plain.assign(reinterpret_cast<const char*>(opened.pbData), opened.cbData);
    SecureZeroMemory(opened.pbData, opened.cbData); LocalFree(opened.pbData);
    return true;
}
// A directory junction: a reparse point any user can create, unlike a symbolic
// link (privilege or developer mode). ntifs.h's REPARSE_DATA_BUFFER, mount point form.
bool Junction(const Path& link, const Path& target) {
    if (!CreateDirectoryW(link.c_str(), nullptr)) return false;
    const auto print = target.wstring(), substitute = L"\\??\\" + print;
    const auto names = (substitute.size() + 1 + print.size() + 1) * sizeof(wchar_t);
    std::vector<unsigned char> buffer(16 + names);
    const auto put = [&](size_t offset, size_t value) { const auto field = static_cast<USHORT>(value); std::memcpy(buffer.data() + offset, &field, sizeof(field)); };
    const DWORD tag = IO_REPARSE_TAG_MOUNT_POINT; std::memcpy(buffer.data(), &tag, sizeof(tag));
    put(4, 8 + names); // ReparseDataLength
    put(8, 0); put(10, substitute.size() * sizeof(wchar_t)); // SubstituteName
    put(12, (substitute.size() + 1) * sizeof(wchar_t)); put(14, print.size() * sizeof(wchar_t)); // PrintName
    std::memcpy(buffer.data() + 16, substitute.c_str(), (substitute.size() + 1) * sizeof(wchar_t));
    std::memcpy(buffer.data() + 16 + (substitute.size() + 1) * sizeof(wchar_t), print.c_str(), (print.size() + 1) * sizeof(wchar_t));
    const auto handle = CreateFileW(link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    DWORD returned = 0;
    const bool made = DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, buffer.data(), static_cast<DWORD>(buffer.size()), nullptr, 0, &returned, nullptr) != 0;
    CloseHandle(handle);
    return made && IsLink(link);
}
}
int main() {
    const auto root = std::filesystem::temp_directory_path() / ("DofusMetamobSecretTests-" + std::to_string(GetCurrentProcessId()));
    const auto file = metamob::KeyPath(root / "DofusMiniatures-3.7.11.11-settings.ini");
    folder = root.filename().string();
    try {
        std::filesystem::remove_all(root); std::filesystem::create_directory(root);
        Check(file == root / "DofusMiniatures-metamob.key" && metamob::KeyPath(L"C:\\Jeu\\settings.ini") == L"C:\\Jeu\\DofusMiniatures-metamob.key",
            "The key file sits next to the active settings file");

        // Nothing stored yet, even under a missing folder.
        const auto absent = metamob::KeyPath(root / "absent" / "settings.ini");
        Check(!metamob::HasKey(file) && !metamob::LoadKey(file) && !metamob::HasKey(absent) && !metamob::LoadKey(absent), "No key file yet");
        metamob::ForgetKey(file); metamob::ForgetKey(absent);
        Reject([&] { metamob::StoreKey(absent, metamob::Secret(FakeKey)); }, Unreachable, "A missing folder is refused");

        // Invalid keys (the caller trims) are refused before anything is written.
        const std::string invalid[] = {"", std::string(15, 'k'), std::string(257, 'k'), "TEST KEY 0123456789", std::string(FakeKey) + "\r\n",
            " " + std::string(FakeKey), "TEST-KEY\t0123456789", "TEST-KEY-0123456789\x7f", "TEST-KEY-0123456789-clé",
            std::string("TEST-KEY-01234\0" "56789", 20)};
        for (const auto& bad : invalid) Reject([&] { metamob::StoreKey(file, metamob::Secret(bad)); }, Invalid, "An invalid key is refused");
        Check(!metamob::HasKey(file) && std::filesystem::is_empty(root), "A refused key writes nothing");

        // Round trip: DPAPI for this Windows user with the fixed entropy, behind the magic header.
        metamob::StoreKey(file, metamob::Secret(FakeKey));
        const auto loaded = metamob::LoadKey(file);
        Check(metamob::HasKey(file) && loaded && loaded->value() == FakeKey, "The stored key loads back");
        const auto stored = Bytes(file);
        std::string wide; for (const char c : std::string_view(FakeKey)) { wide.push_back(c); wide.push_back('\0'); }
        Check(stored.size() > 6 && stored.size() <= 4096 && stored.compare(0, 6, "DEMK1\n") == 0, "Magic header then a bounded blob");
        Check(!Leaks(stored, FakeKey, 6) && !Leaks(stored, wide, 12), "The key file holds no plain key");
        std::string opened; bool described = true;
        Check(Open(stored.substr(6), Entropy, opened, described) && opened == FakeKey && !described, "Blob with the fixed entropy and no description");
        Check(!Open(stored.substr(6), nullptr, opened, described) && !Open(stored.substr(6), "DofusEnhanced/Metamob/v2", opened, described),
            "The blob needs the fixed entropy");
        Check(!Temporaries(root), "No temporary file left");

        // 16 and 256 bytes, then a replacement.
        for (const size_t size : {size_t{16}, size_t{256}}) {
            std::string bound(size, 'k'); bound.front() = '!'; bound.back() = '~';
            metamob::StoreKey(file, metamob::Secret(bound));
            const auto back = metamob::LoadKey(file);
            Check(back && back->value() == bound && Bytes(file).size() <= 4096, "Key length bounds");
        }
        metamob::StoreKey(file, metamob::Secret(OtherKey));
        const auto replaced = metamob::LoadKey(file);
        Check(replaced && replaced->value() == OtherKey && !Temporaries(root), "A new key replaces the old one");

        // Failures keep the exact previous file and leave no temporary file.
        const auto before = Bytes(file);
        Reject([&] { metamob::StoreKey(file, metamob::Secret("too-short")); }, Invalid, "An invalid key keeps the stored one");
        Check(SetFileAttributesW(file.c_str(), FILE_ATTRIBUTE_READONLY) != 0, "Fixture read-only");
        Reject([&] { metamob::StoreKey(file, metamob::Secret(FakeKey)); }, Replace, "A failed replacement is reported");
        Reject([&] { metamob::ForgetKey(file); }, Removal, "A failed removal is reported");
        SetFileAttributesW(file.c_str(), FILE_ATTRIBUTE_NORMAL);
        Check(Bytes(file) == before && !Temporaries(root), "Failed writes keep the previous key file");

        // Unreadable files: refused with a fixed message and kept, until a new save replaces them.
        const auto blob = Seal(FakeKey, Entropy);
        Put(file, "DEMK1\n" + blob);
        const auto sealed = metamob::LoadKey(file);
        Check(sealed && sealed->value() == FakeKey, "A blob sealed like the store loads");
        auto middle = blob, last = blob;
        middle[middle.size() / 2] = static_cast<char>(middle[middle.size() / 2] ^ 0x5A);
        last.back() = static_cast<char>(last.back() ^ 0x01);
        const std::pair<std::string, const char*> unreadable[] = {
            {"DEMK2\n" + blob, BadFile}, {blob, BadFile}, {"", BadFile}, {"DEMK1\n", BadFile},
            {"DEMK1\n" + blob + std::string(4097 - 6 - blob.size(), '\0'), BadFile}, // 4097 bytes
            {"DEMK1\n" + std::string(4090, 'x'), Unreadable}, // 4096 bytes: read, then refused by DPAPI
            {"DEMK1\n" + middle, Unreadable}, {"DEMK1\n" + last, Unreadable}, {"DEMK1\n" + blob.substr(0, blob.size() / 2), Unreadable},
            {"DEMK1\n" + Seal(FakeKey, nullptr), Unreadable}, {"DEMK1\n" + Seal(FakeKey, "DofusEnhanced/Metamob/v2"), Unreadable},
            {"DEMK1\n" + Seal("short", Entropy), Unreadable}, {"DEMK1\n" + Seal(std::string(FakeKey) + " ", Entropy), Unreadable}};
        for (const auto& [bytes, expected] : unreadable) {
            Put(file, bytes);
            Check(metamob::HasKey(file), "An unreadable key file is still present");
            Reject([&] { (void)metamob::LoadKey(file); }, expected, "An unreadable key file is refused");
            Check(Bytes(file) == bytes, "An unreadable key file is kept");
        }
        metamob::StoreKey(file, metamob::Secret(FakeKey));
        const auto saved = metamob::LoadKey(file);
        Check(saved && saved->value() == FakeKey, "Saving again replaces an unreadable key file");
        Put(file, "DEMK1\n" + middle);
        metamob::ForgetKey(file);
        Check(!metamob::HasKey(file), "An unreadable key file can be forgotten");

        // A folder where the key file goes is never read, replaced or removed.
        const auto holder = metamob::KeyPath(root / "folder" / "settings.ini");
        std::filesystem::create_directories(holder);
        Check(metamob::HasKey(holder), "A folder at the key path is present");
        Reject([&] { (void)metamob::LoadKey(holder); }, BadFile, "A folder is not a key file");
        Reject([&] { metamob::StoreKey(holder, metamob::Secret(FakeKey)); }, BadFile, "A folder is not replaced");
        Reject([&] { metamob::ForgetKey(holder); }, BadFile, "A folder is not removed");
        Check(std::filesystem::is_directory(holder), "The folder is kept");

        // Links: a junction on a parent folder, then at the key file itself.
        const auto real = root / "real", link = root / "link", target = root / "target", parent = root / "parent";
        for (const auto& made : {real, target, parent}) std::filesystem::create_directory(made);
        Check(Junction(link, real), "Junction fixture");
        const auto linked = metamob::KeyPath(link / "settings.ini"), direct = metamob::KeyPath(real / "settings.ini");
        Check(!metamob::HasKey(linked) && !metamob::LoadKey(linked), "No key behind the linked folder yet");
        Reject([&] { metamob::StoreKey(linked, metamob::Secret(FakeKey)); }, Link, "Saving through a linked folder is refused");
        Check(std::filesystem::is_empty(real), "Nothing is written through a link");
        metamob::StoreKey(direct, metamob::Secret(FakeKey));
        Check(metamob::HasKey(linked), "The real key is seen through the link");
        Reject([&] { (void)metamob::LoadKey(linked); }, Link, "Reading through a linked folder is refused");
        Reject([&] { metamob::ForgetKey(linked); }, Link, "Removing through a linked folder is refused");
        const auto untouched = metamob::LoadKey(direct);
        Check(untouched && untouched->value() == FakeKey, "The real key is untouched");
        const auto linkedFile = metamob::KeyPath(parent / "settings.ini");
        Check(Junction(linkedFile, target), "Junction fixture");
        Check(metamob::HasKey(linkedFile), "A link at the key path is present");
        Reject([&] { (void)metamob::LoadKey(linkedFile); }, Link, "A linked key file is not read");
        Reject([&] { metamob::StoreKey(linkedFile, metamob::Secret(FakeKey)); }, Link, "A linked key file is not replaced");
        Reject([&] { metamob::ForgetKey(linkedFile); }, Link, "A linked key file is not removed");
        Check(IsLink(linkedFile) && std::filesystem::is_empty(target) && !Temporaries(parent), "The link and its target are untouched");
        RemoveDirectoryW(linkedFile.c_str()); RemoveDirectoryW(link.c_str());

        // Forget removes the file; nothing left is still fine.
        metamob::StoreKey(file, metamob::Secret(FakeKey));
        metamob::ForgetKey(file);
        Check(!metamob::HasKey(file) && !metamob::LoadKey(file) && !std::filesystem::exists(file), "Forget removes the key file");
        metamob::ForgetKey(file);

        // Several game clients share the file: while two of them replace it, a
        // load sees one whole key or the other, never a partial file.
        metamob::StoreKey(file, metamob::Secret(FakeKey));
        std::atomic_bool failed{false};
        const auto writer = [&](const char* key) {
            try { for (int i = 0; i < 20; ++i) metamob::StoreKey(file, metamob::Secret(key)); } catch (...) { failed = true; }
        };
        std::thread first(writer, FakeKey), second(writer, OtherKey), reader([&] {
            try {
                for (int i = 0; i < 60; ++i) {
                    const auto current = metamob::LoadKey(file);
                    if (!current || (current->value() != FakeKey && current->value() != OtherKey)) failed = true;
                }
            } catch (...) { failed = true; }
        });
        first.join(); second.join(); reader.join();
        Check(!failed && !Temporaries(root), "Concurrent clients replace and read whole keys");

        // Secret: moved without a copy, wiped when moved from or destroyed, heap
        // buffer included (every valid key is on the heap); same for the copies
        // StoreKey and LoadKey make. FakeKey/OtherKey are static, never heap copies.
        heapprobe::Arm();
        const auto clean = [](const void* object, size_t size, std::string_view secret) {
            return !Leaks(std::string_view(static_cast<const char*>(object), size), secret, 4);
        };
        metamob::Secret none;
        Check(none.empty() && none.value().empty(), "A default secret is empty");
        metamob::Secret a(FakeKey), b(std::move(a));
        Check(b.value() == FakeKey && a.empty() && clean(&a, sizeof(a), FakeKey), "Move construction transfers and wipes");
        metamob::Secret c(OtherKey);
        c = std::move(b);
        Check(c.value() == FakeKey && b.empty() && clean(&b, sizeof(b), FakeKey), "Move assignment transfers and wipes");
        auto& alias = c; c = std::move(alias);
        Check(c.value() == FakeKey, "A self move keeps the key");
        metamob::Secret inlined("SHORT-SECRET-15"), taken(std::move(inlined));
        Check(taken.value() == "SHORT-SECRET-15" && inlined.empty() && clean(&inlined, sizeof(inlined), "SHORT-SECRET-15"),
            "An inline secret is wiped when moved");
        for (const std::string_view value : {std::string_view(FakeKey), std::string_view("SHORT-SECRET-15")}) {
            alignas(metamob::Secret) unsigned char storage[sizeof(metamob::Secret)];
            std::string grown; for (const char ch : value) grown.push_back(ch); // through the inline buffer first
            auto* placed = new (storage) metamob::Secret(std::move(grown));
            Check(placed->value() == value, "Placed secret");
            placed->~Secret();
            Check(clean(storage, sizeof(storage), value), "A destroyed secret leaves no byte of its key");
        }
        metamob::StoreKey(file, metamob::Secret(FakeKey));
        {
            const auto back = metamob::LoadKey(file);
            Check(back && back->value() == FakeKey, "The stored key loads back");
        }
        Check(heapprobe::Disarm() == 0, "A heap block was released with a plaintext copy of the key");

        std::filesystem::remove_all(root);
        std::cout << "PASS: Metamob key store: DPAPI round trip, no plain key on disk, fixed messages, corrupt, oversized, "
            "wrong header and linked files refused, atomic replace, forget, wiped secrets and heap copies\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        SetFileAttributesW(file.c_str(), FILE_ATTRIBUTE_NORMAL);
        std::error_code ignored; std::filesystem::remove_all(root, ignored);
        return 1;
    }
}
