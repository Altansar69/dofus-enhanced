// CustomCursorTests (offline): custom-cursor.cpp on a fake IL2CPP runtime,
// with a fake Windows clock, primary button, window scale and pointer size.
// No client binary is loaded, no process opened, no cursor changed: the
// detour's trampoline and the game's DofusCursor are fakes that record what
// they get. Every native call the module makes must have been verified at
// install (FindVerified at its pinned RVA, every RVA guarded in
// signatures.hpp). The cursors' pixels come from cursorArt::Render and are
// only compared with it, byte for byte: these tests never depend on the art.
#include "core/module.hpp"
#include "core/signatures.hpp"
#include "appearance/features.hpp"
#include "ui/options-ui.hpp"
#include "ui/cursor-art.hpp"
#include "ui/custom-cursor-policy.hpp"
#include "ui/custom-cursor-player.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

namespace fake {
using namespace managed;
const std::string Ui = "UnityEngine.UIElements.";
const std::string UiImage = "UnityEngine.UIElementsModule.dll", CoreImage = "UnityEngine.CoreModule.dll";
// DofusCursor's requests come back from ApplyCursor here; UI Toolkit's
// CursorManager.SetCursor calls from elsewhere.
constexpr uintptr_t ApplyCursorReturn = 0x280A741, CursorManagerReturn = 0x5752D85;
struct Class { std::string name; };
struct MethodData { std::string type, name; int argc = 0; uintptr_t rva = 0; };
struct FieldInfo { std::string type, name; size_t offset = 0; };
// A managed object laid out as IL2CPP's: its class at 0, its monitor at 8, its
// fields from 0x10 (the module reads m_CachedPtr, the Cursor's layer, type and
// state, and isDragging there); the fake's own state after.
struct Object {
    Class* klass = nullptr;
    void* monitor = nullptr;
    alignas(8) unsigned char fields[0x30]{};
    Obj parent = nullptr, panel = nullptr, style = nullptr, owner = nullptr;
    std::string text;
    bool enabled = true, allocated = false;
    std::map<std::string, std::array<float, 4>> variables; // USS variables declared on an element
    int width = 0, height = 0, format = -1, hideFlags = 0;
    bool mipChain = true, linear = true, updateMipmaps = true, noLongerReadable = true, applied = false, destroyed = false;
    std::vector<uint8_t> pixels;
};
std::map<std::string, std::unique_ptr<Class>> classes;
std::vector<std::unique_ptr<Object>> objects;
std::vector<std::unique_ptr<MethodData>> methods;
std::vector<std::unique_ptr<FieldInfo>> fieldInfos;
std::map<GCHandle, Obj> handles;
GCHandle handleId = 0x500;
// The game's class chains, for class_is_assignable_from and the methods' owners.
const std::map<std::string, std::string> bases{
    {Ui + "TextField", Ui + "TextInputBaseField`1"}, {Ui + "TextInputBaseField`1", Ui + "BaseField`1"},
    {Ui + "BaseField`1", Ui + "BindableElement"}, {Ui + "BindableElement", Ui + "VisualElement"},
    {Ui + "TextElement", Ui + "BindableElement"}, {"Core.UILogic.Components.DofusChatTextField", Ui + "TextField"},
    {Ui + "TemplateContainer", Ui + "BindableElement"}, {"gmy", Ui + "VisualElement"},
    {Ui + "RuntimePanel", Ui + "BaseRuntimePanel"}, {Ui + "BaseRuntimePanel", Ui + "Panel"}, {Ui + "Panel", Ui + "BaseVisualElementPanel"},
    {"UnityEngine.Texture2D", "UnityEngine.Texture"}, {"UnityEngine.Texture", "UnityEngine.Object"}};
// What the module asked for.
std::set<std::tuple<std::string, std::string, int, uintptr_t>> contracts;
std::set<std::pair<std::string, std::string>> classLookups, fieldLookups;
std::set<uintptr_t> natives;
struct Hook { uintptr_t rva; const MethodData* method; size_t contracts, natives, fields; };
std::vector<Hook> hooks;
std::map<std::string, int> invokes;
std::vector<std::string> logs, events;
// Failure injection.
std::set<std::string> missingClasses;
std::set<uintptr_t> failVerified;
uintptr_t nativeMismatch = 0;
int ctorFails = 0, destroyFails = 0; // a count, -1: always
bool updateFails = false, topFails = false, staticReadFails = false;
int destroyAttempts = 0, serviceLookups = 0, staticReads = 0, parentCalls = 0, topCalls = 0;
// The layouts the module checks, (class, field) → offset.
std::map<std::pair<std::string, std::string>, size_t> offsets;
void DefaultOffsets() {
    offsets = {{{"Core.UILogic.DofusCursor", "m_currentCursor"}, 0x8}, {{"Core.UILogic.Cursor", "layerEnum"}, 0x10},
        {{"Core.UILogic.Cursor", "cursorType"}, 0x18}, {{"Core.UILogic.Cursor", "cursorState"}, 0x20},
        {{"UnityEngine.Object", "m_CachedPtr"}, 0x10},
        {{"Core.UILogic.Components.DragAndDrop.DragAndDropService", "<isDragging>k__BackingField"}, 0x10}};
}
// The game: DofusCursor's current request (m_currentCursor), its textures per
// type and their hotspot, the UI under the pointer, the drag service.
Obj currentCursor = nullptr, dragService = nullptr, compositor = nullptr;
std::map<int64_t, Obj> gameTextures;
Obj document = nullptr, visualRoot = nullptr, panel = nullptr, topElement = nullptr;
Obj chatField = nullptr, chatText = nullptr, searchField = nullptr, searchText = nullptr, button = nullptr;
Method setCursorMethod = nullptr;
int updates = 0;
// The game's themes (1.18.1): the theme its options hold (ReadGameTheme, 0
// unknown), FigmaStylesScriptableObject's instance (null before its data is
// loaded) and each theme's colours there; a colour it lacks throws, as the game's.
const std::string FigmaStyles = "Core.UILogic.Figma.ScriptableObject.Script.FigmaStylesScriptableObject";
int gameThemeValue = 0, themeReads = 0, figmaReads = 0;
Obj figmaInstance = nullptr;
std::map<std::pair<int, std::string>, std::array<float, 4>> figmaColours;
// A theme's accent and dark background as GetColorByVarName gives them.
void FigmaTheme(int theme, uint32_t accent, uint32_t dark) {
    const auto colour = [](uint32_t rgb, float alpha) {
        return std::array<float, 4>{static_cast<float>(rgb >> 16 & 0xFF) / 255.f, static_cast<float>(rgb >> 8 & 0xFF) / 255.f,
            static_cast<float>(rgb & 0xFF) / 255.f, alpha};
    };
    figmaColours[{theme, "--highlight_primary100"}] = colour(accent, 1.f);
    figmaColours[{theme, "--background_dark97"}] = colour(dark, .97f);
}
// Windows.
uint64_t nowMs = 1000000;
bool buttonDown = false;
int buttonReads = 0, dpiReads = 0;
unsigned dpi = 96, pointerBase = 32;
uint64_t FakeMilliseconds() { return nowMs; }
bool FakeButton() { ++buttonReads; return buttonDown; }
unsigned FakeDpi() { ++dpiReads; return dpi; }
unsigned FakeBase() { return pointerBase; }
// Unity's player (custom-cursor-player.hpp): its code at the checked site, and
// user32's LoadImageW that its import reaches. Breaking them turns the size
// part off.
std::array<unsigned char, customCursor::player::LoadBytes.size()> playerBytes{};
bool playerMissing = false, loadImageMissing = false, systemHookFails = false;
std::vector<std::pair<uintptr_t, size_t>> playerReads;
const unsigned char* FakePlayerCode(uintptr_t rva, size_t size) {
    playerReads.push_back({rva, size});
    if (playerMissing || rva != customCursor::player::LoadSite || size != playerBytes.size()) return nullptr;
    return playerBytes.data();
}
void FakeLoadImageTarget() {} // stands for user32's LoadImageW
void* FakeLoadImageAddress() { return loadImageMissing ? nullptr : reinterpret_cast<void*>(&FakeLoadImageTarget); }
int FakeWindowsCursor() { return 32; }

void Check(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }
Object& Get(Obj obj) { Check(obj != nullptr, "Null fake object"); return *static_cast<Object*>(obj); }
Class* Type(const std::string& name) { auto& c = classes[name]; if (!c) c = std::make_unique<Class>(Class{name}); return c.get(); }
Obj Make(const std::string& type) {
    auto o = std::make_unique<Object>();
    o->klass = Type(type);
    auto p = o.get();
    objects.push_back(std::move(o));
    return p;
}
template<class T> void Put(Obj o, size_t offset, T value) { std::memcpy(static_cast<char*>(o) + offset, &value, sizeof(T)); }
template<class T> T At(Obj o, size_t offset) { T value{}; std::memcpy(&value, static_cast<const char*>(o) + offset, sizeof(T)); return value; }
template<class T> Obj Box(T value) { auto o = Make("System.Boxed"); std::memcpy(Get(o).fields, &value, sizeof(T)); return o; }
Obj String(const std::string& text) { auto o = Make("System.String"); Get(o).text = text; return o; }
// m_CachedPtr: a native object behind the managed one (the object itself stands for it).
void SetNative(Obj o, bool alive) { Put<void*>(o, 0x10, alive ? o : nullptr); }
bool Native(Obj o) { return At<void*>(o, 0x10) != nullptr; }
bool Derives(const std::string& type, const std::string& base) {
    for (std::string t = type; !t.empty(); t = bases.contains(t) ? bases.at(t) : std::string())
        if (t == base) return true;
    return false;
}
bool Guarded(uintptr_t rva) {
    return std::any_of(std::begin(Signatures), std::end(Signatures), [&](const NativeSignature& s) { return s.rva == rva; });
}
Obj Element(const std::string& type, Obj parent) { auto o = Make(type); Get(o).parent = parent; return o; }
Obj PanelOf(Obj o) { for (; o; o = Get(o).parent) if (Get(o).panel) return Get(o).panel; return nullptr; }
// A Core.UILogic.Cursor as DofusCursor builds it.
Obj CursorObject(int32_t layer, int64_t type, int32_t state) {
    auto o = Make("Core.UILogic.Cursor");
    Put(o, 0x10, layer); Put(o, 0x18, type); Put(o, 0x20, state);
    return o;
}
// The game's own texture of a type (its aero_link, sword…), and its hotspot.
Obj GameTexture(int64_t type) {
    auto& texture = gameTextures[type];
    if (!texture) { texture = Make("UnityEngine.Texture2D"); SetNative(texture, true); }
    return texture;
}
bool Logged(const std::string& line) { return std::find(logs.begin(), logs.end(), line) != logs.end(); }
size_t LoggedPrefix(const std::string& prefix) {
    return static_cast<size_t>(std::count_if(logs.begin(), logs.end(), [&](const std::string& line) { return line.starts_with(prefix); }));
}
// The place of the first line starting with `prefix` (logs.size() without
// one), and that line ("" without one).
size_t LoggedIndex(const std::string& prefix) {
    return static_cast<size_t>(std::find_if(logs.begin(), logs.end(), [&](const std::string& line) { return line.starts_with(prefix); }) -
        logs.begin());
}
std::string LoggedLine(const std::string& prefix) {
    const auto index = LoggedIndex(prefix);
    return index < logs.size() ? logs[index] : std::string();
}
void Setup() {
    api.object_get_class = [](Obj o) -> Klass { return Get(o).klass; };
    api.class_get_name = [](Klass k) -> const char* { return static_cast<Class*>(k)->name.c_str(); };
    api.class_is_assignable_from = [](Klass base, Klass k) { return Derives(static_cast<Class*>(k)->name, static_cast<Class*>(base)->name); };
    api.object_new = [](Klass k) -> Obj { auto o = Make(static_cast<Class*>(k)->name); Get(o).allocated = true; return o; };
    api.object_unbox = [](Obj o) -> void* { return Get(o).fields; };
    api.string_new = [](const char* text) -> Obj { return String(text); };
    api.gchandle_new = [](Obj o, bool) -> GCHandle { handles[++handleId] = o; return handleId; };
    api.gchandle_get_target = [](GCHandle h) -> Obj { return handles.at(h); };
    api.gchandle_free = [](GCHandle h) { Check(handles.erase(h) == 1, "Invalid GC release"); };
    api.class_get_field_from_name = [](Klass k, const char* name) -> void* {
        const auto& type = static_cast<Class*>(k)->name;
        fieldLookups.insert({type, name});
        const auto found = offsets.find({type, name});
        if (found == offsets.end()) return nullptr;
        fieldInfos.push_back(std::make_unique<FieldInfo>(FieldInfo{type, name, found->second}));
        return fieldInfos.back().get();
    };
    api.field_get_offset = [](void* field) { return static_cast<FieldInfo*>(field)->offset; };
    api.field_static_get_value = [](void* field, void* value) {
        const auto& info = *static_cast<FieldInfo*>(field);
        Check(info.type == "Core.UILogic.DofusCursor" && info.name == "m_currentCursor", "Only DofusCursor.m_currentCursor is read statically");
        ++staticReads;
        if (staticReadFails) throw std::runtime_error("Injected static read failure");
        *static_cast<Obj*>(value) = currentCursor;
    };
}
// The game's interface: the panel's root (declaring the theme's variables),
// the UIDocument's root (pq.pbs) under it, the chat's input (a
// DofusChatTextField) and a disabled search field, each holding its text
// element, and a button.
void BuildUi() {
    panel = Make(Ui + "RuntimePanel");
    visualRoot = Element(Ui + "VisualElement", nullptr);
    Get(visualRoot).panel = panel;
    Get(visualRoot).variables["--highlight_primary100"] = {240 / 255.f, 1.f, 79 / 255.f, 1.f};
    Get(visualRoot).variables["--background_dark97"] = {27 / 255.f, 29 / 255.f, 50 / 255.f, .97f};
    document = Element(Ui + "TemplateContainer", visualRoot);
    const auto host = Element("gmy", document);
    chatField = Element("Core.UILogic.Components.DofusChatTextField", host);
    chatText = Element(Ui + "TextElement", chatField);
    searchField = Element(Ui + "TextField", host);
    Get(searchField).enabled = false;
    searchText = Element(Ui + "TextElement", searchField);
    button = Element(Ui + "VisualElement", host);
    topElement = button;
}
}

namespace managed {
Api api{};
uintptr_t gameBase = 0x180000000ULL;
}
Module* module = nullptr;
Module::Module(std::filesystem::path path) : folder(std::move(path)) {}
// The compositor's services: only the drag service is looked up.
managed::Obj Module::FindService(const char* name) const {
    fake::Check(std::string(name) == "fqa", "Only the drag service (fqa) is looked up");
    fake::Check(servicesReady, "Services are looked up once ready only");
    ++fake::serviceLookups;
    return fake::dragService;
}
void Log(const std::string& message) { fake::logs.push_back(message); }
// DofusOptionsContainer.currentTheme (game-theme.cpp is not linked here).
int ReadGameTheme() { ++fake::themeReads; return fake::gameThemeValue; }
#include "ui/custom-cursor.cpp"

namespace fake {
using customCursor::Vector2;
using cursorArt::Shape;
// What reached Unity: the trampoline's calls.
struct Sent { Obj texture; Vector2 hotspot; int32_t mode; Method method; };
std::vector<Sent> sent;
// The size part's hook as the module made it (AddSystemHook), and every
// LoadImageW call that reached user32: its arguments, its thread, the texture
// the player was making a cursor of.
struct SystemHook { void* target; void* replacement; size_t hooksBefore; };
std::vector<SystemHook> systemHooks;
struct Load { Obj texture; HINSTANCE instance; std::wstring file; UINT type; int cx, cy; UINT flags; DWORD thread; };
std::vector<Load> loads;
Obj makingCursorOf = nullptr;
HANDLE WINAPI FakeLoadImage(HINSTANCE instance, LPCWSTR file, UINT type, int cx, int cy, UINT flags) {
    loads.push_back({makingCursorOf, instance, file ? file : L"", type, cx, cy, flags, GetCurrentThreadId()});
    return reinterpret_cast<HANDLE>(static_cast<uintptr_t>(0x10000 + loads.size()));
}
// LoadImageW as the game calls it: through the mod's detour once hooked.
HANDLE CallLoadImage(HINSTANCE instance, LPCWSTR file, UINT type, int cx, int cy, UINT flags) {
    return systemHooks.empty() ? FakeLoadImage(instance, file, type, cx, cy, flags)
                               : customCursor::LoadImageHook(instance, file, type, cx, cy, flags);
}
// While the player makes a cursor: other loads in the same moment, from
// another thread or with other arguments (never changed).
bool probeLoads = false;
const wchar_t* const CursorFile = L"C:\\Users\\Player\\AppData\\Local\\Temp\\CUR1A2B.tmp";
// The player's Cursor.SetCursor (custom-cursor-player.hpp): one Windows cursor
// per texture and hotspot, made at the first call with CursorMode.Auto, its
// temporary file loaded at Windows' size (0 × 0, LR_LOADFROMFILE |
// LR_DEFAULTSIZE); none with ForceSoftware or without a texture.
std::set<std::tuple<Obj, float, float>> playerCursors;
void FakeSetCursor(Obj texture, Vector2 hotspot, int32_t mode, Method method) {
    sent.push_back({texture, hotspot, mode, method});
    if (!texture || mode != 0 || !playerCursors.insert({texture, hotspot.x, hotspot.y}).second) return;
    makingCursorOf = texture;
    CallLoadImage(nullptr, CursorFile, 2, 0, 0, 0x50);
    if (probeLoads) {
        std::thread([] { CallLoadImage(nullptr, L"other thread", 2, 0, 0, 0x50); }).join();
        CallLoadImage(nullptr, L"icon", 1, 0, 0, 0x50);
        CallLoadImage(nullptr, L"sized", 2, 32, 32, 0x50);
        CallLoadImage(nullptr, L"actual size", 2, 0, 0, 0x10);
        CallLoadImage(reinterpret_cast<HINSTANCE>(&FakeLoadImageTarget), L"resource", 2, 0, 0, 0x50);
    }
    makingCursorOf = nullptr;
}
// The player's last cursor made of a texture (its file's load), or null.
const Load* LoadOf(Obj texture) {
    for (auto it = loads.rbegin(); it != loads.rend(); ++it)
        if (it->texture == texture && it->file == CursorFile) return &*it;
    return nullptr;
}
// Loaded at `pixels` (the mod's size), or at Windows' own size (0).
bool LoadedAt(Obj texture, int pixels) {
    const auto load = LoadOf(texture);
    return load && load->instance == nullptr && load->type == 2 && load->cx == pixels && load->cy == pixels &&
        load->flags == (pixels ? 0x10u : 0x50u) && load->thread == GetCurrentThreadId();
}
const Vector2 GameHotspot{7, 3};
// DofusCursor.ApplyCursor: its texture for the current request (none for
// None, or when the game has none), back from UpdateCursorTexture's call.
void ApplyCurrent(bool withTexture = true) {
    Obj texture = nullptr;
    Vector2 hotspot{};
    if (currentCursor && Get(currentCursor).klass->name == "Core.UILogic.Cursor") {
        const auto type = At<int64_t>(currentCursor, 0x18);
        if (type != customCursor::cursorType::None && withTexture) { texture = GameTexture(type); hotspot = GameHotspot; }
    }
    customCursor::OnSetCursor(texture, hotspot, 0, setCursorMethod, gameBase + ApplyCursorReturn);
}
// The game's DofusCursor.SetCursor / RemoveCursor: a new request applied.
void GameAsks(int32_t layer, int64_t type, int32_t state = 0, bool withTexture = true) {
    currentCursor = CursorObject(layer, type, state);
    ApplyCurrent(withTexture);
}
}

void AddHook(managed::Method method, uintptr_t rva, void* replacement, void** original) {
    using namespace fake;
    const auto& m = *static_cast<const MethodData*>(method);
    Check(m.rva == rva && rva == 0x54024F0 && m.type == "UnityEngine.Cursor" && m.name == "SetCursor" && m.argc == 3,
        "The only hook: UnityEngine.Cursor.SetCursor, through its verified method");
    Check(replacement == reinterpret_cast<void*>(&customCursor::SetCursorHook), "The detour is SetCursorHook");
    hooks.push_back({rva, &m, contracts.size(), natives.size(), fieldLookups.size()});
    setCursorMethod = method;
    *original = reinterpret_cast<void*>(&FakeSetCursor);
}
// The size part: a detour on user32's LoadImageW (here its stand-in).
void AddSystemHook(void* target, void* replacement, void** original) {
    using namespace fake;
    Check(target == reinterpret_cast<void*>(&FakeLoadImageTarget) && replacement == reinterpret_cast<void*>(&customCursor::LoadImageHook),
        "The only Windows hook: LoadImageW, its detour LoadImageHook");
    if (systemHookFails) throw std::runtime_error("Injected hook failure");
    systemHooks.push_back({target, replacement, hooks.size()});
    *original = reinterpret_cast<void*>(&FakeLoadImage);
}

namespace managed {
Klass Api::Class(const char* image, const char* space, const char* name) const {
    const std::string full = *space ? std::string(space) + "." + name : std::string(name);
    fake::classLookups.insert({image, full});
    if (fake::missingClasses.contains(full)) throw std::runtime_error("Class unavailable: " + full);
    return fake::Type(full);
}
Klass Api::NestedClass(Klass declaringClass, const char* name) const {
    const auto full = static_cast<fake::Class*>(declaringClass)->name + "+" + name;
    fake::classLookups.insert({"nested", full});
    if (fake::missingClasses.contains(full)) throw std::runtime_error("Nested class unavailable: " + full);
    return fake::Type(full);
}
Method Api::FindVerified(Klass klass, const char* name, int argc, uintptr_t rva) const {
    fake::Check(fake::Guarded(rva), std::string("Verified method without a native guard: ") + name);
    if (fake::failVerified.contains(rva)) throw std::runtime_error(std::string("Verified method unavailable: ") + name);
    fake::methods.push_back(std::make_unique<fake::MethodData>(fake::MethodData{static_cast<fake::Class*>(klass)->name, name, argc, rva}));
    fake::contracts.insert({fake::methods.back()->type, name, argc, rva});
    return fake::methods.back().get();
}
void* Api::NativeAt(uintptr_t rva) const {
    fake::Check(fake::Guarded(rva), "Native site without a guard");
    fake::natives.insert(rva);
    if (rva == fake::nativeMismatch) throw std::runtime_error("Loaded method bytes differ from snapshot");
    return reinterpret_cast<void*>(gameBase + rva);
}
Obj Api::Invoke(Method method, Obj self, std::initializer_list<void*> arguments) const {
    using namespace fake;
    const auto& m = *static_cast<const MethodData*>(method);
    const std::vector<void*> args(arguments);
    Check(static_cast<int>(args.size()) == m.argc, "Invocation argument count: " + m.name);
    ++invokes[m.name];
    if (m.type == "Core.UILogic.DofusCursor") {
        // UpdateCursorTexture: the highest layer's cursor (here the current
        // one, a new None without any) applied again.
        Check(m.name == "UpdateCursorTexture" && !self, "Static DofusCursor.UpdateCursorTexture");
        ++updates;
        events.push_back("update");
        if (updateFails) throw std::runtime_error("Injected refresh failure");
        if (!currentCursor) currentCursor = CursorObject(0, customCursor::cursorType::None, 0);
        ApplyCurrent();
        return nullptr;
    }
    if (m.type == "pq") {
        // As the game: the cached UIDocument root, or a NullReferenceException before any UIDocument.
        Check(m.name == "pbs" && !self, "Static pq.pbs");
        if (!document) throw std::runtime_error("System.NullReferenceException: Object reference not set to an instance of an object.");
        return document;
    }
    if (m.type == FigmaStyles) {
        // GetInstance: static, null before the game's data. GetColorByVarName:
        // a ThemeEnum theme and one of the game's own variables.
        if (m.name == "GetInstance") { Check(!self, "Static FigmaStylesScriptableObject.GetInstance"); return figmaInstance; }
        Check(m.name == "GetColorByVarName" && self && self == figmaInstance, "GetColorByVarName on the game's instance");
        ++figmaReads;
        const int theme = *static_cast<int*>(args[0]);
        const std::string name = Get(static_cast<Obj>(args[1])).text;
        Check(theme >= 1 && theme <= 11 && (name == "--highlight_primary100" || name == "--background_dark97"),
            "Only a game theme and the variables the game itself reads");
        const auto found = figmaColours.find({theme, name});
        if (found == figmaColours.end()) throw std::runtime_error("System.NullReferenceException: Object reference not set to an instance of an object.");
        return Box(found->second);
    }
    if (m.name == "Destroy") {
        Check(m.type == "UnityEngine.Object" && !self, "Static Object.Destroy");
        const Obj target = static_cast<Obj>(args[0]);
        ++destroyAttempts;
        events.push_back("destroy");
        if (destroyFails != 0) { if (destroyFails > 0) --destroyFails; throw std::runtime_error("Injected destroy failure"); }
        Check(Get(target).klass->name == "UnityEngine.Texture2D" && Get(target).allocated && fake::Native(target) && !Get(target).destroyed,
            "Only the mod's own live textures are destroyed, once");
        Get(target).destroyed = true;
        SetNative(target, false);
        return nullptr;
    }
    Check(self && Derives(Get(self).klass->name, m.type), "Method of " + m.type + " called on " + (self ? Get(self).klass->name : std::string("null")));
    auto& o = Get(self);
    if (m.name == ".ctor") {
        Check(m.type == "UnityEngine.Texture2D" && o.allocated && !fake::Native(self), "A Texture2D of the mod constructed once, after object_new");
        events.push_back("ctor");
        if (ctorFails != 0) { if (ctorFails > 0) --ctorFails; throw std::runtime_error("Injected constructor failure"); }
        o.width = *static_cast<int*>(args[0]); o.height = *static_cast<int*>(args[1]); o.format = *static_cast<int*>(args[2]);
        o.mipChain = *static_cast<bool*>(args[3]); o.linear = *static_cast<bool*>(args[4]);
        SetNative(self, true);
        return nullptr;
    }
    if (m.name == "set_hideFlags") {
        Check(fake::Native(self), "Flags set on a constructed texture");
        o.hideFlags = *static_cast<int*>(args[0]);
        return nullptr;
    }
    if (m.name == "LoadRawTextureData") {
        Check(fake::Native(self) && !o.applied, "Pixels loaded once, before Apply");
        const auto data = static_cast<const uint8_t*>(*static_cast<void**>(args[0]));
        const int size = *static_cast<int*>(args[1]);
        Check(data && size == o.width * o.height * 4, "LoadRawTextureData gets width × height × 4 bytes (RGBA32)");
        o.pixels.assign(data, data + size);
        return nullptr;
    }
    if (m.name == "Apply") {
        Check(!o.pixels.empty() && !o.applied, "Applied once, after its pixels");
        o.updateMipmaps = *static_cast<bool*>(args[0]);
        o.noLongerReadable = *static_cast<bool*>(args[1]);
        o.applied = true;
        return nullptr;
    }
    if (m.name == "GetTopElementUnderPointer") {
        Check(*static_cast<int*>(args[0]) == 0 && self == panel, "The mouse pointer's top element, on the UIDocument's panel");
        ++topCalls;
        if (topFails) throw std::runtime_error("Injected pointer failure");
        return topElement;
    }
    if (m.name == "get_parent") { ++parentCalls; return o.parent; }
    if (m.name == "get_panel") return PanelOf(self);
    if (m.name == "get_enabledInHierarchy") return Box(o.enabled);
    if (m.name == "get_customStyle") {
        if (!o.style) { o.style = Make(Ui + "VisualElement+CustomStyleAccess"); Get(o.style).owner = self; }
        return o.style;
    }
    if (m.name == "TryGetValue") {
        // CustomStyleProperty<Color>: its name; out Color: four floats.
        const Obj name = *static_cast<Obj*>(args[0]);
        const auto& variables = Get(o.owner).variables;
        const auto found = variables.find(Get(name).text);
        if (found == variables.end()) return Box(false);
        std::memcpy(args[1], found->second.data(), sizeof(float) * 4);
        return Box(true);
    }
    throw std::runtime_error("Unexpected fake method " + m.type + "." + m.name);
}
}

namespace fake {
using customCursor::Choose;
using customCursor::Hover;
using customCursor::Origin;
using customCursor::Request;
namespace cursorType = customCursor::cursorType;
constexpr int64_t Sword = 8, Bubble = 2, Gear = 1, Ping = 0x400000, Heart = 0x2000000, Magnifier = 0x100000000LL;
constexpr int32_t Map = 0, Interactive = 1, Frustum = 2, UiLayer = 3;

// Everything back to a new game and a new module, installed unless asked.
void Fresh(bool install = true) {
    customCursor::s = customCursor::State{};
    customCursor::n = customCursor::Natives{};
    customCursor::originalSetCursor = nullptr;
    customCursor::originalLoadImage = nullptr;
    customCursor::sizedThread = 0;
    customCursor::sizedPixels = 0;
    customCursor::sizedLoads = 0;
    customCursor::sys = {FakeMilliseconds, FakeButton, FakeDpi, FakeBase, FakePlayerCode, FakeLoadImageAddress, FakeWindowsCursor};
    delete module;
    module = nullptr;
    Check(handles.empty(), "Every GC handle released");
    objects.clear(); methods.clear(); fieldInfos.clear(); classes.clear();
    contracts.clear(); classLookups.clear(); fieldLookups.clear(); natives.clear(); hooks.clear(); invokes.clear();
    logs.clear(); events.clear(); sent.clear(); gameTextures.clear();
    systemHooks.clear(); loads.clear(); playerCursors.clear(); playerReads.clear();
    playerBytes = customCursor::player::LoadBytes;
    playerMissing = loadImageMissing = systemHookFails = probeLoads = false;
    missingClasses.clear(); failVerified.clear(); nativeMismatch = 0;
    ctorFails = destroyFails = 0; updateFails = topFails = staticReadFails = false;
    destroyAttempts = serviceLookups = staticReads = parentCalls = topCalls = updates = 0;
    buttonDown = false; buttonReads = dpiReads = 0; dpi = 96; pointerBase = 32; nowMs += 100000;
    currentCursor = nullptr; setCursorMethod = nullptr;
    gameThemeValue = themeReads = figmaReads = 0; figmaInstance = nullptr; figmaColours.clear();
    DefaultOffsets();
    Setup();
    BuildUi();
    dragService = Make("Core.UILogic.Components.DragAndDrop.DragAndDropService");
    compositor = Make("Core.Engine.Compositor.Compositor");
    module = new Module("fake");
    module->ready = true;
    module->servicesReady = true;
    if (install) InstallCustomCursor();
}
void Frame(uint64_t step = 8) { nowMs += step; CustomCursorFrame(nowMs); }
void Frames(int count) { for (int i = 0; i < count; ++i) Frame(); }
// The textures the mod made (object_new), alive or destroyed.
std::vector<Obj> Made() {
    std::vector<Obj> made;
    for (const auto& o : objects)
        if (o->klass->name == "UnityEngine.Texture2D" && o->allocated) made.push_back(o.get());
    return made;
}
size_t TextureHandles() {
    return static_cast<size_t>(std::count_if(handles.begin(), handles.end(), [](const auto& h) { return Get(h.second).klass->name == "UnityEngine.Texture2D"; }));
}
std::array<Obj, cursorArt::ShapeCount> CurrentSet() {
    std::array<Obj, cursorArt::ShapeCount> set{};
    for (size_t i = 0; i < set.size(); ++i) set[i] = customCursor::s.current.shapes[i].texture.get();
    return set;
}
Obj Ours(Shape shape) { return customCursor::s.current.shapes[static_cast<size_t>(shape)].texture.get(); }
cursorArt::Image Expected(Shape shape, int size = 32, cursorArt::Colour colour = cursorArt::Colour::Gold,
    uint32_t accent = 0, uint32_t dark = 0) {
    return cursorArt::Render(shape, cursorArt::PaletteOf(colour, accent, dark), size);
}
Vector2 HotOf(Shape shape, int size = 32) {
    const auto image = Expected(shape, size);
    return {static_cast<float>(image.hotX), static_cast<float>(image.hotY)};
}
// The last call that reached Unity is the mod's cursor of this shape.
void ExpectOurs(Shape shape, const std::string& message, int size = 32) {
    Check(!sent.empty() && sent.back().texture == Ours(shape) && Ours(shape) && sent.back().hotspot == HotOf(shape, size) &&
        sent.back().mode == 0 && sent.back().method == setCursorMethod, message);
}
// The last call that reached Unity is exactly this one.
void ExpectSent(Obj texture, Vector2 hotspot, int32_t mode, const std::string& message) {
    Check(!sent.empty() && sent.back().texture == texture && sent.back().hotspot == hotspot && sent.back().mode == mode &&
        sent.back().method == setCursorMethod, message);
}
void ExpectGame(int64_t type, const std::string& message) { ExpectSent(GameTexture(type), GameHotspot, 0, message); }
// Ticks « Curseur Dofus »: thirteen frames draw the set, then it is in use.
void Activate(int colour = Settings::CursorGold, int size = 0) {
    module->settings.customCursor = true;
    module->settings.customCursorColor = colour;
    module->settings.customCursorSize = size;
    for (int frame = 0; frame < cursorArt::ShapeCount; ++frame) {
        Check(!customCursor::s.active, "Nothing stands in before the set is complete");
        Frame();
    }
    Check(customCursor::s.active && customCursor::s.current.Complete(), "Thirteen frames draw the set, then it is in use");
}
// A set's textures, each as Render draws its shape: RGBA32 without mipmaps,
// never unloaded, pixels loaded, applied readable.
void CheckSet(const std::array<Obj, cursorArt::ShapeCount>& set, int size, cursorArt::Colour colour, uint32_t accent, uint32_t dark,
    const std::string& message) {
    for (size_t i = 0; i < set.size(); ++i) {
        const auto& texture = Get(set[i]);
        const auto image = Expected(static_cast<Shape>(i), size, colour, accent, dark);
        Check(texture.allocated && texture.width == size && texture.height == size && texture.format == 4 && !texture.mipChain &&
            !texture.linear && texture.hideFlags == 0x20 && texture.applied && !texture.updateMipmaps && !texture.noLongerReadable &&
            texture.pixels == image.rgba && customCursor::s.current.shapes[i].hotspot == Vector2{static_cast<float>(image.hotX),
            static_cast<float>(image.hotY)}, message + ": shape " + std::to_string(i));
    }
}

// ------------------------------------------------------------------ policy
void PolicyTable() {
    const auto game = [](int32_t layer, int64_t type, int32_t state = 0, bool texture = true) {
        Request r;
        r.origin = Origin::ApplyCursor; r.layer = layer; r.type = type; r.state = state; r.textureNull = !texture;
        return r;
    };
    Request other, uss;
    uss.textureNull = false;
    const Hover none, drag{true, false, false}, held{false, true, false}, text{false, false, true};
    struct Row { Request request; Hover hover; std::optional<Shape> shape; const char* what; };
    const Row rows[] = {
        {game(Map, cursorType::None, 0, false), none, Shape::Arrow, "None: the arrow"},
        {game(Map, cursorType::None, 0, false), text, Shape::Beam, "None over a text field: the text cursor"},
        {game(UiLayer, cursorType::Link), none, Shape::Hand, "a button's Link: the hand"},
        {game(Map, cursorType::Link), none, Shape::Hand, "the Link the game asks on the cells: the hand, as the game"},
        {game(Frustum, cursorType::Link), none, Shape::Hand, "a map border's Link: the hand"},
        {game(UiLayer, cursorType::Link), text, Shape::Hand, "a hand over a text field stays the hand"},
        {game(UiLayer, cursorType::Link, 0, false), none, Shape::Hand, "Link without the game's texture: the hand still"},
        {game(UiLayer, cursorType::Beam), none, Shape::Beam, "Beam: the text cursor"},
        {game(UiLayer, cursorType::Move), none, Shape::Open, "Move: the open hand"},
        {game(UiLayer, cursorType::Move), held, Shape::Closed, "Move with the button held: the closed hand"},
        {game(UiLayer, cursorType::ResizeEW), none, Shape::ResizeEW, "ResizeEW"},
        {game(UiLayer, cursorType::ResizeNS), none, Shape::ResizeNS, "ResizeNS"},
        {game(UiLayer, cursorType::ResizeNWSE), none, Shape::ResizeNWSE, "ResizeNWSE"},
        {game(UiLayer, cursorType::ResizeNESW), none, Shape::ResizeNESW, "ResizeNESW"},
        {game(UiLayer, cursorType::ResizeNESW, customCursor::StateDisabled), none, Shape::ResizeNESW, "a resize keeps no badge"},
        {game(UiLayer, cursorType::Forbidden), none, Shape::ArrowForbidden, "Forbidden: the arrow with its badge"},
        {game(UiLayer, cursorType::Link, customCursor::StateDisabled), none, Shape::HandForbidden, "Link disabled: the hand with the forbidden badge"},
        {game(UiLayer, cursorType::Link, customCursor::StateWait), none, Shape::HandBusy, "Link waiting: the hand with the hourglass"},
        {game(UiLayer, cursorType::Beam, customCursor::StateDisabled), none, Shape::ArrowForbidden, "another disabled cursor: the arrow's badge"},
        {game(Map, cursorType::None, customCursor::StateWait, false), none, Shape::ArrowBusy, "waiting: the arrow with the hourglass"},
        {game(Interactive, Sword), none, std::nullopt, "the sword: the game's"},
        {game(Interactive, Sword, customCursor::StateDisabled), none, std::nullopt, "the sword with its cross: the game's"},
        {game(Interactive, Bubble), none, std::nullopt, "the bubble: the game's"},
        {game(Interactive, Gear, customCursor::StateWait), none, std::nullopt, "a tool with its hourglass: the game's"},
        {game(Frustum, Ping), none, std::nullopt, "a fight marker: the game's"},
        {game(UiLayer, Heart), none, std::nullopt, "the heart marker: the game's"},
        {game(Interactive, cursorType::NotFound), none, std::nullopt, "NotFound: the game's question mark"},
        {game(Interactive, Magnifier), none, std::nullopt, "the magnifier: the game's"},
        {game(Interactive, 0), none, std::nullopt, "Invalid: the game's"},
        {game(Interactive, 0x12345), none, std::nullopt, "an unknown type: the game's"},
        {game(Interactive, Sword), text, std::nullopt, "the sword over a text field: the game's"},
        {game(Interactive, Sword, 0, false), none, Shape::Arrow, "the sword without its texture: the mod's arrow, never Windows'"},
        {game(Interactive, Gear, customCursor::StateDisabled, false), none, Shape::ArrowForbidden, "a disabled tool without texture: the badge"},
        {game(Interactive, cursorType::NotFound, 0, false), none, Shape::Arrow, "NotFound without texture: the arrow"},
        {other, none, Shape::Arrow, "another caller's reset: the arrow"},
        {other, text, Shape::Beam, "another caller's reset over a text field: the text cursor"},
        {uss, none, std::nullopt, "another caller's texture: its own"},
        {uss, drag, Shape::Closed, "an item dragged: the closed hand, even over another caller's texture"},
        {game(Interactive, Sword), drag, Shape::Closed, "an item dragged over a monster: the closed hand"},
        {game(UiLayer, cursorType::ResizeNS), drag, Shape::Closed, "an item dragged over a border: the closed hand"},
        {game(Map, cursorType::None, 0, false), Hover{true, false, true}, Shape::Closed, "dragged over a text field: the closed hand"}};
    for (const auto& row : rows) Check(Choose(row.request, row.hover) == row.shape, std::string("Choose: ") + row.what);
    Check(customCursor::AsksArrow(game(Map, cursorType::None, 0, false)) && customCursor::AsksArrow(other) && !customCursor::AsksArrow(uss) &&
        !customCursor::AsksArrow(game(UiLayer, cursorType::Link)) && !customCursor::AsksArrow(game(Interactive, Sword, 0, false)),
        "The text field is read for the plain arrow only");
    Check(customCursor::AsksMove(game(UiLayer, cursorType::Move)) && !customCursor::AsksMove(game(UiLayer, cursorType::Link)) && !customCursor::AsksMove(other),
        "The button is read for Move only");
    // « Taille du curseur »: base × dpi / 96 × {1, 1,5, 2}, within 32..128.
    const std::tuple<unsigned, unsigned, int, int> sizes[] = {
        {96, 32, 0, 32}, {120, 32, 0, 40}, {144, 32, 0, 48}, {168, 32, 0, 56}, {192, 32, 0, 64},
        {96, 32, 1, 48}, {120, 32, 1, 60}, {144, 32, 1, 72}, {168, 32, 1, 84}, {192, 32, 1, 96},
        {96, 32, 2, 64}, {120, 32, 2, 80}, {144, 32, 2, 96}, {168, 32, 2, 112}, {192, 32, 2, 128},
        {288, 32, 2, 128}, {72, 32, 0, 32}, {0, 32, 0, 32}, {0, 32, 2, 64}, {96, 0, 0, 32}, {96, 999, 0, 32}, {96, 31, 0, 32},
        {96, 48, 0, 48}, {96, 64, 1, 96}, {96, 256, 0, 128}, {96, 32, 3, 32}, {96, 32, -1, 32}};
    for (const auto& [dpiValue, base, choice, pixels] : sizes)
        Check(customCursor::CursorPixels(dpiValue, base, choice) == pixels, "CursorPixels(" + std::to_string(dpiValue) + ", " +
            std::to_string(base) + ", " + std::to_string(choice) + ") = " + std::to_string(pixels));
    Check(customCursor::RgbOf(240 / 255.f, 1.f, 79 / 255.f) == customCursor::StandardAccent &&
        customCursor::RgbOf(27 / 255.f, 29 / 255.f, 50 / 255.f) == customCursor::StandardDark &&
        customCursor::RgbOf(-1.f, 2.f, .5f) == 0x00FF80, "USS colours as 0xRRGGBB, clamped");
    // The player's load the mod changes (custom-cursor-player.hpp): exactly
    // its own, a cursor file at Windows' size; its site, byte for byte.
    using customCursor::player::PlayerLoad;
    const int owner = 1;
    Check(PlayerLoad(nullptr, 2, 0, 0, 0x50) && !PlayerLoad(&owner, 2, 0, 0, 0x50) && !PlayerLoad(nullptr, 1, 0, 0, 0x50) &&
        !PlayerLoad(nullptr, 0, 0, 0, 0x50) && !PlayerLoad(nullptr, 2, 32, 0, 0x50) && !PlayerLoad(nullptr, 2, 0, 32, 0x50) &&
        !PlayerLoad(nullptr, 2, 0, 0, 0x10) && !PlayerLoad(nullptr, 2, 0, 0, 0x40) && !PlayerLoad(nullptr, 2, 0, 0, 0x2050),
        "Only the player's load of a cursor file at Windows' size is changed");
    const auto site = customCursor::player::LoadBytes;
    Check(customCursor::player::SiteMatches(site.data()) && !customCursor::player::SiteMatches(nullptr), "The pinned site, never nothing");
    for (size_t i = 0; i < site.size(); ++i) {
        auto altered = site;
        altered[i] ^= 0x80;
        Check(!customCursor::player::SiteMatches(altered.data()), "Any byte different: another player, byte " + std::to_string(i));
    }
}

// ------------------------------------------------------------ installation
void InstallContracts() {
    Fresh();
    Check(customCursor::s.installed && Logged("CUSTOM_CURSOR_READY textHover=1 theme=1 themes=1 drag=1 size=1"), "Installed, with its four optional parts");
    const std::set<std::tuple<std::string, std::string, int, uintptr_t>> expected{
        {"Core.UILogic.DofusCursor", "UpdateCursorTexture", 0, 0x280A4C0}, {"UnityEngine.Cursor", "SetCursor", 3, 0x54024F0},
        {"UnityEngine.Texture2D", ".ctor", 5, 0x5410E20}, {"UnityEngine.Texture2D", "LoadRawTextureData", 2, 0x540E4C0},
        {"UnityEngine.Texture2D", "Apply", 2, 0x540BCE0}, {"UnityEngine.Object", "set_hideFlags", 1, 0x5439360},
        {"UnityEngine.Object", "Destroy", 1, 0x5435EA0},
        {Ui + "VisualElement", "get_parent", 0, 0x55F7180}, {"pq", "pbs", 0, 0x2E93CC0},
        {Ui + "VisualElement", "get_panel", 0, 0x55F7170}, {Ui + "VisualElement", "get_enabledInHierarchy", 0, 0x55F5EF0},
        {Ui + "BaseVisualElementPanel", "GetTopElementUnderPointer", 1, 0x57A3FC0},
        {Ui + "VisualElement", "get_customStyle", 0, 0x55F5C30}, {Ui + "VisualElement+CustomStyleAccess", "TryGetValue", 2, 0x55FCCB0},
        {FigmaStyles, "GetInstance", 0, 0x2CA7EA0}, {FigmaStyles, "GetColorByVarName", 2, 0x2CA7C90}};
    Check(contracts == expected, "Exactly the required and optional natives, each at its pinned RVA");
    for (const auto& contract : expected) Check(Guarded(std::get<3>(contract)), "Every verified method guarded");
    Check(natives == std::set<uintptr_t>{0x280A73C, 0x28088F9}, "UpdateCursorTexture's call of ApplyCursor and ApplyCursor's tail jump checked");
    const std::set<std::pair<std::string, std::string>> images{
        {"Core.dll", "Core.UILogic.DofusCursor"}, {"Core.dll", "Core.UILogic.Cursor"}, {CoreImage, "UnityEngine.Texture2D"},
        {CoreImage, "UnityEngine.Object"}, {CoreImage, "UnityEngine.Cursor"}, {UiImage, Ui + "VisualElement"}, {"Core.dll", "pq"},
        {UiImage, Ui + "BaseVisualElementPanel"}, {UiImage, Ui + "TextField"}, {"nested", Ui + "VisualElement+CustomStyleAccess"},
        {"Core.dll", "Core.UILogic.Components.DragAndDrop.DragAndDropService"}, {"Core.dll", FigmaStyles}};
    Check(classLookups == images, "Each class from its own image");
    for (const auto& [field, offset] : offsets) Check(fieldLookups.contains(field), "Field layout checked: " + field.second);
    Check(hooks.size() == 1 && hooks[0].contracts == contracts.size() && hooks[0].natives == natives.size() && hooks[0].fields == fieldLookups.size(),
        "The detour is added after the game's contracts, every method resolved before its bytes change");
    // The size part: the player's cursor loading checked once, at its pinned
    // site, then LoadImageW hooked, after the SetCursor detour only.
    Check(playerReads == std::vector<std::pair<uintptr_t, size_t>>{{customCursor::player::LoadSite, customCursor::player::LoadBytes.size()}},
        "UnityPlayer.dll's loading of a cursor checked at its pinned site");
    Check(systemHooks.size() == 1 && systemHooks[0].hooksBefore == 1 && customCursor::n.sized &&
        customCursor::originalLoadImage == &FakeLoadImage, "LoadImageW hooked last, once the SetCursor detour exists");
    Check(customCursor::originalSetCursor == &FakeSetCursor && sent.empty() && updates == 0 && Made().empty() && loads.empty() &&
        themeReads == 0 && figmaReads == 0, "Nothing changed nor read at install");
    Fresh(false);
    InstallCustomCursor();
    InstallCustomCursor();
    Check(hooks.size() == 1 && systemHooks.size() == 1 && LoggedPrefix("CUSTOM_CURSOR_READY") == 1, "Installed once");
}
// A required contract missing: no hook, nothing else, the Windows cursor.
void RequiredContractMissing() {
    const std::vector<std::pair<const char*, std::function<void()>>> breaks{
        {"ApplyCursor's call", [] { nativeMismatch = 0x280A73C; }}, {"ApplyCursor's tail jump", [] { nativeMismatch = 0x28088F9; }},
        {"SetCursor", [] { failVerified.insert(0x54024F0); }}, {"UpdateCursorTexture", [] { failVerified.insert(0x280A4C0); }},
        {"LoadRawTextureData", [] { failVerified.insert(0x540E4C0); }}, {"Destroy", [] { failVerified.insert(0x5435EA0); }},
        {"m_currentCursor", [] { offsets[{"Core.UILogic.DofusCursor", "m_currentCursor"}] = 0x10; }},
        {"cursorType", [] { offsets.erase({"Core.UILogic.Cursor", "cursorType"}); }},
        {"m_CachedPtr", [] { offsets[{"UnityEngine.Object", "m_CachedPtr"}] = 0x18; }},
        {"DofusCursor", [] { missingClasses.insert("Core.UILogic.DofusCursor"); }},
        {"UnityEngine.Cursor", [] { missingClasses.insert("UnityEngine.Cursor"); }}};
    for (const auto& [what, breakIt] : breaks) {
        Fresh(false);
        breakIt();
        InstallCustomCursor();
        const std::string name = what;
        Check(!customCursor::s.installed && hooks.empty() && systemHooks.empty() && playerReads.empty() &&
            LoggedPrefix("CUSTOM_CURSOR_DISABLED ") == 1 && LoggedPrefix("CUSTOM_CURSOR_READY") == 0,
            name + " missing: CUSTOM_CURSOR_DISABLED and no hook, LoadImageW's neither");
        module->settings.customCursor = true;
        Frames(30);
        RestoreCustomCursor();
        Check(Made().empty() && !CustomCursorCleanupPending() && updates == 0 && handles.empty(), name + " missing: nothing made, nothing to restore");
    }
}
// An optional contract missing: only its part off, the cursors stay.
void OptionalParts() {
    Fresh(false);
    missingClasses.insert(Ui + "TextField");
    InstallCustomCursor();
    Check(LoggedPrefix("CUSTOM_CURSOR_PART_UNAVAILABLE text ") == 1 && Logged("CUSTOM_CURSOR_READY textHover=0 theme=1 themes=1 drag=1 size=1"), "Without TextField: the text part off");
    Activate();
    topElement = chatText;
    nowMs += 300;
    GameAsks(Map, cursorType::None, 0, false);
    ExpectOurs(Shape::Arrow, "Without the text part, the arrow over the chat");
    Check(topCalls == 0, "No pointer read without the text part");
    Fresh(false);
    failVerified.insert(0x55F7180);
    InstallCustomCursor();
    Check(LoggedPrefix("CUSTOM_CURSOR_PART_UNAVAILABLE ui ") == 1 && Logged("CUSTOM_CURSOR_READY textHover=0 theme=0 themes=1 drag=1 size=1"),
        "Without get_parent: the text and theme parts off");
    Activate(Settings::CursorDefault);
    Check(customCursor::s.current.look.accent == customCursor::StandardAccent && customCursor::s.current.look.dark == customCursor::StandardDark &&
        invokes["pbs"] == 0, "Without the UI part nor the theme's data, « Défaut » draws the Standard theme's colours");
    Fresh(false);
    missingClasses.insert(Ui + "VisualElement+CustomStyleAccess");
    InstallCustomCursor();
    Check(LoggedPrefix("CUSTOM_CURSOR_PART_UNAVAILABLE theme ") == 1 && Logged("CUSTOM_CURSOR_READY textHover=1 theme=0 themes=1 drag=1 size=1"), "Without CustomStyleAccess: the theme part off");
    Get(visualRoot).variables["--highlight_primary100"] = {1.f, 0.f, 0.f, 1.f};
    Activate(Settings::CursorDefault);
    Check(customCursor::s.current.look.accent == customCursor::StandardAccent && invokes["TryGetValue"] == 0, "Without the theme part: the Standard theme's colours");
    topElement = chatText;
    GameAsks(Map, cursorType::None, 0, false);
    ExpectOurs(Shape::Beam, "The text part still works without the theme part");
    // Without GetColorByVarName (1.18.1): a theme's colours are the panel's
    // while the game shows it, the Standard theme's otherwise.
    for (const uintptr_t missing : {uintptr_t{0x2CA7C90}, uintptr_t{0x2CA7EA0}}) {
        Fresh(false);
        failVerified.insert(missing);
        InstallCustomCursor();
        Check(LoggedPrefix("CUSTOM_CURSOR_PART_UNAVAILABLE figma ") == 1 && Logged("CUSTOM_CURSOR_READY textHover=1 theme=1 themes=0 drag=1 size=1"),
            "Without FigmaStylesScriptableObject's methods: the themes part off, the rest on");
        gameThemeValue = 3;
        figmaInstance = Make(FigmaStyles);
        FigmaTheme(3, 0x4F9CFF, 0x292B33);
        Get(visualRoot).variables["--highlight_primary100"] = {1.f, 0.f, 0.f, 1.f};
        Activate(3);
        Check(customCursor::s.current.look.accent == 0xFF0000 && customCursor::s.current.look.dark == customCursor::StandardDark &&
            figmaReads == 0 && Logged("CUSTOM_CURSOR_THEME choice=3 theme=3 shown=3 source=panel accent=#FF0000 dark=#1B1D32"),
            "Without the themes part: Bonta shown, Bonta's colours from the panel");
        module->settings.customCursorColor = 4;
        Frames(20);
        Check(customCursor::s.current.look.accent == customCursor::StandardAccent && customCursor::s.current.look.dark == customCursor::StandardDark &&
            Logged("CUSTOM_CURSOR_THEME choice=4 theme=4 shown=3 source=standard accent=#F0FF4F dark=#1B1D32") && figmaReads == 0,
            "Without the themes part: Brakmar while Bonta shows, the Standard theme's colours");
    }
    Fresh(false);
    offsets[{"Core.UILogic.Components.DragAndDrop.DragAndDropService", "<isDragging>k__BackingField"}] = 0x18;
    InstallCustomCursor();
    Check(LoggedPrefix("CUSTOM_CURSOR_PART_UNAVAILABLE drag ") == 1 && Logged("CUSTOM_CURSOR_READY textHover=1 theme=1 themes=1 drag=0 size=1"), "Another isDragging layout: the drag part off");
    Activate();
    Put<bool>(dragService, 0x10, true);
    GameAsks(UiLayer, cursorType::Link);
    ExpectOurs(Shape::Hand, "Without the drag part, no closed hand");
    Check(serviceLookups == 0, "The drag service is not looked up without its part");
}

// ------------------------------------------------------------- the detour
// Unticked (the default): every call goes on as the game made it.
void OptionOffPassesThrough() {
    Fresh();
    Check(!module->settings.customCursor, "Off by default");
    Frames(40);
    GameAsks(Map, cursorType::None, 0, false);
    ExpectSent(nullptr, {}, 0, "Off: the game's None, unchanged");
    GameAsks(UiLayer, cursorType::Link);
    ExpectGame(cursorType::Link, "Off: the game's hand, unchanged");
    GameAsks(Interactive, Sword, customCursor::StateDisabled);
    ExpectGame(Sword, "Off: the game's sword, unchanged");
    const auto uss = Make("UnityEngine.Texture2D");
    customCursor::OnSetCursor(uss, {3, 4}, 1, setCursorMethod, gameBase + CursorManagerReturn);
    ExpectSent(uss, {3, 4}, 1, "Off: another caller's call, unchanged, its mode too");
    customCursor::OnSetCursor(nullptr, {}, 0, setCursorMethod, gameBase + CursorManagerReturn);
    ExpectSent(nullptr, {}, 0, "Off: another caller's reset, unchanged");
    Check(sent.size() == 5 && Made().empty() && serviceLookups == 0 && invokes.empty() && staticReads == 0 && dpiReads == 0,
        "Off: no texture, no lookup, no read");
    Check(loads.size() == 2 && LoadedAt(GameTexture(cursorType::Link), 0) && LoadedAt(GameTexture(Sword), 0) && customCursor::sizedLoads == 0,
        "Off: the player loads the game's cursors at Windows' size, as it makes them");
}
// Ticked: one shape a frame; the set complete, the game's cursor applied
// again at once, the mod's in its place.
void EnableBuildsAcrossFrames() {
    Fresh();
    module->settings.customCursor = true;
    module->settings.customCursorColor = Settings::CursorGold;
    for (int shape = 0; shape < cursorArt::ShapeCount; ++shape) {
        GameAsks(Map, cursorType::None, 0, false);
        ExpectSent(nullptr, {}, 0, "While the set is drawn: the game's cursor");
        Frame();
        Check(Made().size() == static_cast<size_t>(shape) + 1, "One texture a frame");
        Check(updates == (shape + 1 == cursorArt::ShapeCount ? 1 : 0), "One refresh, once the set is complete");
    }
    Check(customCursor::s.active, "The set in use");
    ExpectOurs(Shape::Arrow, "The refresh: the mod's arrow for the game's None, at its hotspot, CursorMode.Auto");
    CheckSet(CurrentSet(), 32, cursorArt::Colour::Gold, 0, 0, "Gold at 32 px");
    Check(Made().size() == 13 && TextureHandles() == 13, "Thirteen textures, each held once");
    Check(LoggedPrefix("CUSTOM_CURSOR_ON size=32 dpi=96 base=32 color=12 mid=#") == 1 && LoggedLine("CUSTOM_CURSOR_ON ").ends_with(" mode=hardware load=texture"),
        "CUSTOM_CURSOR_ON logs the size, its inputs and the mode: Windows cursors loaded at their size");
    Check(LoadedAt(Ours(Shape::Arrow), 32) && Logged("CUSTOM_CURSOR_LOADED size=32 windows=32") &&
        LoggedIndex("CUSTOM_CURSOR_ON ") < LoggedIndex("CUSTOM_CURSOR_LOADED "), "The player loads the arrow at 32 px, logged after CUSTOM_CURSOR_ON");
    Frames(50);
    Check(Made().size() == 13 && updates == 1, "Nothing drawn again while nothing changes");
}
// Every cursor DofusCursor asks for, and the other callers'.
void Substitution() {
    Fresh();
    Activate();
    struct Case { int32_t layer; int64_t type; int32_t state; bool texture; std::optional<Shape> shape; const char* what; };
    const Case cases[] = {
        {Map, cursorType::None, 0, false, Shape::Arrow, "None: the arrow"},
        {UiLayer, cursorType::Link, 0, true, Shape::Hand, "a button: the hand"},
        {Map, cursorType::Link, 0, true, Shape::Hand, "a cell the game marks: the hand"},
        {Frustum, cursorType::Link, 0, true, Shape::Hand, "a map border: the hand"},
        {UiLayer, cursorType::Beam, 0, true, Shape::Beam, "the game's text cursor"},
        {UiLayer, cursorType::Move, 0, true, Shape::Open, "a title bar: the open hand"},
        {UiLayer, cursorType::ResizeEW, 0, true, Shape::ResizeEW, "ResizeEW"},
        {UiLayer, cursorType::ResizeNS, 0, true, Shape::ResizeNS, "ResizeNS"},
        {UiLayer, cursorType::ResizeNWSE, 0, true, Shape::ResizeNWSE, "ResizeNWSE"},
        {UiLayer, cursorType::ResizeNESW, 0, true, Shape::ResizeNESW, "ResizeNESW"},
        {UiLayer, cursorType::Forbidden, 0, true, Shape::ArrowForbidden, "Forbidden"},
        {UiLayer, cursorType::Link, customCursor::StateDisabled, true, Shape::HandForbidden, "a disabled link"},
        {UiLayer, cursorType::Link, customCursor::StateWait, true, Shape::HandBusy, "a link not available yet"},
        {Interactive, Sword, 0, true, std::nullopt, "the sword"},
        {Interactive, Sword, customCursor::StateDisabled, true, std::nullopt, "the sword with its cross"},
        {Interactive, Bubble, 0, true, std::nullopt, "the bubble"},
        {Interactive, Gear, customCursor::StateWait, true, std::nullopt, "a tool waiting"},
        {Frustum, Ping, 0, true, std::nullopt, "a fight marker"},
        {Interactive, cursorType::NotFound, 0, true, std::nullopt, "NotFound"},
        {Interactive, Magnifier, 0, true, std::nullopt, "the magnifier"},
        {Interactive, 0x12345, 0, true, std::nullopt, "an unknown type"},
        {Interactive, Sword, 0, false, Shape::Arrow, "the sword without its texture"},
        {Interactive, Gear, customCursor::StateDisabled, false, Shape::ArrowForbidden, "a disabled tool without its texture"}};
    for (const auto& c : cases) {
        nowMs += 300; // past the dedupe of a cursor sent just before
        const auto before = sent.size();
        GameAsks(c.layer, c.type, c.state, c.texture);
        Check(sent.size() == before + 1, std::string("One call goes on: ") + c.what);
        if (c.shape) ExpectOurs(*c.shape, std::string("The mod's cursor for ") + c.what);
        else ExpectGame(c.type, std::string("The game's own cursor for ") + c.what);
    }
    // Move with the primary button held (a window dragged): the closed hand.
    nowMs += 300;
    buttonDown = true;
    GameAsks(UiLayer, cursorType::Move);
    ExpectOurs(Shape::Closed, "Move with the button held: the closed hand");
    buttonDown = false;
    // Another caller (UI Toolkit's CursorManager): its texture stays, its reset is the arrow.
    nowMs += 300;
    const auto uss = Make("UnityEngine.Texture2D");
    SetNative(uss, true);
    customCursor::OnSetCursor(uss, {5, 6}, 0, setCursorMethod, gameBase + CursorManagerReturn);
    ExpectSent(uss, {5, 6}, 0, "Another caller's texture: its own");
    customCursor::OnSetCursor(nullptr, {}, 0, setCursorMethod, gameBase + CursorManagerReturn);
    ExpectOurs(Shape::Arrow, "Another caller's reset: the mod's arrow");
    // Back from ApplyCursor without a Cursor in m_currentCursor, or with another object: not DofusCursor's request.
    nowMs += 300;
    currentCursor = nullptr;
    customCursor::OnSetCursor(nullptr, {}, 0, setCursorMethod, gameBase + ApplyCursorReturn);
    ExpectOurs(Shape::Arrow, "m_currentCursor null: another caller's reset, the arrow");
    customCursor::OnSetCursor(GameTexture(cursorType::Link), GameHotspot, 0, setCursorMethod, gameBase + ApplyCursorReturn);
    ExpectGame(cursorType::Link, "m_currentCursor null: a texture stays as it is");
    currentCursor = Make("Core.UILogic.CursorLayerSimple");
    Put(currentCursor, 0x18, cursorType::Link);
    customCursor::OnSetCursor(GameTexture(cursorType::Link), GameHotspot, 0, setCursorMethod, gameBase + ApplyCursorReturn);
    ExpectGame(cursorType::Link, "Another class in m_currentCursor: not read as a Cursor");
    // A Cursor in m_currentCursor, but a call from elsewhere: not DofusCursor's.
    currentCursor = CursorObject(UiLayer, cursorType::Link, 0);
    customCursor::OnSetCursor(GameTexture(cursorType::Link), GameHotspot, 0, setCursorMethod, gameBase + CursorManagerReturn);
    ExpectGame(cursorType::Link, "Only ApplyCursor's return marks DofusCursor's request");
    Check(!customCursor::s.faulted && updates == 1, "No error, no refresh");
}
// The same cursor of the mod sent again within 250 ms, nothing between: skipped.
void Dedupe() {
    Fresh();
    Activate();
    nowMs += 300;
    GameAsks(Map, cursorType::Link);
    ExpectOurs(Shape::Hand, "The hand");
    auto count = sent.size();
    nowMs += 100;
    GameAsks(Map, cursorType::Link);
    nowMs += 100;
    GameAsks(UiLayer, cursorType::Link);
    Check(sent.size() == count, "The same hand within 250 ms: skipped");
    nowMs += 60;
    GameAsks(Map, cursorType::Link);
    Check(sent.size() == count + 1, "250 ms after the last one sent: sent again");
    count = sent.size();
    GameAsks(Interactive, Sword);
    GameAsks(Map, cursorType::Link);
    Check(sent.size() == count + 2, "Another call between: sent again at once");
    ExpectOurs(Shape::Hand, "The hand after the sword");
    count = sent.size();
    customCursor::Refresh();
    Check(sent.size() == count + 1 && updates == 2, "A refresh is never skipped");
    count = sent.size();
    GameAsks(Map, cursorType::None, 0, false);
    nowMs += 10;
    GameAsks(Map, cursorType::None, 0, false);
    Check(sent.size() == count + 1, "The arrow skipped too");
    const auto uss = Make("UnityEngine.Texture2D");
    customCursor::OnSetCursor(uss, {1, 1}, 0, setCursorMethod, gameBase + CursorManagerReturn);
    customCursor::OnSetCursor(uss, {1, 1}, 0, setCursorMethod, gameBase + CursorManagerReturn);
    Check(sent.size() == count + 3, "Never a texture of another: only the mod's own are skipped");
}
// The inputs DofusCursor does not tell: an item dragged, the button held on a
// title bar, a text field under the arrow; each change applies the game's
// cursor again.
void HoverInputs() {
    Fresh();
    Activate();
    Check(serviceLookups == 1 && customCursor::s.drag.get() == dragService && Logged("CUSTOM_CURSOR_DRAG_SERVICE found"), "The drag service found once ready");
    nowMs += 300;
    GameAsks(UiLayer, cursorType::Link);
    ExpectOurs(Shape::Hand, "The hand");
    // An item dragged.
    Put<bool>(dragService, 0x10, true);
    Frame();
    Check(updates == 2, "A drag started: refreshed");
    ExpectOurs(Shape::Closed, "Dragging: the closed hand");
    GameAsks(Interactive, Sword);
    ExpectOurs(Shape::Closed, "Dragging over a monster: the closed hand");
    Frames(10);
    Check(updates == 2, "Still dragging: no refresh");
    Put<bool>(dragService, 0x10, false);
    Frame();
    Check(updates == 3, "The drag stopped: refreshed");
    ExpectGame(Sword, "After the drag: the game's sword again");
    // The button on a title bar.
    nowMs += 300;
    GameAsks(UiLayer, cursorType::Move);
    ExpectOurs(Shape::Open, "Move: the open hand");
    buttonDown = true;
    Frame();
    Check(updates == 4, "The button pressed on Move: refreshed");
    ExpectOurs(Shape::Closed, "Move with the button held: the closed hand");
    buttonDown = false;
    Frame();
    Check(updates == 5, "The button released: refreshed");
    ExpectOurs(Shape::Open, "Released: the open hand again");
    nowMs += 300;
    GameAsks(UiLayer, cursorType::Link);
    const auto reads = buttonReads;
    buttonDown = true;
    Frames(10);
    Check(buttonReads == reads && updates == 5, "The button is read for Move only");
    buttonDown = false;
    // A text field under the arrow.
    nowMs += 300;
    topElement = chatText;
    GameAsks(Map, cursorType::None, 0, false);
    ExpectOurs(Shape::Beam, "The arrow over the chat's input: the text cursor");
    const auto walks = parentCalls;
    Frames(10);
    Check(parentCalls == walks && updates == 5, "The same element: its ancestors never looked at again, no refresh");
    topElement = button;
    Frame();
    Check(updates == 6, "Left the text field: refreshed");
    ExpectOurs(Shape::Arrow, "Over a button the game does not mark: the arrow");
    topElement = searchText;
    Frame();
    Check(updates == 6, "A disabled text field: no change");
    nowMs += 300;
    GameAsks(Map, cursorType::None, 0, false);
    ExpectOurs(Shape::Arrow, "Over a disabled text field: the arrow");
    topElement = chatText;
    Frame();
    ExpectOurs(Shape::Beam, "Back over the chat: the text cursor");
    const auto top = topCalls;
    nowMs += 300;
    GameAsks(UiLayer, cursorType::Link);
    ExpectOurs(Shape::Hand, "A hand over the chat stays the hand");
    Frames(5);
    Check(topCalls == top, "The text field is read for the arrow only");
    // The pointer read fails: the arrow, the panel dropped, the cursors kept.
    Frame(2000); // the panel just looked up: the next lookup two seconds later
    nowMs += 300;
    topFails = true;
    GameAsks(Map, cursorType::None, 0, false);
    ExpectOurs(Shape::Arrow, "The pointer unreadable: the arrow");
    Check(!customCursor::s.panel.get() && !customCursor::s.faulted && customCursor::s.active, "A pointer failure drops the panel only");
    topFails = false;
    const auto afterFailure = topCalls;
    Frames(5);
    Check(topCalls == afterFailure, "No panel: the pointer is not read");
    Frame(2000);
    Check(customCursor::s.panel.get() == panel, "The panel found again two seconds later");
    ExpectOurs(Shape::Beam, "Then the text cursor again");
}
// Nothing changed: no refresh, no call.
void NoRefreshWhenUnchanged() {
    Fresh();
    Activate();
    nowMs += 300;
    GameAsks(UiLayer, cursorType::Link);
    const auto count = sent.size();
    Frames(200);
    Check(updates == 1 && sent.size() == count && Made().size() == 13, "Two hundred frames without a change: no refresh, nothing drawn");
}
// « Défaut » while the game's theme is unknown and its data not loaded: the
// accent and dark colours declared on the panel, followed when they change; a
// new set in use before the old one goes.
void ThemeColour() {
    Fresh();
    Get(visualRoot).variables["--highlight_primary100"] = {79 / 255.f, 156 / 255.f, 1.f, 1.f};
    Get(visualRoot).variables["--background_dark97"] = {41 / 255.f, 43 / 255.f, 51 / 255.f, .97f};
    Activate(Settings::CursorDefault);
    const uint32_t accent = 0x4F9CFF, dark = 0x292B33;
    Check(Logged("CUSTOM_CURSOR_THEME choice=0 theme=0 shown=0 source=panel accent=#4F9CFF dark=#292B33"), "The theme's colours read from the panel's root");
    const auto& look = customCursor::s.current.look;
    Check(look.colour == Settings::CursorDefault && look.accent == accent && look.dark == dark, "Drawn for the theme's colours");
    CheckSet(CurrentSet(), 32, cursorArt::Colour::Theme, accent, dark, "The theme's palette");
    // The player picks another theme.
    const auto old = CurrentSet();
    Get(visualRoot).variables["--highlight_primary100"] = {1.f, 84 / 255.f, 84 / 255.f, 1.f};
    events.clear();
    Frames(70); // the colours read again within 500 ms, then thirteen frames
    Check(Logged("CUSTOM_CURSOR_THEME choice=0 theme=0 shown=0 source=panel accent=#FF5454 dark=#292B33") && LoggedPrefix("CUSTOM_CURSOR_REDRAWN size=32") == 1,
        "The theme changed: drawn again");
    const auto fresh = CurrentSet();
    CheckSet(fresh, 32, cursorArt::Colour::Theme, 0xFF5454, dark, "The new theme's palette");
    std::vector<std::string> expected(13, "ctor");
    expected.push_back("update");
    expected.insert(expected.end(), 13, "destroy");
    Check(events == expected, "The new set made, the game's cursor applied again with it, then the old set destroyed");
    for (const auto texture : old) Check(Get(texture).destroyed, "The old set destroyed");
    for (const auto texture : fresh) Check(!Get(texture).destroyed, "The new set alive");
    Check(TextureHandles() == 13, "Only the set in use held");
    // While a set is drawn, the one in use stays.
    Get(visualRoot).variables["--highlight_primary100"] = {1.f, 1.f, 1.f, 1.f};
    Frame(500);
    Frames(3);
    nowMs += 300;
    GameAsks(UiLayer, cursorType::Link);
    ExpectOurs(Shape::Hand, "The set in use while the next one is drawn");
    Check(Ours(Shape::Hand) == fresh[static_cast<size_t>(Shape::Hand)], "Still the set before");
    // Gold: the theme no longer read, never drawn again for it.
    module->settings.customCursorColor = Settings::CursorGold;
    Frames(20);
    Check(customCursor::s.current.look.colour == Settings::CursorGold && customCursor::s.current.look.accent == 0, "Gold drawn");
    const auto made = Made().size();
    const auto reads = invokes["TryGetValue"], themes = themeReads;
    Get(visualRoot).variables["--highlight_primary100"] = {0.f, 1.f, 0.f, 1.f};
    gameThemeValue = 4;
    Frames(200);
    Check(Made().size() == made && invokes["TryGetValue"] == reads && themeReads == themes, "Gold ignores the theme");
    gameThemeValue = 0;
    // A theme without the variables: the Standard theme's colours.
    Get(visualRoot).variables.clear();
    module->settings.customCursorColor = Settings::CursorDefault;
    Frames(20);
    Check(Logged("CUSTOM_CURSOR_THEME choice=0 theme=0 shown=0 source=standard accent=#F0FF4F dark=#1B1D32") && customCursor::s.current.look.accent == customCursor::StandardAccent &&
        customCursor::s.current.look.dark == customCursor::StandardDark, "No variable: the Standard theme's colours");
    // Before any UIDocument: the Standard theme's colours too, never an error.
    Fresh();
    document = nullptr;
    Activate(Settings::CursorDefault);
    Check(customCursor::s.current.look.accent == customCursor::StandardAccent && !customCursor::s.root.get() && !customCursor::s.faulted,
        "No UIDocument yet: the Standard theme's colours");
}
// « Défaut » and each game theme (1.18.1): a theme's own colours through
// FigmaStylesScriptableObject, whatever theme the game shows, read once per
// theme; « Défaut » the theme the game shows, followed when the player picks
// another; without a theme's data, the panel's colours when the game shows
// that theme, the Standard theme's otherwise; a failure logged once.
void ThemeChoices() {
    // Eleven distinct palettes (ThemeEnum order), none of them the Standard one.
    uint32_t accents[12]{}, darks[12]{};
    for (int theme = 1; theme <= 11; ++theme) {
        accents[theme] = 0x204060u + static_cast<uint32_t>(theme) * 0x0A0B0C;
        darks[theme] = 0x101010u + static_cast<uint32_t>(theme) * 0x010203;
    }
    const auto setUp = [&] {
        figmaInstance = Make(FigmaStyles);
        for (int theme = 1; theme <= 11; ++theme) FigmaTheme(theme, accents[theme], darks[theme]);
    };
    const auto& look = customCursor::s.current.look;
    Fresh();
    setUp();
    gameThemeValue = 3; // Bonta
    Activate(Settings::CursorDefault);
    Check(look.colour == Settings::CursorDefault && look.accent == accents[3] && look.dark == darks[3] &&
        Logged("CUSTOM_CURSOR_THEME choice=0 theme=3 shown=3 source=theme accent=" + customCursor::Rgb(accents[3]) + " dark=" +
            customCursor::Rgb(darks[3])) &&
        Logged("CUSTOM_CURSOR_THEME_COLOURS theme=3 accent=" + customCursor::Rgb(accents[3]) + " dark=" + customCursor::Rgb(darks[3])),
        "« Défaut » while the game shows Bonta: Bonta's own colours");
    CheckSet(CurrentSet(), 32, cursorArt::Colour::Theme, accents[3], darks[3], "« Défaut (Bonta) »: Bonta's palette");
    // The player picks Brakmar in the game's options: « Défaut » follows.
    gameThemeValue = 4;
    Frames(90);
    Check(look.accent == accents[4] && look.dark == darks[4] && LoggedPrefix("CUSTOM_CURSOR_REDRAWN size=32") == 1 &&
        Logged("CUSTOM_CURSOR_THEME choice=0 theme=4 shown=4 source=theme accent=" + customCursor::Rgb(accents[4]) + " dark=" +
            customCursor::Rgb(darks[4])), "« Défaut » follows the game's theme");
    // Each theme on its own, whatever the game shows: its own colours, at once,
    // never the panel's.
    const auto panelReads = invokes["TryGetValue"];
    for (const auto& theme : gameTheme::Themes) {
        module->settings.customCursorColor = theme.id;
        Frames(14);
        Check(look.colour == theme.id && look.accent == accents[theme.id] && look.dark == darks[theme.id], std::string("Its own colours: ") + theme.name);
        CheckSet(CurrentSet(), 32, cursorArt::Colour::Theme, accents[theme.id], darks[theme.id], std::string("Its own palette: ") + theme.name);
    }
    Check(invokes["TryGetValue"] == panelReads && LoggedPrefix("CUSTOM_CURSOR_THEME_COLOURS theme=") == 11,
        "Each theme's colours read once, never the panel's");
    // A theme of its own ignores the game's theme: nothing drawn again, nothing read again.
    const auto made = Made().size();
    const auto reads = figmaReads;
    gameThemeValue = 9;
    Frames(200);
    Check(Made().size() == made && figmaReads == reads && look.colour == 11 && look.accent == accents[11], "Wabbit stays Wabbit, read once");
    // « Défaut » again: the game's Sufokia, from the colours already read.
    module->settings.customCursorColor = Settings::CursorDefault;
    Frames(14);
    Check(look.accent == accents[9] && figmaReads == reads, "« Défaut »: the game's theme, from the colours already read");
    // « Or », « Émeraude », « Argent » keep their palettes.
    for (const auto& [choice, colour] : {std::pair{Settings::CursorGold, cursorArt::Colour::Gold},
             std::pair{Settings::CursorEmerald, cursorArt::Colour::Emerald}, std::pair{Settings::CursorSilver, cursorArt::Colour::Silver}}) {
        module->settings.customCursorColor = choice;
        Frames(14);
        Check(look.colour == choice && look.accent == 0 && look.dark == 0, "A fixed colour draws no theme");
        CheckSet(CurrentSet(), 32, colour, 0, 0, "A fixed colour's own palette");
    }
    // A colour out of range in the settings (never saved): « Défaut ».
    module->settings.customCursorColor = 15;
    Frames(14);
    Check(look.colour == Settings::CursorDefault && look.accent == accents[9], "An unknown choice draws « Défaut »");

    // A theme its data lacks, or gives an unusable colour: logged once, the
    // Standard theme's colours while the game shows another theme, the panel's
    // once it shows that one.
    Fresh();
    setUp();
    figmaColours.erase({6, "--background_dark97"});
    figmaColours[{7, "--highlight_primary100"}] = {2.f, 0.f, 0.f, 1.f};
    figmaColours[{8, "--highlight_primary100"}] = {1.f, 0.f, 0.f, 0.f};
    Get(visualRoot).variables["--highlight_primary100"] = {1.f, 0.f, 0.f, 1.f};
    gameThemeValue = 3;
    for (const int theme : {6, 7, 8}) {
        if (theme == 6) Activate(theme);
        else { module->settings.customCursorColor = theme; Frames(14); }
        Check(LoggedPrefix("CUSTOM_CURSOR_THEME_COLOURS_ERROR theme=" + std::to_string(theme) + " ") == 1 &&
            look.accent == customCursor::StandardAccent && look.dark == customCursor::StandardDark &&
            Logged("CUSTOM_CURSOR_THEME choice=" + std::to_string(theme) + " theme=" + std::to_string(theme) +
                " shown=3 source=standard accent=#F0FF4F dark=#1B1D32"),
            "A theme without usable colours, not shown: the Standard theme's");
    }
    Frames(200);
    Check(LoggedPrefix("CUSTOM_CURSOR_THEME_COLOURS_ERROR theme=8 ") == 1 && !customCursor::s.faulted, "Retried, logged once, never a fault");
    gameThemeValue = 8;
    Frames(90);
    Check(look.accent == 0xFF0000 && look.dark == customCursor::StandardDark &&
        Logged("CUSTOM_CURSOR_THEME choice=8 theme=8 shown=8 source=panel accent=#FF0000 dark=#1B1D32"),
        "The game shows that theme: the panel's colours");

    // Before the game's data (no instance yet): the panel's colours for the
    // theme shown, then its own once loaded.
    Fresh();
    gameThemeValue = 2;
    Get(visualRoot).variables["--highlight_primary100"] = {1.f, 0.f, 0.f, 1.f};
    Activate(Settings::CursorDefault);
    Check(look.accent == 0xFF0000 && Logged("CUSTOM_CURSOR_THEME choice=0 theme=2 shown=2 source=panel accent=#FF0000 dark=#1B1D32") &&
        LoggedPrefix("CUSTOM_CURSOR_THEME_COLOURS_ERROR") == 0, "No data yet: the panel's colours, no error");
    setUp();
    Frames(90);
    Check(look.accent == accents[2] && look.dark == darks[2] && Logged("CUSTOM_CURSOR_THEME_COLOURS theme=2 accent=" + customCursor::Rgb(accents[2]) +
        " dark=" + customCursor::Rgb(darks[2])), "The data loaded: the theme's own colours");
}
// « Taille du curseur » and the window's scale.
void SizeChanges() {
    Fresh();
    Activate();
    dpi = 144;
    Frames(300); // read within a second, then thirteen frames
    Check(customCursor::s.current.look.size == 48 && LoggedPrefix("CUSTOM_CURSOR_REDRAWN size=48 dpi=144 base=32 color=12") == 1,
        "150 %: drawn again at 48 px");
    CheckSet(CurrentSet(), 48, cursorArt::Colour::Gold, 0, 0, "Gold at 48 px");
    nowMs += 300;
    GameAsks(UiLayer, cursorType::Link);
    ExpectOurs(Shape::Hand, "The hand at 48 px, at its hotspot", 48);
    module->settings.customCursorSize = 1;
    Frames(13);
    Check(customCursor::s.current.look.size == 72, "« Grande » at 150 %: 72 px, at once");
    module->settings.customCursorSize = 2;
    Frames(13);
    Check(customCursor::s.current.look.size == 96, "« Très grande » at 150 %: 96 px");
    dpi = 192;
    pointerBase = 48;
    Frames(300);
    Check(customCursor::s.current.look.size == 128, "Never above 128 px");
    module->settings.customCursorSize = 0;
    dpi = 96;
    Frames(300);
    Check(customCursor::s.current.look.size == 48, "A Windows pointer enlarged to 48: 48 px at 100 %");
}
bool AllAuto() { return std::all_of(sent.begin(), sent.end(), [](const Sent& call) { return call.mode == 0; }); }
// « Taille du curseur » as Windows shows it: the player makes the Windows
// cursor of each texture of the mod at that texture's size (its own load at
// Windows' size changed, custom-cursor-player.hpp), always with
// CursorMode.Auto, never a software cursor; the game's own cursors keep
// Windows' size.
void SizeLoads() {
    // Every choice at 100 % and 150 %: the refresh's arrow, then the hand.
    const std::tuple<unsigned, int, int> cases[] = {{96, 0, 32}, {96, 1, 48}, {96, 2, 64}, {144, 0, 48}, {144, 1, 72}, {144, 2, 96}};
    for (const auto& [scale, choice, pixels] : cases) {
        Fresh();
        dpi = scale;
        Activate(Settings::CursorGold, choice);
        const std::string what = std::to_string(pixels) + " px (dpi " + std::to_string(scale) + ", choice " + std::to_string(choice) + ")";
        Check(customCursor::s.current.look.size == pixels && LoadedAt(Ours(Shape::Arrow), pixels), "The arrow loaded at " + what);
        nowMs += 300;
        GameAsks(UiLayer, cursorType::Link);
        ExpectOurs(Shape::Hand, "The hand at " + what, pixels);
        Check(LoadedAt(Ours(Shape::Hand), pixels), "The hand loaded at " + what);
        GameAsks(Interactive, Sword);
        ExpectGame(Sword, "The sword stays the game's: " + what);
        Check(LoadedAt(GameTexture(Sword), 0), "The game's sword at Windows' size, as the player makes it: " + what);
        Check(LoggedLine("CUSTOM_CURSOR_ON size=" + std::to_string(pixels) + " dpi=" + std::to_string(scale) + " base=32 color=12 mid=#")
                .ends_with(" mode=hardware load=texture") &&
            LoggedPrefix("CUSTOM_CURSOR_LOADED ") == 1 && Logged("CUSTOM_CURSOR_LOADED size=" + std::to_string(pixels) + " windows=32"),
            "CUSTOM_CURSOR_ON with the mode, then CUSTOM_CURSOR_LOADED once for the set: " + what);
        Check(AllAuto() && customCursor::sizedLoads == 2, "Always CursorMode.Auto, only the mod's two loads changed: " + what);
        // Sent again: the player reuses the cursor it made, nothing loaded again.
        const auto count = loads.size();
        nowMs += 300;
        GameAsks(UiLayer, cursorType::Link);
        ExpectOurs(Shape::Hand, "The hand again at " + what, pixels);
        Check(loads.size() == count, "The player's own cursor reused: " + what);
    }
    // A switch of size: the new set made, applied in one call, its cursors
    // loaded at the new size; then the old set destroyed. Never two cursors,
    // never none.
    Fresh();
    Activate(Settings::CursorGold, 2);
    nowMs += 300;
    GameAsks(UiLayer, cursorType::Link);
    const Obj big = Ours(Shape::Hand);
    Check(LoadedAt(big, 64), "« Très grande » at 100 %: the hand at 64 px");
    events.clear();
    const auto calls = sent.size();
    module->settings.customCursorSize = 0;
    Frames(13);
    ExpectOurs(Shape::Hand, "« Comme la souris de Windows »: the hand at 32 px, at once", 32);
    std::vector<std::string> expected(13, "ctor");
    expected.push_back("update");
    expected.insert(expected.end(), 13, "destroy");
    Check(events == expected && sent.size() == calls + 1 && Get(big).destroyed && LoadedAt(Ours(Shape::Hand), 32),
        "Switched: the new hand sent once and loaded at 32 px, then the 64 px set destroyed");
    Check(LoggedLine("CUSTOM_CURSOR_REDRAWN size=32 dpi=96 base=32 color=12 mid=#").ends_with(" mode=hardware load=texture") &&
        Logged("CUSTOM_CURSOR_LOADED size=64 windows=32") && Logged("CUSTOM_CURSOR_LOADED size=32 windows=32") && AllAuto(),
        "Each set logged with its mode and its load");
    // While the player makes a cursor of the mod, only its own load changes,
    // on its thread: another thread's, or other arguments, go on as they are.
    Fresh();
    Activate(Settings::CursorGold, 1);
    probeLoads = true;
    nowMs += 300;
    GameAsks(UiLayer, cursorType::Link);
    probeLoads = false;
    Check(LoadedAt(Ours(Shape::Hand), 48), "« Grande » at 100 %: the hand at 48 px");
    const auto probe = [](const wchar_t* file) -> const Load& {
        const auto found = std::find_if(loads.begin(), loads.end(), [&](const Load& load) { return load.file == file; });
        Check(found != loads.end(), "Probed load made");
        return *found;
    };
    Check(probe(L"other thread").thread != GetCurrentThreadId() && probe(L"other thread").cx == 0 && probe(L"other thread").cy == 0 &&
        probe(L"other thread").flags == 0x50, "Another thread's load at that moment: unchanged");
    Check(probe(L"icon").type == 1 && probe(L"icon").cx == 0 && probe(L"icon").flags == 0x50, "An icon: unchanged");
    Check(probe(L"sized").cx == 32 && probe(L"sized").cy == 32 && probe(L"sized").flags == 0x50, "A size asked: unchanged");
    Check(probe(L"actual size").cx == 0 && probe(L"actual size").flags == 0x10, "Other flags: unchanged");
    Check(probe(L"resource").instance != nullptr && probe(L"resource").cx == 0 && probe(L"resource").flags == 0x50, "From a module: unchanged");
    customCursor::LoadImageHook(nullptr, L"after", 2, 0, 0, 0x50);
    Check(loads.back().file == L"after" && loads.back().cx == 0 && loads.back().flags == 0x50 && customCursor::sizedPixels == 0 &&
        customCursor::sizedThread == 0 && customCursor::sizedLoads == 2, "Once the call is over: unchanged, nothing left set");
    // Unticked: the game's own cursors as the player makes them.
    module->settings.customCursor = false;
    Frame();
    ExpectGame(cursorType::Link, "Unticked: the game's hand");
    Check(LoadedAt(GameTexture(cursorType::Link), 0) && customCursor::sizedLoads == 2, "Unticked: the game's hand at Windows' size");
    // Ticked again, then Stop: the game's cursor back as the player makes it,
    // nothing changed after.
    Activate(Settings::CursorGold, 2);
    nowMs += 300;
    GameAsks(UiLayer, cursorType::Move);
    Check(LoadedAt(Ours(Shape::Open), 64), "Ticked again: the open hand at 64 px");
    const unsigned changed = customCursor::sizedLoads;
    module->stopped = true;
    RestoreCustomCursor();
    ExpectGame(cursorType::Move, "Stop: the game's own move cursor");
    GameAsks(UiLayer, cursorType::ResizeNS);
    ExpectGame(cursorType::ResizeNS, "Stopped: the game's resize cursor");
    Check(LoadedAt(GameTexture(cursorType::Move), 0) && LoadedAt(GameTexture(cursorType::ResizeNS), 0) && customCursor::sizedLoads == changed &&
        customCursor::sizedPixels == 0 && AllAuto(), "Stop: every load as the player makes it, never ForceSoftware");
}
// The size part off (another player build, no LoadImageW, the hook refused):
// the cursors still there, at Windows' size as in 1.17.0, and logged.
void SizePartUnavailable() {
    struct Break { const char* what; std::function<void()> breakIt; const char* reason; };
    const Break breaks[] = {
        {"UnityPlayer.dll missing", [] { playerMissing = true; }, "UnityPlayer.dll does not load cursors as the supported client's"},
        {"another UnityPlayer.dll", [] { playerBytes[61] ^= 0x01; }, "UnityPlayer.dll does not load cursors as the supported client's"},
        {"no LoadImageW", [] { loadImageMissing = true; }, "LoadImageW unavailable"},
        {"the hook refused", [] { systemHookFails = true; }, "Injected hook failure"}};
    for (const auto& [what, breakIt, reason] : breaks) {
        Fresh(false);
        breakIt();
        InstallCustomCursor();
        const std::string name = what;
        Check(customCursor::s.installed && !customCursor::n.sized && hooks.size() == 1 && systemHooks.empty() &&
            Logged(std::string("CUSTOM_CURSOR_PART_UNAVAILABLE size ") + reason) && Logged("CUSTOM_CURSOR_READY textHover=1 theme=1 themes=1 drag=1 size=0"),
            name + ": the size part off and logged, the cursors installed");
        Activate(Settings::CursorGold, 2);
        nowMs += 300;
        GameAsks(UiLayer, cursorType::Link);
        ExpectOurs(Shape::Hand, name + ": the mod's hand still", 64);
        Check(LoadedAt(Ours(Shape::Hand), 0) && LoadedAt(Ours(Shape::Arrow), 0) && customCursor::sizedLoads == 0 &&
            LoggedPrefix("CUSTOM_CURSOR_LOADED") == 0 && AllAuto(), name + ": loaded at Windows' size, as 1.17.0");
        Check(LoggedLine("CUSTOM_CURSOR_ON size=64 ").ends_with(" mode=hardware load=windows"), name + ": CUSTOM_CURSOR_ON says so");
    }
}
// Unticked: the game's cursor back in the same frame, the textures destroyed.
void DisableRestores() {
    Fresh();
    Activate();
    nowMs += 300;
    GameAsks(UiLayer, cursorType::Link);
    ExpectOurs(Shape::Hand, "The hand");
    const auto set = CurrentSet();
    events.clear();
    module->settings.customCursor = false;
    Frame();
    ExpectGame(cursorType::Link, "Unticked: the game's own hand at once");
    std::vector<std::string> expected{"update"};
    expected.insert(expected.end(), 13, "destroy");
    Check(events == expected && Logged("CUSTOM_CURSOR_OFF option"), "The game's cursor applied again first, then the thirteen textures destroyed");
    for (const auto texture : set) Check(Get(texture).destroyed, "Every texture destroyed");
    Check(TextureHandles() == 0 && !customCursor::s.active && !CustomCursorCleanupPending(), "Nothing of the mod left");
    GameAsks(Map, cursorType::None, 0, false);
    ExpectSent(nullptr, {}, 0, "Unticked: the Windows arrow again");
    Frames(30);
    Check(Made().size() == 13 && updates == 2, "Unticked: nothing drawn, nothing refreshed");
    Activate();
    Check(Made().size() == 26 && updates == 3, "Ticked again: a new set, applied");
    nowMs += 300;
    GameAsks(UiLayer, cursorType::Link);
    ExpectOurs(Shape::Hand, "Ticked again: the hand");
    // Unticked while a set is drawn: the textures made so far destroyed.
    Fresh();
    module->settings.customCursor = true;
    Frames(5);
    module->settings.customCursor = false;
    Frame();
    Check(updates == 0 && destroyAttempts == 5 && TextureHandles() == 0 && !Logged("CUSTOM_CURSOR_OFF option"),
        "Unticked before the set is complete: its five textures destroyed, nothing to refresh");
}
// Stop: first of the restores, the game's cursor back, every reference released.
void StopRestores() {
    Fresh();
    Activate(Settings::CursorDefault);
    nowMs += 300;
    GameAsks(UiLayer, cursorType::Link);
    Put<bool>(dragService, 0x10, false);
    const auto set = CurrentSet();
    module->stopped = true; // Module::Stop, before its restores
    RestoreCustomCursor();
    ExpectGame(cursorType::Link, "Stop: the game's own hand, applied again");
    for (const auto texture : set) Check(Get(texture).destroyed, "Stop: every texture destroyed");
    Check(handles.empty() && !CustomCursorCleanupPending() && Logged("CUSTOM_CURSOR_OFF stop"), "Stop: no reference left, nothing pending");
    GameAsks(Map, cursorType::None, 0, false);
    ExpectSent(nullptr, {}, 0, "Stopped: every call goes on unchanged");
    const auto made = Made().size();
    Frames(30);
    Check(Made().size() == made, "Stopped: nothing drawn");
    RestoreCustomCursor();
    Check(handles.empty(), "A second restore changes nothing");
    // Stopped without a restore yet: the detour already forwards only.
    Fresh();
    Activate();
    module->stopped = true;
    GameAsks(UiLayer, cursorType::Link);
    ExpectGame(cursorType::Link, "Stopped: the detour forwards the game's call");
    module->stopped = false;
    module->settings.customCursor = false;
    Frame();
    // Stopped while a set was drawn, never active: nothing to refresh.
    Fresh();
    module->settings.customCursor = true;
    Frames(4);
    module->stopped = true;
    RestoreCustomCursor();
    Check(updates == 0 && destroyAttempts == 4 && handles.empty(), "Stopped mid-drawing: the four textures destroyed");
}
// A destroy that fails: kept, tried again twice a second, abandoned after
// twenty tries, or at once when nothing would try again.
void CleanupRetries() {
    Fresh();
    module->compositor.reset(compositor);
    Activate();
    destroyFails = -1;
    module->stopped = true;
    RestoreCustomCursor();
    Check(CustomCursorCleanupPending() && Logged("CUSTOM_CURSOR_CLEANUP_ERROR Injected destroy failure"), "A failed destroy keeps the cleanup pending");
    auto attempts = destroyAttempts;
    RestoreCustomCursor();
    Check(destroyAttempts == attempts, "Not tried again within 500 ms");
    nowMs += 500;
    RestoreCustomCursor();
    Check(destroyAttempts > attempts && CustomCursorCleanupPending(), "Tried again 500 ms later");
    destroyFails = 0;
    nowMs += 500;
    RestoreCustomCursor();
    Check(!CustomCursorCleanupPending() && TextureHandles() == 0 && LoggedPrefix("CUSTOM_CURSOR_RESTORE_ABANDONED") == 0,
        "The destroy succeeds: nothing pending");
    for (const auto texture : Made()) Check(Get(texture).destroyed, "Every texture destroyed in the end");
    module->compositor.reset();
    // Always failing: abandoned after twenty tries.
    Fresh();
    module->compositor.reset(compositor);
    Activate();
    destroyFails = -1;
    module->stopped = true;
    RestoreCustomCursor();
    int tries = 1;
    while (CustomCursorCleanupPending() && tries < 40) {
        nowMs += 500;
        RestoreCustomCursor();
        ++tries;
    }
    Check(tries == 20 && !CustomCursorCleanupPending() && TextureHandles() == 0 && LoggedPrefix("CUSTOM_CURSOR_RESTORE_ABANDONED 13 textures") == 1,
        "Abandoned after twenty tries, every reference released");
    module->compositor.reset();
    // Stopped before the services were ready: abandoned at once.
    Fresh();
    Activate();
    destroyFails = -1;
    module->stopped = true;
    RestoreCustomCursor();
    Check(!CustomCursorCleanupPending() && LoggedPrefix("CUSTOM_CURSOR_RESTORE_ABANDONED 13 textures") == 1 && TextureHandles() == 0,
        "No compositor to try again: abandoned at once");
    // A destroy failed less than 500 ms before a Stop without compositor: the
    // Stop's try is the last one, never put off (nothing would end the stop).
    for (const bool stillFails : {false, true}) {
        Fresh();
        Activate();
        destroyFails = 1;
        module->settings.customCursorColor = Settings::CursorEmerald;
        Frames(13);
        Check(CustomCursorCleanupPending() && customCursor::s.doomed.size() == 1, "The old set's destroy failed once: pending");
        nowMs += 100;
        if (stillFails) destroyFails = -1;
        const auto before = destroyAttempts;
        module->stopped = true;
        RestoreCustomCursor();
        Check(destroyAttempts == before + 14 && !CustomCursorCleanupPending() && TextureHandles() == 0 &&
            LoggedPrefix("CUSTOM_CURSOR_RESTORE_ABANDONED 14 textures") == (stillFails ? 1u : 0u),
            stillFails ? "Still failing at a Stop 100 ms later: the fourteen tried at once, then abandoned"
                       : "A Stop 100 ms later: the fourteen destroyed at once, nothing pending");
    }
    // A failed destroy while running: tried again by the frames.
    Fresh();
    Activate();
    destroyFails = 2;
    module->settings.customCursorColor = Settings::CursorEmerald;
    Frames(13);
    Check(CustomCursorCleanupPending(), "The old set's destroy failed: pending");
    Frame(500);
    Check(!CustomCursorCleanupPending() && TextureHandles() == 13, "The frames destroy it later");
}
// An error inside the detour: the call goes on unchanged, logged once, the
// cursors off until the option is ticked again.
void HookErrors() {
    Fresh();
    Activate();
    staticReadFails = true;
    const auto reads = staticReads;
    nowMs += 300;
    GameAsks(UiLayer, cursorType::Link);
    ExpectGame(cursorType::Link, "An error in the detour: the game's call goes on unchanged");
    GameAsks(Interactive, Sword);
    ExpectGame(Sword, "Faulted: every call unchanged");
    Check(LoggedPrefix("CUSTOM_CURSOR_HOOK_ERROR Injected static read failure") == 1 && staticReads == reads + 1, "Logged once; never read again");
    staticReadFails = false;
    Frame();
    Check(Logged("CUSTOM_CURSOR_OFF faulted") && TextureHandles() == 0 && !customCursor::s.active, "The next frame turns the cursors off");
    const auto made = Made().size();
    Frames(50);
    Check(Made().size() == made, "Off while still ticked");
    module->settings.customCursor = false;
    Frame();
    Activate();
    nowMs += 300;
    GameAsks(UiLayer, cursorType::Link);
    ExpectOurs(Shape::Hand, "Ticked again: the cursors back");
}
// Repeated frame errors: off until ticked again; nothing left behind.
void FrameErrors() {
    Fresh();
    module->settings.customCursor = true;
    ctorFails = -1;
    Frames(3);
    Check(LoggedPrefix("CUSTOM_CURSOR_FRAME_ERROR Injected constructor failure") == 1 && Logged("CUSTOM_CURSOR_FAULTED repeated errors; the game's cursor until the option is ticked again"),
        "Three errors within ten seconds: faulted, the error logged once");
    Frame();
    Check(TextureHandles() == 0 && destroyAttempts == 0 && !CustomCursorCleanupPending(), "The unconstructed textures dropped, never destroyed");
    ctorFails = 0;
    Frames(30);
    Check(!customCursor::s.active && Made().size() == 3, "Off while still ticked");
    module->settings.customCursor = false;
    Frame();
    Activate();
    Check(customCursor::s.active, "Ticked again: drawn");
    // Errors far apart never fault.
    Fresh();
    module->settings.customCursor = true;
    for (int i = 0; i < 4; ++i) {
        ctorFails = 1;
        Frame();
        Frame(11000);
    }
    Check(!customCursor::s.faulted && !Logged("CUSTOM_CURSOR_FAULTED repeated errors; the game's cursor until the option is ticked again"),
        "Errors more than ten seconds apart: not faulted");
}
// A texture of the mod destroyed by something else: the game's cursor for its
// shape, then the set drawn again.
void LostTexture() {
    Fresh();
    Activate();
    SetNative(Ours(Shape::Hand), false);
    nowMs += 300;
    GameAsks(UiLayer, cursorType::Link);
    ExpectGame(cursorType::Link, "A destroyed texture of the mod: the game's cursor");
    GameAsks(Map, cursorType::None, 0, false);
    ExpectOurs(Shape::Arrow, "The other shapes still the mod's");
    Frame(1000);
    Check(Logged("CUSTOM_CURSOR_LOST a texture of the mod was destroyed; drawing them again") && !customCursor::s.active && destroyAttempts == 12,
        "Checked within a second: the set dropped, its twelve live textures destroyed");
    Frames(13);
    Check(customCursor::s.active, "Drawn again");
    nowMs += 300;
    GameAsks(UiLayer, cursorType::Link);
    ExpectOurs(Shape::Hand, "The hand back");
}
// UpdateCursorTexture failing: the plain arrow instead, the mod's or Windows'.
void RefreshFailure() {
    Fresh();
    updateFails = true;
    Activate();
    Check(LoggedPrefix("CUSTOM_CURSOR_REFRESH_ERROR Injected refresh failure") == 1, "Logged");
    ExpectOurs(Shape::Arrow, "The refresh failed: the mod's arrow sent through the trampoline");
    module->settings.customCursor = false;
    Frame();
    ExpectSent(nullptr, {}, 0, "Unticked and the refresh failed: the Windows arrow sent through the trampoline");
    Check(TextureHandles() == 0, "The textures destroyed after");
}
// The first 64 distinct requests logged.
void SeenLog() {
    Fresh();
    Activate();
    Check(Logged("CUSTOM_CURSOR_SEEN caller=DofusCursor layer=0 type=-1 state=0 texture=none cursor=arrow"), "The refresh's None logged");
    GameAsks(UiLayer, cursorType::Link);
    GameAsks(UiLayer, cursorType::Link);
    Check(LoggedPrefix("CUSTOM_CURSOR_SEEN caller=DofusCursor layer=3 type=0x200000 state=0 texture=game cursor=hand") == 1, "A request logged once");
    GameAsks(Interactive, Sword, customCursor::StateDisabled);
    Check(Logged("CUSTOM_CURSOR_SEEN caller=DofusCursor layer=1 type=0x8 state=1 texture=game cursor=game"), "The game's own cursor logged as such");
    const auto uss = Make("UnityEngine.Texture2D");
    customCursor::OnSetCursor(uss, {}, 0, setCursorMethod, gameBase + CursorManagerReturn);
    Check(Logged("CUSTOM_CURSOR_SEEN caller=0x5752D85 layer=0 type=-1 state=0 texture=game cursor=game"), "Another caller logged by its address");
    for (int64_t type = 0x100; type < 0x100 + 100; ++type) GameAsks(Interactive, type);
    Check(LoggedPrefix("CUSTOM_CURSOR_SEEN ") == 64, "At most 64 requests logged");
}
}

int main() {
    try {
        fake::Object probe;
        fake::Check(reinterpret_cast<char*>(probe.fields) - reinterpret_cast<char*>(&probe) == 0x10, "Fake objects keep IL2CPP's field offsets");
        fake::PolicyTable();
        fake::InstallContracts();
        fake::RequiredContractMissing();
        fake::OptionalParts();
        fake::OptionOffPassesThrough();
        fake::EnableBuildsAcrossFrames();
        fake::Substitution();
        fake::Dedupe();
        fake::HoverInputs();
        fake::NoRefreshWhenUnchanged();
        fake::ThemeColour();
        fake::ThemeChoices();
        fake::SizeChanges();
        fake::SizeLoads();
        fake::SizePartUnavailable();
        fake::DisableRestores();
        fake::StopRestores();
        fake::CleanupRetries();
        fake::HookErrors();
        fake::FrameErrors();
        fake::LostTexture();
        fake::RefreshFailure();
        fake::SeenLog();
        fake::Fresh(false);
        delete module;
        module = nullptr;
        std::cout << "PASS: « Curseur Dofus » runtime: the policy table (the arrow, the hand on every layer, the text cursor, the open and "
            "closed hands, the four resizes, the badges, the game's icons and unknown types kept, another caller's texture kept and its "
            "reset replaced), the sizes, the exact contracts (the detour added after the game's, then LoadImageW once the player's "
            "loading is checked), a required contract missing (no hook) or an optional one (its part off; without the size part, "
            "Windows' size as in 1.17.0), the option off (every call unchanged), the set drawn one shape a frame then applied at once, "
            "the dedupe, the drag, the button and the text field read and followed, the theme's colours followed (a new set in use "
            "before the old one goes), « Défaut » following the game's theme and each of its eleven themes in its own colours (read once "
            "through FigmaStylesScriptableObject, the panel's or the Standard theme's without them), the window's scale and the size choice, each size a Windows cursor (CursorMode.Auto) loaded by "
            "the player at the texture's size at 100 % and 150 % (only the mod's own loads, on their thread; the game's at Windows' "
            "size; a switch applied in one call), the mode and the loads logged, unticking and Stop (the game's cursor back first, every "
            "texture destroyed, every reference released), failed destroys retried then abandoned (at once by a Stop before the "
            "services, even within the retry wait), errors in the detour (forwarded, "
            "then off) and in the frames, a lost texture, a failed refresh, the requests logged. No game process accessed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << "\n";
        return 1;
    }
}
