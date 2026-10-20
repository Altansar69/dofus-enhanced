#include "common.hpp"
#include <fstream>
#include <stdexcept>

static HMODULE selfModule;
template<class T> T Export(HMODULE module, const char* name) {
    auto p = GetProcAddress(module, name);
    if (!p) throw std::runtime_error(std::string("Missing export: ") + name);
    return reinterpret_cast<T>(p);
}
static DWORD Probe() {
    std::ofstream log(ModulePath(selfModule).parent_path() / L"runtime-probe.log", std::ios::app);
    log << "PROBE_START pid=" << GetCurrentProcessId() << std::endl;
    try {
        if (!SupportedClient(ModulePath(nullptr))) throw std::runtime_error("Unsupported client build");
        HMODULE game = GetModuleHandleW(L"GameAssembly.dll");
        if (!game) throw std::runtime_error("GameAssembly is not loaded");
        auto domainGet = Export<void*(*)()>(game, "il2cpp_domain_get");
        auto attach = Export<void*(*)(void*)>(game, "il2cpp_thread_attach");
        auto detach = Export<void(*)(void*)>(game, "il2cpp_thread_detach");
        auto assemblies = Export<const void**(*)(void*, size_t*)>(game, "il2cpp_domain_get_assemblies");
        auto imageGet = Export<void*(*)(const void*)>(game, "il2cpp_assembly_get_image");
        auto imageName = Export<const char*(*)(void*)>(game, "il2cpp_image_get_name");
        auto classGet = Export<void*(*)(void*, const char*, const char*)>(game, "il2cpp_class_from_name");
        auto methodGet = Export<const void*(*)(void*, const char*, int)>(game, "il2cpp_class_get_method_from_name");
        auto domain = domainGet();
        if (!domain) throw std::runtime_error("IL2CPP domain unavailable");
        auto thread = attach(domain);
        if (!thread) throw std::runtime_error("IL2CPP attach failed");
        size_t count = 0;
        auto list = assemblies(domain, &count);
        if (!list || count > 4096) throw std::runtime_error("Invalid assembly count");
        log << "IL2CPP_ATTACHED assemblies=" << count << std::endl;
        bool coreFound = false;
        for (size_t i = 0; i < count; ++i) {
            auto image = imageGet(list[i]);
            auto name = imageName(image);
            if (!name || std::string(name) != "Core.dll") continue;
            auto compositor = classGet(image, "Core.Engine.Compositor", "Compositor");
            auto update = compositor ? methodGet(compositor, "Update", 0) : nullptr;
            auto creatures = classGet(image, "", "exf");
            auto converter = creatures ? methodGet(creatures, "bazg", 2) : nullptr;
            log << "CORE_RESOLVED compositor=" << (compositor != nullptr)
                << " update=" << (update != nullptr) << " creature_converter=" << (converter != nullptr) << std::endl;
            coreFound = update && converter;
        }
        detach(thread);
        log << (coreFound ? "PROBE_OK" : "PROBE_INCOMPLETE") << std::endl;
        return coreFound ? 0 : 2;
    } catch (const std::exception& e) {
        log << "PROBE_ERROR " << e.what() << std::endl;
        return 1;
    }
}
static DWORD WINAPI Worker(void*) {
    DWORD result = 1;
    __try { result = Probe(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { result = GetExceptionCode(); }
    FreeLibraryAndExitThread(selfModule, result);
}
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        selfModule = module;
        DisableThreadLibraryCalls(module);
        HANDLE thread = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
        if (!thread) return FALSE;
        CloseHandle(thread);
    }
    return TRUE;
}
