// PerfOverlayTests (offline): perf-overlay.cpp on a fake IL2CPP runtime with
// fake Windows counters, clocks, mouse and performance counters (PDH). No
// client binary is loaded, no process opened, no real counter read: the GPU
// and disk worker runs on a real thread against a fake PDH. Every native call
// the module makes must have been verified at install (FindVerified at its
// pinned RVA, every RVA guarded in signatures.hpp), except the style value
// setters every module uses. The top-bar button's registration (tools-ui) is
// recorded as made.
#include "core/module.hpp"
#include "core/signatures.hpp"
#include "appearance/features.hpp"
#include "ui/options-ui.hpp"
#include "ui/tools-ui.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <tuple>

namespace fixture {
using namespace managed;
struct Class { std::string name; };
struct MethodData { std::string type, name; int argc = 0; bool verified = false; };
struct Object {
    Class* klass = nullptr;
    std::map<std::string, Obj> fields;                  // parent, panel, style, owner, dzrv
    std::map<std::string, std::array<char, 16>> styles; // inline styles written: the value's bytes, by property
    std::vector<Obj> children;
    std::array<char, 16> bytes{};                       // a boxed value
    std::string text, name, property;
    int picking = 0, texts = 0;
    bool focusable = true, rich = true, constructed = false;
    bool allocated = false, visible = true; // allocated by the module (object_new); VisualElement.visible
};
std::map<std::string, std::unique_ptr<Class>> classes;
std::vector<std::unique_ptr<Object>> objects;
std::vector<std::unique_ptr<MethodData>> methods;
std::map<GCHandle, Obj> handles;
GCHandle handleId = 100;
std::map<std::string, size_t> offsets{{"dzrv", 0x70}};
std::set<std::tuple<std::string, std::string, int, uintptr_t>> contracts;
// The game's class chain, for the verified-call rule.
const std::map<std::string, std::string> bases{{"Label", "TextElement"}, {"TextElement", "VisualElement"},
    {"VisualElement", "Focusable"}, {"gmy", "VisualElement"}, {"UIDocumentRootElement", "VisualElement"},
    {"RuntimePanel", "BaseRuntimePanel"}, {"BaseRuntimePanel", "Panel"}, {"Panel", "BaseVisualElementPanel"}};
std::vector<std::string> logs;
// Failure injection: this method on this object throws (failCount times; -1:
// always), only while the object is shown when failAttachedOnly.
std::string failMethod;
Obj failOwner = nullptr;
int failCount = 0;
bool failAttachedOnly = false;
int labelsBuilt = 0, removals = 0, gripRemovals = 0; // the labels' detaches, the other elements'
float collapsedWidth = -1; // >= 0 or NaN: the label's layout while the game hides its interface
std::set<uintptr_t> natives;  // the native helpers checked (NativeAt)
uintptr_t nativeMismatch = 0; // a helper whose bytes differ from the snapshot
// The panel: RootHigh laid out over it, 1920 x 1080 panel pixels unless a test
// changes the resolution or the interface scale.
float layerWidth = 1920, layerHeight = 1080;
// Game elements drawn above the overlay (menus, popups, the loading screen):
// the panel picks them first.
struct Blocker { Obj element; float x, y, width, height; };
std::vector<Blocker> blockers;
int picks = 0;
// The settings: Module::Change (never called by the overlay: its appearance
// refresh), and the place saved as Change saves it: another client's shared
// Metamob choice kept (sharedMetamob), the options page synchronised, the
// file written (which may fail).
int changes = 0, saves = 0, syncs = 0;
bool saveFails = false;
std::optional<bool> sharedMetamob;
// The game's interface.
Obj document = nullptr, host = nullptr, service = nullptr, high = nullptr, gameHigh = nullptr, panel = nullptr;

void Check(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }
Object& Get(Obj obj) { Check(obj != nullptr, "Null fake object"); return *static_cast<Object*>(obj); }
Class* Type(const std::string& name) { auto& c = classes[name]; if (!c) c = std::make_unique<Class>(Class{name}); return c.get(); }
Obj Make(const std::string& name) { auto o = std::make_unique<Object>(); o->klass = Type(name); auto p = o.get(); objects.push_back(std::move(o)); return p; }
template<class T> Obj Box(T value) {
    static_assert(sizeof(T) <= 16);
    auto o = Make("Box"); std::memcpy(Get(o).bytes.data(), &value, sizeof(value)); return o;
}
Obj String(const std::string& text) { auto o = Make("String"); Get(o).text = text; return o; }
Obj Element(const std::string& type, const std::string& name = {}) { auto o = Make(type); Get(o).name = name; return o; }
void Add(Obj parent, Obj child) {
    Check(!Get(child).fields["parent"], "Element already attached");
    Get(parent).children.push_back(child); Get(child).fields["parent"] = parent;
}
void Detach(Obj child) {
    if (auto parent = Get(child).fields["parent"]) { auto& v = Get(parent).children; v.erase(std::remove(v.begin(), v.end(), child), v.end()); }
    Get(child).fields["parent"] = nullptr;
}
Obj Panel(Obj o) { if (Get(o).fields["panel"]) return Get(o).fields["panel"]; auto p = Get(o).fields["parent"]; return p ? Panel(p) : nullptr; }
bool Derives(const std::string& type, const std::string& base) {
    for (std::string t = type; !t.empty(); t = bases.contains(t) ? bases.at(t) : std::string())
        if (t == base) return true;
    return false;
}
size_t Size(const std::string& property) {
    if (property == "position" || property == "whiteSpace") return 4;
    if (property == "color" || property == "backgroundColor") return 16;
    return 8; // Length: value, unit
}
std::string StyleKind(const std::string& property) {
    const auto size = Size(property);
    return size == 4 ? "StyleEnum" : size == 16 ? "StyleColor" : "StyleLength";
}
bool Styled(Obj o, const std::string& property) { return Get(o).styles.contains(property); }
float Pixels(Obj o, const std::string& property) {
    Check(Styled(o, property), "Style not written: " + property);
    float value = 0; int unit = -1;
    std::memcpy(&value, Get(o).styles.at(property).data(), 4); std::memcpy(&unit, Get(o).styles.at(property).data() + 4, 4);
    Check(unit == 0, "Pixels expected: " + property);
    return value;
}
int EnumStyle(Obj o, const std::string& property) {
    Check(Styled(o, property), "Style not written: " + property);
    int value = 0; std::memcpy(&value, Get(o).styles.at(property).data(), 4); return value;
}
std::array<float, 4> ColorStyle(Obj o, const std::string& property) {
    Check(Styled(o, property), "Style not written: " + property);
    std::array<float, 4> value{}; std::memcpy(value.data(), Get(o).styles.at(property).data(), 16); return value;
}
void Fail(Obj o, const std::string& method) {
    if (o != failOwner || method != failMethod || failCount == 0) return;
    if (failAttachedOnly && !Panel(o)) return;
    if (failCount > 0) --failCount;
    throw std::runtime_error("Injected failure: " + method);
}
// The fake text layout scales with the label's font size (12 px: 1).
float FontFactor(Obj o) {
    if (!Get(o).styles.contains("fontSize")) return 1;
    float size = 0; std::memcpy(&size, Get(o).styles.at("fontSize").data(), 4);
    return size / 12.f;
}
// 6 px a character of the longest line, plus the padding (all at 12 px);
// never narrower than the label's minimum width.
float LaidOutWidth(Obj o) {
    if (!(collapsedWidth < 0)) return collapsedWidth;
    size_t longest = 0, line = 0;
    for (const unsigned char c : Get(o).text) {
        if (c == '\n') { line = 0; continue; }
        if ((c & 0xC0) != 0x80) longest = std::max(longest, ++line);
    }
    const float factor = FontFactor(o);
    float width = 6.f * factor * static_cast<float>(longest) + 16.f * factor;
    if (Get(o).styles.contains("minWidth")) {
        float minimum = 0; std::memcpy(&minimum, Get(o).styles.at("minWidth").data(), 4);
        width = std::max(width, minimum);
    }
    return width;
}
// 16 px a line plus the padding (at 12 px): 52 for three lines, 68 for four.
float LaidOutHeight(Obj o) {
    if (!(collapsedWidth < 0)) return collapsedWidth;
    const float factor = FontFactor(o);
    return 16.f * factor * static_cast<float>(1 + std::count(Get(o).text.begin(), Get(o).text.end(), '\n')) + 4.f * factor;
}
// The label's layout in its layer: x, y, width, height, from its anchored sides.
std::array<float, 4> LabelBox(Obj o) {
    const float width = LaidOutWidth(o), height = LaidOutHeight(o);
    float x = 0, y = 0;
    if (Styled(o, "left")) x = Pixels(o, "left");
    else if (Styled(o, "right")) x = layerWidth - Pixels(o, "right") - width;
    if (Styled(o, "top")) y = Pixels(o, "top");
    else if (Styled(o, "bottom")) y = layerHeight - Pixels(o, "bottom") - height;
    return {x, y, width, height};
}
bool Within(const std::array<float, 4>& box, float x, float y) {
    return x >= box[0] && x < box[0] + box[2] && y >= box[1] && y < box[1] + box[3];
}
// The Windows side.
constexpr int64_t Frequency = 10000000; // 10 MHz: one tick is 100 ns
constexpr int64_t FrameTicks = 166667;  // 60 frames a second
int64_t counter = 1000000000, fakeFrequency = Frequency;
uint64_t nowMs = 100000;
unsigned fakeProcessors = 8;
bool memoryReadable = true, lookupMatches = true;
uint64_t workingSet = 1632087573ULL, privateBytes = 1406601790ULL, cpuTime = 0;
int64_t heapUsed = 312LL << 20, heapSize = 420LL << 20;
// Every read of the frame counter, the memory, the CPU time and the managed
// heap: none without the frame rate shown.
int measureReads = 0;
int64_t FakeCounter() { ++measureReads; return counter; }
int64_t FakeFrequency() { return fakeFrequency; }
uint64_t FakeMilliseconds() { return nowMs; }
unsigned FakeProcessors() { return fakeProcessors; }
bool FakeMemory(uint64_t& w, uint64_t& p) { ++measureReads; if (!memoryReadable) return false; w = workingSet; p = privateBytes; return true; }
bool FakeCpu(uint64_t& t) { ++measureReads; t = cpuTime; return true; }
int64_t FakeGcUsed() { ++measureReads; return heapUsed; }
int64_t FakeGcHeap() { ++measureReads; return heapSize; }
FARPROC FakeLookup(const char* name) {
    const std::string exported = name;
    const uintptr_t rva = exported == "il2cpp_gc_get_used_size" ? 0x5B9230 : exported == "il2cpp_gc_get_heap_size" ? 0x5B9210 : 0;
    if (!rva) return nullptr;
    return reinterpret_cast<FARPROC>(gameBase + rva + (lookupMatches ? 0 : 0x10));
}
bool Guarded(uintptr_t rva) {
    return std::any_of(std::begin(Signatures), std::end(Signatures), [&](const NativeSignature& s) { return s.rva == rva; });
}
void Setup() {
    api.object_get_class = [](Obj o) -> Klass { return Get(o).klass; };
    api.class_is_valuetype = [](Klass k) {
        const auto& type = static_cast<Class*>(k)->name;
        return type == "Box" || type == "Length" || type == "Hierarchy" || type.starts_with("Style");
    };
    api.class_is_assignable_from = [](Klass base, Klass k) { return Derives(static_cast<Class*>(k)->name, static_cast<Class*>(base)->name); };
    api.object_new = [](Klass k) -> Obj { auto o = Make(static_cast<Class*>(k)->name); Get(o).allocated = true; return o; };
    api.object_unbox = [](Obj o) -> void* { return Get(o).bytes.data(); };
    api.string_new = [](const char* s) -> Obj { return String(s); };
    api.gchandle_new = [](Obj o, bool) -> GCHandle { handles[++handleId] = o; return handleId; };
    api.gchandle_get_target = [](GCHandle h) -> Obj { return handles.at(h); };
    api.gchandle_free = [](GCHandle h) { Check(handles.erase(h) == 1, "Invalid GC release"); };
    api.class_get_field_from_name = [](Klass, const char* name) -> void* { const auto it = offsets.find(name); return it == offsets.end() ? nullptr : &it->second; };
    api.field_get_offset = [](void* f) { return *static_cast<size_t*>(f); };
}
// RootLayer (gmy) under the UIDocument root, with the ten native layers in
// gmo's order; RootHigh already holds a game element.
void BuildInterface() {
    panel = Make("RuntimePanel");
    document = Element("UIDocumentRootElement", "RootUI-container");
    Get(document).fields["panel"] = panel;
    host = Element("gmy", "RootLayer");
    Add(document, host);
    for (const char* name : {"RootWorld", "RootLow", "RootMedium", "RootHigh", "RootTop", "RootMenu", "RootPopup", "RootTooltip",
                             "RootLoadingScreen", "RootCursor"}) {
        auto layer = Element("VisualElement", name);
        Add(host, layer);
        if (std::string(name) == "RootHigh") high = layer;
    }
    gameHigh = Element("VisualElement", "GameHighWindow");
    Add(high, gameHigh);
    service = Make("gmo");
    Get(service).fields["dzrv"] = host;
}
}

namespace managed {
Api api{};
uintptr_t gameBase = 0x180000000ULL;
Klass Api::Class(const char*, const char*, const char* name) const { return fixture::Type(name); }
Klass Api::NestedClass(Klass declaringClass, const char* name) const {
    fixture::Check(static_cast<fixture::Class*>(declaringClass)->name == "VisualElement" && std::string(name) == "Hierarchy", "Nested Hierarchy of VisualElement");
    return fixture::Type(name);
}
Method Api::Find(Klass klass, const char* name, int argc) const {
    auto m = std::make_unique<fixture::MethodData>(); m->type = static_cast<fixture::Class*>(klass)->name; m->name = name; m->argc = argc;
    auto p = m.get(); fixture::methods.push_back(std::move(m)); return p;
}
Method Api::FindVerified(Klass klass, const char* name, int argc, uintptr_t rva) const {
    fixture::Check(fixture::Guarded(rva), std::string("Verified method without a native guard: ") + name);
    auto method = static_cast<fixture::MethodData*>(const_cast<void*>(Find(klass, name, argc)));
    method->verified = true;
    fixture::contracts.insert({method->type, name, argc, rva});
    return method;
}
void* Api::NativeAt(uintptr_t rva) const {
    fixture::Check(fixture::Guarded(rva), "Native helper without a guard");
    fixture::natives.insert(rva);
    if (rva == fixture::nativeMismatch) throw std::runtime_error("Loaded method bytes differ from snapshot");
    if (rva == 0x5B9230) return reinterpret_cast<void*>(&fixture::FakeGcUsed);
    if (rva == 0x5B9210) return reinterpret_cast<void*>(&fixture::FakeGcHeap);
    // The code behind the export thunks: checked only, never called directly.
    if (rva == 0x5FE340 || rva == 0x5FDB20 || rva == 0x6194D0 || rva == 0x6194B0) return reinterpret_cast<void*>(gameBase + rva);
    throw std::runtime_error("Unexpected native helper");
}
Obj Api::Field(Obj obj, const char* name) const {
    fixture::Check(std::string(name) == "dzrv", "Only the layer host field is read");
    return fixture::Get(obj).fields.at(name);
}
std::string Api::String(Obj obj) const { return obj ? fixture::Get(obj).text : std::string(); }
Obj Api::New(Klass klass, std::initializer_list<void*> args) const {
    using namespace fixture;
    const auto name = static_cast<fixture::Class*>(klass)->name;
    Check(name == "Length" && args.size() == 2 && contracts.contains({"Length", ".ctor", 2, 0x561D3D0}), "Only verified Length values are built");
    auto o = Make(name);
    size_t i = 0;
    for (auto arg : args) { std::memcpy(Get(o).bytes.data() + i * 4, arg, 4); ++i; }
    return o;
}
// By name: only the style value setters (generic and value types, as in every module).
Obj Api::Call(Obj self, const char* name, std::initializer_list<void*> args) const {
    using namespace fixture;
    auto& o = Get(self);
    Check(std::string(name) == "set_value" && o.klass->name.starts_with("Style") && args.size() == 1, std::string("Unverified call: ") + name);
    std::memcpy(o.bytes.data(), *args.begin(), Size(o.property));
    return nullptr;
}
Obj Api::Invoke(Method method, Obj self, std::initializer_list<void*> arguments) const {
    using namespace fixture;
    const auto& info = *static_cast<const MethodData*>(method);
    const std::vector<void*> args(arguments);
    Check(info.verified, "Native call not verified at install: " + info.type + "." + info.name);
    Check(info.argc == static_cast<int>(args.size()), "Managed invocation argument count: " + info.name);
    if (info.type == "pq") {
        // As the game: the cached UIDocument root, or a NullReferenceException before any UIDocument.
        Check(info.name == "pbs" && !self, "Static pq.pbs");
        if (!document) throw std::runtime_error("System.NullReferenceException: Object reference not set to an instance of an object.");
        return document;
    }
    if (info.type == "Hierarchy") {
        // A value type: self is the unboxed Hierarchy, whose only field is its owner.
        Obj owner = nullptr; std::memcpy(&owner, self, sizeof(owner));
        auto& children = Get(owner).children;
        if (info.name == "get_childCount") return Box(static_cast<int>(children.size()));
        Check(info.name == "get_Item", "Hierarchy accessor");
        return children.at(static_cast<size_t>(*static_cast<int*>(args[0])));
    }
    auto& o = Get(self);
    Check(Derives(o.klass->name, info.type), "Method of " + info.type + " called on " + o.klass->name);
    if (info.name == "RemoveFromHierarchy") ++(o.klass->name == "Label" ? removals : gripRemovals);
    Fail(self, info.name);
    const std::string& m = info.name;
    if (info.type == "InlineStyleAccess") {
        const std::string prefix = "UnityEngine.UIElements.IStyle.";
        Check(m.starts_with(prefix), "Inline styles through their explicit IStyle methods");
        const auto operation = m.substr(prefix.size()), property = operation.substr(4);
        auto& target = Get(o.fields.at("owner"));
        if (operation.starts_with("get_")) {
            auto box = Make(StyleKind(property)); Get(box).property = property;
            if (target.styles.contains(property)) Get(box).bytes = target.styles.at(property);
            return box;
        }
        Check(operation.starts_with("set_"), "Unexpected style method");
        std::memcpy(target.styles[property].data(), args.at(0), Size(property));
        return nullptr;
    }
    if (info.type == "BaseVisualElementPanel") {
        // IPanel.Pick(Vector2): the topmost picking element at a panel point.
        Check(m == "Pick", "Only the panel's pick");
        float point[2]; std::memcpy(point, args[0], sizeof(point));
        ++picks;
        for (auto it = blockers.rbegin(); it != blockers.rend(); ++it)
            if (Within({it->x, it->y, it->width, it->height}, point[0], point[1])) return it->element;
        auto& layered = Get(high).children;
        for (auto it = layered.rbegin(); it != layered.rend(); ++it)
            if (Get(*it).klass->name == "Label" && Get(*it).picking == 0 && Within(LabelBox(*it), point[0], point[1])) return *it;
        return high;
    }
    if (m == ".ctor") {
        Check(info.type == o.klass->name && (o.klass->name == "Label" || o.klass->name == "VisualElement"), "Label or VisualElement constructor");
        o.constructed = true;
        if (o.klass->name == "Label") ++labelsBuilt;
        return nullptr;
    }
    Check((o.klass->name != "Label" && !o.allocated) || o.constructed, "Element used before its constructor");
    if (m == "get_parent") return o.fields["parent"];
    if (m == "get_panel") return Panel(self);
    if (m == "get_name") return fixture::String(o.name);
    if (m == "get_hierarchy") { auto box = Make("Hierarchy"); std::memcpy(Get(box).bytes.data(), &self, sizeof(self)); return box; }
    if (m == "get_layout") {
        struct Rect { float x, y, width, height; };
        if (o.klass->name != "Label") return Box(Rect{0, 0, layerWidth, layerHeight});
        const auto box = LabelBox(self);
        return Box(Rect{box[0], box[1], box[2], box[3]});
    }
    if (m == "Insert") {
        const int index = *static_cast<int*>(args[0]);
        Obj child = static_cast<Obj>(args[1]);
        Check(!Get(child).fields["parent"], "Element already attached");
        Check(index >= 0 && index <= static_cast<int>(o.children.size()), "Insert index out of range");
        o.children.insert(o.children.begin() + index, child); Get(child).fields["parent"] = self;
        return nullptr;
    }
    if (m == "RemoveFromHierarchy") { Detach(self); return nullptr; }
    if (m == "set_pickingMode") { o.picking = *static_cast<int*>(args[0]); return nullptr; }
    if (m == "set_name") { o.name = Get(static_cast<Obj>(args[0])).text; return nullptr; }
    if (m == "get_style") {
        if (!o.fields["style"]) { auto style = Make("InlineStyleAccess"); Get(style).fields["owner"] = self; o.fields["style"] = style; }
        return o.fields["style"];
    }
    if (m == "set_focusable") { o.focusable = *static_cast<bool*>(args[0]); return nullptr; }
    if (m == "set_text") { o.text = Get(static_cast<Obj>(args[0])).text; ++o.texts; return nullptr; }
    if (m == "set_enableRichText") { o.rich = *static_cast<bool*>(args[0]); return nullptr; }
    if (m == "set_visible") { o.visible = *static_cast<bool*>(args[0]); return nullptr; }
    throw std::runtime_error("Unexpected fake method " + info.type + "." + m);
}
}
Module* module = nullptr;
Module::Module(std::filesystem::path path) : folder(std::move(path)) {}
managed::Obj Module::FindService(const char* name) const {
    fixture::Check(std::string(name) == "fqp", "Only the layer service is looked up");
    return fixture::service;
}
// As the module. Change: counted, to tell it never comes from the overlay.
void Module::Change(Settings next, bool) {
    ++fixture::changes;
    settings = next;
}
// The shared Metamob choice another client made meanwhile, kept.
bool Module::KeepSharedMetamobSync(Settings& next) const {
    if (fixture::sharedMetamob) next.metamobSync = *fixture::sharedMetamob;
    return true;
}
// The settings file written (which may fail).
void Module::SaveSettings(bool) {
    ++fixture::saves;
    if (fixture::saveFails) throw std::runtime_error("Injected save failure");
}
void OptionsUi::Sync() { ++fixture::syncs; }
void Log(const std::string& message) { fixture::logs.push_back(message); }
// The mod's top-bar buttons (tools-ui.cpp): each registration recorded.
namespace toolsFake {
struct Registration { std::string key, title; int icon = 0; bool (*visible)() = nullptr, (*toggle)() = nullptr; size_t layouts = 0; bool (*button)() = nullptr; };
std::vector<Registration> registrations;
}
namespace toolsUi {
void Register(const char* key, const char* title, int icon, bool (*visible)(), bool (*toggle)(), Layouts layouts, bool (*button)()) {
    toolsFake::registrations.push_back({key, title, icon, visible, toggle, layouts.choices.size(), button});
}
}
#include "ui/perf-overlay.cpp"

// The mouse: the game's window in the foreground or not, the cursor in its
// client area (the panel's size unless a test scales it), the left button.
namespace mouse {
bool foreground = true, button = false;
long clientX = 0, clientY = 0, clientWidth = 1920, clientHeight = 1080;
int buttonReads = 0, cursorReads = 0;
bool Button() { ++buttonReads; return button; }
bool Cursor(perfOverlay::Cursor& cursor) {
    ++cursorReads;
    if (!foreground) return false;
    cursor = {clientX, clientY, clientWidth, clientHeight};
    return true;
}
}

// PDH as Windows answers it, without reading a counter: a rate needs two
// collections, so every read before a query's second collection fails with
// PDH_CSTATUS_INVALID_DATA. Called on the worker threads: everything under
// `lock`, the tests' settings included (Configure).
namespace pdhFake {
using perfOverlay::CounterItems;
std::mutex lock;
std::vector<std::wstring> paths;          // every counter path added, in order
std::map<uintptr_t, int> collections;     // the open queries' collections
int opens = 0, closes = 0, spawns = 0;
uintptr_t lastQuery = 0;
PDH_STATUS openStatus = ERROR_SUCCESS, gpuStatus = ERROR_SUCCESS, diskStatus = ERROR_SUCCESS,
    collectStatus = ERROR_SUCCESS, readStatus = ERROR_SUCCESS;
// This process's engines (the busiest, 3D at 22,6 %: « GPU 23 % », never the
// 25 % of both adapters' 3D added up) and the physical disks: C: 3,6 %
// active, 1,2 Mo/s.
CounterItems engines{
    {L"pid_4242_luid_0x00000000_0x0000D1E2_phys_0_eng_0_engtype_3D", 22.6, true},
    {L"pid_4242_luid_0x00000000_0x0000E5F0_phys_0_eng_0_engtype_3D", 2.5, true},
    {L"pid_4242_luid_0x00000000_0x0000D1E2_phys_0_eng_1_engtype_Copy", 3.0, true},
    {L"pid_4242_luid_0x00000000_0x0000D1E2_phys_0_eng_9_engtype_High Priority 3D", 1.0, true}};
CounterItems idle{{L"0 D:", 56.5, true}, {L"3 C:", 96.4, true}, {L"_Total", 88.6, true}};
CounterItems bytes{{L"0 D:", 10866306, true}, {L"3 C:", 1.2 * 1048576, true}, {L"_Total", 12124546, true}};
HANDLE gate = nullptr;  // set: the collections wait until it is signalled
std::atomic<int> waiting{0};
bool spawnFails = false;
unsigned pid = 4242;
std::wstring path = L"C:\\Jeux\\Dofus\\Dofus.exe";
template<class F> void Configure(F change) { const std::lock_guard<std::mutex> guard(lock); change(); }
PDH_STATUS Open(PDH_HQUERY* query) {
    const std::lock_guard<std::mutex> guard(lock);
    if (openStatus != ERROR_SUCCESS) return openStatus;
    ++opens;
    collections[++lastQuery] = 0;
    *query = reinterpret_cast<PDH_HQUERY>(lastQuery);
    return ERROR_SUCCESS;
}
PDH_STATUS Add(PDH_HQUERY query, const wchar_t* counterPath, PDH_HCOUNTER* counter) {
    const std::lock_guard<std::mutex> guard(lock);
    const std::wstring text = counterPath;
    paths.push_back(text);
    const bool gpu = text.starts_with(L"\\GPU Engine(");
    const PDH_STATUS status = gpu ? gpuStatus : diskStatus;
    if (status != ERROR_SUCCESS) return status;
    const uintptr_t kind = gpu ? 1 : text.find(L"Idle") != std::wstring::npos ? 2 : 3;
    *counter = reinterpret_cast<PDH_HCOUNTER>(reinterpret_cast<uintptr_t>(query) << 4 | kind);
    return ERROR_SUCCESS;
}
PDH_STATUS Collect(PDH_HQUERY query) {
    HANDLE wait = nullptr;
    { const std::lock_guard<std::mutex> guard(lock); wait = gate; }
    if (wait) { ++waiting; WaitForSingleObject(wait, INFINITE); --waiting; }
    const std::lock_guard<std::mutex> guard(lock);
    if (collectStatus != ERROR_SUCCESS) return collectStatus;
    ++collections[reinterpret_cast<uintptr_t>(query)];
    return ERROR_SUCCESS;
}
PDH_STATUS Read(PDH_HCOUNTER counter, CounterItems& items) {
    const std::lock_guard<std::mutex> guard(lock);
    const auto handle = reinterpret_cast<uintptr_t>(counter);
    items.clear();
    if (collections[handle >> 4] < 2) return static_cast<PDH_STATUS>(PDH_CSTATUS_INVALID_DATA);
    if (readStatus != ERROR_SUCCESS) return readStatus;
    const auto kind = handle & 15;
    items = kind == 1 ? engines : kind == 2 ? idle : bytes;
    return ERROR_SUCCESS;
}
PDH_STATUS Close(PDH_HQUERY query) {
    const std::lock_guard<std::mutex> guard(lock);
    ++closes;
    collections.erase(reinterpret_cast<uintptr_t>(query));
    return ERROR_SUCCESS;
}
unsigned Pid() { return pid; }
std::wstring Path() { return path; }
HANDLE Spawn(LPTHREAD_START_ROUTINE routine, void* parameter) {
    ++spawns;
    if (spawnFails) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return nullptr; }
    return CreateThread(nullptr, 0, routine, parameter, 0, nullptr);
}
int Alive() { const std::lock_guard<std::mutex> guard(lock); return opens - closes; }
std::vector<std::wstring> Paths() { const std::lock_guard<std::mutex> guard(lock); return paths; }
void ClearPaths() { const std::lock_guard<std::mutex> guard(lock); paths.clear(); }
}

// PdhGetFormattedCounterArrayW as Windows answers it: the buffer's size first
// (PDH_MORE_DATA), then the items, their names in that same buffer.
namespace formatFake {
struct Entry { std::wstring name; double value; DWORD status; };
std::vector<Entry> entries;
PDH_STATUS answer = ERROR_SUCCESS; // another answer: the first collection's, no instance
int calls = 0, growths = 0;        // growths: instances coming between two calls
DWORD flags = 0;
DWORD Needed() {
    size_t size = entries.size() * sizeof(PDH_FMT_COUNTERVALUE_ITEM_W);
    for (const auto& entry : entries) size += (entry.name.size() + 1) * sizeof(wchar_t);
    return static_cast<DWORD>(size);
}
PDH_STATUS WINAPI Format(PDH_HCOUNTER, DWORD format, LPDWORD size, LPDWORD count, PPDH_FMT_COUNTERVALUE_ITEM_W buffer) {
    ++calls;
    flags = format;
    if (answer != ERROR_SUCCESS) return answer;
    if (buffer && growths > 0) {
        --growths;
        entries.push_back({L"pid_9_luid_0x0_0x1_phys_0_eng_" + std::to_wstring(entries.size()) + L"_engtype_Copy", 1, PDH_CSTATUS_VALID_DATA});
    }
    const DWORD needed = Needed();
    if (!buffer || *size < needed) { *size = needed; return static_cast<PDH_STATUS>(PDH_MORE_DATA); }
    auto* names = reinterpret_cast<wchar_t*>(buffer + entries.size());
    for (size_t i = 0; i < entries.size(); ++i) {
        std::memcpy(names, entries[i].name.c_str(), (entries[i].name.size() + 1) * sizeof(wchar_t));
        buffer[i].szName = names;
        buffer[i].FmtValue.CStatus = entries[i].status;
        buffer[i].FmtValue.doubleValue = entries[i].value;
        names += entries[i].name.size() + 1;
    }
    *count = static_cast<DWORD>(entries.size());
    return ERROR_SUCCESS;
}
}

namespace {
using namespace fixture;
size_t Logged(const std::string& prefix) {
    return static_cast<size_t>(std::count_if(logs.begin(), logs.end(), [&](const std::string& line) { return line.rfind(prefix, 0) == 0; }));
}
size_t LoggedLine(const std::string& text) { return static_cast<size_t>(std::count(logs.begin(), logs.end(), text)); }
Obj Overlay() { return perfOverlay::ui.label.get(); }
// The overlay's grip (1.16.1): its own element right above the label.
Obj GripOf() { return perfOverlay::ui.grip.get(); }
std::vector<Obj> Grips(Obj layer) {
    std::vector<Obj> result;
    for (auto child : Get(layer).children) if (Get(child).name == "dofus_miniatures_perf_overlay_grip") result.push_back(child);
    return result;
}
std::vector<Obj> Owned(Obj layer) {
    std::vector<Obj> result;
    for (auto child : Get(layer).children) if (Get(child).name == "dofus_miniatures_perf_overlay") result.push_back(child);
    return result;
}
void Tick() { nowMs += 200; TickPerfOverlay(); }
// One frame of `ticks` (100 ns), then the main compositor's frame call.
void Frame(int64_t ticks = FrameTicks) { counter += ticks; PerfOverlayFrame(nowMs); }
// Pumps at 60 FPS: 12 frames, the game using 12 % of 8 processors, then the 200 ms pump.
void Pump(int pumps = 1) {
    for (int p = 0; p < pumps; ++p) {
        for (int i = 0; i < 12; ++i) Frame();
        cpuTime += 1920000; // 0,192 s of CPU in 0,2 s on 8 processors
        Tick();
    }
}
// One frame at 60 FPS with the game at 12 % of 8 processors, as the pumps.
void Step() { cpuTime += 160000; Frame(); }
// One such frame with the cursor at this client point and the button down or up.
void Mouse(long x, long y, bool down) { mouse::clientX = x; mouse::clientY = y; mouse::button = down; Step(); }
bool Near(double value, double expected, double tolerance = 1e-6) { return std::fabs(value - expected) <= tolerance; }
// Real time: the device worker runs on its own thread, a sample every 2 ms here.
template<class Ready> bool WaitFor(Ready ready, uint64_t ms = 5000) {
    const auto start = GetTickCount64();
    while (!ready()) {
        if (GetTickCount64() - start > ms) return false;
        Sleep(1);
    }
    return true;
}
perfOverlay::DeviceFigures Figures() {
    const auto worker = perfOverlay::devices.worker;
    if (!worker) return {};
    const std::lock_guard<std::mutex> guard(worker->lock);
    return worker->figures;
}
// Holds the device workers in their PDH collections until ReleaseCollections:
// a worker started but without any figure yet, as a real one for about a
// second (its first collection gives no rate).
HANDLE HoldCollections() {
    const HANDLE gate = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    pdhFake::Configure([&] { pdhFake::gate = gate; });
    return gate;
}
void ReleaseCollections(HANDLE gate) {
    pdhFake::Configure([] { pdhFake::gate = nullptr; });
    SetEvent(gate);
    Check(WaitFor([] { return pdhFake::waiting.load() == 0; }), "The worker out of its collection");
    CloseHandle(gate);
}
const std::string Measuring = "FPS : mesure en cours…";
// Without the frame rate: nothing to show yet, or nothing to give.
const std::string WaitingLine = "Mesure en cours…", UnavailableLine = "Mesure indisponible";
const std::string Memory = "Mémoire 1,52 Go · privée 1,31 Go";
const std::string Heap = "Tas géré 312 Mo / 420 Mo";
const std::string Steady = "60 FPS · 16,7 ms (max 16,7 ms)\n" + Memory + "\n" + Heap + " · CPU 12 %";
// A new label's first text: its CPU share needs a second sample.
const std::string SteadyNoCpu = "60 FPS · 16,7 ms (max 16,7 ms)\n" + Memory + "\n" + Heap;
// The fourth line from the fake counters.
const std::string DevicesLine = "GPU 23 % · Disque C: 4 % · 1,2 Mo/s";
// The fake layout's widths (6 px a character of the longest line, plus 16).
constexpr float MemoryLineWidth = 6 * 32 + 16, CpuLineWidth = 6 * 35 + 16; // longest line: the memory one, the third one with the CPU share

// ------------------------------------------------------------- the figures
void FramesModel() {
    using namespace perfOverlay;
    FrameRing frames;
    Check(!Frames(frames, Frequency, Frequency).valid, "No frame: no figure");
    frames.Push(1000);
    Check(!Frames(frames, Frequency, Frequency).valid, "One frame: no figure");
    // Two seconds at 60 FPS: the last second only.
    frames.Clear();
    int64_t at = 5000000;
    for (int i = 0; i < 120; ++i) frames.Push(at += FrameTicks);
    auto stats = Frames(frames, Frequency, Frequency);
    Check(stats.valid && Near(stats.fps, 60.0 / 1.000002) && Near(stats.averageMs, 16.6667) && Near(stats.worstMs, 16.6667), "60 FPS steady");
    // A 50 ms frame within the last second: the worst frame, and the average.
    frames.Push(at += 500000);
    for (int i = 0; i < 10; ++i) frames.Push(at += FrameTicks);
    stats = Frames(frames, Frequency, Frequency);
    Check(stats.valid && Near(stats.worstMs, 50.0), "The worst frame of the last second");
    Check(Near(stats.fps, 58.0 / 1.0000019) && Near(stats.averageMs, 1000.0019 / 58.0, 1e-4), "Rate and average over the last second (58 frames)");
    // More than a second later: out of the window.
    for (int i = 0; i < 61; ++i) frames.Push(at += FrameTicks);
    stats = Frames(frames, Frequency, Frequency);
    Check(stats.valid && Near(stats.worstMs, 16.6667), "A slow frame older than a second is forgotten");
    // Less than a second of frames: all of them.
    frames.Clear();
    for (int i = 0; i < 10; ++i) frames.Push(at += 100000);
    stats = Frames(frames, Frequency, Frequency);
    Check(stats.valid && Near(stats.fps, 100.0) && Near(stats.averageMs, 10.0) && Near(stats.worstMs, 10.0), "Fewer frames than a second: 9 intervals");
    // 2000 FPS: the 1024 frames kept cover half a second.
    frames.Clear();
    for (int i = 0; i < 3000; ++i) frames.Push(at += 5000);
    stats = Frames(frames, Frequency, Frequency);
    Check(frames.count == FrameRing::Capacity && stats.valid && Near(stats.fps, 2000.0) && Near(stats.worstMs, 0.5), "Wrapped ring at 2000 FPS");
    // A stamp out of order (never with a monotonic counter): nothing invented.
    frames.Push(at - 1);
    Check(!Frames(frames, Frequency, Frequency).valid, "Newest frame out of order: no figure");
    Check(!Frames(frames, 0, Frequency).valid, "No counter frequency: no figure");
}
void CpuModel() {
    using namespace perfOverlay;
    Check(Near(CpuPercent({0, 0}, {2000000, 10000000}, 4), 5.0), "0,2 s of CPU in 1 s on 4 processors: 5 %");
    Check(Near(CpuPercent({0, 0}, {80000000, 10000000}, 4), 100.0), "Clamped to 100 %");
    Check(CpuPercent({0, 0}, {1, 10000000}, 0) < 0 && CpuPercent({5, 10}, {6, 10}, 4) < 0 && CpuPercent({5, 10}, {4, 20}, 4) < 0,
        "No processor, no elapsed time or a CPU time going back: no figure");
    CpuMeter meter;
    Check(meter.Add({0, 0}, 1) < 0, "The first sample gives no figure");
    Check(Near(meter.Add({1000000, 4000000}, 1), 25.0), "Second sample: over one interval");
    Check(Near(meter.Add({1000000, 8000000}, 1), 12.5), "Third sample: over the two last intervals");
    Check(Near(meter.Add({5000000, 12000000}, 1), 50.0), "Then always over the two last intervals");
    meter.Clear();
    Check(meter.Add({9, 9}, 1) < 0, "Cleared: starts again");
    Check(Hundreds(Frequency, Frequency) == 10000000 && Hundreds(3000000, 3000000) == 10000000 && Hundreds(0, Frequency) == 0,
        "One second is 10 000 000 hundreds of nanoseconds");
    Check(Hundreds(int64_t(1) << 62, Frequency) == uint64_t(1) << 62 && Hundreds(int64_t(3) << 60, 3000000000LL) == (uint64_t(1) << 60) / 100,
        "No overflow on large counters");
}
void Formatting() {
    using namespace perfOverlay;
    Check(Decimal(16.666, 1) == "16,7" && Decimal(0.04, 1) == "0,0" && Decimal(1.5, 2) == "1,50", "Decimal comma");
    Check(Rate(59.9999) == "60" && Rate(144.4) == "144" && Rate(9.94) == "9,9" && Rate(9.96) == "10" && Rate(0.5) == "0,5", "Frame rates");
    Check(Milliseconds(16.6667) == "16,7 ms" && Milliseconds(99.94) == "99,9 ms" && Milliseconds(99.96) == "100 ms" &&
        Milliseconds(250.4) == "250 ms" && Milliseconds(999.4) == "999 ms" && Milliseconds(999.6) == "1,0 s" && Milliseconds(2500) == "2,5 s",
        "Frame times: ms, then seconds");
    constexpr uint64_t MiB = 1 << 20;
    Check(Bytes(0) == "0 Mo" && Bytes(312 * MiB) == "312 Mo" && Bytes(1023 * MiB + MiB * 4 / 10) == "1023 Mo" &&
        Bytes(1023 * MiB + MiB * 6 / 10) == "1,00 Go" && Bytes(1632087573ULL) == "1,52 Go", "Memory: Mo, then Go");
    Check(Throughput(0) == "0 Ko/s" && Throughput(-5) == "0 Ko/s" && Throughput(300 * 1024.0) == "300 Ko/s" &&
        Throughput(1023.4 * 1024) == "1023 Ko/s" && Throughput(1023.6 * 1024) == "1,0 Mo/s" && Throughput(1.2 * MiB) == "1,2 Mo/s" &&
        Throughput(9.94 * MiB) == "9,9 Mo/s" && Throughput(9.96 * MiB) == "10 Mo/s" && Throughput(512.3 * MiB) == "512 Mo/s" &&
        Throughput(2.5 * 1024 * MiB) == "2,5 Go/s" && Throughput(12.3 * 1024 * MiB) == "12 Go/s", "Transfer rates: Ko/s, then Mo/s and Go/s");
    Sample sample;
    sample.fps = true;
    sample.frames = {true, 60, 16.6667, 33.4};
    sample.memory = true; sample.workingSet = 1632087573ULL; sample.privateBytes = 1406601790ULL;
    sample.heap = true; sample.heapUsed = 312 * MiB; sample.heapSize = 420 * MiB;
    sample.cpu = 12.4;
    const std::string three = "60 FPS · 16,7 ms (max 33,4 ms)\n" + Memory + "\n" + Heap + " · CPU 12 %";
    Check(Text(sample) == three, "Three lines");
    // The fourth line: the graphics card and the game's disk, each left out when unknown.
    auto full = sample;
    full.gpu = 23; full.drive = L'C'; full.diskBusy = 4; full.diskRate = 1.2 * MiB;
    Check(Text(full) == three + "\nGPU 23 % · Disque C: 4 % · 1,2 Mo/s", "Four lines: " + Text(full));
    full.gpu = -1;
    Check(Text(full) == three + "\nDisque C: 4 % · 1,2 Mo/s", "Without the GPU");
    full.diskRate = -1;
    Check(Text(full) == three + "\nDisque C: 4 %", "Without the transfer rate");
    full.diskBusy = -1; full.diskRate = 2048;
    Check(Text(full) == three + "\nDisque C: 2 Ko/s", "Without the active time");
    full.gpu = 0; full.diskRate = -1;
    Check(Text(full) == three + "\nGPU 0 %", "Without the disk");
    full.drive = 0; full.diskBusy = 7;
    Check(Text(full) == three + "\nGPU 0 %", "No drive: no disk");
    full.gpu = -1;
    Check(Text(full) == three, "Neither: three lines");
    sample.heap = false;
    Check(Text(sample) == "60 FPS · 16,7 ms (max 33,4 ms)\n" + Memory + "\nCPU 12 %", "Without the managed heap");
    sample.heap = true; sample.cpu = -1;
    Check(Text(sample) == "60 FPS · 16,7 ms (max 33,4 ms)\n" + Memory + "\n" + Heap, "Without the CPU share yet");
    sample.heap = false;
    Check(Text(sample) == "60 FPS · 16,7 ms (max 33,4 ms)\n" + Memory, "Two lines");
    sample.memory = false; sample.frames.valid = false; sample.cpu = 3;
    Check(Text(sample) == Measuring + "\nCPU 3 %", "No frame yet, memory unreadable");
    sample.waiting = true;
    Check(Text(sample) == Measuring + "\nCPU 3 %", "With the frame rate, never the neutral text");
    // Without the frame rate (1.16.0): its figures left out whatever they are,
    // the GPU and disk line alone, or a neutral text without any figure.
    auto alone = full;
    alone.fps = false;
    alone.gpu = 23; alone.drive = L'C'; alone.diskBusy = 4; alone.diskRate = 1.2 * MiB;
    Check(Text(alone) == "GPU 23 % · Disque C: 4 % · 1,2 Mo/s", "The GPU and the disk alone: " + Text(alone));
    alone.waiting = true;
    Check(Text(alone) == "GPU 23 % · Disque C: 4 % · 1,2 Mo/s", "A part still waiting: the other's figures");
    alone.drive = 0;
    Check(Text(alone) == "GPU 23 %", "The GPU alone");
    alone.gpu = -1; alone.drive = L'D'; alone.diskBusy = -1; alone.diskRate = 0;
    Check(Text(alone) == "Disque D: 0 Ko/s", "The disk alone");
    alone.drive = 0;
    Check(Text(alone) == WaitingLine, "Nothing to show yet: the neutral text, never the FPS one");
    alone.waiting = false;
    Check(Text(alone) == UnavailableLine, "Nothing to give (Windows refusing the parts shown)");
    Check(Placeholder(WaitingLine) && Placeholder(UnavailableLine) && !Placeholder(Measuring) && !Placeholder("GPU 23 %"),
        "The neutral texts never set the width");
    const auto left = Place(0), right = Place(1), bottomLeft = Place(2), bottomRight = Place(3), other = Place(7);
    Check(!left.right && !left.bottom && left.horizontal == 8 && left.vertical == 8, "Top left");
    Check(right.right && !right.bottom && right.horizontal == 8 && right.vertical == 48, "Top right: under the game's menu buttons");
    Check(!bottomLeft.right && bottomLeft.bottom && bottomLeft.horizontal == 8 && bottomLeft.vertical == 8, "Bottom left");
    Check(bottomRight.right && bottomRight.bottom && bottomRight.horizontal == 8 && bottomRight.vertical == 8, "Bottom right");
    Check(!other.right && !other.bottom && other.vertical == 8, "Anything else: top left");
}
// The PDH figures: instance names filtered, the busiest engine, the game's disk found.
void DeviceModel() {
    using namespace perfOverlay;
    const std::wstring engine = L"pid_4242_luid_0x00000000_0x0000D1E2_phys_0_eng_0_engtype_3D";
    Check(EngineType(engine, 4242) == L"3D" && EngineType(engine, 424).empty() && EngineType(engine, 42420).empty() &&
        EngineType(L"pid_4242_luid_0x00000000_0x0000D1E2_phys_0_eng_9_engtype_High Priority 3D", 4242) == L"High Priority 3D" &&
        EngineType(L"pid_4242_luid_0x00000000_0x0000D1E2_phys_0_eng_9", 4242).empty() && EngineType(L"pid_4242_", 4242).empty() &&
        EngineType(L"pid_4242_luid_0x0_engtype_", 4242).empty() && EngineType(L"xpid_4242_engtype_3D", 4242).empty(),
        "Engine types of this process only (pid_424_ is not pid_4242_)");
    CounterItems engines{
        {L"pid_4242_luid_0x00000000_0x0000D1E2_phys_0_eng_0_engtype_3D", 20.1, true},
        {L"pid_4242_luid_0x00000000_0x0000E5F0_phys_0_eng_0_engtype_3D", 2.5, true}, // another adapter's 3D: another engine
        {L"pid_4242_luid_0x00000000_0x0000D1E2_phys_0_eng_1_engtype_Copy", 9.0, true},
        {L"pid_4242_luid_0x00000000_0x0000D1E2_phys_0_eng_4_engtype_Copy", 9.0, true},
        {L"pid_4242_luid_0x00000000_0x0000D1E2_phys_0_eng_9_engtype_High Priority 3D", 15.0, true},
        {L"pid_4243_luid_0x00000000_0x0000D1E2_phys_0_eng_0_engtype_3D", 80.0, true},             // another process
        {L"pid_4242_luid_0x00000000_0x0000D1E2_phys_0_eng_2_engtype_Compute 0", 90.0, false}};    // no valid value
    Check(GpuPercent(engines, 4242) == 20, "The busiest engine, as the Task Manager: 3D 20,1 % (never 22,6 % with the other adapter's)");
    engines[4].value = 30.4;
    Check(GpuPercent(engines, 4242) == 30, "Another engine busier: High Priority 3D");
    engines[4].value = 15.0;
    engines[2].value = engines[3].value = 24.4;
    Check(GpuPercent(engines, 4242) == 24, "Two Copy engines at 24,4 %: 24 %, never their sum");
    Check(GpuPercent(engines, 4243) == 80, "Another process: its own engines");
    Check(GpuPercent(engines, 1) == -1 && GpuPercent({}, 4242) == -1, "No engine of this process: no figure");
    for (auto& item : engines) item.valid = false;
    Check(GpuPercent(engines, 4242) == -1, "The first sample (no valid value): no figure");
    Check(GpuPercent({{L"pid_7_luid_0x0_0x1_phys_0_eng_0_engtype_3D", 70, true}, {L"pid_7_luid_0x0_0x2_phys_0_eng_0_engtype_3D", 60, true}}, 7) == 70 &&
        GpuPercent({{L"pid_7_luid_0x0_0x1_phys_0_eng_0_engtype_3D", 104.5, true}}, 7) == 100,
        "The 3D engines of two adapters: the busier one; clamped at 100 % (uncapped values)");
    Check(GpuPercent({{L"pid_7_luid_engtype_3D", -3, true}, {L"pid_7_luid_engtype_Copy", 0.4, true}}, 7) == 0 &&
        GpuPercent({{L"pid_7_luid_engtype_3D", std::nan(""), true}}, 7) == -1, "Negative values as 0, NaN skipped");
    Check(DriveOf(L"C:\\Jeux\\Dofus\\Dofus.exe") == L'C' && DriveOf(L"d:\\Dofus.exe") == L'D' &&
        DriveOf(L"\\\\?\\E:\\Dofus\\Dofus.exe") == L'E' && DriveOf(L"\\\\serveur\\jeux\\Dofus.exe") == 0 &&
        DriveOf(L"\\\\?\\UNC\\serveur\\jeux\\Dofus.exe") == 0 && DriveOf(L"") == 0 && DriveOf(L"1:\\x") == 0, "The game's drive letter");
    Check(HoldsDrive(L"3 C:", L'C') && HoldsDrive(L"1 D: E:", L'E') && HoldsDrive(L"1 D: E:", L'D') && HoldsDrive(L"12 c:", L'C') &&
        !HoldsDrive(L"0 D:", L'C') && !HoldsDrive(L"_Total", L'C') && !HoldsDrive(L"0 CD:", L'C') && !HoldsDrive(L"C:", L'C') &&
        !HoldsDrive(L"3 C:", 0), "Physical disks by the letters of their volumes");
    const CounterItems idle{{L"0 D:", 56.5, true}, {L"3 C:", 96.4, true}, {L"_Total", 88.6, true}};
    const CounterItems bytes{{L"0 D:", 10866306, true}, {L"3 C:", 1.2 * 1048576, true}, {L"_Total", 12124546, true}};
    const auto disk = Disk(idle, bytes, L'C');
    Check(disk.busy == 4 && Near(disk.rate, 1.2 * 1048576), "The game's disk: active 100 − idle, its bytes per second");
    Check(Disk(idle, bytes, L'D').busy == 44 && Near(Disk(idle, bytes, L'D').rate, 10866306), "Another drive, another disk");
    Check(Disk(idle, bytes, L'E').busy == -1 && Disk(idle, bytes, L'E').rate < 0, "A drive no physical disk holds: no figure");
    Check(Disk({{L"3 C:", 100.002, true}}, {}, L'C').busy == 0 && Disk({{L"3 C:", -2, true}}, {}, L'C').busy == 100,
        "Clamped 0 to 100 % (PDH idles over 100 %)");
    Check(Disk({{L"3 C:", 50, false}}, {{L"3 C:", 5, false}}, L'C').busy == -1 && Disk({{L"3 C:", 50, false}}, {{L"3 C:", 5, false}}, L'C').rate < 0,
        "Invalid values (the first sample): no figure");
    Check(Disk({{L"3 C:", 50, true}}, {{L"3 C:", -1, true}}, L'C').rate < 0 && Disk({{L"3 C:", 50, true}}, {}, L'C').busy == 50,
        "A negative rate is none; the active time alone");
}
// CounterRead's array (ReadArray on a fake PdhGetFormattedCounterArrayW):
// sized, then read, each item valid with fresh data only.
void CounterArrays() {
    using namespace perfOverlay;
    using namespace formatFake;
    const std::wstring engine = L"pid_9_luid_0x0_0x1_phys_0_eng_";
    entries = {{engine + L"0_engtype_3D", 37.5, PDH_CSTATUS_VALID_DATA}, {engine + L"1_engtype_Copy", 2.25, PDH_CSTATUS_NEW_DATA},
        {engine + L"2_engtype_Video Decode", 5, PDH_CSTATUS_INVALID_DATA}, {engine + L"3_engtype_Compute 0", -1, PDH_CALC_NEGATIVE_VALUE}};
    CounterItems items{{L"stale", 1, true}};
    Check(ReadArray(Format, nullptr, items) == ERROR_SUCCESS && calls == 2 && flags == (PDH_FMT_DOUBLE | PDH_FMT_NOCAP100),
        "Sized, then read: uncapped doubles");
    Check(items.size() == 4 && items[0].name == engine + L"0_engtype_3D" && items[0].value == 37.5 && items[0].valid &&
        items[1].value == 2.25 && items[1].valid && !items[2].valid && !items[3].valid && items[3].name == engine + L"3_engtype_Compute 0",
        "Each instance with its name and value, valid with fresh data only");
    calls = 0;
    growths = 1;
    Check(ReadArray(Format, nullptr, items) == ERROR_SUCCESS && calls == 3 && items.size() == 5 && items[4].valid,
        "An instance coming between two calls: sized again");
    calls = 0;
    growths = 5;
    Check(ReadArray(Format, nullptr, items) == static_cast<PDH_STATUS>(PDH_MORE_DATA) && calls == 3 && items.empty(),
        "Always more instances: given up after three calls");
    growths = 0;
    for (const PDH_STATUS refused : {static_cast<PDH_STATUS>(PDH_CSTATUS_INVALID_DATA), static_cast<PDH_STATUS>(PDH_NO_DATA)}) {
        calls = 0;
        answer = refused;
        items = {{L"stale", 1, true}};
        Check(ReadArray(Format, nullptr, items) == refused && calls == 1 && items.empty(), "Refused (the first collection, no instance): passed on");
    }
    answer = ERROR_SUCCESS;
}
// The mouse placement: edges, places and the drag's steps.
void PlacementModel() {
    using namespace perfOverlay;
    Check(ClampEdge(500, 200, 1920) == 500 && ClampEdge(-50, 200, 1920) == 8 && ClampEdge(1900, 200, 1920) == 1712 &&
        ClampEdge(10, 2000, 1920) == 8, "Edges inside the margins; the first margin when the panel is too small");
    Check(PlaceOfEdge(860, 200, 1920) == 5000 && PlaceOfEdge(8, 200, 1920) == 47 && PlaceOfEdge(1712, 200, 1920) == 9953 &&
        PlaceOfEdge(0, 200, 1920) == 0 && PlaceOfEdge(1720, 200, 1920) == 10000 && PlaceOfEdge(3000, 200, 1920) == 10000 &&
        PlaceOfEdge(-5, 200, 1920) == 0 && PlaceOfEdge(10, 2000, 1920) == 0 && PlaceOfEdge(std::nanf(""), 200, 1920) == 0,
        "Edges as ten-thousandths of the free space");
    Check(EdgeOfPlace(5000, 200, 1920) == 860 && EdgeOfPlace(0, 200, 1920) == 8 && EdgeOfPlace(10000, 200, 1920) == 1712 &&
        EdgeOfPlace(-1, 200, 1920) == 8 && EdgeOfPlace(20000, 200, 1920) == 1712 && EdgeOfPlace(5000, 200, 1280) == 540 &&
        EdgeOfPlace(5000, 2000, 1920) == 8, "Places back to edges, inside the margins");
    // Whatever the resolution and the overlay's size: the same share of the free space.
    for (const float space : {1280.f, 1920.f, 2560.f, 3840.f})
        for (const float size : {150.f, 226.f, 400.f}) {
            const int place = PlaceOfEdge(.25f * (space - size), size, space);
            Check(place == 2500 && std::fabs(EdgeOfPlace(place, size, space) - .25f * (space - size)) < .01f,
                "Round trip at " + std::to_string(space) + " for " + std::to_string(size));
        }
    Check(Held(100, 226, 12) == 112 && Held(100, 226, 300) == 325 && Held(100, 226, -5) == 100 && Held(100, .75f, 3) == 100,
        "The point held, kept inside the overlay");
    // Client pixels to the panel's: scaled alike on both axes, centred between black bars.
    const auto at = [](Point point, float x, float y) { return Near(point.x, x) && Near(point.y, y); };
    Check(at(PanelPoint(960, 540, 1920, 1080, 1920, 1080), 960, 540) && at(PanelPoint(2996, 1796, 3840, 2160, 1920, 1080), 1498, 898),
        "The display's aspect ratio: scaled");
    Check(at(PanelPoint(100, 60, 1920, 1200, 1920, 1080), 100, 0) && at(PanelPoint(100, 30, 1920, 1200, 1920, 1080), 100, -30) &&
        at(PanelPoint(1920, 1200, 3840, 2400, 1920, 1080), 960, 540), "Letterboxed: the bars above and below left out");
    Check(at(PanelPoint(320, 0, 2560, 1080, 1920, 1080), 0, 0) && at(PanelPoint(2240, 1080, 2560, 1080, 1920, 1080), 1920, 1080) &&
        at(PanelPoint(100, 500, 2560, 1080, 1920, 1080), -220, 500), "Pillarboxed: the bars left and right left out");
    Rect box{8, 8, 226, 52};
    Drag state;
    // One frame; the overlay then laid out where it was drawn (the game's layout pass).
    const auto step = [&](bool press, bool down, bool located, bool reached, float x, float y) {
        const auto result = Advance(state, press, down, located, reached, x, y, box, 1920, 1080);
        if (result == DragStep::Move || result == DragStep::Back) { box.x = state.left; box.y = state.top; }
        return result;
    };
    Check(step(true, true, true, false, 500, 500) == DragStep::None && state.phase == Drag::Idle, "A press the panel gives to something else");
    Check(step(true, true, false, true, 20, 20) == DragStep::None && state.phase == Drag::Idle, "A press not located (another window)");
    Check(step(false, true, true, true, 20, 20) == DragStep::None && state.phase == Drag::Idle, "The button held from before: no press");
    Check(step(true, true, true, true, 20, 20) == DragStep::None && state.phase == Drag::Pressed && state.grabX == 12 && state.grabY == 12,
        "Pressed on the overlay: the grab offset");
    Check(step(false, true, true, true, 22, 22) == DragStep::None && state.phase == Drag::Pressed, "Under 4 px: still a click");
    Check(step(false, false, true, true, 22, 22) == DragStep::None && state.phase == Drag::Idle, "Released: a click, nothing saved");
    step(true, true, true, true, 20, 20);
    Check(step(false, true, true, true, 520, 320) == DragStep::Move && state.phase == Drag::Moving && state.left == 508 && state.top == 308,
        "Moving with the grab offset");
    Check(step(false, true, true, true, 5000, -100) == DragStep::Move && state.left == 1686 && state.top == 8, "Clamped inside the margins");
    Check(step(false, false, true, true, 0, 0) == DragStep::End && state.phase == Drag::Idle && state.left == 1686 && state.top == 8 &&
        state.width == 226 && state.height == 52 && state.spaceWidth == 1920 && state.spaceHeight == 1080, "Released anywhere: the last place");
    box = {8, 8, 226, 52};
    step(true, true, true, true, 20, 20);
    Check(step(false, true, false, false, 300, 300) == DragStep::None && state.phase == Drag::Idle, "Pressed, then the window left: nothing");
    step(true, true, true, true, 20, 20);
    Check(step(false, true, true, false, 300, 300) == DragStep::None && state.phase == Drag::Idle && box.x == 8,
        "Pressed, then the point held covered: nothing moved");
    step(true, true, true, true, 20, 20);
    step(false, true, true, true, 120, 20); // drawn at 108
    step(false, true, true, true, 220, 20); // seen at 108, drawn at 208
    Check(step(false, true, true, false, 230, 20) == DragStep::Back && state.phase == Drag::Idle && state.left == 108 && state.top == 8 &&
        state.width == 226 && state.spaceWidth == 1920, "The point held covered while moving: back at the last place seen");
    step(true, true, true, true, 120, 20);
    step(false, true, true, true, 220, 20); // seen at 108, drawn at 208
    Check(step(false, false, true, false, 220, 20) == DragStep::Back && state.left == 108, "Covered at the release: back too, never left under it");
    step(true, true, true, true, 120, 20);
    step(false, true, true, true, 220, 20);
    Check(step(false, true, false, false, 330, 20) == DragStep::End && state.left == 208 && state.phase == Drag::Idle,
        "The window left while moving: ended where it was");
    // The interface hidden while moving (no layout): ended, the sizes of the last move kept.
    step(true, true, true, true, 220, 20);
    step(false, true, true, true, 320, 20);
    const float none = std::nanf("");
    Check(Advance(state, false, true, false, false, 0, 0, Rect{none, none, none, none}, none, none) == DragStep::End && state.left == 308 &&
        state.width == 226 && state.height == 52 && state.spaceWidth == 1920 && state.spaceHeight == 1080, "Hidden while moving: the last move's sizes");
    // 1.16.0, the size: within 50..300 %, as the settings keep it.
    Check(Settings::ValidPerfOverlayScale(MinScale) && !Settings::ValidPerfOverlayScale(MinScale - 1) && Settings::ValidPerfOverlayScale(MaxScale) &&
        !Settings::ValidPerfOverlayScale(MaxScale + 1) && Settings{}.perfOverlayScale == BaseScale && BaseScale == 100, "The sizes the settings keep");
    Check(Scaled(226, 100, 150) == 339 && Scaled(339, 150, 50) == 113 && Scaled(52, 100, 200) == 104, "Sizes scaled from one scale to another");
    // 1.16.1, its handles: its bottom-right corner, the last 16 px of both
    // sides at its size but never under 18 pixels of the screen, then its
    // right and bottom edges, 5 px thick at its size but never under 6 pixels
    // of the screen; the corner never wider than half of it nor higher than
    // it, an edge never thicker than a quarter of it (the rest moves it).
    using enum ResizeHandle;
    const auto zone = [](const Rect& box, float x, float y, int scale, float pixels = 1) { return HandleAt(box, x, y, scale, pixels); };
    const Rect overlay{8, 8, 226, 52};
    Check(zone(overlay, 233, 59, 100) == Corner && zone(overlay, 216, 42, 100) == Corner &&
        zone(overlay, 215.5f, 59, 100) == Bottom && zone(overlay, 233, 41.5f, 100) == Right,
        "100 %: its corner 18 px a side (the screen's least), the edges beyond it");
    Check(zone(overlay, 228, 20, 100) == Right && zone(overlay, 227.5f, 20, 100) == Body && zone(overlay, 100, 54, 100) == Bottom &&
        zone(overlay, 100, 53.5f, 100) == Body && zone(overlay, 20, 20, 100) == Body && zone(overlay, 20, 59, 100) == Bottom,
        "100 %: its right and bottom edges 6 px thick, the rest moves it");
    const Rect doubled{8, 8, 452, 104}, half{8, 8, 113, 26}, tripled{8, 8, 678, 156};
    Check(zone(doubled, 428, 80, 200) == Corner && zone(doubled, 427.5f, 80, 200) == Body && zone(doubled, 450.5f, 30, 200) == Right &&
        zone(doubled, 449.5f, 30, 200) == Body && zone(doubled, 100, 102.5f, 200) == Bottom && zone(doubled, 100, 101.5f, 200) == Body,
        "200 %: its corner 32 px, its edges 10 px: its handles grow with it");
    Check(zone(tripled, 638, 116, 300) == Corner && zone(tripled, 637, 116, 300) == Body && zone(tripled, 671, 30, 300) == Right &&
        zone(tripled, 100, 149, 300) == Bottom, "300 %: its corner 48 px, its edges 15 px");
    Check(zone(half, 103, 16, 50) == Corner && zone(half, 102.5f, 16, 50) == Body && zone(half, 115, 12, 50) == Right &&
        zone(half, 50, 28, 50) == Bottom && zone(half, 50, 27.5f, 50) == Body,
        "50 %: still 18 px and 6 px of the screen, never smaller with the overlay");
    // A small window (a panel pixel half a pixel of the screen) or a 4K one
    // (two): 18 pixels of the screen are 36, then 9 panel pixels.
    Check(zone(overlay, 198, 24, 100, .5f) == Corner && zone(overlay, 197.5f, 24, 100, .5f) == Body &&
        zone(overlay, 222, 20, 100, .5f) == Right && zone(overlay, 100, 48, 100, .5f) == Bottom,
        "A small window: its corner 36 panel pixels, its edges 12 (still 18 and 6 pixels of the screen)");
    Check(zone(overlay, 218, 44, 100, 2) == Corner && zone(overlay, 217.5f, 44, 100, 2) == Body &&
        zone(overlay, 229, 20, 100, 2) == Right && zone(overlay, 228.5f, 20, 100, 2) == Body,
        "Twice the screen's pixels: its own size wins (16 and 5 panel pixels)");
    Check(zone(overlay, 218, 44, 100, std::nanf("")) == Corner && zone(overlay, 216, 42, 100, 0) == Corner,
        "No screen scale known: one pixel of the screen a panel pixel");
    // A one-line overlay at 50 % (the GPU alone): its corner never wider
    // than half of it, the whole of its height; its bottom edge a quarter.
    const Rect line{8, 8, 32, 10};
    Check(zone(line, 24, 9, 50) == Corner && zone(line, 24, 17.5f, 50) == Corner && zone(line, 23.5f, 9, 50) == Body &&
        zone(line, 10, 16, 50) == Bottom && zone(line, 10, 15, 50) == Body,
        "A small overlay: its corner half its width and its whole height, its left half moves it");
    const Handles sizes = HandleSizes(overlay, 100, 1);
    Check(sizes.cornerWidth == 18 && sizes.cornerHeight == 18 && sizes.right == 6 && sizes.bottom == 6 &&
        Near(PanelScale(3840, 2160, 1920, 1080), 2) && Near(PanelScale(1920, 1200, 1920, 1080), 1) && Near(PanelScale(960, 540, 1920, 1080), .5),
        "The handles' sizes; the screen's pixels in a panel pixel");
    Check(std::string(HandleName(ResizeHandle::Corner)) == "corner" && std::string(HandleName(ResizeHandle::Right)) == "right" &&
        std::string(HandleName(ResizeHandle::Bottom)) == "bottom", "The handles' names in the log");
    // The scale follows the corner along the diagonal, from the size at the
    // press; the top-left stays put; whole percents within 50..300, never
    // past the margins.
    Drag sizing;
    sizing.resize = true;
    sizing.left = sizing.top = 8;
    sizing.startWidth = 226;
    sizing.startHeight = 52;
    sizing.grabX = sizing.grabY = 5;
    Check(ResizedScale(sizing, 229, 55, 1920, 1080) == 100, "Where it was pressed: the same size");
    Check(ResizedScale(sizing, 342, 81, 1920, 1080) == 150 && ResizedScale(sizing, 455, 107, 1920, 1080) == 200,
        "Its corner under the cursor: 150 %, then 200 %");
    Check(ResizedScale(sizing, 342, 55, 1920, 1080) == 147 && ResizedScale(sizing, 229, 81, 1920, 1080) == 103,
        "Along one side only: the closest size on its diagonal (a wide overlay follows its width)");
    Check(ResizedScale(sizing, -300, -300, 1920, 1080) == 50 && ResizedScale(sizing, 5000, 5000, 1920, 1080) == 300, "Within 50..300 %");
    sizing.left = 1500;
    sizing.top = 900;
    Check(ResizedScale(sizing, 5000, 5000, 1920, 1080) == 182 && 1500 + Scaled(226, 100, 182) <= 1920 - 8,
        "Never past the panel's margins: entirely on screen, as a move keeps it");
    Check(ResizedScale(sizing, 5000, 5000, 1580, 1080) == 50, "No room even for the smallest size: the smallest size");
    sizing.left = 8;
    sizing.top = 1000;
    Check(ResizedScale(sizing, 5000, 5000, 1920, 1080) == 138 && 1000 + Scaled(52, 100, 138) <= 1080 - 8,
        "Never past the bottom margin either: low on the screen, its height is the limit");
    sizing.left = sizing.top = 8;
    sizing.startWidth = 339;
    sizing.startHeight = 78;
    sizing.startScale = sizing.scale = 150;
    Check(ResizedScale(sizing, 342, 81, 1920, 1080) == 150 && ResizedScale(sizing, 229, 55, 1920, 1080) == 100, "From another size: the same sizes");
    sizing.startWidth = 0;
    Check(ResizedScale(sizing, 342, 81, 1920, 1080) == 150, "No size known: unchanged");
    // Its right edge follows the cursor across, its bottom edge down, the
    // whole overlay alike, wherever the cursor goes along it.
    Drag edge;
    edge.resize = true;
    edge.handle = ResizeHandle::Right;
    edge.left = edge.top = 8;
    edge.startWidth = 226;
    edge.startHeight = 52;
    edge.grabX = 2;
    edge.grabY = 30;
    Check(ResizedScale(edge, 232, 30, 1920, 1080) == 100 && ResizedScale(edge, 345, 30, 1920, 1080) == 150 &&
        ResizedScale(edge, 345, 700, 1920, 1080) == 150 && ResizedScale(edge, 458, -50, 1920, 1080) == 200 &&
        ResizedScale(edge, 100, 30, 1920, 1080) == 50, "Its right edge: its width under the cursor, the height alike");
    edge.handle = ResizeHandle::Bottom;
    edge.grabX = 130;
    edge.grabY = 2;
    Check(ResizedScale(edge, 100, 58, 1920, 1080) == 100 && ResizedScale(edge, 100, 84, 1920, 1080) == 150 &&
        ResizedScale(edge, 1500, 84, 1920, 1080) == 150 && ResizedScale(edge, 100, 5000, 1920, 1080) == 300,
        "Its bottom edge: 26 px down, 150 % (the corner gives 103 % for that move), within 50..300 %");
    edge.top = 1000;
    Check(ResizedScale(edge, 100, 5000, 1920, 1080) == 138, "An edge never past the margins either");
    // A resize's steps: a press in the grip, a click changes nothing, the scale
    // follows (drawn at once here), released: saved; the point held covered:
    // back at the last scale where the panel picked it.
    box = {8, 8, 226, 52};
    Drag resize;
    const auto resizeStep = [&](bool press, bool down, bool located, bool reached, float x, float y) {
        const auto result = Advance(resize, press, down, located, reached, x, y, box, 1920, 1080, resize.scale);
        if (result == DragStep::Scale || result == DragStep::Back) { box.width = Scaled(226, 100, resize.scale); box.height = Scaled(52, 100, resize.scale); }
        return result;
    };
    Check(resizeStep(true, true, true, true, 229, 55) == DragStep::None && resize.phase == Drag::Pressed && resize.resize &&
        resize.grabX == 5 && resize.grabY == 5 && resize.left == 8 && resize.top == 8 && resize.startWidth == 226 && resize.startScale == 100,
        "Pressed in the grip: a resize, its offset from the corner");
    Check(resizeStep(false, true, true, true, 231, 57) == DragStep::None && resizeStep(false, false, true, true, 231, 57) == DragStep::None &&
        resize.phase == Drag::Idle && box.width == 226, "A click: nothing");
    Check(resizeStep(true, true, true, true, 20, 20) == DragStep::None && !resize.resize, "Pressed elsewhere: a move");
    resizeStep(false, false, true, true, 20, 20);
    resizeStep(true, true, true, true, 229, 55);
    Check(resizeStep(false, true, true, true, 342, 81) == DragStep::Scale && resize.phase == Drag::Moving && resize.scale == 150 &&
        resize.seenScale == 100 && resize.left == 8 && resize.top == 8, "Resized: 150 %, its top-left kept");
    Check(resizeStep(false, true, true, true, 455, 107) == DragStep::Scale && resize.scale == 200 && resize.seenScale == 150, "200 %");
    Check(resizeStep(false, true, true, false, 455, 107) == DragStep::Back && resize.phase == Drag::Idle && resize.scale == 150 && box.width == 339,
        "The point held covered: back at the last scale where the panel picked it");
    resizeStep(true, true, true, true, 342, 81);
    resizeStep(false, true, true, true, 455, 107);
    Check(resizeStep(false, false, true, true, 455, 107) == DragStep::End && resize.phase == Drag::Idle && resize.scale == 200 &&
        resize.spaceWidth == 1920 && resize.spaceHeight == 1080, "Released: ended at the last scale");
    resizeStep(true, true, true, true, 455, 107);
    resizeStep(false, true, true, true, 229, 55);
    Check(resizeStep(false, true, false, false, 0, 0) == DragStep::End && resize.scale == 100, "The window left while resized: ended there");
    const Point corner = HeldPoint(resize, {8, 8, 339, 78}), held = HeldPoint(state, {100, 100, 226, 52});
    Check(corner.x == 342 && corner.y == 81 && held.x == 100 + std::clamp(state.grabX, 0.f, 225.f), "The point held: from the corner when resized");
    // Pressed on its right or bottom edge: a resize by that edge, the point
    // held at the same offsets from its bottom-right corner.
    box = {8, 8, 226, 52};
    Drag edges;
    Check(Advance(edges, true, true, true, true, 232, 30, box, 1920, 1080) == DragStep::None && edges.resize && edges.handle == ResizeHandle::Right &&
        edges.grabX == 2 && edges.grabY == 30, "Pressed on its right edge: a resize by it");
    Check(Advance(edges, false, true, true, true, 345, 400, box, 1920, 1080) == DragStep::Scale && edges.scale == 150 && edges.left == 8 &&
        edges.top == 8, "Its right edge dragged: 150 %, its top-left kept");
    Check(Advance(edges, false, false, true, true, 345, 400, box, 1920, 1080) == DragStep::End && edges.scale == 150, "Released");
    Check(Advance(edges, true, true, true, true, 100, 58, box, 1920, 1080) == DragStep::None && edges.handle == ResizeHandle::Bottom && edges.grabY == 2,
        "Pressed on its bottom edge: a resize by it");
    Check(Advance(edges, false, true, true, true, 900, 84, box, 1920, 1080) == DragStep::Scale && edges.scale == 150, "Its bottom edge dragged: 150 %");
    Advance(edges, false, false, true, true, 900, 84, box, 1920, 1080);
    Check(Advance(edges, true, true, true, true, 100, 30, box, 1920, 1080) == DragStep::None && !edges.resize && edges.handle == ResizeHandle::Body,
        "Pressed elsewhere: a move");
    Advance(edges, false, false, true, true, 100, 30, box, 1920, 1080);
    // A small window: the corner pressed 30 panel pixels from it (15 pixels of the screen).
    Check(Advance(edges, true, true, true, true, 204, 30, box, 1920, 1080, 100, .5f) == DragStep::None && edges.handle == ResizeHandle::Corner,
        "A small window: its corner still 18 pixels of the screen");
}

// ------------------------------------------------------------ installation
void ResetInstall() {
    perfOverlay::installed = false;
    perfOverlay::n = {};
    contracts.clear();
    natives.clear();
}
void Install() {
    using namespace perfOverlay;
    sys.counter = FakeCounter; sys.frequency = FakeFrequency; sys.milliseconds = FakeMilliseconds;
    sys.processors = FakeProcessors; sys.memory = FakeMemory; sys.cpu = FakeCpu; sys.lookup = FakeLookup;
    sys.button = mouse::Button; sys.cursor = mouse::Cursor;
    sys.pid = pdhFake::Pid; sys.gamePath = pdhFake::Path; sys.spawn = pdhFake::Spawn; sys.devicePeriod = 2;
    sys.pdh = {pdhFake::Open, pdhFake::Add, pdhFake::Collect, pdhFake::Read, pdhFake::Close};
    // No performance counter: disabled, inert.
    fakeFrequency = 0;
    InstallPerfOverlay();
    Check(!installed && Logged("PERF_OVERLAY_DISABLED") == 1, "No counter: the overlay is disabled, the mod is not");
    fakeFrequency = Frequency;
    // A field layout that is not the pinned one: disabled.
    ResetInstall();
    offsets["dzrv"] = 0x78;
    InstallPerfOverlay();
    Check(!installed && Logged("PERF_OVERLAY_DISABLED") == 2, "The layer host's offset is checked");
    Check(toolsFake::registrations.empty(), "Disabled: no top-bar button");
    offsets["dzrv"] = 0x70;
    module->settings.perfOverlay = true;
    for (int i = 0; i < 3; ++i) { Frame(); Tick(); }
    Check(Owned(high).empty() && ring.count == 0 && !sampling && !PerfOverlayCleanupPending(), "Disabled: nothing created, nothing measured");
    RestorePerfOverlay();
    module->settings.perfOverlay = false;
    // The game export at another address: the overlay without the managed heap.
    ResetInstall();
    lookupMatches = false;
    InstallPerfOverlay();
    Check(installed && !n.gcUsed && !n.gcHeap && Logged("PERF_OVERLAY_HEAP_UNAVAILABLE") == 1, "Managed heap optional");
    lookupMatches = true;
    // The code behind the thunks differing from the snapshot: the same.
    for (const uintptr_t body : {0x5FE340, 0x5FDB20, 0x6194D0, 0x6194B0}) {
        ResetInstall();
        nativeMismatch = body;
        InstallPerfOverlay();
        Check(installed && !n.gcUsed && !n.gcHeap && natives.contains(body), "GC code checked behind the thunks");
    }
    Check(Logged("PERF_OVERLAY_HEAP_UNAVAILABLE") == 5, "Each mismatch logged");
    nativeMismatch = 0;
    ResetInstall();
    InstallPerfOverlay();
    Check(installed && n.gcUsed == &FakeGcUsed && n.gcHeap == &FakeGcHeap && Logged("PERF_OVERLAY_HEAP_UNAVAILABLE") == 5 &&
        processors == 8 && frequency == Frequency, "Installed with the managed heap");
    Check(natives == std::set<uintptr_t>{0x5B9230, 0x5B9210, 0x5FE340, 0x5FDB20, 0x6194D0, 0x6194B0},
        "The two export thunks and the four functions behind them are checked");
    // Its top-bar button (1.16.0), registered with the overlay, as the treasure
    // hunt's and the notebook's: no layouts, no window.
    const auto& registration = toolsFake::registrations.back();
    Check(registration.key == "perf" && registration.title == "Performances" && registration.icon == 1 && registration.layouts == 0 &&
        registration.visible == &ButtonPressed && registration.toggle == &ButtonClick && registration.button,
        "Its top-bar button: « Performances », FigmaIcons.activity (1, not the statistics window's chart), no layouts");
    const auto registered = toolsFake::registrations.size();
    // Every contract at its pinned RVA (an independent copy of the snapshot's).
    std::set<std::tuple<std::string, std::string, int, uintptr_t>> expected{
        {"VisualElement", "get_parent", 0, 0x55F7180}, {"VisualElement", "get_panel", 0, 0x55F7170}, {"VisualElement", "get_name", 0, 0x3158710},
        {"VisualElement", "get_hierarchy", 0, 0x185C4A0}, {"VisualElement", "Insert", 2, 0x55E5710},
        {"VisualElement", "RemoveFromHierarchy", 0, 0x55E7BD0}, {"VisualElement", "set_pickingMode", 1, 0x55F97B0},
        {"VisualElement", "set_name", 1, 0x55F9670}, {"VisualElement", "get_style", 0, 0x55F74A0}, {"VisualElement", "get_layout", 0, 0x55F6930},
        {"Hierarchy", "get_childCount", 0, 0x55FFA80}, {"Hierarchy", "get_Item", 1, 0x55FE670}, {"Focusable", "set_focusable", 1, 0x5777EF0},
        {"Label", ".ctor", 0, 0x5729760}, {"TextElement", "set_text", 1, 0x564B780}, {"TextElement", "set_enableRichText", 1, 0x564B1E0},
        {"Length", ".ctor", 2, 0x561D3D0}, {"BaseVisualElementPanel", "Pick", 1, 0x57A4050}, {"pq", "pbs", 0, 0x2E93CC0},
        {"VisualElement", ".ctor", 0, 0x55F50B0}, {"VisualElement", "set_visible", 1, 0x55FA170}};
    const std::vector<std::tuple<std::string, uintptr_t, uintptr_t>> styles{
        {"position", 0x55D1A30, 0x55D48B0}, {"left", 0x55D1610, 0x55D3EB0}, {"top", 0x55D1CD0, 0x55D4F70}, {"right", 0x55D1AC0, 0x55D4960},
        {"bottom", 0x55D1190, 0x55D3530}, {"paddingLeft", 0x55D1970, 0x55D46D0}, {"paddingRight", 0x55D19B0, 0x55D4770},
        {"paddingTop", 0x55D19F0, 0x55D4810}, {"paddingBottom", 0x55D1930, 0x55D4630}, {"borderTopLeftRadius", 0x55D1100, 0x55D33E0},
        {"borderTopRightRadius", 0x55D1140, 0x55D3440}, {"borderBottomLeftRadius", 0x55D0F90, 0x55D3050},
        {"borderBottomRightRadius", 0x55D0FD0, 0x55D30B0}, {"backgroundColor", 0x55D0D90, 0x55D2BA0}, {"color", 0x55D11D0, 0x55D35D0},
        {"fontSize", 0x55D1500, 0x55D3D00}, {"whiteSpace", 0x55D26B0, 0x55D6370}, {"minWidth", 0x55D1850, 0x55D4410}};
    for (const auto& [property, get, set] : styles) {
        expected.insert({"InlineStyleAccess", "UnityEngine.UIElements.IStyle.get_" + property, 0, get});
        expected.insert({"InlineStyleAccess", "UnityEngine.UIElements.IStyle.set_" + property, 1, set});
    }
    Check(contracts == expected, "Exactly the pinned contracts: " + std::to_string(contracts.size()) + " of " + std::to_string(expected.size()));
    for (const uintptr_t rva : {0x5B9230, 0x5B9210, 0x5FE340, 0x5FDB20, 0x6194D0, 0x6194B0, 0x57A4050, 0x55F50B0, 0x55FA170})
        Check(Guarded(rva), "The GC exports, the code behind them, the panel's pick and the grip's elements are guarded");
    InstallPerfOverlay();
    Check(contracts == expected && Logged("PERF_OVERLAY_DISABLED") == 2 && toolsFake::registrations.size() == registered, "Installed once");
}

// --------------------------------------------------------------- lifecycle
void OptionOff() {
    using namespace perfOverlay;
    module->settings.perfOverlay = false;
    for (int i = 0; i < 5; ++i) Pump();
    Check(Owned(high).empty() && ring.count == 0 && !sampling && !PerfOverlayCleanupPending(), "Off: nothing created, nothing measured");
    // The mouse placement is no part: ticked alone, still nothing, nothing read.
    module->settings.perfOverlayDrag = true;
    const auto buttons = mouse::buttonReads, measured = measureReads;
    for (int i = 0; i < 5; ++i) Pump();
    Check(Owned(high).empty() && !sampling && !timing && mouse::buttonReads == buttons && measureReads == measured && !devices.worker,
        "« Déplacer et redimensionner l’affichage à la souris » alone: nothing created, nothing read");
    module->settings.perfOverlayDrag = false;
}
void Waiting() {
    using namespace perfOverlay;
    module->settings.perfOverlay = true;
    // A loading screen: no UIDocument root yet.
    const auto savedDocument = document;
    document = nullptr;
    Pump(3);
    Check(Owned(high).empty() && sampling && LoggedLine("PERF_OVERLAY_WAITING document") == 1, "No root: waits (logged once)");
    document = savedDocument;
    // The layer host replaced by something else, then RootHigh missing.
    Get(service).fields["dzrv"] = gameHigh;
    Pump();
    Check(LoggedLine("PERF_OVERLAY_WAITING layer-host") == 1, "A foreign layer host: waits");
    Get(service).fields["dzrv"] = host;
    Get(high).name = "RootHighRenamed";
    Pump();
    Check(Owned(high).empty() && LoggedLine("PERF_OVERLAY_WAITING layer") == 1, "No RootHigh: waits");
    Get(high).name = "RootHigh";
    // Two RootHigh: refused.
    auto twin = Element("VisualElement", "RootHigh");
    Add(host, twin);
    Pump();
    Check(Owned(high).empty() && Owned(twin).empty() && LoggedLine("PERF_OVERLAY_WAITING ambiguous-layer") == 1, "Two RootHigh: waits");
    Detach(twin);
    // The document not shown (no panel).
    Get(document).fields["panel"] = nullptr;
    Pump();
    Check(Owned(high).empty() && LoggedLine("PERF_OVERLAY_WAITING panel") == 1, "Layer without a panel: waits");
    Get(document).fields["panel"] = panel;
    Check(Logged("PERF_OVERLAY_ERROR") == 0, "Waiting is not an error");
}
void Created() {
    using namespace perfOverlay;
    ring.Clear(); // the frames of the waits above are left out of the exact figures below
    cpuMeter.Clear();
    Tick();
    const auto owned = Owned(high);
    Check(owned.size() == 1 && Overlay() == owned[0] && Get(high).children.front() == owned[0] && Get(high).children.back() == gameHigh,
        "Created first in RootHigh: under the game's own RootHigh elements");
    Check(Logged("PERF_OVERLAY_READY layer=RootHigh corner=0 heap=1 processors=8 drag=0 place=-1,-1") == 1, "Ready logged");
    const auto label = owned[0];
    auto& o = Get(label);
    Check(o.klass->name == "Label" && o.constructed && o.picking == 1 && !o.focusable && !o.rich,
        "Without the mouse placement: never a click, a hover or the keyboard focus");
    Check(EnumStyle(label, "position") == 1 && Pixels(label, "left") == 8 && Pixels(label, "top") == 8 &&
        !Styled(label, "right") && !Styled(label, "bottom"), "Absolute, 8 px from the top left corner");
    Check(Pixels(label, "paddingLeft") == 8 && Pixels(label, "paddingRight") == 8 && Pixels(label, "paddingTop") == 4 &&
        Pixels(label, "paddingBottom") == 4, "Compact inset");
    for (const char* radius : {"borderTopLeftRadius", "borderTopRightRadius", "borderBottomLeftRadius", "borderBottomRightRadius"})
        Check(Pixels(label, radius) == 6, std::string("Rounded: ") + radius);
    const auto background = ColorStyle(label, "backgroundColor"), color = ColorStyle(label, "color");
    Check(background[0] == 0 && background[1] == 0 && background[2] == 0 && background[3] > .4f && background[3] < .7f,
        "Semi-transparent dark background");
    Check(color[0] > .85f && color[1] > .85f && color[2] > .85f && color[3] > .9f, "Light text");
    Check(Pixels(label, "fontSize") == 12 && EnumStyle(label, "whiteSpace") == 2, "Small text, lines broken at the newlines only (WhiteSpace.Pre)");
    Check(o.text == Measuring + "\n" + Memory + "\n" + Heap && o.texts == 1, "First text: no frame, no CPU share yet: " + o.text);
    Check(!Styled(label, "minWidth"), "No minimum width before a layout");
    Check(mouse::buttonReads == 0 && mouse::cursorReads == 0 && picks == 0, "Without the mouse placement: no mouse read");
    // 1.16.1, its grip: its own element right above the label (never one of
    // the label's children: a child would take its text measure away), six
    // dots of its text's colour, partly transparent, never picked, hidden
    // without the mouse placement.
    const auto grip = GripOf();
    Check(grip && Get(high).children.at(1) == grip && Grips(high).size() == 1 && Get(label).children.empty(),
        "Its grip right above the label, never one of its children");
    const auto& g = Get(grip);
    Check(g.klass->name == "VisualElement" && g.constructed && g.picking == 1 && !g.visible && EnumStyle(grip, "position") == 1 &&
        Pixels(grip, "paddingLeft") == 9 && Pixels(grip, "paddingTop") == 9 && g.children.size() == 6, "A hidden box of six dots, never picked");
    std::set<std::pair<float, float>> dots;
    for (const auto dot : g.children) {
        const auto& d = Get(dot);
        const auto tint = ColorStyle(dot, "backgroundColor");
        Check(d.klass->name == "VisualElement" && d.constructed && d.picking == 1 && EnumStyle(dot, "position") == 1 &&
            Pixels(dot, "paddingLeft") == 2 && Pixels(dot, "paddingTop") == 2 && d.children.empty() && tint[0] == color[0] &&
            tint[1] == color[1] && tint[2] == color[2] && tint[3] > .5f && tint[3] < .8f, "Each dot 2 px, of the text's colour, never picked");
        dots.insert({Pixels(dot, "left"), Pixels(dot, "top")});
    }
    Check(dots == std::set<std::pair<float, float>>{{6.f, 0.f}, {3.f, 3.f}, {6.f, 3.f}, {0.f, 6.f}, {3.f, 6.f}, {6.f, 6.f}},
        "A triangle against its bottom-right corner");
    // About twice a second: not at the next pump, at the second one.
    Pump();
    Check(o.texts == 1, "No refresh 200 ms later");
    Pump();
    Check(o.text == Steady && o.texts == 2, "Refreshed 400 ms later: " + o.text);
    Check(Pixels(label, "minWidth") == MemoryLineWidth, "At least as wide as its last text was laid out");
    // Same figures: the text is left alone.
    Pump(4);
    Check(o.text == Steady && o.texts == 2, "Unchanged figures: no new text");
    Check(Pixels(label, "minWidth") == CpuLineWidth, "Widened to the wider text");
    // A 50 ms frame: the worst frame of the last second, then forgotten.
    Frame(500000);
    Pump(2);
    Check(o.text.find("(max 50,0 ms)") != std::string::npos && o.texts == 3, "A 50 ms frame shows: " + o.text);
    Pump(6);
    Check(o.text == Steady, "A second later it is forgotten: " + o.text);
    // Combat, map changes and resizes do not concern the label: kept as it is.
    Pump(20);
    Check(Overlay() == label && Owned(high).size() == 1 && Logged("PERF_OVERLAY_READY") == 1 && Logged("PERF_OVERLAY_REMOVED") == 0,
        "Kept across pumps");
    // Memory unreadable for a moment: that line is left out.
    memoryReadable = false;
    Pump(2);
    Check(o.text.find("Mémoire") == std::string::npos && o.text.find("CPU 12 %") != std::string::npos, "Unreadable memory left out: " + o.text);
    memoryReadable = true;
    Pump(2);
    Check(o.text == Steady, "Memory back");
    // Managed heap figures that make no sense: that part is left out.
    heapSize = 0;
    Pump(2);
    Check(o.text.find("Tas géré") == std::string::npos && o.text.find("CPU 12 %") != std::string::npos, "No heap size: left out: " + o.text);
    heapSize = 420LL << 20;
    heapUsed = heapSize + 1;
    Pump(2);
    Check(o.text.find("Tas géré") == std::string::npos, "More used than reserved: left out");
    heapUsed = 312LL << 20;
    Pump(2);
    Check(o.text == Steady, "Heap back");
    Check(Pixels(label, "minWidth") == CpuLineWidth, "Never narrower: its free edge stays still while shorter texts come and go");
    // The interface hidden by the game: no layout (0 or NaN), the width kept for when it shows again.
    for (const float hidden : {0.f, std::nanf("")}) {
        collapsedWidth = hidden;
        Pump(2);
        Check(Pixels(label, "minWidth") == CpuLineWidth, "No layout: the width is kept");
    }
    collapsedWidth = -1;
}
void Corners() {
    using namespace perfOverlay;
    const std::array<std::tuple<int, const char*, const char*, float, float>, 4> corners{{
        {1, "right", "top", 8.f, 48.f}, {3, "right", "bottom", 8.f, 8.f}, {2, "left", "bottom", 8.f, 8.f}, {0, "left", "top", 8.f, 8.f}}};
    for (const auto& [corner, horizontal, vertical, x, y] : corners) {
        const auto previous = Overlay();
        module->settings.perfOverlayCorner = corner;
        Tick();
        const auto owned = Owned(high);
        Check(owned.size() == 1 && owned[0] != previous && Overlay() == owned[0] && !Get(previous).fields["parent"],
            "Corner " + std::to_string(corner) + ": a new label, the previous one removed");
        const auto label = owned[0];
        Check(Pixels(label, horizontal) == x && Pixels(label, vertical) == y, "Corner " + std::to_string(corner) + " placement");
        for (const char* side : {"left", "right", "top", "bottom"})
            if (std::string(side) != horizontal && std::string(side) != vertical)
                Check(!Styled(label, side), std::string("Corner ") + std::to_string(corner) + ": no " + side);
        // The CPU samples went with the previous label; a fresh width too.
        Check(Get(label).text == SteadyNoCpu && !Styled(label, "minWidth"), "A new label's first figures: " + Get(label).text);
        Pump(2);
        Check(Get(label).text == Steady && Pixels(label, "minWidth") == MemoryLineWidth, "Then the CPU share: " + Get(label).text);
    }
    // Rate-limited: the same line once a minute.
    Check(Logged("PERF_OVERLAY_REMOVED corner") == 1 && Logged("PERF_OVERLAY_READY layer=RootHigh corner=1") == 1 &&
        Logged("PERF_OVERLAY_READY layer=RootHigh corner=3") == 1, "Corner changes logged");
    module->settings.perfOverlayCorner = 9; // never written by the options; read as the top left
    const auto kept = Overlay();
    Pump();
    Check(Overlay() == kept && Pixels(kept, "left") == 8 && Pixels(kept, "top") == 8, "Invalid corner: top left");
    module->settings.perfOverlayCorner = 0;
}
void Rebuilt() {
    using namespace perfOverlay;
    // The game replaces RootHigh: the label goes with the old one, a new one in the new layer.
    const auto previous = Overlay();
    auto fresh = Element("VisualElement", "RootHigh");
    Detach(high);
    Add(host, fresh);
    high = fresh;
    Pump();
    Check(Overlay() && Overlay() != previous && Owned(fresh).size() == 1 && Logged("PERF_OVERLAY_REMOVED replaced") == 1,
        "RootHigh replaced: recreated in the new one");
    // A new layer host while the old one stays attached: found by the 2 s check.
    const auto before = Overlay();
    auto newHost = Element("gmy", "RootLayer");
    Add(document, newHost);
    auto newHigh = Element("VisualElement", "RootHigh");
    Add(newHost, newHigh);
    Get(service).fields["dzrv"] = newHost;
    Pump(9);
    Check(Overlay() == before, "Kept until the 2 s check");
    Pump(2);
    Check(Owned(newHigh).size() == 1 && Overlay() == Owned(newHigh)[0] && Owned(high).empty() && !Get(before).fields["parent"],
        "Another layer host: moved within two seconds");
    high = newHigh;
    host = newHost;
    // Another session (compositor): recreated.
    const auto old = Overlay();
    module->compositor.reset(Make("Compositor"));
    Pump();
    Check(Overlay() && Overlay() != old && !Get(old).fields["parent"] && Owned(high).size() == 1, "Another session: recreated");
}
void StaleCpu() {
    using namespace perfOverlay;
    // The layer gone for 5 s while the game keeps all 8 processors busy: the
    // next label's figures never span that wait.
    Pump(2);
    Check(Overlay() && Get(Overlay()).text == Steady, "Shown with its CPU share");
    Get(document).fields["panel"] = nullptr;
    for (int p = 0; p < 25; ++p) {
        for (int i = 0; i < 12; ++i) Frame();
        cpuTime += 16000000; // 1,6 s of CPU in 0,2 s: 100 %
        Tick();
    }
    Check(!Overlay() && Owned(high).empty() && cpuMeter.count == 0, "Waiting: no label, no CPU sample kept");
    Get(document).fields["panel"] = panel;
    Tick();
    Check(Overlay() && Get(Overlay()).text == SteadyNoCpu, "Back: no CPU share yet rather than the wait's: " + Get(Overlay()).text);
    Pump(2);
    Check(Get(Overlay()).text == Steady, "Then the last 400 ms only: " + Get(Overlay()).text);
}
// Errors only once the label is shown (its set_text then sends a ChangeEvent):
// each label works until its first refresh. Three of them, then the overlay gives up.
void ShownErrors() {
    using namespace perfOverlay;
    const auto plainNew = api.object_new;
    api.object_new = [](Klass k) -> Obj { auto o = Make(static_cast<Class*>(k)->name); if (Get(o).klass->name == "Label") failOwner = o; return o; };
    failMethod = "set_text"; failCount = -1; failAttachedOnly = true; failOwner = Overlay();
    const auto built = labelsBuilt;
    const auto disabled = Logged("PERF_OVERLAY_DISABLED repeated errors");
    const auto savedWorkingSet = workingSet;
    int pumps = 0;
    // The working set grows: every refresh has a new text to set.
    while (!broken && pumps < 300) { workingSet += 64ULL << 20; Pump(); ++pumps; }
    workingSet = savedWorkingSet;
    Check(broken && labelsBuilt - built == 2 && Logged("PERF_OVERLAY_DISABLED repeated errors") == disabled + 1 && !Overlay() &&
        Owned(high).empty(), "Created twice more, removed at each first refresh, then given up (" + std::to_string(pumps) + " pumps)");
    Pump(60);
    Check(labelsBuilt - built == 2 && !Overlay(), "Given up: no more blinking");
    failCount = 0; failOwner = nullptr; failAttachedOnly = false;
    api.object_new = plainNew;
    module->settings.perfOverlay = false;
    Tick();
    module->settings.perfOverlay = true;
    Tick();
    Pump(2);
    Check(!broken && failures == 0 && Overlay() && Get(Overlay()).text == Steady, "Unchecked and checked again: back");
}
void Errors() {
    using namespace perfOverlay;
    module->settings.perfOverlay = false;
    Pump();
    Check(!Overlay() && Owned(high).empty() && Logged("PERF_OVERLAY_REMOVED option-off") == 1 && !sampling, "Option off: removed at once");
    Frame();
    Check(ring.count == 0, "Off: no frame measured");
    // Every new label armed for the failure injected below.
    const auto plainNew = api.object_new;
    api.object_new = [](Klass k) -> Obj { auto o = Make(static_cast<Class*>(k)->name); if (Get(o).klass->name == "Label") failOwner = o; return o; };
    // An error while creating: logged, nothing left in the layer, another try 5 s later.
    failMethod = "set_enableRichText"; failCount = 1;
    module->settings.perfOverlay = true;
    Tick();
    Check(!Overlay() && Owned(high).empty() && Grips(high).empty() && Logged("PERF_OVERLAY_ERROR Injected failure: set_enableRichText") == 1 &&
        failures == 1, "Creation error logged, nothing attached");
    Pump(24);
    Check(!Overlay() && failures == 1, "No retry before 5 s");
    Pump();
    Check(Overlay() && Owned(high).size() == 1 && failures == 1, "Retried after 5 s (a success does not clear the count)");
    // Unchecked and checked again: the count starts over.
    module->settings.perfOverlay = false;
    Tick();
    module->settings.perfOverlay = true;
    Tick();
    Check(Overlay() && failures == 0, "The option clears the count");
    // Errors in a row: the overlay gives up until the option is unchecked and checked again.
    failMethod = "set_text"; failCount = -1; failOwner = Overlay();
    Pump(2);
    Check(!Overlay() && Owned(high).empty() && failures == 1, "Refresh error: the label is removed");
    Pump(25);
    Check(failures == 2 && Owned(high).empty(), "Second error 5 s later");
    Pump(25);
    Check(broken && Logged("PERF_OVERLAY_DISABLED repeated errors") == 1 && !sampling && !PerfOverlayCleanupPending(), "Third: gives up");
    Check(Logged("PERF_OVERLAY_ERROR Injected failure: set_text") == 1, "The same error logged once");
    Pump(40);
    Check(!Overlay() && Owned(high).empty() && Logged("PERF_OVERLAY_DISABLED repeated errors") == 1, "Stays off");
    failCount = 0; failOwner = nullptr;
    api.object_new = plainNew;
    module->settings.perfOverlay = false;
    Tick();
    module->settings.perfOverlay = true;
    Tick();
    Check(!broken && Overlay() && Owned(high).size() == 1, "Unchecked and checked again: back");
}
void Cleanup() {
    using namespace perfOverlay;
    // Restore (mod stop, logout): removed at once, nothing measured any more.
    Pump(2);
    const auto label = Overlay(), grip = GripOf();
    Check(label && grip && PerfOverlayCleanupPending(), "Shown before the stop");
    RestorePerfOverlay();
    Check(!Overlay() && !GripOf() && !Get(label).fields["parent"] && !Get(grip).fields["parent"] && Owned(high).empty() && Grips(high).empty() &&
        !PerfOverlayCleanupPending() && !sampling, "Restored: the label and its grip");
    Frame();
    Check(ring.count == 0, "No frame measured after Restore");
    // A detach that fails: pending, tried again 500 ms later (Restore runs every frame once the mod stops).
    Tick();
    const auto shown = Overlay(), shownGrip = GripOf();
    Check(shown && Owned(high).size() == 1, "Shown again");
    failMethod = "RemoveFromHierarchy"; failOwner = shown; failCount = 1;
    auto tries = removals;
    const auto gripTries = gripRemovals;
    RestorePerfOverlay();
    Check(PerfOverlayCleanupPending() && Get(shown).fields["parent"] && !Get(shownGrip).fields["parent"] && gripRemovals == gripTries + 1 &&
        Logged("PERF_OVERLAY_CLEANUP_ERROR") == 1, "Failed detach: pending (its grip taken out first)");
    for (int frame = 0; frame < 30; ++frame) { nowMs += 16; RestorePerfOverlay(); }
    Check(PerfOverlayCleanupPending() && removals - tries == 1, "Not tried again within 500 ms");
    for (int frame = 0; frame < 10; ++frame) { nowMs += 16; RestorePerfOverlay(); }
    Check(!PerfOverlayCleanupPending() && !Get(shown).fields["parent"] && removals - tries == 2 && gripRemovals == gripTries + 1,
        "Done at the next try, 500 ms later (its grip taken out once)");
    // A label the game never lets go: tried twice a second, forgotten after ten seconds (the stop completes).
    Tick();
    const auto stuck = Overlay();
    failOwner = stuck; failCount = -1;
    tries = removals;
    const auto start = nowMs;
    uint64_t pendingUntil = start;
    while (PerfOverlayCleanupPending() && nowMs - start < 30000) {
        RestorePerfOverlay();
        if (PerfOverlayCleanupPending()) pendingUntil = nowMs;
        nowMs += 16;
    }
    Check(!PerfOverlayCleanupPending() && Logged("PERF_OVERLAY_CLEANUP_ABANDONED") == 1 && Logged("PERF_OVERLAY_CLEANUP_ERROR") == 2,
        "Abandoned once, the error logged once");
    Check(pendingUntil - start >= 9500 && pendingUntil - start < 10600 && removals - tries >= 20 && removals - tries <= 22,
        "Every 500 ms for 10 s: " + std::to_string(removals - tries) + " tries over " + std::to_string(pendingUntil - start) + " ms");
    failCount = 0; failOwner = nullptr;
    Detach(stuck);
    // Called rarely (every 2 s, a slow frame rate): still forgotten ten seconds after the first failure.
    Tick();
    const auto rare = Overlay();
    failOwner = rare; failCount = -1;
    tries = removals;
    const auto from = nowMs;
    while (PerfOverlayCleanupPending() && nowMs - from < 60000) {
        RestorePerfOverlay();
        if (PerfOverlayCleanupPending()) nowMs += 2000;
    }
    Check(!PerfOverlayCleanupPending() && nowMs - from == 10000 && removals - tries == 6 && Logged("PERF_OVERLAY_CLEANUP_ABANDONED") == 2,
        "Ten seconds whatever the call rate: " + std::to_string(removals - tries) + " tries over " + std::to_string(nowMs - from) + " ms");
    failCount = 0; failOwner = nullptr;
    Detach(rare);
    // The module stopping: the next tick removes it too.
    Tick();
    Check(Overlay(), "Shown");
    module->stopped = true;
    Tick();
    Check(!Overlay() && Owned(high).empty() && Logged("PERF_OVERLAY_REMOVED session") == 1, "Stopped session: removed");
    module->stopped = false;
}

// ------------------------------------------------------- mouse placement
void Dragging() {
    using namespace perfOverlay;
    using namespace mouse;
    module->settings.perfOverlayDrag = true;
    module->settings.perfOverlayCorner = 0;
    Tick();
    Pump(2);
    auto label = Overlay();
    Check(label && Get(label).picking == 0 && ui.picking == 0, "Mouse placement on: the overlay takes its clicks (pickingMode Position)");
    Check(Logged("PERF_OVERLAY_READY layer=RootHigh corner=0 heap=1 processors=8 drag=1 place=-1,-1") == 1, "Ready with the mouse placement");
    Check(Pixels(label, "left") == 8 && Pixels(label, "top") == 8 && !Styled(label, "right") && !Styled(label, "bottom"), "In its corner until moved");
    // Idle frames: one button read each; the cursor once every 50 ms (its
    // grip), the panel's pick only over the overlay.
    auto reads = buttonReads, cursors = cursorReads, picked = picks;
    for (int i = 0; i < 5; ++i) Mouse(500, 500, false);
    Check(buttonReads == reads + 5 && cursorReads == cursors + 1 && picks == picked && !Get(GripOf()).visible,
        "Idle frames: the button each, the cursor once within 50 ms, no pick away from the overlay");
    cursors = cursorReads;
    // A press outside the overlay: the panel picks something else, nothing follows the cursor.
    Mouse(500, 500, true);
    Check(cursorReads == cursors + 1 && picks == picked + 1 && drag.phase == Drag::Idle, "A press elsewhere: no drag");
    Mouse(20, 20, true);
    Mouse(600, 400, true);
    Mouse(600, 400, false);
    Check(Pixels(label, "left") == 8 && Pixels(label, "top") == 8 && saves == 0 && picks == picked + 1,
        "Held from elsewhere onto the overlay: still nothing");
    // The game's window in the background: no press of its own.
    foreground = false;
    Mouse(20, 20, true);
    Mouse(300, 300, true);
    Mouse(300, 300, false);
    foreground = true;
    Check(Pixels(label, "left") == 8 && saves == 0 && picks == picked + 1, "Another window in the foreground: no drag");
    // A game element drawn above the overlay (a menu, a popup): the press is its own.
    blockers.push_back({Element("VisualElement", "GameMenu"), 0, 0, 100, 100});
    Mouse(20, 20, true);
    Mouse(300, 300, true);
    Mouse(300, 300, false);
    blockers.clear();
    Check(Pixels(label, "left") == 8 && saves == 0 && picks == picked + 2, "Under a game element: no drag");
    // The interface hidden by the game: no layout, no drag.
    collapsedWidth = 0;
    Mouse(20, 20, true);
    Mouse(300, 300, true);
    Mouse(300, 300, false);
    collapsedWidth = -1;
    Check(Pixels(label, "left") == 8 && saves == 0 && picks == picked + 2, "Interface hidden: no drag");
    // A click: pressed and released within 4 px.
    Mouse(20, 20, true);
    Mouse(22, 21, true);
    Mouse(22, 21, false);
    Check(Pixels(label, "left") == 8 && Pixels(label, "top") == 8 && saves == 0 && drag.phase == Drag::Idle, "A click moves nothing");
    // The drag: the overlay follows the cursor, keeping the grab offset (12, 12).
    Mouse(20, 20, true);
    Mouse(1000, 500, true);
    Check(Pixels(label, "left") == 988 && Pixels(label, "top") == 488 && drag.phase == Drag::Moving && saves == 0, "Follows the cursor");
    Check(Get(GripOf()).visible && Pixels(GripOf(), "left") == 988 + LabelBox(label)[2] - 9 && Pixels(GripOf(), "top") == 488 + LabelBox(label)[3] - 9,
        "Its grip shown during the drag, at its bottom-right corner");
    Pump(2);
    Check(Pixels(label, "left") == 988 && Pixels(label, "top") == 488 && Overlay() == label && Get(label).text == Steady,
        "The pumps refresh the text and leave the place to the drag");
    // Beyond the screen: fully visible, 8 px inside.
    Mouse(2600, 1500, true);
    const float width = LabelBox(label)[2], height = LabelBox(label)[3];
    Check(Pixels(label, "left") == 1920 - width - 8 && Pixels(label, "top") == 1080 - height - 8, "Clamped inside the margins");
    Mouse(-300, -200, true);
    Check(Pixels(label, "left") == 8 && Pixels(label, "top") == 8, "Clamped at the top left too");
    Mouse(1500, 900, true);
    // Released: the place saved once, as ten-thousandths of the free space.
    Mouse(1500, 900, false);
    const int x = PlaceOfEdge(1488, width, 1920), y = PlaceOfEdge(888, height, 1080);
    Check(saves == 1 && module->settings.perfOverlayX == x && module->settings.perfOverlayY == y && x == 8784 && y == 8638 &&
        LoggedLine("PERF_OVERLAY_PLACED x=8784 y=8638") == 1, "Saved once: " + std::to_string(module->settings.perfOverlayX) + "," +
        std::to_string(module->settings.perfOverlayY));
    Pump(4);
    Check(saves == 1 && Pixels(label, "left") == 1488 && Pixels(label, "top") == 888 && Overlay() == label, "Stays where dropped");
    // Another resolution or interface scale: the same share of the free space.
    layerWidth = 1280;
    layerHeight = 720;
    Pump(2);
    Check(Pixels(label, "left") == EdgeOfPlace(x, width, 1280) && Pixels(label, "top") == EdgeOfPlace(y, height, 720) &&
        std::fabs(Pixels(label, "left") - 925.8f) < .1f, "Placed again for a smaller panel: " + std::to_string(Pixels(label, "left")));
    layerWidth = 1920;
    layerHeight = 1080;
    Pump(2);
    Check(std::fabs(Pixels(label, "left") - 1488) < .5f && std::fabs(Pixels(label, "top") - 888) < .5f && saves == 1, "Back, nothing saved");
    // The client area at twice the panel's pixels (the interface scale): client pixels converted.
    clientWidth = 3840;
    clientHeight = 2160;
    const auto at = LabelBox(label);
    Mouse(2996, 1796, true); // the panel's (1498, 898): on the overlay
    Mouse(1000, 1000, true); // (500, 500): 998 and 398 panel pixels away
    Check(std::fabs(Pixels(label, "left") - (at[0] - 998)) < .01f && std::fabs(Pixels(label, "top") - (at[1] - 398)) < .01f,
        "Client pixels scaled to the panel's");
    Mouse(1000, 1000, false);
    clientWidth = 1920;
    clientHeight = 1080;
    const float droppedLeft = Pixels(label, "left"), droppedTop = Pixels(label, "top");
    Check(saves == 2 && module->settings.perfOverlayX == PlaceOfEdge(droppedLeft, width, 1920) &&
        module->settings.perfOverlayY == PlaceOfEdge(droppedTop, height, 1080), "Saved");
    // The next session: a new label at that place (the last size until laid out, then exactly).
    RestorePerfOverlay();
    Tick();
    label = Overlay();
    Check(label && ui.pending && std::fabs(Pixels(label, "left") - droppedLeft) < .5f && std::fabs(Pixels(label, "top") - droppedTop) < .5f,
        "Recreated at its place");
    Step();
    Check(!ui.pending && Pixels(label, "left") == EdgeOfPlace(module->settings.perfOverlayX, LabelBox(label)[2], 1920) &&
        Pixels(label, "top") == EdgeOfPlace(module->settings.perfOverlayY, LabelBox(label)[3], 1080), "Placed exactly once laid out");
    // A label in a right corner moves by its right and bottom sides (the options page clears the place with the corner).
    module->settings.perfOverlayX = module->settings.perfOverlayY = -1;
    module->settings.perfOverlayCorner = 3;
    Tick();
    label = Overlay();
    Step(); // the button seen up once: a new label sees a press only when it starts
    Check(Pixels(label, "right") == 8 && Pixels(label, "bottom") == 8 && !Styled(label, "left") && !Styled(label, "top"), "Bottom right corner");
    const auto corner = LabelBox(label);
    Mouse(static_cast<long>(corner[0] + 5), static_cast<long>(corner[1] + 5), true);
    Mouse(705, 405, true);
    const float cornerWidth = LabelBox(label)[2], cornerHeight = LabelBox(label)[3];
    Check(Pixels(label, "right") == 1920 - 700 - cornerWidth && Pixels(label, "bottom") == 1080 - 400 - cornerHeight &&
        !Styled(label, "left") && !Styled(label, "top") && LabelBox(label)[0] == 700 && LabelBox(label)[1] == 400, "Its right and bottom sides moved");
    Mouse(705, 405, false);
    Check(saves == 3 && module->settings.perfOverlayX == PlaceOfEdge(700, cornerWidth, 1920) && module->settings.perfOverlayCorner == 3,
        "Saved, its corner kept");
    // The window losing the foreground mid-drag: the drag ends there, saved.
    Mouse(705, 405, true);
    Mouse(305, 305, true);
    foreground = false;
    Mouse(800, 800, true);
    Check(drag.phase == Drag::Idle && saves == 4 && LabelBox(label)[0] == 300 && LabelBox(label)[1] == 300,
        "Foreground lost: ended where it was, saved");
    foreground = true;
    Mouse(900, 600, true);
    Mouse(900, 600, false);
    Check(LabelBox(label)[0] == 300 && saves == 4, "The button still down when it comes back: no drag");
    // Something of the game drawn over the point held mid-drag (the loading
    // screen): ended, back at the last place where the panel picked it, saved.
    Mouse(305, 305, true);
    Mouse(405, 305, true);
    Mouse(505, 305, true);
    blockers.push_back({Element("VisualElement", "LoadingScreen"), 0, 0, 1920, 1080});
    Mouse(905, 305, true);
    Check(drag.phase == Drag::Idle && saves == 5 && LabelBox(label)[0] == 400 && LabelBox(label)[1] == 300,
        "Covered mid-drag: back at the last place seen, saved");
    Mouse(905, 305, false);
    Mouse(405, 305, true);
    Mouse(805, 305, true);
    Mouse(805, 305, false);
    blockers.clear();
    Check(LabelBox(label)[0] == 400 && saves == 5, "Under the loading screen: no drag");
    // Restore mid-drag: stopped, nothing saved.
    Mouse(405, 305, true);
    Mouse(605, 305, true);
    Check(drag.phase == Drag::Moving, "Dragging");
    RestorePerfOverlay();
    Check(drag.phase == Drag::Idle && saves == 5 && !Overlay(), "Restore: the drag stopped, nothing saved");
    Mouse(705, 305, true);
    Mouse(705, 305, false);
    Tick();
    label = Overlay();
    Step();
    Check(saves == 5 && std::fabs(LabelBox(label)[0] - EdgeOfPlace(module->settings.perfOverlayX, LabelBox(label)[2], 1920)) < .01f,
        "Recreated at the saved place");
    // The label taken away by the game mid-drag (its panel gone): stopped, nothing saved.
    const auto start = LabelBox(label);
    Mouse(static_cast<long>(start[0] + 5), static_cast<long>(start[1] + 5), true);
    Mouse(static_cast<long>(start[0] + 105), static_cast<long>(start[1] + 5), true);
    Check(drag.phase == Drag::Moving, "Dragging again");
    Get(document).fields["panel"] = nullptr;
    Tick();
    Check(!Overlay() && drag.phase == Drag::Idle, "Removed with its layer: the drag stopped");
    Mouse(static_cast<long>(start[0] + 205), static_cast<long>(start[1] + 5), true);
    Mouse(static_cast<long>(start[0] + 205), static_cast<long>(start[1] + 5), false);
    Get(document).fields["panel"] = panel;
    Tick();
    label = Overlay();
    Check(label && saves == 5, "Back, nothing saved");
    // A button held while the label appears: never a press.
    RestorePerfOverlay();
    button = true;
    Tick();
    label = Overlay();
    Step();
    const auto held = LabelBox(label);
    Mouse(static_cast<long>(held[0] + 5), static_cast<long>(held[1] + 5), true);
    Mouse(static_cast<long>(held[0] + 300), static_cast<long>(held[1] + 5), true);
    Mouse(static_cast<long>(held[0] + 300), static_cast<long>(held[1] + 5), false);
    Check(LabelBox(label)[0] == held[0] && saves == 5, "Held before the label: no drag");
    // A save that fails: logged, the place kept for this session.
    saveFails = true;
    Mouse(static_cast<long>(held[0] + 5), static_cast<long>(held[1] + 5), true);
    Mouse(static_cast<long>(held[0] + 55), static_cast<long>(held[1] + 5), true);
    Mouse(static_cast<long>(held[0] + 55), static_cast<long>(held[1] + 5), false);
    saveFails = false;
    const int failedPlace = module->settings.perfOverlayX;
    Check(saves == 6 && Logged("PERF_OVERLAY_PLACE_ERROR Injected save failure") == 1 && failedPlace == PlaceOfEdge(LabelBox(label)[0], LabelBox(label)[2], 1920) &&
        std::fabs(LabelBox(label)[0] - (held[0] + 50)) < .01f, "Save failure logged, the overlay where dropped");
    // Its text then widens it (the first figures, then the CPU share): a right
    // corner's label grows to the left, then the next refresh places it again
    // at the same share of the free space.
    Pump(4);
    Check(module->settings.perfOverlayX == failedPlace &&
        std::fabs(LabelBox(label)[0] - EdgeOfPlace(failedPlace, LabelBox(label)[2], 1920)) < .01f, "Kept at that place for this session");
    // Unchecked (1.16.0): locked at its place, never sent back to its corner;
    // the drag in progress stops unsaved (back at the place saved), clicks
    // let through, the button no longer read.
    Mouse(static_cast<long>(held[0] + 55), static_cast<long>(held[1] + 5), true);
    Mouse(static_cast<long>(held[0] + 255), static_cast<long>(held[1] + 5), true);
    Check(drag.phase == Drag::Moving, "Dragging when unchecked");
    module->settings.perfOverlayDrag = false;
    Tick();
    Check(Overlay() == label && Get(label).picking == 1 && ui.picking == 1 && drag.phase == Drag::Idle && saves == 6 &&
        module->settings.perfOverlayX == failedPlace, "Unchecked: clicks let through, the drag stopped without saving, the place kept");
    Pump(2);
    const auto locked = LabelBox(label);
    Check(std::fabs(locked[0] - EdgeOfPlace(failedPlace, locked[2], 1920)) < .01f &&
        std::fabs(locked[1] - EdgeOfPlace(module->settings.perfOverlayY, locked[3], 1080)) < .01f && Styled(label, "right") &&
        Pixels(label, "right") > 8, "Locked at its place, not in its corner");
    reads = buttonReads;
    cursors = cursorReads;
    picked = picks;
    Mouse(static_cast<long>(locked[0] + 5), static_cast<long>(locked[1] + 5), true);
    Mouse(600, 600, true);
    Mouse(600, 600, false);
    Check(buttonReads == reads && cursorReads == cursors && picks == picked && LabelBox(label)[0] == locked[0] && saves == 6 &&
        !Get(GripOf()).visible, "Unchecked: the mouse is not read, the overlay stays, no grip");
    Pump(4);
    Check(LabelBox(label)[0] == locked[0] && LabelBox(label)[1] == locked[1], "Still there");
    // Locked across a restart: recreated at its place, its clicks let through.
    RestorePerfOverlay();
    Tick();
    label = Overlay();
    Step();
    Check(label && Get(label).picking == 1 && !ui.pending &&
        std::fabs(LabelBox(label)[0] - EdgeOfPlace(module->settings.perfOverlayX, LabelBox(label)[2], 1920)) < .01f &&
        std::fabs(LabelBox(label)[1] - EdgeOfPlace(module->settings.perfOverlayY, LabelBox(label)[3], 1080)) < .01f,
        "Recreated locked at its place");
    // No place saved (a settings file without one): its corner, the top left
    // unless an earlier revision chose another.
    module->settings.perfOverlayX = module->settings.perfOverlayY = -1;
    module->settings.perfOverlayCorner = 0;
    Pump(2);
    Check(Pixels(Overlay(), "left") == 8 && Pixels(Overlay(), "top") == 8, "Top left again");
    Check(changes == 0 && syncs == saves, "Every place saved without Module::Change (its appearance refresh), the options page synchronised");
}

// The mouse placement at the screen's edges: the game's menu buttons drawn
// over the top right, black bars around the panel; another client's Metamob
// choice kept with the place.
void DragEdges() {
    using namespace perfOverlay;
    using namespace mouse;
    module->settings.perfOverlayDrag = true;
    Tick();
    const auto label = Overlay();
    Step(); // the button seen up
    Check(label && Get(label).picking == 0 && LabelBox(label)[0] == 8 && LabelBox(label)[1] == 8, "Mouse placement on, top left");
    const float width = LabelBox(label)[2], height = LabelBox(label)[3];
    const auto saved = saves;
    // The game's menu buttons over the top right (RootPopup, 41 px high).
    const Blocker buttons{Element("VisualElement", "MenuButtons"), 1300, 0, 620, 41};
    blockers.push_back(buttons);
    // Held by its lower part, the overlay goes partly under them: its centre
    // covered, never the point held.
    Mouse(20, 50, true); // the point held: (12, 42) in the overlay
    Mouse(900, 53, true);
    Mouse(1800, 50, true);
    Mouse(1800, 50, false);
    const auto under = LabelBox(label);
    Check(under[0] == 1920 - 8 - width && under[1] == 8 && saves == saved + 1 &&
        Within({buttons.x, buttons.y, buttons.width, buttons.height}, under[0] + under[2] / 2, under[1] + under[3] / 2),
        "Dropped at the top right, its centre under the menu buttons");
    // Grabbed again by its visible part, above its bottom edge (6 px: a
    // resize): it moves.
    Mouse(static_cast<long>(under[0]) + 14, 50, true);
    Mouse(1000, 500, true);
    Check(drag.phase == Drag::Moving && !drag.resize && LabelBox(label)[0] == 986 && LabelBox(label)[1] == 458, "Grabbed by its visible part");
    Mouse(1000, 500, false);
    // The point held taken under them: back at the last place where it was seen.
    Mouse(1000, 470, true); // the point held: (14, 12)
    Mouse(1200, 100, true);
    Mouse(1400, 20, true);  // drawn at (1386, 8): the point held, (1400, 20), under the buttons
    Mouse(1400, 20, true);
    Check(drag.phase == Drag::Idle && LabelBox(label)[0] == 1186 && LabelBox(label)[1] == 88 && saves == saved + 3 &&
        module->settings.perfOverlayX == PlaceOfEdge(1186, width, 1920) && module->settings.perfOverlayY == PlaceOfEdge(88, height, 1080),
        "The point held covered: back at the last place seen, saved there");
    Mouse(1400, 20, false);
    Check(LabelBox(label)[0] == 1186 && saves == saved + 3, "Released afterwards: nothing more");
    blockers.clear();
    // A borderless full screen at another aspect ratio than the display's:
    // the panel scaled alike on both axes and centred between black bars.
    clientWidth = 1920;
    clientHeight = 1200; // 60 px above and below the 1920 x 1080 panel
    Mouse(1196, 153, true); // the panel's (1196, 93): on the overlay
    Mouse(1296, 253, true);
    Check(drag.phase == Drag::Moving && LabelBox(label)[0] == 1286 && LabelBox(label)[1] == 188, "Letterboxed: the bars left out");
    Mouse(1296, 253, false);
    clientWidth = 2560;
    clientHeight = 1080; // 320 px left and right
    Mouse(1616, 193, true); // the panel's (1296, 193): on the overlay
    Mouse(1016, 393, true);
    Check(drag.phase == Drag::Moving && LabelBox(label)[0] == 686 && LabelBox(label)[1] == 388, "Pillarboxed: the bars left out");
    Mouse(1016, 393, false);
    Mouse(100, 393, true); // a black bar: outside the panel
    Check(drag.phase == Drag::Idle, "A press on a black bar: not the overlay");
    Mouse(100, 393, false);
    clientWidth = 1920;
    clientHeight = 1080;
    Check(saves == saved + 5, "Each drop saved once");
    // Another client's Metamob choice not read yet: kept with the place, the
    // options page synchronised; never Module::Change and its appearance refresh.
    const bool metamob = module->settings.metamobSync;
    const auto synced = syncs;
    sharedMetamob = !metamob;
    Mouse(696, 393, true);
    Mouse(796, 493, true);
    Mouse(796, 493, false);
    Check(saves == saved + 6 && syncs == synced + 1 && module->settings.metamobSync == !metamob && changes == 0 &&
        LabelBox(label)[0] == 786, "Saved as Module::Change saves, without its appearance refresh");
    sharedMetamob.reset();
    module->settings.metamobSync = metamob;
    module->settings.perfOverlayDrag = false;
    Pump(2);
    Check(Overlay() == label && std::fabs(LabelBox(label)[0] - 786) < .5f && Get(label).picking == 1 && saves == saved + 6,
        "Unchecked: locked where it was dropped");
    module->settings.perfOverlayX = module->settings.perfOverlayY = -1;
    Pump(2);
    Check(Overlay() == label && Pixels(label, "left") == 8 && Pixels(label, "top") == 8 && Get(label).picking == 1,
        "No place saved: top left again");
}

// ---------------------------------------------------------- GPU and disk
// A worker that missed JoinMs (a busy machine) ends by itself after its PDH
// call: wait until it has, its query closed, before expecting a new one.
bool Retired() {
    const HANDLE retiring = perfOverlay::devices.retiring;
    return pdhFake::Alive() == 0 && (!retiring || WaitForSingleObject(retiring, 0) == WAIT_OBJECT_0);
}
void DeviceWorkers() {
    using namespace perfOverlay;
    Pump(2);
    Check(Overlay() && Get(Overlay()).text == Steady && !devices.worker && !devices.thread && pdhFake::spawns == 0,
        "GPU and disk off: no worker");
    // Both on: a worker with this process's engines and the physical disks.
    module->settings.perfOverlayGpu = module->settings.perfOverlayDisk = true;
    Tick();
    Check(devices.worker && devices.thread && pdhFake::spawns == 1 && devices.worker->gpu && devices.worker->disk &&
        devices.worker->pid == 4242 && devices.worker->drive == L'C' && devices.worker->period == 2, "Started while the overlay is shown");
    Check(WaitFor([] { const auto figures = Figures(); return figures.gpu >= 0 && figures.disk.busy >= 0 && figures.disk.rate >= 0; }),
        "Figures from the second collection");
    Check(pdhFake::Paths() == std::vector<std::wstring>{L"\\GPU Engine(pid_4242_*)\\Utilization Percentage",
        L"\\PhysicalDisk(*)\\% Idle Time", L"\\PhysicalDisk(*)\\Disk Bytes/sec"}, "English counter paths, this process's engines only");
    Check(Figures().gpuProblem.empty() && Figures().diskProblem.empty(), "No problem");
    Pump(2);
    Check(Get(Overlay()).text == Steady + "\n" + DevicesLine, "Fourth line: " + Get(Overlay()).text);
    Check(pdhFake::spawns == 1 && pdhFake::Alive() == 1, "One worker, one query");
    // The GPU unchecked: left out at the next refresh; the worker restarted with the disk's counters only.
    pdhFake::ClearPaths();
    module->settings.perfOverlayGpu = false;
    Tick();
    Check(pdhFake::spawns == 2 && devices.worker && !devices.worker->gpu && devices.worker->disk, "Restarted for the disk only");
    Check(WaitFor([] { return Figures().disk.busy >= 0; }), "Disk figures");
    Check(pdhFake::Paths() == std::vector<std::wstring>{L"\\PhysicalDisk(*)\\% Idle Time", L"\\PhysicalDisk(*)\\Disk Bytes/sec"} &&
        pdhFake::Alive() == 1, "No GPU counter; the first worker's query closed");
    Pump(2);
    Check(Get(Overlay()).text == Steady + "\nDisque C: 4 % · 1,2 Mo/s", "Disk only: " + Get(Overlay()).text);
    // Both unchecked: stopped and joined, its query closed.
    module->settings.perfOverlayDisk = false;
    Tick();
    Check(!devices.worker && !devices.thread && WaitFor(Retired), "Both off: the worker stopped");
    Pump(2);
    Check(Get(Overlay()).text == Steady, "Three lines again");
    // The GPU alone.
    module->settings.perfOverlayGpu = true;
    Tick();
    Check(WaitFor([] { return Figures().gpu >= 0; }), "GPU figure");
    Pump(2);
    Check(Get(Overlay()).text == Steady + "\nGPU 23 %", "GPU only: " + Get(Overlay()).text);
    // The label gone (a loading screen): stopped; shown again: started again.
    module->settings.perfOverlayDisk = true;
    Tick();
    auto spawned = pdhFake::spawns;
    Get(document).fields["panel"] = nullptr;
    Tick();
    Check(!Overlay() && !devices.worker && !devices.thread && WaitFor(Retired), "Not shown: stopped");
    Pump(3);
    Check(pdhFake::spawns == spawned && !devices.worker, "Not started while waiting");
    Get(document).fields["panel"] = panel;
    Tick();
    Check(Overlay() && devices.worker && pdhFake::spawns == spawned + 1, "Shown again: started again");
    // Restore (the mod stopping): stopped and joined.
    RestorePerfOverlay();
    Check(!devices.worker && !devices.thread && WaitFor(Retired), "Restore: stopped");
    Tick();
    Check(Overlay() && devices.worker && WaitFor([] { return Figures().gpu >= 0 && Figures().disk.busy >= 0; }), "Back after a restore");
    Pump(2);
    Check(Get(Overlay()).text == Steady + "\n" + DevicesLine, "Four lines again");
    // What PDH refuses leaves its figure out, logged once; the overlay goes on.
    const auto restart = [] {
        module->settings.perfOverlayGpu = module->settings.perfOverlayDisk = false;
        Tick();
        Check(WaitFor(Retired), "The previous worker ended");
        module->settings.perfOverlayGpu = module->settings.perfOverlayDisk = true;
        Tick();
    };
    const auto problems = [] { const auto figures = Figures(); return !figures.gpuProblem.empty() && !figures.diskProblem.empty(); };
    pdhFake::Configure([] { pdhFake::openStatus = static_cast<PDH_STATUS>(PDH_CSTATUS_NO_OBJECT); });
    restart();
    Check(WaitFor(problems), "PDH refused");
    Pump(2);
    Check(Get(Overlay()).text == Steady && LoggedLine("PERF_OVERLAY_GPU_UNAVAILABLE query=C0000BB8") == 1 &&
        LoggedLine("PERF_OVERLAY_DISK_UNAVAILABLE query=C0000BB8") == 1, "No PDH: both left out, logged: " + Get(Overlay()).text);
    restart();
    Check(WaitFor(problems), "PDH refused again");
    Pump(4);
    Check(Logged("PERF_OVERLAY_GPU_UNAVAILABLE") == 1 && Logged("PERF_OVERLAY_DISK_UNAVAILABLE") == 1, "Logged once");
    pdhFake::Configure([] { pdhFake::openStatus = ERROR_SUCCESS; pdhFake::gpuStatus = static_cast<PDH_STATUS>(PDH_CSTATUS_NO_OBJECT); });
    gpuReported = diskReported = false; // each problem's own line below
    restart();
    Check(WaitFor([] { const auto figures = Figures(); return !figures.gpuProblem.empty() && figures.disk.busy >= 0; }), "GPU counter refused");
    Pump(2);
    Check(Get(Overlay()).text == Steady + "\nDisque C: 4 % · 1,2 Mo/s" && LoggedLine("PERF_OVERLAY_GPU_UNAVAILABLE counter=C0000BB8") == 1 &&
        !diskReported, "No GPU counter: the disk still shown: " + Get(Overlay()).text);
    pdhFake::Configure([] { pdhFake::gpuStatus = ERROR_SUCCESS; pdhFake::diskStatus = static_cast<PDH_STATUS>(PDH_CSTATUS_NO_COUNTER); });
    restart();
    Check(WaitFor([] { const auto figures = Figures(); return !figures.diskProblem.empty() && figures.gpu >= 0; }), "Disk counter refused");
    Pump(2);
    Check(Get(Overlay()).text == Steady + "\nGPU 23 %" && LoggedLine("PERF_OVERLAY_DISK_UNAVAILABLE counter=C0000BB9") == 1,
        "No disk counter: the GPU still shown: " + Get(Overlay()).text);
    pdhFake::Configure([] { pdhFake::diskStatus = ERROR_SUCCESS; });
    diskReported = false;
    pdhFake::path = L"\\\\serveur\\jeux\\Dofus.exe";
    restart();
    Check(devices.worker && devices.worker->drive == 0 && WaitFor([] { return !Figures().diskProblem.empty() && Figures().gpu >= 0; }),
        "A network path");
    Pump(2);
    Check(Get(Overlay()).text == Steady + "\nGPU 23 %" && LoggedLine("PERF_OVERLAY_DISK_UNAVAILABLE no-drive") == 1, "No drive letter: no disk");
    diskReported = false;
    pdhFake::path = L"E:\\Dofus\\Dofus.exe";
    restart();
    Check(WaitFor([] { return !Figures().diskProblem.empty() && Figures().gpu >= 0; }), "A drive no physical disk holds");
    Pump(2);
    Check(Get(Overlay()).text == Steady + "\nGPU 23 %" && LoggedLine("PERF_OVERLAY_DISK_UNAVAILABLE no-instance E:") == 1, "No disk for E:");
    pdhFake::path = L"C:\\Jeux\\Dofus\\Dofus.exe";
    gpuReported = diskReported = false;
    pdhFake::Configure([] { pdhFake::readStatus = static_cast<PDH_STATUS>(PDH_NO_DATA); });
    restart();
    Check(WaitFor(problems), "Reads failing");
    Pump(2);
    Check(Get(Overlay()).text == Steady && LoggedLine("PERF_OVERLAY_GPU_UNAVAILABLE read=800007D5") == 1 &&
        LoggedLine("PERF_OVERLAY_DISK_UNAVAILABLE read=800007D5") == 1, "Reads failing: both left out");
    gpuReported = diskReported = false;
    pdhFake::Configure([] { pdhFake::readStatus = ERROR_SUCCESS; pdhFake::collectStatus = static_cast<PDH_STATUS>(PDH_NO_DATA); });
    restart();
    Check(WaitFor(problems), "Collections failing");
    Pump(2);
    Check(Get(Overlay()).text == Steady && LoggedLine("PERF_OVERLAY_GPU_UNAVAILABLE collect=800007D5") == 1 &&
        LoggedLine("PERF_OVERLAY_DISK_UNAVAILABLE collect=800007D5") == 1, "Collections failing: both left out");
    pdhFake::Configure([] { pdhFake::collectStatus = ERROR_SUCCESS; });
    restart();
    Check(WaitFor([] { return Figures().gpu >= 0 && Figures().disk.busy >= 0; }), "Back");
    Pump(2);
    Check(Get(Overlay()).text == Steady + "\n" + DevicesLine, "Four lines once PDH answers");
    // A thread that cannot start: logged once, tried again 5 s later; the overlay goes on.
    module->settings.perfOverlayGpu = module->settings.perfOverlayDisk = false;
    Tick();
    Check(WaitFor(Retired), "The previous worker ended");
    pdhFake::spawnFails = true;
    module->settings.perfOverlayGpu = module->settings.perfOverlayDisk = true;
    spawned = pdhFake::spawns;
    Tick();
    Check(!devices.worker && !devices.thread && pdhFake::spawns == spawned + 1 && LoggedLine("PERF_OVERLAY_DEVICES_ERROR thread 8") == 1,
        "No thread: logged");
    Pump(24);
    Check(pdhFake::spawns == spawned + 1 && Get(Overlay()).text == Steady, "Not tried again before 5 s, three lines meanwhile");
    Pump();
    Check(pdhFake::spawns == spawned + 2 && LoggedLine("PERF_OVERLAY_DEVICES_ERROR thread 8") == 1, "Tried again 5 s later, logged once");
    pdhFake::spawnFails = false;
    Pump(25);
    Check(devices.worker && devices.thread && pdhFake::spawns == spawned + 3, "Started at the next try");
    // A worker stuck in a PDH call: the game's thread waits 250 ms at most,
    // and no second worker starts before the first one ends.
    module->settings.perfOverlayGpu = module->settings.perfOverlayDisk = false;
    Tick();
    const HANDLE gate = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    pdhFake::Configure([&] { pdhFake::gate = gate; });
    module->settings.perfOverlayGpu = module->settings.perfOverlayDisk = true;
    Tick();
    Check(WaitFor([] { return pdhFake::waiting.load() == 1; }), "Stuck in a collection");
    spawned = pdhFake::spawns;
    module->settings.perfOverlayGpu = module->settings.perfOverlayDisk = false;
    const auto before = GetTickCount64();
    Tick();
    const auto waited = GetTickCount64() - before;
    Check(!devices.worker && !devices.thread && devices.retiring && waited >= 200 && waited < 2000 &&
        Logged("PERF_OVERLAY_DEVICES_SLOW") == 1, "Waited " + std::to_string(waited) + " ms, then left to end by itself");
    module->settings.perfOverlayGpu = module->settings.perfOverlayDisk = true;
    Pump(3);
    Check(!devices.worker && pdhFake::spawns == spawned && pdhFake::Alive() == 1, "No second worker while the first one reads");
    pdhFake::Configure([] { pdhFake::gate = nullptr; });
    SetEvent(gate);
    Check(WaitFor([] { return pdhFake::waiting.load() == 0 && Retired(); }), "The stuck worker ends and closes its query");
    CloseHandle(gate);
    Tick();
    Check(devices.worker && devices.thread && !devices.retiring && pdhFake::spawns == spawned + 1, "Then a new worker");
    Check(WaitFor([] { return Figures().gpu >= 0 && Figures().disk.busy >= 0; }), "With its figures");
    module->settings.perfOverlayGpu = module->settings.perfOverlayDisk = false;
}

// ------------------------------------------------------ independent parts
// 1.16.0: each box its own part. The overlay shows as soon as one is ticked,
// with those parts only, and reads only what it shows: without the frame rate,
// never the frame counter, the memory, the managed heap or the CPU time.
void IndependentParts() {
    using namespace perfOverlay;
    Pump(3); // the GPU and the disk unchecked: a new label, its CPU share at its first refresh
    Check(Overlay() && Get(Overlay()).text == Steady && timing && !devices.worker && Logged("PERF_OVERLAY_REMOVED parts") >= 1,
        "The frame rate alone");
    // The GPU alone: a new label without the frame rate, a neutral text until
    // its first figure, before its worker starts and while the worker has none
    // yet (held in its first collection).
    const auto measured = measureReads;
    HANDLE gate = HoldCollections();
    module->settings.perfOverlay = false;
    module->settings.perfOverlayGpu = true;
    Tick();
    auto label = Overlay();
    Check(label && Get(label).text == WaitingLine && !timing && ring.count == 0 && cpuMeter.count == 0 &&
        Logged("PERF_OVERLAY_READY layer=RootHigh corner=0 heap=1 processors=8 drag=0 place=-1,-1 fps=0 gpu=1 disk=0") == 1,
        "« Charge du GPU » alone: shown, waiting for its first figure: " + (label ? Get(label).text : std::string()));
    Check(devices.worker && devices.worker->gpu && !devices.worker->disk, "The worker for the GPU only");
    Check(WaitFor([] { return pdhFake::waiting.load() == 1; }), "Its worker in its first collection");
    Pump(2);
    Check(devices.worker && Figures().gpu < 0 && Figures().gpuProblem.empty() && Get(label).text == WaitingLine,
        "Its worker started, no figure yet: still waiting, never « Mesure indisponible »: " + Get(label).text);
    ReleaseCollections(gate);
    Check(WaitFor([] { return Figures().gpu >= 0; }), "Its figure");
    Pump(2);
    Check(Get(label).text == "GPU 23 %" && !Styled(label, "minWidth"), "The GPU alone: " + Get(label).text);
    Pump(2);
    Check(Pixels(label, "minWidth") == 6 * 8 + 16, "Its width from its figures, never from the neutral text");
    Check(measureReads == measured && ring.count == 0 && cpuMeter.count == 0, "Without the frame rate: no counter, memory, heap or CPU read");
    // The disk alone, then both: the line of the parts ticked, nothing else.
    module->settings.perfOverlayGpu = false;
    module->settings.perfOverlayDisk = true;
    Tick();
    label = Overlay();
    Check(label && Get(label).text == WaitingLine && devices.worker && !devices.worker->gpu && devices.worker->disk, "« Activité du disque » alone");
    Check(WaitFor([] { return Figures().disk.busy >= 0; }), "Its figures");
    Pump(2);
    Check(Get(label).text == "Disque C: 4 % · 1,2 Mo/s", "The disk alone: " + Get(label).text);
    module->settings.perfOverlayGpu = true;
    Tick();
    label = Overlay();
    Check(WaitFor([] { return Figures().gpu >= 0 && Figures().disk.busy >= 0; }), "Both figures");
    Pump(2);
    Check(Get(label).text == DevicesLine && measureReads == measured && ring.count == 0, "Both, without the frame rate: " + Get(label).text);
    // The mouse placement without the frame rate: the button read, never the frame counter.
    module->settings.perfOverlayDrag = true;
    Tick();
    Step(); // the button seen up
    const auto box = LabelBox(label);
    const auto saved = saves;
    Mouse(static_cast<long>(box[0] + 5), static_cast<long>(box[1] + 5), true);
    Mouse(static_cast<long>(box[0] + 405), static_cast<long>(box[1] + 305), true);
    Mouse(static_cast<long>(box[0] + 405), static_cast<long>(box[1] + 305), false);
    Check(Overlay() == label && LabelBox(label)[0] == box[0] + 400 && LabelBox(label)[1] == box[1] + 300 && saves == saved + 1 &&
        module->settings.perfOverlayX >= 0 && measureReads == measured && ring.count == 0, "Dragged and saved, no frame read");
    // Unticked: locked there, its clicks let through.
    module->settings.perfOverlayDrag = false;
    Pump(2);
    Check(Overlay() == label && Get(label).picking == 1 && std::fabs(LabelBox(label)[0] - (box[0] + 400)) < .5f &&
        std::fabs(LabelBox(label)[1] - (box[1] + 300)) < .5f, "Locked where it was dropped");
    // The frame rate ticked again: its lines first, from no frame, then the GPU and disk line.
    module->settings.perfOverlay = true;
    Tick();
    label = Overlay();
    Check(label && timing && Get(label).text == Measuring + "\n" + Memory + "\n" + Heap + "\n" + DevicesLine &&
        std::fabs(LabelBox(label)[0] - EdgeOfPlace(module->settings.perfOverlayX, LabelBox(label)[2], 1920)) < .5f,
        "The frame rate back, at the same place: " + (label ? Get(label).text : std::string()));
    Pump(2);
    Check(Get(label).text == Steady + "\n" + DevicesLine && measureReads > measured, "Four lines: " + Get(label).text);
    // The GPU alone and refused by Windows: a neutral text, never an empty overlay.
    module->settings.perfOverlay = module->settings.perfOverlayDisk = false;
    pdhFake::Configure([] { pdhFake::gpuStatus = static_cast<PDH_STATUS>(PDH_CSTATUS_NO_OBJECT); });
    Tick();
    label = Overlay();
    Check(WaitFor([] { return !Figures().gpuProblem.empty(); }), "The GPU refused");
    Pump(2);
    Check(Get(label).text == UnavailableLine && !timing, "Nothing to give: " + Get(label).text);
    pdhFake::Configure([] { pdhFake::gpuStatus = ERROR_SUCCESS; });
    // Nothing ticked: removed, nothing measured, the worker stopped.
    module->settings.perfOverlayGpu = false;
    Tick();
    Check(!Overlay() && Owned(high).empty() && !sampling && !timing && !devices.worker && !devices.thread && WaitFor(Retired),
        "No part ticked: removed, nothing measured");
    // The disk alone, its worker started but without a figure yet: the
    // neutral text too, then its figures.
    gate = HoldCollections();
    module->settings.perfOverlayDisk = true;
    Tick();
    label = Overlay();
    Check(label && Get(label).text == WaitingLine && devices.worker && !devices.worker->gpu && devices.worker->disk,
        "« Activité du disque » alone again");
    Check(WaitFor([] { return pdhFake::waiting.load() == 1; }), "Its worker in its first collection");
    Pump(2);
    const auto none = Figures();
    Check(devices.worker && none.disk.busy < 0 && none.disk.rate < 0 && none.diskProblem.empty() && Get(label).text == WaitingLine,
        "Its worker started, no figure yet: still waiting, never « Mesure indisponible »: " + Get(label).text);
    ReleaseCollections(gate);
    Check(WaitFor([] { return Figures().disk.busy >= 0; }), "Its figures");
    Pump(2);
    Check(Get(label).text == "Disque C: 4 % · 1,2 Mo/s", "Then its figures: " + Get(label).text);
    module->settings.perfOverlayDisk = false;
    Tick();
    Check(!Overlay() && !devices.worker && !devices.thread && WaitFor(Retired), "Unticked again: removed, its worker stopped");
    module->settings.perfOverlayX = module->settings.perfOverlayY = -1;
}

// --------------------------------------------------------------- the resize
// 1.16.0: with the mouse placement on, its bottom-right corner (its grip, the
// only corner left square) resizes it: the whole overlay scales alike (text,
// insets, corners, minimum width), its top-left kept, entirely on screen,
// within 50..300 %; saved once at the end with the place of its top-left
// for that size; a click changes nothing; never while locked.
void Resizing() {
    using namespace perfOverlay;
    using namespace mouse;
    using Box = std::array<float, 4>;
    auto& settings = module->settings;
    settings.perfOverlay = settings.perfOverlayDrag = settings.perfOverlayShown = true;
    settings.perfOverlayGpu = settings.perfOverlayDisk = false;
    settings.perfOverlayCorner = 0;
    settings.perfOverlayX = settings.perfOverlayY = -1;
    Check(settings.perfOverlayScale == 100, "Its natural size until resized");
    RestorePerfOverlay();
    Tick();
    Pump(4); // its figures, then its widest text
    auto label = Overlay();
    Step(); // the button seen up
    Check(label && ui.scale == 100 && LabelBox(label) == Box{8, 8, 226, 52} && Pixels(label, "fontSize") == 12 &&
        Pixels(label, "minWidth") == CpuLineWidth && Logged("PERF_OVERLAY_READY layer=RootHigh corner=0 heap=1 processors=8 drag=1 place=-1,-1 fps=1 gpu=0 disk=0 scale=100") >= 1,
        "At its natural size, top left");
    for (const char* radius : {"borderTopLeftRadius", "borderTopRightRadius", "borderBottomLeftRadius"})
        Check(Pixels(label, radius) == 6, std::string("Rounded: ") + radius);
    Check(Pixels(label, "borderBottomRightRadius") == 0, "Its bottom-right corner square, where its grip shows");
    // A click on the grip: nothing.
    const auto saved = saves;
    Mouse(229, 55, true);
    Mouse(231, 57, true);
    Mouse(231, 57, false);
    Check(drag.phase == Drag::Idle && ui.scale == 100 && LabelBox(label) == Box{8, 8, 226, 52} && saves == saved, "A click on the grip changes nothing");
    // Its corner dragged: 150 %, everything alike, its top-left kept.
    Mouse(229, 55, true);
    Mouse(342, 81, true);
    Check(drag.phase == Drag::Moving && drag.resize && ui.scale == 150 && Pixels(label, "fontSize") == 18 && Pixels(label, "paddingLeft") == 12 &&
        Pixels(label, "paddingRight") == 12 && Pixels(label, "paddingTop") == 6 && Pixels(label, "paddingBottom") == 6 &&
        Pixels(label, "borderTopLeftRadius") == 9 && Pixels(label, "borderBottomLeftRadius") == 9 && Pixels(label, "borderBottomRightRadius") == 0 &&
        Pixels(label, "minWidth") == 339, "150 %: its text, insets, corners and minimum width alike");
    Check(LabelBox(label) == Box{8, 8, 339, 78} && Pixels(label, "left") == 8 && Pixels(label, "top") == 8 && saves == saved,
        "Its top-left kept, its corner under the cursor (at the grab offset); nothing saved yet");
    // A layout of another size meanwhile (the game lays it out after the
    // frame): the pumps neither widen nor place it during a resize.
    collapsedWidth = 1000;
    Tick();
    Tick();
    collapsedWidth = -1;
    Check(ui.minWidth == CpuLineWidth && Pixels(label, "minWidth") == 339 && Pixels(label, "left") == 8 && Pixels(label, "top") == 8 &&
        drag.phase == Drag::Moving, "Never widened nor placed during a resize");
    Mouse(455, 107, true);
    Check(ui.scale == 200 && Pixels(label, "fontSize") == 24 && LabelBox(label) == Box{8, 8, 452, 104}, "200 %");
    Mouse(455, 107, false);
    Check(drag.phase == Drag::Idle && saves == saved + 1 && settings.perfOverlayScale == 200 && settings.perfOverlayX == PlaceOfEdge(8, 452, 1920) &&
        settings.perfOverlayY == PlaceOfEdge(8, 104, 1080) && LoggedLine("PERF_OVERLAY_SCALED scale=200 x=54 y=82 handle=corner") == 1,
        "Saved once at the release: its size, and the place of its top-left for that size");
    Pump(4);
    Check(LabelBox(label) == Box{8, 8, 452, 104} && Pixels(label, "minWidth") == 452 && saves == saved + 1, "Stays at that size and place");
    // Down to the smallest size, then back up: the point held covered on the
    // way, back at the last size where the panel picked it, saved there.
    Mouse(455, 107, true);
    Mouse(-300, -300, true);
    Check(ui.scale == 50 && Pixels(label, "fontSize") == 6 && LabelBox(label) == Box{8, 8, 113, 26} && Pixels(label, "minWidth") == 113,
        "Never below 50 %");
    Mouse(229, 55, true);
    Check(ui.scale == 100 && LabelBox(label) == Box{8, 8, 226, 52}, "Back up to 100 %");
    blockers.push_back({Element("VisualElement", "Popup"), 200, 40, 200, 100});
    Mouse(229, 55, true);
    Check(drag.phase == Drag::Idle && ui.scale == 50 && LabelBox(label) == Box{8, 8, 113, 26} && saves == saved + 2 &&
        settings.perfOverlayScale == 50 && settings.perfOverlayX == 44 && settings.perfOverlayY == 76 &&
        LoggedLine("PERF_OVERLAY_SCALED scale=50 x=44 y=76 handle=corner") == 1, "The point held covered: back at the last size where it was picked, saved");
    // The pumps of that frame come before the game lays it out at that size
    // (its panels' update follows the compositor's): its layout still that of
    // 100 % neither widens it nor places it, placed at the next frame.
    collapsedWidth = 226;
    Tick();
    Tick();
    collapsedWidth = -1;
    Check(ui.rescaled && ui.pending && ui.minWidth == CpuLineWidth && Pixels(label, "minWidth") == 113,
        "Laid out at the size before: neither widened nor placed");
    Step();
    Check(!ui.rescaled && !ui.pending && LabelBox(label) == Box{8, 8, 113, 26}, "Placed at the next frame, at its size");
    Mouse(229, 55, false);
    blockers.clear();
    // Locked: its clicks let through, never resized, all its corners rounded.
    settings.perfOverlayDrag = false;
    Tick();
    Check(Get(label).picking == 1 && Pixels(label, "borderBottomRightRadius") == 3 && Pixels(label, "borderTopLeftRadius") == 3,
        "Locked: all its corners rounded (3 px at 50 %)");
    const auto buttons = buttonReads, cursors = cursorReads;
    Mouse(116, 29, true);
    Mouse(400, 400, true);
    Mouse(400, 400, false);
    Check(ui.scale == 50 && LabelBox(label) == Box{8, 8, 113, 26} && buttonReads == buttons && cursorReads == cursors && saves == saved + 2,
        "Locked: never resized, the mouse not read");
    settings.perfOverlayDrag = true;
    Tick();
    Step();
    Check(Get(label).picking == 0 && Pixels(label, "borderBottomRightRadius") == 0, "Unlocked: its grip's mark back");
    // The next start: at its size and its place.
    RestorePerfOverlay();
    Tick();
    label = Overlay();
    Step();
    Check(label && ui.scale == 50 && Pixels(label, "fontSize") == 6 && Pixels(label, "paddingLeft") == 4 && Pixels(label, "paddingTop") == 2 &&
        Pixels(label, "borderBottomRightRadius") == 0 && !ui.pending && std::fabs(LabelBox(label)[0] - 8) < .5f && std::fabs(LabelBox(label)[1] - 8) < .5f &&
        LoggedLine("PERF_OVERLAY_READY layer=RootHigh corner=0 heap=1 processors=8 drag=1 place=44,76 fps=1 gpu=0 disk=0 scale=50") == 1,
        "Recreated at its size and its place");
    Pump(4);
    Check(LabelBox(label)[2] == 113 && Pixels(label, "minWidth") == 113 && ui.minWidth == CpuLineWidth, "Its widest text kept at 100 %, drawn at 50 %");
    // A size outside 50..300 % (never saved): the natural size; the size saved again: back to it.
    settings.perfOverlayScale = 999;
    Tick();
    Check(ui.scale == 100 && Pixels(label, "fontSize") == 12 && Pixels(label, "minWidth") == 226 && LabelBox(label)[2] == 226, "Anything else: its natural size");
    settings.perfOverlayScale = 50;
    Tick();
    Check(ui.scale == 50 && Pixels(label, "fontSize") == 6 && LabelBox(label)[2] == 113, "Back at its size");
    // Unticked mid-resize: back at the size saved, nothing saved.
    Step();
    Mouse(116, 29, true);
    Mouse(229, 55, true);
    Check(drag.phase == Drag::Moving && ui.scale == 100, "Resizing again");
    settings.perfOverlayDrag = false;
    collapsedWidth = 226; // still laid out at 100 % until the next frame
    Tick();
    Tick();
    collapsedWidth = -1;
    Check(drag.phase == Drag::Idle && ui.scale == 50 && Pixels(label, "fontSize") == 6 && LabelBox(label)[2] == 113 && saves == saved + 2 &&
        settings.perfOverlayScale == 50 && ui.minWidth == CpuLineWidth && Pixels(label, "minWidth") == 113,
        "Unticked mid-resize: back at the size saved, nothing saved, never widened by the layout of the size before");
    Mouse(229, 55, false);
    settings.perfOverlayDrag = true;
    Tick();
    Step();
    // Restore mid-resize: nothing saved; recreated at the size saved.
    Mouse(116, 29, true);
    Mouse(229, 55, true);
    Check(drag.phase == Drag::Moving && ui.scale == 100, "Resizing once more");
    RestorePerfOverlay();
    Check(!Overlay() && drag.phase == Drag::Idle && saves == saved + 2, "Restore mid-resize: nothing saved");
    Mouse(229, 55, false);
    Tick();
    label = Overlay();
    Step();
    Check(label && ui.scale == 50 && Pixels(label, "fontSize") == 6, "Recreated at the size saved");
    // A label anchored by its right and bottom sides (the corner an earlier
    // revision chose), without a place: resized, its top-left kept all the
    // same, never past the margins; saved with the place of its top-left.
    settings.perfOverlayX = settings.perfOverlayY = -1;
    settings.perfOverlayScale = 100;
    settings.perfOverlayCorner = 3;
    Tick();
    label = Overlay();
    Pump(4);
    Step();
    Check(label && ui.corner == 3 && ui.scale == 100 && LabelBox(label) == Box{1686, 1020, 226, 52} && Pixels(label, "right") == 8 &&
        Pixels(label, "bottom") == 8, "Bottom right, at its natural size");
    Mouse(1907, 1067, true);
    Mouse(2500, 1500, true);
    Check(drag.phase == Drag::Moving && ui.scale == 100 && LabelBox(label) == Box{1686, 1020, 226, 52}, "No room to grow: kept entirely on screen");
    Mouse(1794, 1041, true);
    Check(ui.scale == 50 && LabelBox(label) == Box{1686, 1020, 113, 26} && Pixels(label, "right") == 121 && Pixels(label, "bottom") == 34 &&
        !Styled(label, "left") && !Styled(label, "top"), "Its right and bottom sides moved: its top-left kept");
    Mouse(1794, 1041, false);
    Check(saves == saved + 3 && settings.perfOverlayScale == 50 && settings.perfOverlayX == PlaceOfEdge(1686, 113, 1920) &&
        settings.perfOverlayY == PlaceOfEdge(1020, 26, 1080) && settings.perfOverlayCorner == 3, "Saved: its size and the place of its top-left, its corner kept");
    Pump(4);
    Check(std::fabs(LabelBox(label)[0] - 1686) < .5f && std::fabs(LabelBox(label)[1] - 1020) < .5f && LabelBox(label)[2] == 113, "Stays there");
    // Grown back from there: up to the panel's margins only.
    Mouse(1794, 1041, true);
    Mouse(3000, 3000, true);
    const auto grown = LabelBox(label);
    Check(ui.scale == 100 && std::fabs(grown[0] - 1686) < .5f && std::fabs(grown[1] - 1020) < .5f && grown[0] + grown[2] <= 1920 - 8 + .5f &&
        grown[1] + grown[3] <= 1080 - 8 + .5f, "Grown up to the panel's margins only: entirely on screen");
    Mouse(3000, 3000, false);
    Check(saves == saved + 4 && settings.perfOverlayScale == 100, "Saved at that size");
    Check(changes == 0 && syncs == saves, "Every size saved without Module::Change (its appearance refresh), the options page synchronised");
    settings.perfOverlayCorner = 0;
    settings.perfOverlayX = settings.perfOverlayY = -1;
    settings.perfOverlayDrag = false;
    RestorePerfOverlay();
}

// --------------------------------------------------------------- the grip
// 1.16.1: with the mouse placement on, six dots in its bottom-right corner
// show while the cursor is over the overlay (looked at every 50 ms, where a
// press would reach it: the panel picking it there, the game's window in the
// foreground) and during a drag, hidden otherwise and while locked; its
// corner (at least 18 pixels of the screen), its right edge and its bottom
// edge resize it, the whole overlay alike, its top-left kept; each resize
// logged when it ends (PERF_OVERLAY_SCALED, with the handle used).
void GripAndEdges() {
    using namespace perfOverlay;
    using namespace mouse;
    using Box = std::array<float, 4>;
    auto& settings = module->settings;
    settings.perfOverlay = settings.perfOverlayDrag = settings.perfOverlayShown = true;
    settings.perfOverlayGpu = settings.perfOverlayDisk = false;
    settings.perfOverlayCorner = 0;
    settings.perfOverlayX = settings.perfOverlayY = -1;
    settings.perfOverlayScale = 100;
    RestorePerfOverlay();
    Tick();
    Pump(4); // its figures, then its widest text
    const auto label = Overlay(), grip = GripOf();
    Mouse(600, 600, false); // the cursor away from it
    Check(label && grip && LabelBox(label) == Box{8, 8, 226, 52} && !Get(grip).visible && !ui.gripShown, "Away from it: no grip");
    // Over it: shown at its bottom-right corner, within 50 ms; logged once a label.
    const auto reads = cursorReads, picked = picks;
    const auto shownLogs = LoggedLine("PERF_OVERLAY_GRIP shown scale=100");
    Mouse(100, 30, false);
    Check(!Get(grip).visible && cursorReads == reads && picks == picked, "Not looked at again within 50 ms");
    nowMs += HoverMs;
    Mouse(100, 30, false);
    Check(Get(grip).visible && ui.gripShown && cursorReads == reads + 1 && picks == picked + 1 && Pixels(grip, "left") == 225 &&
        Pixels(grip, "top") == 51 && LoggedLine("PERF_OVERLAY_GRIP shown scale=100") == shownLogs + 1,
        "Over it: its grip at its bottom-right corner, logged");
    nowMs += HoverMs;
    Mouse(120, 40, false);
    Check(Get(grip).visible && Pixels(grip, "left") == 225 && LoggedLine("PERF_OVERLAY_GRIP shown scale=100") == shownLogs + 1,
        "Still over it: kept, logged once");
    // The cursor gone, a game element over it, another window in the foreground: hidden.
    nowMs += HoverMs;
    Mouse(600, 600, false);
    Check(!Get(grip).visible && !ui.gripShown, "The cursor gone: hidden");
    blockers.push_back({Element("VisualElement", "Popup"), 50, 0, 200, 100});
    nowMs += HoverMs;
    Mouse(100, 30, false);
    Check(!Get(grip).visible, "Under a game element: hidden");
    blockers.clear();
    nowMs += HoverMs;
    Mouse(100, 30, false);
    Check(Get(grip).visible, "Uncovered: back");
    foreground = false;
    nowMs += HoverMs;
    Mouse(100, 30, false);
    foreground = true;
    Check(!Get(grip).visible, "Another window in the foreground: hidden");
    // Locked: hidden at the next frame, the cursor never read.
    nowMs += HoverMs;
    Mouse(100, 30, false);
    Check(Get(grip).visible, "Shown again");
    settings.perfOverlayDrag = false;
    Step();
    Check(!Get(grip).visible, "Unticked: hidden at the next frame");
    Tick();
    const auto locked = cursorReads;
    for (int i = 0; i < 3; ++i) { nowMs += HoverMs; Mouse(100, 30, false); }
    Check(!Get(grip).visible && cursorReads == locked && Get(label).picking == 1, "Locked: never shown, the cursor never read");
    settings.perfOverlayDrag = true;
    Tick();
    Step();
    // Its corner pressed 17 px from it (18 at least, whatever its size): a
    // resize, its grip at its new corner and at its size.
    Mouse(217, 43, true);
    Check(drag.phase == Drag::Pressed && drag.resize && drag.handle == ResizeHandle::Corner && Get(grip).visible, "Pressed 17 px from its corner: a resize");
    Mouse(330, 69, true);
    Check(drag.phase == Drag::Moving && ui.scale == 150 && LabelBox(label) == Box{8, 8, 339, 78} && Get(grip).visible &&
        Pixels(grip, "left") == 333.5f && Pixels(grip, "top") == 72.5f && Pixels(grip, "paddingLeft") == 13.5f,
        "150 %: its grip at its new corner, at its size");
    std::set<std::pair<float, float>> dots;
    for (const auto dot : Get(grip).children) {
        Check(Pixels(dot, "paddingLeft") == 3 && Pixels(dot, "paddingTop") == 3, "Its dots at 150 %");
        dots.insert({Pixels(dot, "left"), Pixels(dot, "top")});
    }
    Check(dots == std::set<std::pair<float, float>>{{9.f, 0.f}, {4.5f, 4.5f}, {9.f, 4.5f}, {0.f, 9.f}, {4.5f, 9.f}, {9.f, 9.f}},
        "Their triangle at 150 %");
    const std::string at150 = " x=" + std::to_string(PlaceOfEdge(8, 339, 1920)) + " y=" + std::to_string(PlaceOfEdge(8, 78, 1080));
    const std::string at100 = " x=" + std::to_string(PlaceOfEdge(8, 226, 1920)) + " y=" + std::to_string(PlaceOfEdge(8, 52, 1080));
    const auto byCorner = LoggedLine("PERF_OVERLAY_SCALED scale=150" + at150 + " handle=corner");
    const auto byRight = LoggedLine("PERF_OVERLAY_SCALED scale=100" + at100 + " handle=right");
    const auto byBottom = LoggedLine("PERF_OVERLAY_SCALED scale=150" + at150 + " handle=bottom");
    Mouse(330, 69, false);
    Check(drag.phase == Drag::Idle && settings.perfOverlayScale == 150 &&
        LoggedLine("PERF_OVERLAY_SCALED scale=150" + at150 + " handle=corner") == byCorner + 1, "Released: saved, logged with the handle used");
    // Its right edge, anywhere along it: back to 100 %, the height alike, its top-left kept.
    Mouse(345, 30, true);
    Check(drag.phase == Drag::Pressed && drag.resize && drag.handle == ResizeHandle::Right && Get(grip).visible, "Pressed on its right edge");
    Mouse(232, 300, true);
    Check(drag.phase == Drag::Moving && ui.scale == 100 && LabelBox(label) == Box{8, 8, 226, 52} && Pixels(grip, "left") == 225 &&
        Pixels(grip, "top") == 51, "Its right edge dragged: 100 %, its grip at its corner");
    Mouse(232, 300, false);
    Check(settings.perfOverlayScale == 100 && LoggedLine("PERF_OVERLAY_SCALED scale=100" + at100 + " handle=right") == byRight + 1, "Saved, logged");
    // Its bottom edge: 26 px down, 150 % (a wide overlay's corner gives 103 % for that move).
    Mouse(100, 58, true);
    Check(drag.phase == Drag::Pressed && drag.resize && drag.handle == ResizeHandle::Bottom, "Pressed on its bottom edge");
    Mouse(700, 84, true);
    Check(drag.phase == Drag::Moving && ui.scale == 150 && LabelBox(label) == Box{8, 8, 339, 78}, "Its bottom edge dragged: 150 %");
    Mouse(700, 84, false);
    Check(settings.perfOverlayScale == 150 && LoggedLine("PERF_OVERLAY_SCALED scale=150" + at150 + " handle=bottom") == byBottom + 1,
        "Saved, logged");
    // Elsewhere: still a move, its grip along.
    Mouse(40, 30, true);
    Mouse(140, 130, true);
    Check(drag.phase == Drag::Moving && !drag.resize && LabelBox(label)[0] == 108 && LabelBox(label)[1] == 108 &&
        Pixels(grip, "left") == 108 + 339 - 13.5f && Pixels(grip, "top") == 108 + 78 - 13.5f, "Elsewhere: moved, its grip along");
    Mouse(140, 130, false);
    // A small window (a panel pixel half a pixel of the screen): its corner
    // still 18 pixels of the screen, 36 panel pixels (24 otherwise at 150 %).
    clientWidth = 960;
    clientHeight = 540;
    Mouse(206, 75, true); // the panel's (412, 150): 35 and 36 px from its corner
    Check(drag.phase == Drag::Pressed && drag.handle == ResizeHandle::Corner, "A small window: its corner 18 pixels of the screen");
    Mouse(206, 75, false);
    clientWidth = 1920;
    clientHeight = 1080;
    Check(drag.phase == Drag::Idle && ui.scale == 150 && LabelBox(label)[0] == 108, "A click: nothing");
    // Its grip taken away by the game alone: a new label and grip.
    Detach(grip);
    Tick();
    Check(Overlay() && Overlay() != label && GripOf() && GripOf() != grip && Grips(high).size() == 1 && Owned(high).size() == 1 &&
        !Get(label).fields["parent"], "Its grip gone: both recreated");
    // Restore: the grip goes with the label.
    const auto last = GripOf();
    RestorePerfOverlay();
    Check(!Overlay() && !GripOf() && !Get(last).fields["parent"] && Grips(high).empty() && !PerfOverlayCleanupPending(), "Restored: its grip gone too");
    settings.perfOverlayX = settings.perfOverlayY = -1;
    settings.perfOverlayScale = 100;
    settings.perfOverlayDrag = false;
}

// ------------------------------------------------------ the top-bar button
// 1.16.0: its button among the mod's in the top bar (registered at its
// installation), pressed while the overlay is shown: a part ticked, not
// hidden by it. A click hides it, its parts kept ticked and nothing measured
// any more, or shows it again, « Afficher les FPS et la mémoire » ticked
// when no part is; saved as a drop is, never with Module::Change. Its option
// removes the button.
void TopBarButton() {
    using namespace perfOverlay;
    const auto registration = toolsFake::registrations.back();
    auto& settings = module->settings;
    settings.perfOverlay = true;
    settings.perfOverlayGpu = settings.perfOverlayDisk = settings.perfOverlayDrag = false;
    Check(settings.perfOverlayShown && settings.perfOverlayButton, "Not hidden, its button on");
    Tick();
    Pump(2);
    Check(Overlay() && registration.visible() && registration.button(), "Shown: its button there and pressed");
    settings.perfOverlayButton = false;
    Check(!registration.button() && registration.visible() && Overlay(), "« Bouton dans la barre du haut » unticked: no button, the overlay still shown");
    settings.perfOverlayButton = true;
    // Hidden: its part still ticked, saved once, the options page in step.
    const auto saved = saves, synced = syncs, changed = changes;
    Check(registration.toggle() && !settings.perfOverlayShown && settings.perfOverlay && !registration.visible() && saves == saved + 1 &&
        syncs == synced + 1 && changes == changed && LoggedLine("PERF_OVERLAY_BUTTON hidden") == 1,
        "Clicked: hidden, « Afficher les FPS et la mémoire » still ticked, saved");
    Tick();
    Check(!Overlay() && Owned(high).empty() && !sampling && !timing && LoggedLine("PERF_OVERLAY_REMOVED hidden") == 1, "Removed at once, nothing measured");
    const auto measured = measureReads;
    Pump(3);
    Check(!Overlay() && measureReads == measured, "Hidden: nothing created, nothing read");
    // Shown again: the same part, nothing else ticked.
    Check(registration.toggle() && settings.perfOverlayShown && settings.perfOverlay && !settings.perfOverlayGpu && !settings.perfOverlayDisk &&
        registration.visible() && saves == saved + 2 && LoggedLine("PERF_OVERLAY_BUTTON shown") == 1, "Clicked again: shown, the same part");
    Tick();
    Check(Overlay() && Owned(high).size() == 1, "Back");
    // The GPU alone: hidden, its worker stops and the GPU stays ticked; shown
    // again, the GPU alone, never the FPS added.
    settings.perfOverlay = false;
    settings.perfOverlayGpu = true;
    Tick();
    Check(Overlay() && devices.worker && devices.worker->gpu, "The GPU alone, its worker running");
    registration.toggle();
    Tick();
    Check(!Overlay() && !devices.worker && !devices.thread && settings.perfOverlayGpu && !settings.perfOverlay && !settings.perfOverlayShown &&
        WaitFor([] { return pdhFake::Alive() == 0; }), "Hidden: its worker stopped, the GPU still ticked");
    registration.toggle();
    Tick();
    Check(Overlay() && devices.worker && settings.perfOverlayGpu && !settings.perfOverlay && settings.perfOverlayShown, "Shown again: the GPU alone");
    settings.perfOverlayGpu = false;
    Tick();
    Check(!Overlay() && !devices.worker && !registration.visible() && settings.perfOverlayShown, "No part ticked: not shown, its button up");
    // Its click without a part ticked: « Afficher les FPS et la mémoire »
    // ticked, so that it shows something.
    Check(registration.toggle() && settings.perfOverlay && settings.perfOverlayShown && registration.visible() &&
        LoggedLine("PERF_OVERLAY_BUTTON shown fps") == 1, "Clicked without a part: the FPS ticked");
    Tick();
    Check(Overlay() && timing, "Shown with the FPS");
    // Hidden, then its part unticked meanwhile: the click shows it with the FPS ticked.
    registration.toggle();
    settings.perfOverlay = false;
    Check(!settings.perfOverlayShown && !registration.visible(), "Hidden, then its part unticked");
    Check(registration.toggle() && settings.perfOverlayShown && settings.perfOverlay && registration.visible(), "Clicked: shown, the FPS ticked");
    Tick();
    Check(Overlay(), "Shown");
    // Its errors given up: hidden and shown again with the button, tried again.
    broken = true;
    Tick();
    Check(!Overlay() && Logged("PERF_OVERLAY_REMOVED errors") >= 1, "Given up");
    registration.toggle();
    Tick();
    Check(!broken && !Overlay(), "Hidden: its errors forgotten");
    registration.toggle();
    Tick();
    Check(Overlay() && !broken, "Shown again: tried again");
    // A save that fails: logged, hidden for this session all the same.
    saveFails = true;
    registration.toggle();
    saveFails = false;
    Tick();
    Check(!settings.perfOverlayShown && !Overlay() && Logged("PERF_OVERLAY_BUTTON_ERROR Injected save failure") == 1, "Save failure logged, hidden all the same");
    registration.toggle();
    Tick();
    Check(Overlay() && changes == changed && syncs - synced == saves - saved, "Every click saved without Module::Change, the options page in step");
    settings.perfOverlay = false;
    Tick();
    Check(!Overlay(), "Unticked: removed");
}
}

int main() {
    // The device worker runs below normal priority, as in the game. On a machine
    // busy with other programs (parallel builds), Windows can leave it about 4 s
    // without a processor: it then misses JoinMs (the restart that follows is
    // deferred until it ends) and the 5 s WaitFor bound. One priority class
    // above normal keeps it ahead of those programs.
    SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
    try {
        Setup();
        Module mod({});
        module = &mod;
        mod.ui = std::make_unique<OptionsUi>(); // its Sync only (a fake): the options page kept in step with a saved place
        mod.ready = true;
        mod.servicesReady = true;
        mod.compositor.reset(Make("Compositor"));
        // The lifecycle below first runs without the 1.14.0 parts, then with each of them.
        mod.settings.perfOverlayGpu = mod.settings.perfOverlayDisk = mod.settings.perfOverlayDrag = false;
        BuildInterface();
        FramesModel();
        CpuModel();
        Formatting();
        DeviceModel();
        CounterArrays();
        PlacementModel();
        Install();
        OptionOff();
        Waiting();
        Created();
        Corners();
        Rebuilt();
        StaleCpu();
        Errors();
        ShownErrors();
        Cleanup();
        Dragging();
        DragEdges();
        DeviceWorkers();
        IndependentParts();
        Resizing();
        GripAndEdges();
        TopBarButton();
        RestorePerfOverlay();
        Check(!perfOverlay::devices.worker && !perfOverlay::devices.thread && WaitFor(Retired), "No worker left after Restore");
        mod.compositor.reset();
        module = nullptr;
        Check(handles.empty(), "No managed reference kept after Restore");
        std::cout << "PASS: performance overlay: frame rate and frame times over the last second (a slow frame forgotten after "
            "a second, 1024-frame ring), CPU share over the last two refreshes, Mo/Go and French decimals, three-line text and "
            "a fourth for the GPU and the disk, every native call verified and guarded (57 methods, 2 GC exports and the 4 "
            "functions behind them, the layer host's offset), inert when off or disabled, waits for its layer (loading screens, "
            "pq.pbs throwing), first child of RootHigh, ignores clicks and focus without the mouse placement, rounded dark "
            "background, never narrower once laid out, four corners (top right under the game menu), refreshed every second "
            "pump and only when changed, no CPU share spanning a wait, recreated when the game rebuilds its layers or the "
            "session changes, removed at once when unchecked or restored, 3 errors within 5 minutes give up (errors at each "
            "first refresh included), cleanup retried twice a second for 10 s; mouse placement: a press on the overlay only "
            "(the panel's pick, the game's window in the foreground, the interface shown), a click moves nothing, follows the "
            "cursor inside the margins, client pixels scaled to the panel (black bars aside), saved once as ten-thousandths of "
            "the free space without Module::Change's appearance refresh, placed again for another resolution, right and bottom "
            "corners moved by their own sides, ended by the release or a lost foreground, the point held covered sending it "
            "back to the last place seen (grabbed again by any visible part), stopped without saving by Restore or the label "
            "going, never a press held from before, the clicks let through and the mouse unread when unchecked; GPU and disk: "
            "this process's busiest engine (never a sum), the game's physical disk (active time, transfer rate), the first "
            "sample without a figure, PDH arrays sized then read (fresh items only), a worker thread only while shown with one "
            "of them on, restarted with the counters asked for, stopped and "
            "joined (250 ms at most, never two at once), every PDH refusal leaving its figure out and logged once; "
            "independent parts (1.16.0): shown with any of the three boxes and only those parts (the GPU or the disk alone, "
            "a neutral text until their first figure, before their worker starts and while it has none yet, and « Mesure "
            "indisponible » when Windows refuses them, never sized by those texts), only what is shown measured (no frame "
            "counter, memory, heap or CPU read without the frame rate, the "
            "mouse placement alone showing nothing), recreated for another choice of parts, and the mouse place kept and the "
            "overlay locked there when the mouse placement is unchecked, across restarts; resize (1.16.0): its bottom-right "
            "corner (the only corner left square) scales the whole overlay alike "
            "(text, insets, corners, minimum width, the widest text kept at 100 %), its top-left kept even when anchored by "
            "its right and bottom sides, its corner following the cursor along its diagonal in whole percents within 50..300 "
            "and the panel's margins, a click changing nothing, never widened or placed meanwhile nor by the layout of the "
            "size before (the game lays it out after the frame), back at the last size "
            "seen when the point held is covered, saved once with the place of its top-left, back at the size saved when "
            "interrupted, recreated at that size, any other size read as 100 %, never while locked (corners all rounded); "
            "grip and edges (1.16.1): six dots of its text's colour in its bottom-right corner, elements of their own right "
            "above the label (never its children: its text measure kept), never picked, shown while the cursor is over it "
            "(looked at every 50 ms, the panel's pick only over it) and during a drag, hidden when it leaves, under a game "
            "element, with another window in the foreground and while locked, following the corner at the overlay's size; "
            "its corner (at least 18 pixels of the screen, 16 px at its size, never wider than half of it), its right edge and "
            "its bottom edge (at least 6 pixels of the screen, 5 px at its size, never thicker than a quarter of it) resize it, "
            "an edge across, the rest moving it; each resize logged as it ends (PERF_OVERLAY_SCALED, with its handle), the "
            "grip recreated with the label and removed with it; "
            "top-bar button (1.16.0): registered at installation (« Performances », FigmaIcons.activity, no layouts, its "
            "option removing it), pressed while a part is ticked and not hidden by it, a click hiding the overlay with its "
            "parts kept ticked and nothing measured, or showing it again (« Afficher les FPS et la mémoire » ticked when no "
            "part is), saved without Module::Change (a failed save logged, the choice kept for the session), its errors "
            "forgotten once hidden\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        for (const auto& line : fixture::logs) std::cerr << "  log: " << line << '\n';
        return 1;
    }
}
