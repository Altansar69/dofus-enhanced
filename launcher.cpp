#include "common.hpp"
#include "autoload.hpp"
#include "autoload-policy.hpp"
#include "version.hpp"
#include "update-launch.hpp"
#include <tlhelp32.h>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <fstream>
#include <functional>
#ifdef DOFUS_AUTOLOADER
#include <shellapi.h>
#endif

static DWORD ChooseClient() {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot) throw std::runtime_error("Cannot list processes");
    std::vector<DWORD> clients;
    PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot.value, &entry)) do {
        if (_wcsicmp(entry.szExeFile, L"Dofus.exe") == 0) clients.push_back(entry.th32ProcessID);
    } while (Process32NextW(snapshot.value, &entry));
    if (clients.empty()) throw std::runtime_error("Aucun client Dofus lance. Lance le jeu toi-meme puis reessaie.");
    std::cout << ModDisplayName << " " << ModSemanticVersion << " (" << ModVersion << ") - lanceur pour 3.7.12.12\n"
                 "Le module sera charge en memoire dans le client choisi.\n"
                 "La compilation est verifiee, les fonctions en jeu restent a tester.\n"
                 "Aucun clic, touche ou changement de fenetre ne sera effectue.\n\nClients trouves (PID) :";
    for (auto pid : clients) std::cout << " " << pid;
    std::cout << "\nEntre le PID du client a modifier (0 pour annuler) : ";
    std::string input;
    if (!std::getline(std::cin, input) || input.empty() || input == "0") return 0;
    size_t end = 0;
    auto parsed = std::stoul(input, &end);
    if (end != input.size() || parsed > MAXDWORD) throw std::runtime_error("PID invalide");
    auto selected = static_cast<DWORD>(parsed);
    if (std::find(clients.begin(), clients.end(), selected) == clients.end()) throw std::runtime_error("Ce PID ne correspond pas a Dofus");
    return selected;
}

static uintptr_t RemoteModule(DWORD pid, const std::wstring& name) {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid));
    if (!snapshot) throw std::runtime_error("Cannot enumerate target modules");
    MODULEENTRY32W entry{}; entry.dwSize = sizeof(entry);
    if (Module32FirstW(snapshot.value, &entry)) do {
        if (_wcsicmp(entry.szModule, name.c_str()) == 0) return reinterpret_cast<uintptr_t>(entry.modBaseAddr);
    } while (Module32NextW(snapshot.value, &entry));
    return 0;
}
static void WaitForStartup(HANDLE process, DWORD pid, const std::filesystem::path& folder, std::ostream& output,
    const std::function<bool()>& stillEnabled) {
    const auto started = GetTickCount64();
    auto deadline = started + 20000;
    output << "DLL chargee par Windows. Verification de l'initialisation du module...\n";
    std::string previous;
    while (GetTickCount64() < deadline) {
        if (stillEnabled && !stillEnabled()) throw std::runtime_error("Auto-load disabled while awaiting startup; DLL is already loaded, no retry");
        if (WaitForSingleObject(process, 150) == WAIT_OBJECT_0)
            throw std::runtime_error("Dofus s'est ferme pendant l'initialisation. Ne pas relancer le module; conserver module.log.");
        auto status = ReadStartupStatus(folder, pid);
        if (status != previous) {
            if (!status.empty()) output << "Etat : " << status << '\n';
            previous = status;
        }
        if (status.rfind("ERROR ", 0) == 0) throw std::runtime_error(status);
        if (status == "WAITING_ACCOUNT_OPTIONS") deadline = started + 180000;
        if (status == "STOPPED") throw std::runtime_error("Le module s'est desactive; consulter module.log.");
        if (status == "READY") {
            output << "Initialisation confirmee. Ouvre ou rouvre Options > Mod (version chargee) > General.\n"
                         "Cela ne valide pas encore le rendu ni les fonctions en combat.\n";
            return;
        }
    }
    throw std::runtime_error("Initialisation encore en attente. Termine la connexion au personnage puis consulte module.log; ne recharge pas la DLL.");
}
static uint64_t FileTimeValue(const FILETIME& value) {
    return (static_cast<uint64_t>(value.dwHighDateTime) << 32) | value.dwLowDateTime;
}
static uint64_t ProcessCreationTime(HANDLE process) {
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(process, &creation, &exit, &kernel, &user))
        throw std::runtime_error("Cannot verify process creation time");
    return FileTimeValue(creation);
}
static std::filesystem::path ProcessPath(HANDLE process) {
    std::vector<wchar_t> name(32768); DWORD size = static_cast<DWORD>(name.size());
    if (!QueryFullProcessImageNameW(process, 0, name.data(), &size)) throw std::runtime_error("Cannot verify process identity");
    return std::wstring(name.data(), size);
}
struct LoadLock {
    Handle mutex;
    bool acquired = false;
    LoadLock(DWORD pid, uint64_t creation) : mutex(CreateMutexW(nullptr, FALSE,
        (L"Local\\DofusMiniatures.Load." + std::to_wstring(pid) + L"." + std::to_wstring(creation)).c_str())) {
        if (!mutex) throw std::runtime_error("Cannot create module load lock");
        const auto result = WaitForSingleObject(mutex.value, 0);
        acquired = result == WAIT_OBJECT_0 || result == WAIT_ABANDONED;
        if (!acquired) throw std::runtime_error("Another loader already handles this client; no retry");
    }
    ~LoadLock() { if (acquired) ReleaseMutex(mutex.value); }
};
static void LoadModule(DWORD pid, const std::filesystem::path& requestedDll, std::ostream& output,
    const std::filesystem::path& expectedExe = {}, uint64_t expectedCreation = 0,
    const std::function<bool()>& stillEnabled = {}) {
    const auto dll = std::filesystem::canonical(requestedDll);
    if (dll.extension() != L".dll") throw std::runtime_error("Expected a DLL");
    Handle process(OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION |
        PROCESS_VM_READ | PROCESS_VM_WRITE | SYNCHRONIZE, FALSE, pid));
    if (!process) throw std::runtime_error("Cannot access Dofus process; no privilege escalation attempted");
    const auto creation = ProcessCreationTime(process.value);
    if (expectedCreation && creation != expectedCreation) throw std::runtime_error("Process identity changed; no module loaded");
    LoadLock loading(pid, creation);
    const auto exe = ProcessPath(process.value);
    if (!expectedExe.empty() && _wcsicmp(exe.c_str(), expectedExe.c_str()) != 0)
        throw std::runtime_error("Process path changed; no module loaded");
    if (!SupportedClient(exe)) throw std::runtime_error("Unsupported client: no module loaded");
    if (!RemoteModule(pid, L"GameAssembly.dll") || !RemoteModule(pid, L"UnityPlayer.dll"))
        throw std::runtime_error("Game runtime is not loaded");
    if (RemoteModule(pid, L"DofusMiniatures.dll") || RemoteModule(pid, dll.filename().wstring()))
        throw std::runtime_error("Module already loaded");
    auto localLoad = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    HMODULE owner = nullptr;
    if (!localLoad || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(localLoad), &owner))
        throw std::runtime_error("Cannot resolve Windows loader");
    const auto remoteOwner = RemoteModule(pid, ModulePath(owner).filename().wstring());
    if (!remoteOwner) throw std::runtime_error("Windows loader module unavailable");
    const auto remoteLoad = reinterpret_cast<LPTHREAD_START_ROUTINE>(remoteOwner +
        reinterpret_cast<uintptr_t>(localLoad) - reinterpret_cast<uintptr_t>(owner));
    const auto checkEnabled = [&] {
        if (stillEnabled && !stillEnabled()) throw std::runtime_error("Auto-load disabled; no module loaded");
        if (WaitForSingleObject(process.value, 0) != WAIT_TIMEOUT) throw std::runtime_error("Client exited; no module loaded");
    };
    checkEnabled();
    std::filesystem::remove(dll.parent_path() / L"stop.request");
    std::filesystem::remove(StartupStatusPath(dll.parent_path(), pid));
    const auto bytes = (dll.native().size() + 1) * sizeof(wchar_t);
    checkEnabled();
    const auto remote = VirtualAllocEx(process.value, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) throw std::runtime_error("Cannot allocate module path");
    try { checkEnabled(); }
    catch (...) { VirtualFreeEx(process.value, remote, 0, MEM_RELEASE); throw; }
    SIZE_T written = 0;
    if (!WriteProcessMemory(process.value, remote, dll.c_str(), bytes, &written) || written != bytes) {
        VirtualFreeEx(process.value, remote, 0, MEM_RELEASE);
        throw std::runtime_error("Cannot copy module path");
    }
    try { checkEnabled(); }
    catch (...) { VirtualFreeEx(process.value, remote, 0, MEM_RELEASE); throw; }
    Handle thread(CreateRemoteThread(process.value, nullptr, 0, remoteLoad, remote, 0, nullptr));
    if (!thread) {
        VirtualFreeEx(process.value, remote, 0, MEM_RELEASE);
        throw std::runtime_error("Module loading denied");
    }
    const auto wait = WaitForSingleObject(thread.value, 10000);
    // A still-running LoadLibraryW owns the remote path; freeing it here could
    // crash the client. An uncertain outcome must never trigger a second load.
    if (wait != WAIT_OBJECT_0) throw std::runtime_error("Module load still pending: inspect log before retrying");
    DWORD result = 0;
    const auto gotResult = GetExitCodeThread(thread.value, &result);
    VirtualFreeEx(process.value, remote, 0, MEM_RELEASE);
    if (WaitForSingleObject(process.value, 0) == WAIT_OBJECT_0)
        throw std::runtime_error("Dofus s'est ferme pendant le chargement. Conserver module.log.");
    if (!gotResult || !result) throw std::runtime_error("LoadLibrary failed");
    WaitForStartup(process.value, pid, dll.parent_path(), output, stillEnabled);
}
static void LogEvent(std::ostream& log, const char* event, DWORD pid = 0) {
    SYSTEMTIME time{}; GetLocalTime(&time);
    log << time.wYear << '-' << time.wMonth << '-' << time.wDay << ' ' << time.wHour << ':' << time.wMinute << ':' << time.wSecond
        << ' ' << event;
    if (pid) log << " pid=" << pid;
    log << '\n'; log.flush();
}
static int Watch(const std::filesystem::path& folder, const std::filesystem::path& clientExe) {
    std::ofstream log(folder / L"autoload.log", std::ios::app);
    if (!log) throw std::runtime_error("Cannot open autoload.log");
    try {
        if (!AutoLoadEnabled(folder, clientExe)) { LogEvent(log, "DISABLED no startup entry owned by this helper"); return 0; }
        Handle singleton(CreateMutexW(nullptr, FALSE, AutoLoadMutexName(folder).c_str()));
        if (!singleton) throw std::runtime_error("Cannot create auto-loader mutex");
        const auto singletonWait = WaitForSingleObject(singleton.value, 5000);
        if (singletonWait == WAIT_TIMEOUT) { LogEvent(log, "ALREADY_RUNNING"); return 0; }
        if (singletonWait != WAIT_OBJECT_0 && singletonWait != WAIT_ABANDONED) throw std::runtime_error("Cannot acquire auto-loader mutex");
        struct Release { HANDLE value; ~Release() { ReleaseMutex(value); } } release{singleton.value};
        FILETIME started{}; GetSystemTimeAsFileTime(&started);
        AutoLoadPolicy policy(FileTimeValue(started));
        LogEvent(log, "WATCH_STARTED pre-existing clients ignored; 25s grace; one attempt per new process");
        const auto enabled = [&] { return AutoLoadEnabled(folder, clientExe); };
        while (enabled()) {
            Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
            if (!snapshot) throw std::runtime_error("Cannot list processes");
            PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
            if (Process32FirstW(snapshot.value, &entry)) do {
                if (_wcsicmp(entry.szExeFile, L"Dofus.exe") != 0) continue;
                const auto pid = entry.th32ProcessID;
                Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid));
                if (!process) continue;
                uint64_t creation = 0;
                try {
                    if (_wcsicmp(ProcessPath(process.value).c_str(), clientExe.c_str()) != 0) continue;
                    creation = ProcessCreationTime(process.value);
                } catch (...) { continue; }
                const auto now = GetTickCount64();
                const bool alive = WaitForSingleObject(process.value, 0) == WAIT_TIMEOUT;
                // Check the pure policy before inspecting modules so old clients
                // are ignored entirely and never considered load candidates.
                auto decision = policy.Observe(pid, creation, now, alive, false, enabled());
                if (decision == AutoLoadPolicy::Decision::Disabled) break;
                if (decision == AutoLoadPolicy::Decision::Expired) { LogEvent(log, "SKIPPED startup deadline", pid); continue; }
                if (decision != AutoLoadPolicy::Decision::Wait) continue;
                bool ready = false;
                try {
                    ready = RemoteModule(pid, L"GameAssembly.dll") && RemoteModule(pid, L"UnityPlayer.dll") &&
                        WaitForInputIdle(process.value, 0) == 0;
                } catch (...) { /* Module snapshots may fail transiently during startup. */ }
                decision = policy.Observe(pid, creation, now, alive, ready, enabled());
                if (decision != AutoLoadPolicy::Decision::Attempt) continue;
                try {
                    CheckGithubUpdates(folder, clientExe, log, enabled);
                    if (!enabled() || WaitForSingleObject(process.value, 0) != WAIT_TIMEOUT) continue;
                    const auto selectedRelease = LatestReleaseForClient(folder, clientExe);
                    const auto pathText = selectedRelease.folder.u8string();
                    log << "LOCAL_RELEASE r" << selectedRelease.revision << " folder=" << std::string(pathText.begin(), pathText.end()) << '\n';
                    log.flush();
                    LogEvent(log, "LOAD_ATTEMPT", pid);
                    if (DispatchVerifiedLoader(selectedRelease.folder, pid, creation, clientExe, log) != 0)
                        throw std::runtime_error("Selected loader did not confirm startup; inspect its autoload.log and module.log");
                    LogEvent(log, "LOAD_READY", pid);
                } catch (const std::exception& error) {
                    LogEvent(log, "LOAD_NOT_CONFIRMED no retry for this process", pid);
                    log << error.what() << '\n'; log.flush();
                }
            } while (Process32NextW(snapshot.value, &entry));
            Sleep(1000);
        }
        LogEvent(log, "WATCH_STOPPED startup option disabled or another installation selected");
        return 0;
    } catch (const std::exception& error) {
        LogEvent(log, "WATCH_ERROR"); log << error.what() << '\n'; log.flush(); return 1;
    }
}
int wmain(int argc, wchar_t** argv) {
    try {
        std::wstring selectedPid, selectedDll;
        wchar_t* selectedArgs[3]{};
        if (argc == 2 && std::wstring(argv[1]) == L"--interactive") {
            auto chosen = ChooseClient();
            if (!chosen) { std::cout << "Annule. Aucun module charge.\n"; return 0; }
            selectedPid = std::to_wstring(chosen);
            const auto release = LatestLocalRelease(ModulePath(nullptr).parent_path());
            std::cout << "Revision locale compatible choisie : r" << release.revision << '\n';
            const auto logPath = (release.folder / L"module.log").u8string();
            std::cout << "Journal du module : " << std::string(logPath.begin(), logPath.end()) << '\n';
            selectedDll = (release.folder / L"DofusMiniatures.dll").wstring();
            selectedArgs[0] = argv[0]; selectedArgs[1] = selectedPid.data(); selectedArgs[2] = selectedDll.data();
            argv = selectedArgs; argc = 3;
        }
        if (argc == 2 && std::wstring(argv[1]) == L"--help") {
            std::cout << "ModuleLoader --interactive\nModuleLoader <Dofus PID> <absolute module DLL>\n"
                         "ModuleLoader --verify-files <GameAssembly.dll> <global-metadata.dat>\n"
                         "ModuleLoader --watch <absolute Dofus.exe>\n"
                         "  Enabled only by Options > Mod; watches newly started clients, never existing ones.\n"
                         "  Chooses the newest compatible sibling rN folder using release.ini and SHA256.\n"
                         "  Local files only; no downloads or automatic changes to an already running game.\n"
                         "ModuleLoader --disable-auto\n"
                         "  Removes only this release family's startup entry; no client is accessed.\n"
                         "ModuleLoader --enable-auto <absolute Dofus.exe>\n"
                         "  Registers this verified helper for future launches; existing clients are ignored.\n"
                         "ModuleLoader --check-updates <absolute Dofus.exe>\n"
                         "  Checks GitHub and installs a verified compatible package; never accesses a game process.\n"
                         "ModuleLoader --resolve-client <absolute Dofus.exe>\n"
                         "  Reports the newest verified package matching installed files, without loading it.\n"
                         "ModuleLoader --stop-local-releases\n"
                         "  Writes stop.request in verified sibling releases only; no process is accessed.\n";
            return 0;
        }
        if (argc == 4 && std::wstring(argv[1]) == L"--verify-files") {
            if (FileHash(argv[2]) != ExpectedGameHash || FileHash(argv[3]) != ExpectedMetadataHash)
                throw std::runtime_error("Unsupported snapshot: no process accessed");
            std::cout << "Supported snapshot 3.7.12.12. Files only; no process accessed.\n";
            return 0;
        }
        if (argc == 3 && (std::wstring(argv[1]) == L"--check-updates" ||
                          std::wstring(argv[1]) == L"--resolve-client")) {
            const std::filesystem::path client(argv[2]);
            if (!client.is_absolute() || _wcsicmp(client.filename().c_str(), L"Dofus.exe") != 0)
                throw std::runtime_error("Exact absolute Dofus.exe path required");
            const auto folder = ModulePath(nullptr).parent_path();
            if (std::wstring(argv[1]) == L"--check-updates")
                CheckGithubUpdates(folder, client, std::cout);
            const auto selected = LatestReleaseForClient(folder, client);
            std::cout << "Compatible local release r" << selected.revision << "; files only, no process accessed.\n";
            return 0;
        }
        if (argc == 5 && std::wstring(argv[1]) == L"--auto-load") {
            const auto folder = ModulePath(nullptr).parent_path();
            const std::filesystem::path client(argv[4]);
            wchar_t *pidEnd = nullptr, *timeEnd = nullptr;
            const auto pid = wcstoull(argv[2], &pidEnd, 10);
            const auto creation = wcstoull(argv[3], &timeEnd, 10);
            if (!pid || pid > MAXDWORD || *pidEnd || !creation || *timeEnd ||
                !client.is_absolute() || !VerifiedReleaseForClient(folder, client))
                throw std::runtime_error("Invalid verified loader request");
            const auto enabled = [&] { return AutoLoadEnabled(folder, client); };
            if (!enabled()) throw std::runtime_error("Automatic loading disabled");
            LoadModule(static_cast<DWORD>(pid), folder / L"DofusMiniatures.dll", std::cout,
                client, creation, enabled);
            return 0;
        }
        if (argc == 2 && std::wstring(argv[1]) == L"--disable-auto") {
            const auto folder = ModulePath(nullptr).parent_path();
            const bool owned = AutoLoadOwnedByFolder(folder);
            DisableAutoLoadFromFolder(folder);
            std::cout << (owned ? "Chargement automatique desactive pour cette famille de revisions.\n" :
                "Aucun chargement automatique enregistre pour cette famille. Les autres installations restent inchangees.\n")
                << "Le client ouvert reste inchange.\n";
            return 0;
        }
        if (argc == 3 && std::wstring(argv[1]) == L"--enable-auto") {
            const std::filesystem::path client(argv[2]);
            if (!client.is_absolute() || _wcsicmp(client.filename().c_str(), L"Dofus.exe") != 0)
                throw std::runtime_error("Auto-loader requires the exact absolute Dofus.exe path");
            ConfigureAutoLoad(true, ModulePath(nullptr).parent_path(), client.lexically_normal());
            std::cout << "Chargement automatique configure pour les prochains lancements.\n"
                         "Les clients deja ouverts restent inchanges.\n";
            return 0;
        }
        if (argc == 2 && std::wstring(argv[1]) == L"--stop-local-releases") {
            const auto count = RequestStopLocalReleases(ModulePath(nullptr).parent_path());
            std::cout << "Demande d'arret ecrite dans " << count << " revision(s) locale(s) verifiee(s).\n"
                         "Aucun processus consulte. Le module traite la demande a la prochaine frame.\n";
            return 0;
        }
        if (argc == 3 && std::wstring(argv[1]) == L"--watch") {
            const std::filesystem::path client(argv[2]);
            if (!client.is_absolute() || _wcsicmp(client.filename().c_str(), L"Dofus.exe") != 0)
                throw std::runtime_error("Auto-loader requires the exact absolute Dofus.exe path");
            return Watch(ModulePath(nullptr).parent_path(), client.lexically_normal());
        }
        if (argc != 3) throw std::runtime_error("Usage: ModuleLoader <Dofus PID> <absolute module DLL>");
        wchar_t* end = nullptr;
        const auto parsed = wcstoull(argv[1], &end, 10);
        if (!parsed || *end || parsed > MAXDWORD) throw std::runtime_error("Invalid PID");
        LoadModule(static_cast<DWORD>(parsed), argv[2], std::cout);
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}

#ifdef DOFUS_AUTOLOADER
// Windows starts this GUI-subsystem helper directly at logon, so no console is
// created even before our code runs. All output goes to autoload.log. The manual
// console loader keeps its existing entrypoint, stdin and redirected output.
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0;
    auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return 1;
    int result = 1;
    if (argc == 3 && std::wstring(argv[1]) == L"--watch") result = wmain(argc, argv);
    LocalFree(argv);
    return result;
}
#endif
