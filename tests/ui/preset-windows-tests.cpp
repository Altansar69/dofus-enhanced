#include "ui/preset-windows.hpp"
#include "ui/preset-windows-store.hpp"
#include "ui/tools-ui.hpp"
#include "notebook/notebook-panel.hpp"
#include "core/module.hpp"
#include "appearance/features.hpp"
#include "ui/options-ui.hpp"
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>

// The game's saved layouts and the mod's windows on a fake IL2CPP runtime: the
// production hooks, capture and apply of preset-windows.cpp, the real file in a
// temporary folder. No client, process, settings or layouts of the player.
namespace fake {
using namespace managed;
struct Type { std::string name; };
struct MethodData { Type* type; std::string name; int argc; uintptr_t rva; };
struct FieldData { std::string name; size_t offset; };
struct Rect { float x, y, width, height; };
struct Object {
    std::array<unsigned char, 0x100> raw{}; // first: Read<T>(object, offset) and unboxing read it
    Type* type = nullptr;
    std::string text;
    std::vector<Obj> items;
    Obj property = nullptr;
    bool loaded = true;
    Obj parent = nullptr;          // an element's layer; a style's element
    Rect base{};                   // an element's layout before any inline value
    std::map<int, float> style;    // inline left, top, width, height written (Property order)
};
std::map<std::string, std::unique_ptr<Type>> types;
std::vector<std::unique_ptr<Object>> objects;
std::vector<std::unique_ptr<MethodData>> methods;
std::map<std::string, std::unique_ptr<FieldData>> fields;
std::map<GCHandle, Obj> handles;
GCHandle nextHandle = 0x5A5A00000000ULL;
std::vector<std::string> logs;
void Check(bool ok, const std::string& message) { if (!ok) throw std::runtime_error(message); }
Object& Get(Obj value) { Check(value != nullptr, "Null fixture object"); return *static_cast<Object*>(value); }
Type* TypeOf(const std::string& name) { auto& type = types[name]; if (!type) type = std::make_unique<Type>(Type{name}); return type.get(); }
Obj Make(const std::string& type) {
    auto object = std::make_unique<Object>(); object->type = TypeOf(type); auto raw = object.get(); objects.push_back(std::move(object)); return raw;
}
template<class T> Obj Box(T value, const char* type = "Box") { auto box = Make(type); std::memcpy(Get(box).raw.data(), &value, sizeof(T)); return box; }
template<class T> void Put(Obj owner, size_t offset, T value) { std::memcpy(Get(owner).raw.data() + offset, &value, sizeof(T)); }
template<class T> T Take(Obj owner, size_t offset) { T value; std::memcpy(&value, Get(owner).raw.data() + offset, sizeof(T)); return value; }

// ------------------------------------------------------------------ the game
using IntFn = void (*)(Obj, int, Method);
using AddFn = void (*)(Obj, Obj, Method);
using EditFn = void (*)(Obj, int, Obj, int, Method);
IntFn saveHook = nullptr, removeHook = nullptr, applyHook = nullptr;
AddFn addHook = nullptr;
EditFn editHook = nullptr;
Obj cached = nullptr, container = nullptr, presets = nullptr, root = nullptr, fightId = nullptr;
int nextUid = 0, applies = 0, saves = 0;
bool inFight = false, fightThrows = false;
Obj Preset(int uid, const std::string& name) {
    auto preset = Make("PresetData"); Put<int>(preset, 0xA8, uid);
    auto text = Make("String"); Get(text).text = name; Put<Obj>(preset, 0x18, text);
    return preset;
}
Obj Find(int uid) {
    for (auto preset : Get(presets).items) if (Take<int>(preset, 0xA8) == uid) return preset;
    return nullptr;
}
void NativeSave(Obj, int, Method) { ++saves; }
void NativeAdd(Obj, Obj preset, Method) {
    if (!preset) preset = Preset(nextUid, "Affichage " + std::to_string(nextUid));
    ++nextUid;
    Get(presets).items.push_back(preset);
}
void NativeRemove(Obj, int uid, Method) {
    auto& items = Get(presets).items;
    std::erase_if(items, [uid](Obj preset) { return Take<int>(preset, 0xA8) == uid; });
}
void NativeApply(Obj, int, Method) { ++applies; }
void NativeEdit(Obj, int uid, Obj name, int, Method) { if (auto preset = Find(uid)) Put<Obj>(preset, 0x18, name); }
// The player's own actions, through the game's methods (hooked).
void Save(int uid) { saveHook(container, uid, nullptr); }
void Add(Obj preset) { addHook(container, preset, nullptr); }
void Remove(int uid) { removeHook(container, uid, nullptr); }
void Apply(int uid) { applyHook(container, uid, nullptr); }
void Rename(int uid, const char* name) { auto text = Make("String"); Get(text).text = name; editHook(container, uid, text, 3, nullptr); }

// ------------------------------------------------------------------ the mod's windows
struct FakePanel { bool visible = false, refuse = false; Obj window = nullptr; int toggles = 0; Rect built{}; };
std::map<std::string, FakePanel> panels;
int headerApplies = 0, layoutChoices = 0, changes = 0;
bool editing = false;
// The panels' own ticks: a panel asked open builds its window at its default place.
void BuildPanels() {
    for (auto& [key, panel] : panels) {
        if (!panel.visible) { panel.window = nullptr; continue; }
        if (panel.window) continue;
        panel.window = Make("MagneticDragWindow");
        Get(panel.window).parent = root;
        Get(panel.window).base = panel.built;
    }
}
Rect LayoutOf(Obj element) {
    auto& e = Get(element);
    Rect r = e.base;
    if (e.style.contains(0)) r.x = e.style[0];
    if (e.style.contains(1)) r.y = e.style[1];
    if (e.style.contains(2)) r.width = e.style[2];
    if (e.style.contains(3)) r.height = e.style[3];
    return r;
}

const std::map<uintptr_t, std::pair<std::string, int>> Verified = {
    {0x7E0890, {"get_uiPresetOptionsContainer", 0}}, {0x20939D0, {"get_isUsingFightPreset", 0}},
    {0x55F7180, {"get_parent", 0}}, {0x55F6930, {"get_layout", 0}}, {0x55F74A0, {"get_style", 0}},
    {0x55D1610, {"UnityEngine.UIElements.IStyle.get_left", 0}}, {0x55D3EB0, {"UnityEngine.UIElements.IStyle.set_left", 1}},
    {0x55D1CD0, {"UnityEngine.UIElements.IStyle.get_top", 0}}, {0x55D4F70, {"UnityEngine.UIElements.IStyle.set_top", 1}},
    {0x55D2740, {"UnityEngine.UIElements.IStyle.get_width", 0}}, {0x55D63F0, {"UnityEngine.UIElements.IStyle.set_width", 1}},
    {0x55D1540, {"UnityEngine.UIElements.IStyle.get_height", 0}}, {0x55D3D60, {"UnityEngine.UIElements.IStyle.set_height", 1}},
    {0x561D3D0, {".ctor", 2}}, {0x5623940, {"set_value", 1}},
    {0x20926E0, {"SavePreset", 1}}, {0x2090D40, {"AddPreset", 1}}, {0x2091F60, {"RemovePreset", 1}},
    {0x2090E40, {"ApplyPreset", 1}}, {0x20916A0, {"EditPreset", 3}}};
std::map<std::string, size_t> FieldOffsets = {{"savedPresets", 0x48}, {"fightPresetId", 0x30}, {"presetName", 0x18}, {"uid", 0xA8}};
int StyleIndex(const std::string& name) {
    for (int i = 0; i < 4; ++i)
        if (name.ends_with(std::string("_") + std::array<const char*, 4>{"left", "top", "width", "height"}[i])) return i;
    throw std::runtime_error("Unexpected style property " + name);
}
void Initialize() {
    api.object_get_class = [](Obj value) -> Klass { return Get(value).type; };
    api.object_unbox = [](Obj value) -> void* { return Get(value).raw.data(); };
    api.class_is_valuetype = [](Klass type) { const auto& name = static_cast<Type*>(type)->name; return name == "StyleLength" || name == "Box"; };
    api.class_get_field_from_name = [](Klass type, const char* name) -> void* {
        const auto found = FieldOffsets.find(name);
        if (found == FieldOffsets.end()) return nullptr;
        auto& field = fields[static_cast<Type*>(type)->name + "." + name];
        if (!field) field = std::make_unique<FieldData>(FieldData{name, found->second});
        return field.get();
    };
    api.field_get_offset = [](void* field) -> size_t { return static_cast<FieldData*>(field)->offset; };
    api.gchandle_new = [](Obj value, bool) -> GCHandle { handles[++nextHandle] = value; return nextHandle; };
    api.gchandle_get_target = [](GCHandle handle) -> Obj { return handles.at(handle); };
    api.gchandle_free = [](GCHandle handle) { Check(handles.erase(handle) == 1, "Double-freed GC handle"); };
}
}

namespace managed {
Api api;
Klass Api::Class(const char*, const char*, const char* name) const { return fake::TypeOf(name); }
Method Api::FindVerified(Klass klass, const char* name, int argc, uintptr_t rva) const {
    const auto& expected = fake::Verified.at(rva);
    fake::Check(expected.first == name && expected.second == argc, std::string("Guarded method ABI: ") + name);
    fake::methods.push_back(std::make_unique<fake::MethodData>(fake::MethodData{static_cast<fake::Type*>(klass), name, argc, rva}));
    return fake::methods.back().get();
}
Obj Api::Invoke(Method method, Obj self, std::initializer_list<void*> values) const {
    const auto& m = *static_cast<const fake::MethodData*>(method);
    const std::vector<void*> args(values);
    fake::Check(static_cast<int>(args.size()) == m.argc, "Invoke argument count " + m.name);
    if (m.name == "get_uiPresetOptionsContainer") { fake::Check(self == fake::cached, "The cached data service"); return fake::container; }
    if (m.name == "get_isUsingFightPreset") {
        fake::Check(self == fake::container, "The live container");
        if (fake::fightThrows) throw std::runtime_error("NullReferenceException");
        return fake::Box(fake::inFight && fake::Find(fake::Take<int>(fake::Get(fake::fightId).property, 0)) != nullptr);
    }
    if (m.name == "get_parent") return fake::Get(self).parent;
    if (m.name == "get_layout") return fake::Box(fake::LayoutOf(self));
    if (m.name == "get_style") { auto style = fake::Make("InlineStyleAccess"); fake::Get(style).parent = self; return style; }
    if (m.name.find("IStyle.get_") != std::string::npos) {
        auto& element = fake::Get(fake::Get(self).parent);
        const int index = fake::StyleIndex(m.name);
        return fake::Box(element.style.contains(index) ? element.style[index] : -1.f, "StyleLength");
    }
    if (m.name.find("IStyle.set_") != std::string::npos) {
        float value; std::memcpy(&value, args[0], sizeof(value));
        fake::Get(fake::Get(self).parent).style[fake::StyleIndex(m.name)] = value;
        return nullptr;
    }
    if (m.name == "set_value") { std::memcpy(self, args[0], sizeof(float)); return nullptr; } // StyleLength.set_value(Length)
    throw std::runtime_error("Unexpected native call " + m.name);
}
Obj Api::Call(Obj self, const char* rawName, std::initializer_list<void*> values) const {
    const std::string name(rawName);
    const std::vector<void*> args(values);
    auto& object = fake::Get(self);
    if (name == "IsLoaded") return fake::Box(object.loaded);
    if (name == "get_property") return object.property;
    if (name == "get_Count") return fake::Box(static_cast<int>(object.items.size()));
    if (name == "get_Item") return object.items.at(static_cast<size_t>(*static_cast<int*>(args[0])));
    throw std::runtime_error("Unexpected managed call " + name);
}
Obj Api::New(Klass klass, std::initializer_list<void*> values) const {
    const std::vector<void*> args(values);
    fake::Check(static_cast<fake::Type*>(klass)->name == "Length" && args.size() == 2 && *static_cast<int*>(args[1]) == 0, "Length in pixels");
    return fake::Box(*static_cast<float*>(args[0]));
}
std::string Api::String(Obj value) const { return value ? fake::Get(value).text : std::string(); }
}

Module* module = nullptr;
Module::Module(std::filesystem::path path) : folder(std::move(path)) {}
void Module::Change(Settings next, bool) { ++fake::changes; settings = next; }
void Log(const std::string& text) { fake::logs.push_back(text); }
void AddHook(managed::Method method, uintptr_t rva, void* replacement, void** original) {
    const auto& m = *static_cast<const fake::MethodData*>(method);
    fake::Check(m.rva == rva, "Hook at its guarded method");
    if (rva == 0x20926E0) { fake::saveHook = reinterpret_cast<fake::IntFn>(replacement); *original = reinterpret_cast<void*>(&fake::NativeSave); }
    else if (rva == 0x2090D40) { fake::addHook = reinterpret_cast<fake::AddFn>(replacement); *original = reinterpret_cast<void*>(&fake::NativeAdd); }
    else if (rva == 0x2091F60) { fake::removeHook = reinterpret_cast<fake::IntFn>(replacement); *original = reinterpret_cast<void*>(&fake::NativeRemove); }
    else if (rva == 0x2090E40) { fake::applyHook = reinterpret_cast<fake::IntFn>(replacement); *original = reinterpret_cast<void*>(&fake::NativeApply); }
    else if (rva == 0x20916A0) { fake::editHook = reinterpret_cast<fake::EditFn>(replacement); *original = reinterpret_cast<void*>(&fake::NativeEdit); }
    else throw std::runtime_error("Unexpected hook");
}
namespace toolsUi {
bool Visible(const char* key) { return fake::panels.at(key).visible; }
bool Toggle(const char* key) {
    auto& panel = fake::panels.at(key);
    if (panel.refuse) return false;
    panel.visible = !panel.visible; ++panel.toggles;
    if (!panel.visible) panel.window = nullptr;
    return true;
}
managed::Obj Window(const char* key) { return fake::panels.at(key).window; }
void ApplyHeaderSizes() { ++fake::headerApplies; }
bool ChooseLayout(const char* key, int layout) {
    fake::Check(std::string(key) == "notebook", "Only the notebook has layouts");
    ++fake::layoutChoices; module->settings.notebookCompact = layout == 1; return true;
}
}
bool NotebookPanelEditing() { return fake::editing; }

namespace fake {
std::string Text(const std::filesystem::path& path) { std::ifstream file(path, std::ios::binary); std::stringstream text; text << file.rdbuf(); return text.str(); }
bool Logged(const std::string& start) {
    return std::any_of(logs.begin(), logs.end(), [&](const std::string& line) { return line.rfind(start, 0) == 0; });
}
bool installed = false;
struct Fixture {
    Module owner{std::filesystem::path{}};
    std::filesystem::path folder;
    Fixture() {
        folder = std::filesystem::temp_directory_path() / ("preset-windows-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
        std::filesystem::create_directories(folder);
        owner.settingsPath = folder / L"DofusMiniatures-3.7.11.11-settings.ini";
        module = &owner; owner.ready = true; owner.servicesReady = true;
        cached = Make("CachedDataService"); owner.cached.reset(cached);
        container = Make("UiPresetOptionsContainer");
        auto shared = Make("SharedOptionJson"); presets = Make("ObservableCollection");
        Get(shared).property = presets; Put<Obj>(container, 0x48, shared);
        fightId = Make("AccountOptionJson"); Get(fightId).property = Box(-1); Put<Obj>(container, 0x30, fightId);
        root = Make("Root"); Get(root).base = {0, 0, 1000, 800};
        panels = {{"stats", {}}, {"notebook", {}}, {"treasure", {}}};
        panels["stats"].built = {16, 48, 900, 500};
        panels["notebook"].built = {80, 90, 760, 520};
        panels["treasure"].built = {80, 90, 520, 400};
        nextUid = applies = saves = headerApplies = layoutChoices = changes = 0;
        inFight = fightThrows = editing = false;
        logs.clear();
        if (!installed) { InstallPresetWindows(); installed = true; Check(Logged("PRESET_WINDOWS_READY hooks=5"), "Every hook installed at its guarded method"); }
        // The layouts the player has: « Classico 1 » (0) and « Combat 1 » (1).
        Add(Preset(nextUid, "Classico 1")); Add(Preset(nextUid, "Combat 1"));
        Tick();
        logs.clear();
        presetWindows::Erase(File(), 0); presetWindows::Erase(File(), 1);
    }
    ~Fixture() {
        RestorePresetWindows();
        owner.cached.reset(); module = nullptr;
        std::error_code ignored; std::filesystem::remove_all(folder, ignored);
    }
    std::filesystem::path File() const { return presetWindows::DataPath(owner.settingsPath); }
    // One pump: the panels' ticks, then the layouts'.
    void Tick() { BuildPanels(); TickPresetWindows(); }
    void SetFightPreset(int uid) { Get(fightId).property = Box(uid); }
};
// The mod's windows away from their defaults, every kind.
void Arrange(Fixture& f) {
    auto& s = f.owner.settings;
    panels["stats"].visible = true;
    BuildPanels();
    Get(panels["stats"].window).base = {100, 50, 600, 400};
    panels["treasure"].visible = false;
    panels["notebook"].visible = false;
    s.smallHeaderStats = true; s.smallHeaderNotebook = false; s.smallHeaderTreasure = true; s.smallHeaderSoul = true;
    s.notebookCompact = true; s.soulReminder = false;
    s.perfOverlayShown = false; s.perfOverlay = true; s.perfOverlayGpu = true; s.perfOverlayDisk = false;
    s.perfOverlayX = 9000; s.perfOverlayY = 120; s.perfOverlayScaleX = 150; s.perfOverlayScaleY = 80;
    s.gaugeVertical = 0x1 | 0x10;
    s.gaugeVerticalBoxes = {};
    s.gaugeVerticalBoxes[0] = {884, 7418, 1815};
    s.gaugeVerticalBoxes[4] = {0, 120, 9000};
}
// The opposite of Arrange.
void Disarrange(Fixture& f) {
    auto& s = f.owner.settings;
    s = Settings{};
    panels["stats"].visible = false; panels["stats"].window = nullptr;
    panels["treasure"].visible = true; panels["notebook"].visible = true;
    f.Tick();
}

// ------------------------------------------------------------------ tests
void SaveKeepsEveryWindow() {
    Fixture f;
    Arrange(f);
    Save(0);
    Check(saves == 1, "The game's own save runs first");
    Check(!presetWindows::Load(f.File(), 0), "Nothing written inside the hook: at the next pump");
    f.Tick();
    const auto kept = presetWindows::Load(f.File(), 0);
    Check(kept.has_value(), "The layout's windows kept under its uid");
    Check(kept->stats && kept->stats->open && kept->stats->smallHeader && kept->stats->place == presetWindows::Place{1000, 625, 6000, 5000},
        "The statistics window: open, its header, its place and size in ten-thousandths of the screen");
    Check(kept->notebook && !kept->notebook->open && !kept->notebook->place && kept->notebookCompact == true, "The notebook: closed, compact");
    Check(kept->treasure && !kept->treasure->open && kept->treasure->smallHeader, "The treasure hunt: closed, its header");
    Check(kept->soul == presetWindows::Soul{false, true}, "Capture d’âme and the soul panel's header");
    Check(kept->perf == presetWindows::Perf{false, true, true, false, 9000, 120, 150, 80}, "The overlay: hidden, its parts, place and sizes");
    Check(kept->gauges && kept->gauges->vertical == 0x11 && kept->gauges->boxes == f.owner.settings.gaugeVerticalBoxes, "The vertical gauges and their boxes");
    Check(Text(f.File()).find("\"name\": \"Classico 1\"") != std::string::npos, "With the layout's name");
    Check(Logged("PRESET_WINDOWS_SAVED uid=0 new=0 stats=open+place notebook=closed treasure=closed compact=1 soul=0 perf=hidden gauges=17"),
        "The log line says what was kept");
    Check(changes == 0, "Saving changes no setting");
}
void ApplyPutsThemBack() {
    Fixture f;
    Arrange(f);
    const auto arranged = f.owner.settings;
    Save(0); f.Tick();
    Disarrange(f);
    Apply(0);
    Check(applies == 1 && changes == 0, "The game's apply runs first; the mod waits for the pump");
    f.Tick();
    Check(changes == 1, "The settings in one change");
    auto expected = arranged;
    Check(f.owner.settings == expected, "Every setting of the layout back (headers, Capture d’âme, overlay, gauges, notebook version)");
    Check(layoutChoices == 1 && headerApplies >= 1, "The notebook's version through its own choice, the headers applied");
    Check(panels["stats"].visible && !panels["notebook"].visible && !panels["treasure"].visible, "Each window opened or closed as kept");
    Check(panels["stats"].window == nullptr, "The statistics window not built yet");
    f.Tick(); // its panel builds it, then it is placed in the same pump
    auto& stats = Get(panels["stats"].window);
    Check(stats.style[0] == 100 && stats.style[1] == 50 && stats.style[2] == 600 && stats.style[3] == 400,
        "Placed and sized as kept, in pixels of the screen");
    Check(Logged("PRESET_WINDOWS_PLACED key=stats place=1000,625 size=6000x5000"), "Its log line");
    Check(Logged("PRESET_WINDOWS_APPLIED uid=0 reason=layout"), "The apply's log line");
}
void OpenWindowMovedAtOnce() {
    Fixture f;
    Arrange(f);
    Save(0); f.Tick();
    Get(panels["stats"].window).base = {300, 300, 640, 420};
    Apply(0); f.Tick();
    auto& stats = Get(panels["stats"].window);
    Check(panels["stats"].toggles == 0 && stats.style[0] == 100 && stats.style[1] == 50 && stats.style[2] == 600 && stats.style[3] == 400,
        "An open window is moved and sized in place, not reopened");
    // Another screen: the same share of it, kept on it.
    Get(root).base = {0, 0, 2000, 1000};
    Apply(0); f.Tick();
    Check(stats.style[0] == 200 && stats.style[1] == 62.5f && stats.style[2] == 1200 && stats.style[3] == 500, "The place follows the screen's size");
}
void OlderLayoutLeavesThem() {
    Fixture f;
    Arrange(f);
    const auto before = f.owner.settings;
    Apply(1); f.Tick();
    Check(changes == 0 && f.owner.settings == before && panels["stats"].visible && panels["stats"].toggles == 0,
        "A layout saved before 1.20.0 leaves the mod's windows as they are");
    Check(Logged("PRESET_WINDOWS_NONE uid=1 reason=not-kept"), "Its log line");
}
void RoundTripEveryKind() {
    Fixture f;
    Arrange(f);
    Save(0); f.Tick();
    const auto first = presetWindows::Load(f.File(), 0);
    Disarrange(f);
    Save(1); f.Tick();
    Apply(0); f.Tick(); f.Tick();
    Save(0); f.Tick();
    Check(presetWindows::Load(f.File(), 0) == first, "Applied then saved again: the same windows");
    Apply(1); f.Tick(); f.Tick();
    Check(f.owner.settings == Settings{} && !panels["stats"].visible && panels["notebook"].visible && panels["treasure"].visible,
        "The other layout's windows back too");
}
void NewDeletedRenamed() {
    Fixture f;
    Arrange(f);
    // A new layout made by the game (no PresetData given): the last one.
    Add(nullptr); f.Tick();
    Check(presetWindows::Load(f.File(), 2).has_value() && Logged("PRESET_WINDOWS_SAVED uid=2 new=1"), "A new layout keeps the windows");
    Rename(2, "Farm");
    f.Tick();
    Check(Text(f.File()).find("\"name\": \"Farm\"") != std::string::npos && Logged("PRESET_WINDOWS_RENAMED uid=2"), "A renamed layout keeps them");
    Remove(2); f.Tick();
    Check(!presetWindows::Load(f.File(), 2) && Logged("PRESET_WINDOWS_REMOVED uid=2"), "A deleted layout loses its entry");
    // RemovePreset that removed nothing (still there): the entry stays.
    Save(0); f.Tick();
    removeHook(container, 0, nullptr); // the fake game removes it...
    Get(presets).items.push_back(Preset(0, "Classico 1")); // ...but another one has the same uid
    f.Tick();
    Check(presetWindows::Load(f.File(), 0).has_value() && Logged("PRESET_WINDOWS_SKIPPED uid=0 reason=layout-still-there"), "Kept while the layout is there");
}
void OptionOff() {
    Fixture f;
    Arrange(f);
    Save(0); f.Tick();
    const auto kept = presetWindows::Load(f.File(), 0);
    Disarrange(f);
    f.owner.settings.presetModWindows = false;
    const auto before = f.owner.settings;
    Save(0); Apply(0); f.Tick(); f.Tick();
    Check(presetWindows::Load(f.File(), 0) == kept && f.owner.settings == before && changes == 0 && panels["notebook"].visible,
        "Unticked: saving keeps nothing new, applying touches nothing");
    // A new layout never takes what a former one with its uid kept.
    presetWindows::Put(f.File(), 2, "stale", *kept);
    Add(nullptr); f.Tick();
    Check(!presetWindows::Load(f.File(), 2), "A new layout, unticked: a stale entry of its uid erased");
    // Read at each tick: ticked again, it works at once.
    f.owner.settings.presetModWindows = true;
    Apply(0); f.Tick();
    Check(changes == 1 && panels["stats"].visible, "Ticked again: applied");
    // Deleting works ticked or not.
    f.owner.settings.presetModWindows = false;
    Remove(0); f.Tick();
    Check(!presetWindows::Load(f.File(), 0), "A deleted layout loses its entry, unticked too");
}
void NotebookDraftKept() {
    Fixture f;
    Arrange(f);
    Save(0); f.Tick(); // notebook closed, compact
    f.owner.settings.notebookCompact = false;
    panels["notebook"].visible = true; f.Tick();
    editing = true;
    Apply(0); f.Tick();
    Check(panels["notebook"].visible && panels["notebook"].toggles == 0 && layoutChoices == 0 && !f.owner.settings.notebookCompact,
        "A notebook with unsaved edits stays open in its version");
    Check(Logged("PRESET_WINDOWS_KEPT key=notebook reason=unsaved-edits"), "Its log line");
    // A refused toggle is reported, the rest applied.
    editing = false; panels["stats"].refuse = true; panels["stats"].visible = false;
    Apply(0); f.Tick();
    Check(!panels["notebook"].visible && Logged("PRESET_WINDOWS_KEPT key=stats reason=refused"), "Refused: reported, the others applied");
}
void ForeignAndInvalid() {
    Fixture f;
    Arrange(f);
    saveHook(Make("UiPresetOptionsContainer"), 0, nullptr); f.Tick();
    Check(!presetWindows::Load(f.File(), 0) && Logged("PRESET_WINDOWS_SKIPPED uid=0 reason=other-container"), "Another container's save is not ours");
    Save(7); f.Tick();
    Check(!presetWindows::Load(f.File(), 7) && Logged("PRESET_WINDOWS_SKIPPED uid=7 reason=layout-missing"), "No layout with that uid: nothing kept");
    Save(-1); f.Tick();
    Check(Logged("PRESET_WINDOWS_SKIPPED uid=-1 reason=uid"), "A copy's uid is never kept");
    // Logged out: what the hooks saw is dropped, nothing applied.
    Save(0);
    f.owner.accountOptionsUnbound = true; f.Tick();
    f.owner.accountOptionsUnbound = false; f.Tick();
    Check(!presetWindows::Load(f.File(), 0), "A save seen before a logout is dropped");
    // Stopped: the hooks only run the game's own.
    f.owner.stopped = true;
    const int before = saves;
    Save(0); f.owner.stopped = false; f.Tick();
    Check(saves == before + 1 && !presetWindows::Load(f.File(), 0), "Stopped: the game's own save only");
}
void FightLayout() {
    Fixture f;
    // « Combat 1 » (1) is the fight layout; its windows: the soul panel on, the statistics closed.
    f.SetFightPreset(1);
    Arrange(f);
    f.owner.settings.soulReminder = true;
    panels["stats"].visible = false; f.Tick();
    Save(1); f.Tick();
    // Out of the fight: the player's own windows.
    Disarrange(f);
    f.owner.settings.soulReminder = false;
    panels["stats"].visible = true; f.Tick();
    Get(panels["stats"].window).base = {10, 20, 700, 500};
    const auto roleplay = f.owner.settings;
    f.Tick(); // the fight state known
    inFight = true; f.Tick();
    Check(f.owner.settings.soulReminder && !panels["stats"].visible && Logged("PRESET_WINDOWS_APPLIED uid=1 reason=fight"),
        "The fight layout's windows when the game switches to it");
    inFight = false; f.Tick(); f.Tick();
    Check(f.owner.settings == roleplay && panels["stats"].visible && Logged("PRESET_WINDOWS_APPLIED reason=after-fight"),
        "Back as before the fight when it ends");
    auto& stats = Get(panels["stats"].window);
    Check(stats.style[0] == 10 && stats.style[1] == 20 && stats.style[2] == 700 && stats.style[3] == 500, "At their place of before the fight");
}
void FightWithoutKeptWindows() {
    Fixture f;
    f.SetFightPreset(1); // saved before 1.20.0: nothing kept
    Arrange(f);
    Save(0); f.Tick();
    Disarrange(f);
    const auto before = f.owner.settings;
    inFight = true; f.Tick();
    Check(f.owner.settings == before && changes == 0 && Logged("PRESET_WINDOWS_NONE uid=1 reason=fight"), "Nothing kept: left as they are");
    // A layout applied during the fight is undone with it, as the game's own.
    Apply(0); f.Tick();
    Check(!(f.owner.settings == before) && !panels["treasure"].visible, "Applied during the fight");
    inFight = false; f.Tick();
    Check(f.owner.settings == before && panels["treasure"].visible, "Undone when the fight ends");
    // A fight without fight layout: no switch, nothing done.
    f.SetFightPreset(-1);
    changes = 0;
    inFight = true; f.Tick(); inFight = false; f.Tick();
    Check(changes == 0, "No fight layout: nothing at the fight's start or end");
    // The game's context unavailable (a loading screen): nothing, known again after.
    fightThrows = true; f.Tick(); fightThrows = false; f.Tick();
    Check(changes == 0, "An unavailable context changes nothing");
}
}

int main() {
    fake::Initialize();
    const struct { const char* name; void (*run)(); } tests[] = {
        {"SaveKeepsEveryWindow", fake::SaveKeepsEveryWindow}, {"ApplyPutsThemBack", fake::ApplyPutsThemBack},
        {"OpenWindowMovedAtOnce", fake::OpenWindowMovedAtOnce}, {"OlderLayoutLeavesThem", fake::OlderLayoutLeavesThem},
        {"RoundTripEveryKind", fake::RoundTripEveryKind}, {"NewDeletedRenamed", fake::NewDeletedRenamed}, {"OptionOff", fake::OptionOff},
        {"NotebookDraftKept", fake::NotebookDraftKept}, {"ForeignAndInvalid", fake::ForeignAndInvalid}, {"FightLayout", fake::FightLayout},
        {"FightWithoutKeptWindows", fake::FightWithoutKeptWindows}};
    int failed = 0;
    for (const auto& test : tests) {
        try { test.run(); std::cout << "PASS " << test.name << '\n'; }
        catch (const std::exception& error) {
            ++failed; std::cout << "FAIL " << test.name << ": " << error.what() << '\n';
            for (const auto& line : fake::logs) std::cout << "  log: " << line << '\n';
        }
    }
    std::cout << (failed ? "PRESET_WINDOWS_TESTS_FAILED" : "PRESET_WINDOWS_TESTS_PASSED") << '\n';
    return failed ? 1 : 0;
}
