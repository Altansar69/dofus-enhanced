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
// TextElement.MeasureTextSize (1.18.1): with the font each label was last laid
// out at (its inline font size as resolved by the game's layout pass, after
// the compositor's frame: Resolve, at each of the tests' frames); nothing
// before it, or when broken (measureBroken: NaN, as without a font).
std::map<Obj, float> resolvedFonts;
int measures = 0;
bool measureBroken = false;
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
// The game's cursor manager (DofusCursor, static): its UI layer's requests,
// one per target in the order made (an OrderedDictionary: a target asking
// again goes last), the last one shown.
struct CursorRequest { Obj target; int64_t type; };
std::vector<CursorRequest> cursorRequests;
int cursorSets = 0, cursorRemovals = 0;
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
    if (property == "position" || property == "whiteSpace" || property == "unityTextAlign") return 4;
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
// 16 px a line plus the padding (at 12 px): 52 for three lines, 68 for four;
// never lower than the label's minimum height.
float LaidOutHeight(Obj o) {
    if (!(collapsedWidth < 0)) return collapsedWidth;
    const float factor = FontFactor(o);
    float height = 16.f * factor * static_cast<float>(1 + std::count(Get(o).text.begin(), Get(o).text.end(), '\n')) + 4.f * factor;
    if (Get(o).styles.contains("minHeight")) {
        float minimum = 0; std::memcpy(&minimum, Get(o).styles.at("minHeight").data(), 4);
        height = std::max(height, minimum);
    }
    return height;
}
// The game's layout pass: every label's font as it is now drawn.
void Resolve() {
    for (const auto& object : objects)
        if (object->klass->name == "Label" && object->fields["parent"]) resolvedFonts[object.get()] = FontFactor(object.get());
}
// MeasureTextSize: 6 px a character of the longest line, 12 px for the first
// line and 16 more for each next one (the fake layout's, without the padding),
// at the font resolved; it must be the one drawn (measured before the layout
// pass after a new font: the module divides by the wrong one).
std::array<float, 2> Measured(Obj o, const std::string& text) {
    const auto resolved = resolvedFonts.find(o);
    if (measureBroken || resolved == resolvedFonts.end()) return {std::nanf(""), std::nanf("")};
    Check(resolved->second == FontFactor(o), "Measured with a font drawn since the last layout pass");
    size_t longest = 0, line = 0, lines = 1;
    for (const unsigned char c : text) {
        if (c == '\n') { line = 0; ++lines; continue; }
        if ((c & 0xC0) != 0x80) longest = std::max(longest, ++line);
    }
    const float factor = resolved->second;
    return {6.f * factor * static_cast<float>(longest), factor * (12.f + 16.f * static_cast<float>(lines - 1))};
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
    if (info.type == "DofusCursor") {
        // Static, as the game's: SetCursor(layer, type, state, target) and RemoveCursor(layer, target).
        Check(!self && (info.name == "SetCursor" || info.name == "RemoveCursor"), "Static DofusCursor.SetCursor or RemoveCursor");
        Check(*static_cast<int*>(args[0]) == 3, "The cursor manager's UI layer (CursorLayerEnum.UI)");
        const Obj target = static_cast<Obj>(args.back());
        Check(target && Get(target).klass->name == "Label", "The overlay's label as the request's target");
        Fail(target, info.name);
        const auto it = std::find_if(cursorRequests.begin(), cursorRequests.end(), [&](const CursorRequest& r) { return r.target == target; });
        if (info.name == "RemoveCursor") {
            ++cursorRemovals;
            if (it != cursorRequests.end()) cursorRequests.erase(it);
            return nullptr;
        }
        Check(*static_cast<int*>(args[2]) == 0, "CursorStateEnum.None");
        int64_t type = 0; std::memcpy(&type, args[1], sizeof(type));
        ++cursorSets;
        if (it != cursorRequests.end()) cursorRequests.erase(it);
        cursorRequests.push_back({target, type});
        return nullptr;
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
    if (m == "MeasureTextSize") {
        Check(o.klass->name == "Label" && Panel(self), "Only the overlay's label measures, in its panel");
        Check(*static_cast<float*>(args[1]) == 0 && *static_cast<int*>(args[2]) == 0 && *static_cast<float*>(args[3]) == 0 &&
            *static_cast<int*>(args[4]) == 0, "Measured without a width or height limit (MeasureMode.Undefined)");
        ++measures;
        return Box(Measured(self, Get(static_cast<Obj>(args[0])).text));
    }
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
// The cursor the game's cursor manager holds for the label (0: none), and the
// one it shows: the last request made.
int64_t CursorAsked(Obj label) {
    for (const auto& request : cursorRequests) if (request.target == label) return request.type;
    return 0;
}
int64_t CursorShown() { return cursorRequests.empty() ? 0 : cursorRequests.back().type; }
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
// The game's layout pass came between the last frame and this one (Resolve).
void Frame(int64_t ticks = FrameTicks) { counter += ticks; Resolve(); PerfOverlayFrame(nowMs); }
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
const std::string Memory = "Mémoire 1,52 Go · Privée 1,31 Go";
const std::string Heap = "Tas géré 312 Mo / 420 Mo";
const std::string Steady = "60 FPS · 16,7 ms (max 16,7 ms)\n" + Memory + "\n" + Heap + " · CPU 12 %";
// A new label's first text: its CPU share needs a second sample.
const std::string SteadyNoCpu = "60 FPS · 16,7 ms (max 16,7 ms)\n" + Memory + "\n" + Heap;
// The fourth line from the fake counters.
const std::string DevicesLine = "GPU 23 % · Disque C: 4 % (1,2 Mo/s)";
const std::string DiskLine = "Disque C: 4 % (1,2 Mo/s)";
// 1.18.1: its items on two lines (150 % wide, 100 % high).
const std::string TwoLines = "60 FPS · 16,7 ms (max 16,7 ms) · Mémoire 1,52 Go\nPrivée 1,31 Go · Tas géré 312 Mo / 420 Mo · CPU 12 %";
// A layout within a hundredth of a pixel (a text size not a whole factor).
bool Close(const std::array<float, 4>& box, const std::array<float, 4>& expected, float tolerance = .01f) {
    for (size_t i = 0; i < box.size(); ++i)
        if (!(std::fabs(box[i] - expected[i]) <= tolerance)) return false;
    return true;
}
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
    // 1.18.1: each figure an item of its own, its slot and its line's group.
    const auto items = ItemsOf(sample);
    Check(items == Items{{SlotFps, "60 FPS", 0}, {SlotTimes, "16,7 ms (max 33,4 ms)", 0}, {SlotMemory, "Mémoire 1,52 Go", 1},
        {SlotPrivate, "Privée 1,31 Go", 1}, {SlotHeap, "Tas géré 312 Mo / 420 Mo", 2}, {SlotCpu, "CPU 12 %", 2}}, "Its items");
    Check(GroupStarts(items) == std::vector<size_t>{0, 2, 4} && Join(items, {0}) == "60 FPS · 16,7 ms (max 33,4 ms) · Mémoire 1,52 Go · "
        "Privée 1,31 Go · Tas géré 312 Mo / 420 Mo · CPU 12 %" && Join(items, {0, 3, 5}) == "60 FPS · 16,7 ms (max 33,4 ms) · Mémoire 1,52 Go\n"
        "Privée 1,31 Go · Tas géré 312 Mo / 420 Mo\nCPU 12 %", "Its items on any lines, « · » between those of a line");
    Check(Shape("16,7 ms (max 33,4 ms)") == "00,0 ms (max 00,0 ms)" && Shape("Disque C: 4 % (1,2 Mo/s)") == "Disque C: 0 % (0,0 Mo/s)" &&
        Shape("Mémoire 1,52 Go") == "Mémoire 0,00 Go", "Their shapes: every digit a zero");
    // The fourth line: the graphics card and the game's disk, each left out when unknown.
    auto full = sample;
    full.gpu = 23; full.drive = L'C'; full.diskBusy = 4; full.diskRate = 1.2 * MiB;
    Check(Text(full) == three + "\n" + DevicesLine, "Four lines: " + Text(full));
    Check(ItemsOf(full).size() == 8 && ItemsOf(full)[6] == Item{SlotGpu, "GPU 23 %", 3} && ItemsOf(full)[7] == Item{SlotDisk, DiskLine, 3},
        "The disk one item: its active time, its transfer rate in brackets");
    full.gpu = -1;
    Check(Text(full) == three + "\n" + DiskLine, "Without the GPU");
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
    Check(Text(alone) == DevicesLine, "The GPU and the disk alone: " + Text(alone));
    alone.waiting = true;
    Check(Text(alone) == DevicesLine, "A part still waiting: the other's figures");
    alone.drive = 0;
    Check(Text(alone) == "GPU 23 %", "The GPU alone");
    alone.gpu = -1; alone.drive = L'D'; alone.diskBusy = -1; alone.diskRate = 0;
    Check(Text(alone) == "Disque D: 0 Ko/s", "The disk alone");
    alone.drive = 0;
    Check(Text(alone) == WaitingLine, "Nothing to show yet: the neutral text, never the FPS one");
    alone.waiting = false;
    Check(Text(alone) == UnavailableLine, "Nothing to give (Windows refusing the parts shown)");
    Check(Placeholder(ItemsOf(alone)) && ItemsOf(alone).front().slot == SlotNote && !Placeholder(ItemsOf(full)), "A neutral text: one item of its own");
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
// 1.18.1, the reflow: for each number of lines the narrowest width its items
// need on them (their order kept), the largest text size its box holds over
// them (fewer lines between equals), then its items filling lines as wide as
// its box gives them at that size (an item up on the line before as soon as
// it fits there); its natural size its groups' lines.
void FlowModel() {
    using namespace perfOverlay;
    // The steady items at 100 % (6 px a character): 60 FPS, the frame times,
    // the memory, the private bytes, the heap, the CPU share; « · » 18 px, a
    // line 12 px and 16 px more for each next one.
    const std::vector<float> widths{36, 126, 90, 84, 144, 48};
    const Metrics metrics{18, 12, 16};
    Check(LineWidth(widths, 0, 2, 18) == 180 && LineWidth(widths, 4, 6, 18) == 210 && LineWidth(widths, 3, 3, 18) == 0 &&
        FlowWidth(widths, {0, 2, 4}, 18) == 210 && FlowHeight(3, metrics) == 44 && FlowHeight(1, metrics) == 12 && FlowHeight(0, metrics) == 0,
        "Its lines' widths and heights");
    Check(Balanced(widths, 18, 1) == std::vector<size_t>{0} && Balanced(widths, 18, 2) == std::vector<size_t>{0, 3} &&
        Balanced(widths, 18, 3) == std::vector<size_t>{0, 2, 4} && Balanced(widths, 18, 4) == std::vector<size_t>{0, 2, 4, 5} &&
        Balanced(widths, 18, 5) == std::vector<size_t>{0, 2, 3, 4, 5} && Balanced(widths, 18, 6) == std::vector<size_t>{0, 1, 2, 3, 4, 5} &&
        Balanced(widths, 18, 9) == std::vector<size_t>{0, 1, 2, 3, 4, 5} && Balanced({}, 18, 3) == std::vector<size_t>{0},
        "Each number of lines: the widest line the narrowest, in their order");
    Check(Balanced({60, 60, 60}, 0, 2) == std::vector<size_t>{0, 2}, "Between equals, the earlier lines the fuller");
    Items items{{SlotFps, "60 FPS", 0}, {SlotTimes, "16,7 ms (max 16,7 ms)", 0}, {SlotMemory, "Mémoire 1,52 Go", 1},
        {SlotPrivate, "Privée 1,31 Go", 1}, {SlotHeap, "Tas géré 312 Mo / 420 Mo", 2}, {SlotCpu, "CPU 12 %", 2}};
    const Extent natural = NaturalOf(widths, items, metrics);
    Check(natural.width == 226 && natural.height == 52, "Its natural size: its groups' three lines with its insets");
    const auto fit = [&](float width, float height) { return Fit(widths, metrics, width, height); };
    Check(Greedy(widths, 18, 210) == std::vector<size_t>{0, 2, 4} && Greedy(widths, 18, 294) == std::vector<size_t>{0, 3, 5} &&
        Greedy(widths, 18, 1000) == std::vector<size_t>{0} && Greedy(widths, 18, 10) == std::vector<size_t>{0, 1, 2, 3, 4, 5} &&
        Greedy({}, 18, 100) == std::vector<size_t>{0}, "Filled: each item on the line before when it fits there");
    // Its natural box: its groups' three lines at 100 %.
    auto flow = fit(226, 52);
    Check(flow.starts == std::vector<size_t>{0, 2, 4} && flow.factor == 1 && Join(items, flow.starts) == Steady, "100 %: its three lines, its text at 100 %");
    // Wider: two lines (12 px more for the second one than one line), its
    // text larger as soon as they give it more room: 328 px at 100 %.
    flow = fit(339, 52);
    Check(flow.starts == std::vector<size_t>{0, 3} && Near(flow.factor, 339.0 / 328, 1e-5) && 328 * flow.factor <= 339 &&
        Join(items, flow.starts) == "60 FPS · 16,7 ms (max 16,7 ms) · Mémoire 1,52 Go\nPrivée 1,31 Go · Tas géré 312 Mo / 420 Mo · CPU 12 %",
        "150 % wide: the items flow onto two lines, its text a little larger");
    flow = fit(678, 52);
    Check(flow.starts == std::vector<size_t>{0, 4} && Near(flow.factor, 52.0 / 36, 1e-5),
        "300 % wide: two lines as high as its box allows, the first as full as its width allows");
    flow = fit(678, 26);
    Check(flow.starts == std::vector<size_t>{0} && Near(flow.factor, 678.0 / 634, 1e-5) && 634 * flow.factor <= 678,
        "300 % wide, 50 % high: all on one line");
    // Narrower or taller: more lines.
    flow = fit(113, 26);
    Check(flow.Lines() == 3 && flow.factor == .5f, "50 % both ways: its three lines at 50 %");
    flow = fit(226, 156);
    Check(flow.Lines() == 6 && Near(flow.factor, 226.0 / 160, 1e-5), "100 % wide, 300 % high: an item a line, its text larger");
    flow = fit(339, 156);
    Check(flow.starts == std::vector<size_t>{0, 2, 3, 4, 5} && Near(flow.factor, 339.0 / 196, 1e-5) && 196 * flow.factor <= 339 &&
        84 * flow.factor <= 156, "150 % wide, 300 % high: five lines");
    // Widened a little at a time, its text still at 100 % (its height the
    // limit): the items come up to the line above as soon as they fit there,
    // then two lines once they give it a larger text; back as it narrows.
    flow = fit(300, 52);
    Check(flow.starts == std::vector<size_t>{0, 2, 4} && flow.factor == 1, "Not room enough yet for the memory on the first line");
    flow = fit(310, 52);
    Check(flow.starts == std::vector<size_t>{0, 3, 5} && flow.factor == 1 && Join(items, flow.starts) ==
        "60 FPS · 16,7 ms (max 16,7 ms) · Mémoire 1,52 Go\nPrivée 1,31 Go · Tas géré 312 Mo / 420 Mo\nCPU 12 %",
        "Room enough: the memory up on the first line, the next items up with it");
    flow = fit(330, 52);
    Check(flow.starts == std::vector<size_t>{0, 3} && Near(flow.factor, 330.0 / 328, 1e-5), "Two lines, its text larger");
    Check(fit(330, 52).starts == flow.starts && fit(310, 52).starts == std::vector<size_t>{0, 3, 5} && fit(226, 52).starts ==
        std::vector<size_t>{0, 2, 4}, "A function of its box alone: the same layout every time, back as it narrows");
    // Never more than three times its natural size, never without a box.
    Check(Fit({10}, {0, 10, 10}, 1000, 1000).factor == 3 && Fit({}, metrics, 226, 52).factor == 0 && fit(0, 52).factor == 0 &&
        fit(226, std::nanf("")).factor == 0 && fit(-5, 52).Lines() == 0, "At most three times, nothing without a box");
}
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
    Check(step(true, true, false, true, 40, 20) == DragStep::None && state.phase == Drag::Idle, "A press not located (another window)");
    Check(step(false, true, true, true, 40, 20) == DragStep::None && state.phase == Drag::Idle, "The button held from before: no press");
    Check(step(true, true, true, true, 40, 20) == DragStep::None && state.phase == Drag::Pressed && state.grabX == 32 && state.grabY == 12,
        "Pressed on the overlay (beyond its top-left corner's handle): the grab offset");
    Check(step(false, true, true, true, 42, 22) == DragStep::None && state.phase == Drag::Pressed, "Under 4 px: still a click");
    Check(step(false, false, true, true, 42, 22) == DragStep::None && state.phase == Drag::Idle, "Released: a click, nothing saved");
    step(true, true, true, true, 40, 20);
    Check(step(false, true, true, true, 540, 320) == DragStep::Move && state.phase == Drag::Moving && state.left == 508 && state.top == 308,
        "Moving with the grab offset");
    Check(step(false, true, true, true, 5000, -100) == DragStep::Move && state.left == 1686 && state.top == 8, "Clamped inside the margins");
    Check(step(false, false, true, true, 20, 0) == DragStep::End && state.phase == Drag::Idle && state.left == 1686 && state.top == 8 &&
        state.width == 226 && state.height == 52 && state.spaceWidth == 1920 && state.spaceHeight == 1080, "Released anywhere: the last place");
    box = {8, 8, 226, 52};
    step(true, true, true, true, 40, 20);
    Check(step(false, true, false, false, 320, 300) == DragStep::None && state.phase == Drag::Idle, "Pressed, then the window left: nothing");
    step(true, true, true, true, 40, 20);
    Check(step(false, true, true, false, 320, 300) == DragStep::None && state.phase == Drag::Idle && box.x == 8,
        "Pressed, then the point held covered: nothing moved");
    step(true, true, true, true, 40, 20);
    step(false, true, true, true, 140, 20); // drawn at 108
    step(false, true, true, true, 240, 20); // seen at 108, drawn at 208
    Check(step(false, true, true, false, 250, 20) == DragStep::Back && state.phase == Drag::Idle && state.left == 108 && state.top == 8 &&
        state.width == 226 && state.spaceWidth == 1920, "The point held covered while moving: back at the last place seen");
    step(true, true, true, true, 140, 20);
    step(false, true, true, true, 240, 20); // seen at 108, drawn at 208
    Check(step(false, false, true, false, 240, 20) == DragStep::Back && state.left == 108, "Covered at the release: back too, never left under it");
    step(true, true, true, true, 140, 20);
    step(false, true, true, true, 240, 20);
    Check(step(false, true, false, false, 350, 20) == DragStep::End && state.left == 208 && state.phase == Drag::Idle,
        "The window left while moving: ended where it was");
    // The interface hidden while moving (no layout): ended, the sizes of the last move kept.
    step(true, true, true, true, 240, 20);
    step(false, true, true, true, 340, 20);
    const float none = std::nanf("");
    Check(Advance(state, false, true, false, false, 0, 0, Rect{none, none, none, none}, none, none) == DragStep::End && state.left == 308 &&
        state.width == 226 && state.height == 52 && state.spaceWidth == 1920 && state.spaceHeight == 1080, "Hidden while moving: the last move's sizes");
    // 1.16.0, the size: within 50..300 %, as the settings keep it; 1.17.1, its
    // width and its height each (Scales), its text at the smaller one.
    Check(Settings::ValidPerfOverlayScale(MinScale) && !Settings::ValidPerfOverlayScale(MinScale - 1) && Settings::ValidPerfOverlayScale(MaxScale) &&
        !Settings::ValidPerfOverlayScale(MaxScale + 1) && Settings{}.perfOverlayScaleX == BaseScale && Settings{}.perfOverlayScaleY == BaseScale &&
        BaseScale == 100, "The sizes the settings keep");
    Check(Scales{}.x == 100 && Scales{}.y == 100 && Scales{150, 100}.Text() == 100 && Scales{80, 200}.Text() == 80 && Scales{300, 300}.Text() == 300 &&
        Scales{150, 100} == Scales{150, 100} && !(Scales{150, 100} == Scales{100, 150}), "Its text, insets and corners at the smaller of its width and its height");
    Check(Scaled(226, 100, 150) == 339 && Scaled(339, 150, 50) == 113 && Scaled(52, 100, 200) == 104, "Sizes scaled from one scale to another");
    Check(BoxAt(226, 150) == 339 && BoxAt(52, 120) == 63 && BoxAt(208.5f, 100) == 209 && BoxAt(226, 50) == 113 && BoxAt(0, 200) == 0,
        "A side of its box: its natural length at its scale, rounded up so that its text fits; none while unknown");
    // Its handles (1.18.1: as a window's, all four edges and corners): each
    // corner the last 16 px of both sides at its text's size but never under
    // 18 pixels of the screen, then its edges, 5 px thick at its size but
    // never under 6 pixels of the screen; a corner never wider nor higher
    // than a third of it, an edge never thicker than a quarter of it (its
    // middle moves it).
    using enum ResizeHandle;
    const auto zone = [](const Rect& box, float x, float y, float scale, float pixels = 1) { return HandleAt(box, x, y, scale, pixels); };
    const Rect overlay{8, 8, 226, 52};
    Check(zone(overlay, 233, 59, 100) == BottomRight && zone(overlay, 9, 9, 100) == TopLeft && zone(overlay, 233, 9, 100) == TopRight &&
        zone(overlay, 9, 59, 100) == BottomLeft, "100 %: its four corners");
    Check(zone(overlay, 216, 43, 100) == BottomRight && zone(overlay, 215.5f, 43, 100) == Body && zone(overlay, 216, 42, 100) == Body &&
        zone(overlay, 26, 25, 100) == TopLeft && zone(overlay, 26.5f, 25, 100) == Body && zone(overlay, 20, 20, 100) == TopLeft,
        "100 %: each corner 18 px wide (the screen's least) and a third of it high (17,3 px)");
    Check(zone(overlay, 228, 30, 100) == Right && zone(overlay, 227.5f, 30, 100) == Body && zone(overlay, 14, 30, 100) == Left &&
        zone(overlay, 14.5f, 30, 100) == Body && zone(overlay, 100, 14, 100) == Top && zone(overlay, 100, 14.5f, 100) == Body &&
        zone(overlay, 100, 54, 100) == Bottom && zone(overlay, 100, 53.5f, 100) == Body && zone(overlay, 40, 20, 100) == Body,
        "100 %: its four edges 6 px thick, the rest moves it");
    const Rect doubled{8, 8, 452, 104}, half{8, 8, 113, 26}, tripled{8, 8, 678, 156};
    Check(zone(doubled, 428, 80, 200) == BottomRight && zone(doubled, 427.5f, 80, 200) == Body && zone(doubled, 40, 40, 200) == TopLeft &&
        zone(doubled, 450.5f, 60, 200) == Right && zone(doubled, 449.5f, 60, 200) == Body && zone(doubled, 100, 102.5f, 200) == Bottom &&
        zone(doubled, 100, 101.5f, 200) == Body && zone(doubled, 18, 60, 200) == Left && zone(doubled, 100, 18, 200) == Top,
        "200 %: its corners 32 px, its edges 10 px: its handles grow with it");
    Check(zone(tripled, 638, 116, 300) == BottomRight && zone(tripled, 637, 116, 300) == Body && zone(tripled, 671, 60, 300) == Right &&
        zone(tripled, 100, 149, 300) == Bottom && zone(tripled, 23, 60, 300) == Left && zone(tripled, 100, 23, 300) == Top,
        "300 %: its corners 48 px, its edges 15 px");
    Check(zone(half, 103, 26, 50) == BottomRight && zone(half, 102.5f, 26, 50) == Body && zone(half, 115, 20, 50) == Right &&
        zone(half, 50, 28, 50) == Bottom && zone(half, 50, 27.5f, 50) == Body && zone(half, 14, 20, 50) == Left,
        "50 %: still 18 px and 6 px of the screen, never smaller with the overlay");
    // A small window (a panel pixel half a pixel of the screen) or a 4K one
    // (two): 18 pixels of the screen are 36, then 9 panel pixels.
    Check(zone(overlay, 198, 44, 100, .5f) == BottomRight && zone(overlay, 197.5f, 44, 100, .5f) == Body &&
        zone(overlay, 222, 30, 100, .5f) == Right && zone(overlay, 100, 48, 100, .5f) == Bottom && zone(overlay, 20, 30, 100, .5f) == Left,
        "A small window: its corners 36 panel pixels wide, its edges 12 (still 18 and 6 pixels of the screen)");
    Check(zone(overlay, 218, 44, 100, 2) == BottomRight && zone(overlay, 217.5f, 44, 100, 2) == Body &&
        zone(overlay, 229, 30, 100, 2) == Right && zone(overlay, 228.5f, 30, 100, 2) == Body,
        "Twice the screen's pixels: its own size wins (16 and 5 panel pixels)");
    Check(zone(overlay, 218, 44, 100, std::nanf("")) == BottomRight && zone(overlay, 216, 43, 100, 0) == BottomRight,
        "No screen scale known: one pixel of the screen a panel pixel");
    // A one-line overlay at 50 % (the GPU alone): its corners a third of its
    // width and of its height, its top and bottom edges a quarter of it.
    const Rect line{8, 8, 32, 10};
    Check(zone(line, 30, 17, 50) == BottomRight && zone(line, 30, 9, 50) == TopRight && zone(line, 10, 9, 50) == TopLeft &&
        zone(line, 24, 13, 50) == Body && zone(line, 35, 13, 50) == Right && zone(line, 24, 16.5f, 50) == Bottom && zone(line, 24, 9.5f, 50) == Top,
        "A small overlay: its corners a third of it, its middle moves it");
    const Handles sizes = HandleSizes(overlay, 100, 1);
    Check(sizes.cornerWidth == 18 && Near(sizes.cornerHeight, 52.0 / 3, 1e-4) && sizes.edgeX == 6 && sizes.edgeY == 6 &&
        Near(PanelScale(3840, 2160, 1920, 1080), 2) && Near(PanelScale(1920, 1200, 1920, 1080), 1) && Near(PanelScale(960, 540, 1920, 1080), .5),
        "The handles' sizes; the screen's pixels in a panel pixel");
    Check(SideX(Left) == -1 && SideX(TopLeft) == -1 && SideX(BottomLeft) == -1 && SideX(Right) == 1 && SideX(TopRight) == 1 &&
        SideX(BottomRight) == 1 && SideX(Top) == 0 && SideX(Bottom) == 0 && SideX(Body) == 0 && SideY(Top) == -1 && SideY(TopLeft) == -1 &&
        SideY(TopRight) == -1 && SideY(Bottom) == 1 && SideY(BottomLeft) == 1 && SideY(BottomRight) == 1 && SideY(Left) == 0 &&
        SideY(Right) == 0 && SideY(Body) == 0, "The side each handle moves on each axis");
    Check(std::string(HandleName(BottomRight)) == "bottom-right" && std::string(HandleName(TopLeft)) == "top-left" &&
        std::string(HandleName(TopRight)) == "top-right" && std::string(HandleName(BottomLeft)) == "bottom-left" &&
        std::string(HandleName(Right)) == "right" && std::string(HandleName(Bottom)) == "bottom" && std::string(HandleName(Left)) == "left" &&
        std::string(HandleName(Top)) == "top", "The handles' names in the log");
    // The game's cursors over it (CursorType): the double arrow of the handle
    // under the cursor, the move cursor elsewhere.
    Check(CursorOf(Right) == 0x20000 && CursorOf(Left) == 0x20000 && CursorOf(Bottom) == 0x80000 && CursorOf(Top) == 0x80000 &&
        CursorOf(BottomRight) == 0x100000 && CursorOf(TopLeft) == 0x100000 && CursorOf(TopRight) == 0x40000 && CursorOf(BottomLeft) == 0x40000 &&
        CursorOf(Body) == 0x10000, "Its handles' cursors: ResizeEW, ResizeNS, ResizeNWSE, ResizeNESW, and Move elsewhere");
    // Its handles at its text's size: 452 px wide at 200 % but 100 % high,
    // its corner is that of 100 % (18 px), not of 200 % (32).
    const Rect wide{8, 8, 452, 52};
    const auto text = static_cast<float>(Scales{200, 100}.Text());
    Check(zone(wide, 443, 43, text) == BottomRight && zone(wide, 440, 43, text) == Body &&
        zone(wide, 440, 43, 200) == BottomRight, "Its handles at its text's size");
    // 1.17.1, as a window's: each side its own way from its length at the
    // press, its top-left staying put; its corner both freely (no longer along
    // its diagonal), its right edge its width only, its bottom edge its height
    // only; whole percents within 50..300, never past the panel's margins.
    Drag sizing;
    sizing.resize = true;
    sizing.handle = ResizeHandle::BottomRight;
    sizing.left = sizing.top = 8;
    sizing.startWidth = 226;
    sizing.startHeight = 52;
    sizing.grabX = sizing.grabY = 5;
    const auto sized = [&](float x, float y, float spaceWidth = 1920) { return ResizedScales(sizing, x, y, spaceWidth, 1080); };
    Check(sized(229, 55) == Scales{100, 100}, "Where it was pressed: the same sizes");
    Check(sized(342, 81) == Scales{150, 150} && sized(455, 107) == Scales{200, 200}, "Its corner under the cursor: 150 %, then 200 %");
    Check(sized(342, 55) == Scales{150, 100} && sized(229, 81) == Scales{100, 150} && sized(455, 81) == Scales{200, 150} &&
        sized(342, 107) == Scales{150, 200}, "Its corner freely: its width and its height each under the cursor");
    Check(sized(-300, -300) == Scales{50, 50} && sized(5000, 5000) == Scales{300, 300} && sized(-300, 5000) == Scales{50, 300},
        "Each within 50..300 %");
    sizing.left = 1500;
    sizing.top = 900;
    Check(sized(5000, 5000) == Scales{182, 300} && 1500 + BoxAt(226, 182) <= 1920 - 8 && 900 + BoxAt(52, 300) <= 1080 - 8,
        "Never past the panel's margins: entirely on screen, each side within its own room");
    Check(sized(5000, 5000, 1580) == Scales{50, 300}, "No room even for the smallest width: the smallest width, its height free");
    sizing.left = 8;
    sizing.top = 1000;
    Check(sized(5000, 5000) == Scales{300, 138} && 1000 + BoxAt(52, 138) <= 1080 - 8, "Low on the screen: its height is the limit, not its width");
    sizing.left = sizing.top = 8;
    sizing.startWidth = 339;
    sizing.startScales = sizing.scales = Scales{150, 100};
    Check(sized(342, 55) == Scales{150, 100} && sized(229, 81) == Scales{100, 150}, "From other sizes: the same sizes");
    sizing.startWidth = 0;
    Check(sized(800, 81) == Scales{150, 150}, "No width known: its width as drawn, its height under the cursor");
    // Its right edge follows the cursor across, its bottom edge down,
    // wherever the cursor goes along them; the other side kept as drawn.
    Drag edge;
    edge.resize = true;
    edge.handle = ResizeHandle::Right;
    edge.left = edge.top = 8;
    edge.startWidth = 226;
    edge.startHeight = 52;
    edge.grabX = 2;
    edge.grabY = 30;
    const auto edged = [&](float x, float y) { return ResizedScales(edge, x, y, 1920, 1080); };
    Check(edged(232, 30) == Scales{100, 100} && edged(345, 30) == Scales{150, 100} && edged(345, 700) == Scales{150, 100} &&
        edged(458, -50) == Scales{200, 100} && edged(100, 30) == Scales{50, 100}, "Its right edge: its width under the cursor, its height kept");
    edge.handle = ResizeHandle::Bottom;
    edge.grabX = 130;
    edge.grabY = 2;
    Check(edged(100, 58) == Scales{100, 100} && edged(100, 84) == Scales{100, 150} && edged(1500, 84) == Scales{100, 150} &&
        edged(100, 5000) == Scales{100, 300}, "Its bottom edge: its height under the cursor, its width kept");
    edge.startScales = edge.scales = Scales{180, 100};
    Check(edged(100, 84) == Scales{180, 150}, "The width drawn kept while its bottom edge moves");
    edge.top = 1000;
    Check(edged(100, 5000) == Scales{180, 138}, "An edge never past the margins either");
    // 1.18.1, its left and top sides too: the edge held follows the cursor,
    // the opposite one stays put (its right or bottom side), each side within
    // 50..300 % and its own room to the panel's margin.
    Drag sides;
    sides.resize = true;
    sides.handle = ResizeHandle::TopLeft;
    sides.left = 500;
    sides.top = 300;
    sides.right = 726;
    sides.bottom = 352;
    sides.startWidth = 226;
    sides.startHeight = 52;
    sides.grabX = sides.grabY = 5; // from its top-left: its left and top sides held
    const auto sided = [&](float x, float y) { return ResizedScales(sides, x, y, 1920, 1080); };
    Check(sided(505, 305) == Scales{100, 100} && sided(392, 279) == Scales{150, 150} && ResizedLeft(sides, 339) == 387 &&
        ResizedTop(sides, 78) == 274, "Its top-left corner: 150 % both ways, its bottom-right corner kept");
    Check(sided(392, 305) == Scales{150, 100} && sided(505, 279) == Scales{100, 150} && sided(5000, 5000) == Scales{50, 50} &&
        sided(-5000, -5000) == Scales{300, 300} && ResizedLeft(sides, BoxAt(226, 300)) == 48, "Each side freely, within 50..300 %");
    sides.right = 300;
    sides.bottom = 100;
    Check(sided(-5000, -5000) == Scales{129, 176} && ResizedLeft(sides, BoxAt(226, 129)) == 8 && ResizedTop(sides, BoxAt(52, 176)) == 8,
        "Never past the panel's left and top margins");
    sides.right = 726;
    sides.bottom = 352;
    sides.handle = ResizeHandle::Left;
    sides.grabY = 22; // from its bottom side, not moved
    Check(sided(392, 900) == Scales{150, 100} && ResizedTop(sides, 52) == 300 && ResizedLeft(sides, 339) == 387,
        "Its left edge: its width only, its right side and its top kept");
    sides.handle = ResizeHandle::Top;
    sides.grabX = 100;
    sides.grabY = 5;
    Check(sided(1500, 279) == Scales{100, 150} && ResizedLeft(sides, 226) == 500 && ResizedTop(sides, 78) == 274,
        "Its top edge: its height only, its bottom side and its left kept");
    sides.handle = ResizeHandle::TopRight;
    sides.grabX = 5; // from its right side
    Check(sided(834, 279) == Scales{150, 150} && ResizedLeft(sides, 339) == 500 && ResizedTop(sides, 78) == 274,
        "Its top-right corner: its left and bottom sides kept");
    sides.handle = ResizeHandle::BottomLeft;
    sides.grabX = 5;
    sides.grabY = 5; // from its left and bottom sides
    Check(sided(392, 373) == Scales{150, 150} && ResizedLeft(sides, 339) == 387 && ResizedTop(sides, 78) == 300,
        "Its bottom-left corner: its right and top sides kept");
    Check(HeldPoint(sides, {387, 300, 339, 78}).x == 392 && HeldPoint(sides, {387, 300, 339, 78}).y == 373,
        "The point held: from its left side and its bottom side");
    // Pressed on its left edge: a resize by it, from its left side.
    Drag leftEdge;
    const Rect placed{500, 300, 226, 52};
    Check(Advance(leftEdge, true, true, true, true, 503, 330, placed, 1920, 1080) == DragStep::None && leftEdge.handle == ResizeHandle::Left &&
        leftEdge.grabX == 3 && leftEdge.grabY == 22 && leftEdge.right == 726 && leftEdge.bottom == 352, "Pressed on its left edge");
    Check(Advance(leftEdge, false, true, true, true, 390, 340, placed, 1920, 1080) == DragStep::Scale && leftEdge.scales == Scales{150, 100} &&
        ResizedLeft(leftEdge, 339) == 387 && leftEdge.left == 500, "Its left edge dragged: 150 % wide, its right side kept");
    Check(Advance(leftEdge, false, false, true, true, 390, 340, placed, 1920, 1080) == DragStep::End, "Released");
    Check(Advance(leftEdge, true, true, true, true, 502, 302, placed, 1920, 1080) == DragStep::None && leftEdge.handle == ResizeHandle::TopLeft &&
        leftEdge.grabX == 2 && leftEdge.grabY == 2, "Pressed on its top-left corner");
    Advance(leftEdge, false, false, true, true, 502, 302, placed, 1920, 1080);
    // Its handles at its text's size when the reflow draws it larger.
    Check(Advance(leftEdge, true, true, true, true, 712, 330, placed, 1920, 1080, Scales{100, 100}, 1, 300) == DragStep::None &&
        leftEdge.handle == ResizeHandle::Right, "Its handles at its text's size (300 %: 15 px edges)");
    Advance(leftEdge, false, false, true, true, 712, 330, placed, 1920, 1080);
    // A resize's steps: a press in the grip, a click changes nothing, the
    // scales follow (drawn at once here), released: saved; the point held
    // covered: back at the last scales where the panel picked it.
    box = {8, 8, 226, 52};
    Drag resize;
    const auto resizeStep = [&](bool press, bool down, bool located, bool reached, float x, float y) {
        const auto result = Advance(resize, press, down, located, reached, x, y, box, 1920, 1080, resize.scales);
        if (result == DragStep::Scale || result == DragStep::Back) { box.width = BoxAt(226, resize.scales.x); box.height = BoxAt(52, resize.scales.y); }
        return result;
    };
    Check(resizeStep(true, true, true, true, 229, 55) == DragStep::None && resize.phase == Drag::Pressed && resize.resize &&
        resize.grabX == 5 && resize.grabY == 5 && resize.left == 8 && resize.top == 8 && resize.startWidth == 226 && resize.startHeight == 52 &&
        resize.startScales == Scales{100, 100}, "Pressed in the grip: a resize, its offset from the corner");
    Check(resizeStep(false, true, true, true, 231, 57) == DragStep::None && resizeStep(false, false, true, true, 231, 57) == DragStep::None &&
        resize.phase == Drag::Idle && box.width == 226, "A click: nothing");
    Check(resizeStep(true, true, true, true, 40, 20) == DragStep::None && !resize.resize, "Pressed elsewhere: a move");
    resizeStep(false, false, true, true, 40, 20);
    resizeStep(true, true, true, true, 229, 55);
    Check(resizeStep(false, true, true, true, 342, 81) == DragStep::Scale && resize.phase == Drag::Moving && resize.scales == Scales{150, 150} &&
        resize.seenScales == Scales{100, 100} && resize.left == 8 && resize.top == 8, "Resized: 150 % both ways, its top-left kept");
    Check(resizeStep(false, true, true, true, 455, 81) == DragStep::Scale && resize.scales == Scales{200, 150} && resize.seenScales == Scales{150, 150} &&
        box.width == 452 && box.height == 78, "Wider only: 200 % by 150 %");
    Check(resizeStep(false, true, true, false, 455, 81) == DragStep::Back && resize.phase == Drag::Idle && resize.scales == Scales{150, 150} &&
        box.width == 339 && box.height == 78, "The point held covered: back at the last scales where the panel picked it");
    resizeStep(true, true, true, true, 342, 81);
    resizeStep(false, true, true, true, 455, 107);
    Check(resizeStep(false, false, true, true, 455, 107) == DragStep::End && resize.phase == Drag::Idle && resize.scales == Scales{200, 200} &&
        resize.spaceWidth == 1920 && resize.spaceHeight == 1080, "Released: ended at the last scales");
    resizeStep(true, true, true, true, 455, 107);
    resizeStep(false, true, true, true, 229, 55);
    Check(resizeStep(false, true, false, false, 0, 0) == DragStep::End && resize.scales == Scales{100, 100}, "The window left while resized: ended there");
    const Point corner = HeldPoint(resize, {8, 8, 339, 78}), held = HeldPoint(state, {100, 100, 226, 52});
    Check(corner.x == 342 && corner.y == 81 && held.x == 100 + std::clamp(state.grabX, 0.f, 225.f), "The point held: from the corner when resized");
    // Pressed on its right or bottom edge: a resize by that edge, the point
    // held at the same offsets from its bottom-right corner, the other side kept.
    box = {8, 8, 226, 52};
    Drag edges;
    Check(Advance(edges, true, true, true, true, 232, 30, box, 1920, 1080) == DragStep::None && edges.resize && edges.handle == ResizeHandle::Right &&
        edges.grabX == 2 && edges.grabY == 30, "Pressed on its right edge: a resize by it");
    Check(Advance(edges, false, true, true, true, 345, 400, box, 1920, 1080) == DragStep::Scale && edges.scales == Scales{150, 100} && edges.left == 8 &&
        edges.top == 8, "Its right edge dragged: 150 % wide, its height and its top-left kept");
    Check(Advance(edges, false, false, true, true, 345, 400, box, 1920, 1080) == DragStep::End && edges.scales == Scales{150, 100}, "Released");
    Check(Advance(edges, true, true, true, true, 100, 58, box, 1920, 1080) == DragStep::None && edges.handle == ResizeHandle::Bottom && edges.grabY == 2,
        "Pressed on its bottom edge: a resize by it");
    Check(Advance(edges, false, true, true, true, 900, 84, box, 1920, 1080) == DragStep::Scale && edges.scales == Scales{100, 150},
        "Its bottom edge dragged: 150 % high, its width kept");
    Advance(edges, false, false, true, true, 900, 84, box, 1920, 1080);
    Check(Advance(edges, true, true, true, true, 100, 30, box, 1920, 1080) == DragStep::None && !edges.resize && edges.handle == ResizeHandle::Body,
        "Pressed elsewhere: a move");
    Advance(edges, false, false, true, true, 100, 30, box, 1920, 1080);
    // A small window: the corner pressed 30 panel pixels from it (15 pixels of the screen).
    Check(Advance(edges, true, true, true, true, 204, 44, box, 1920, 1080, Scales{}, .5f) == DragStep::None && edges.handle == ResizeHandle::BottomRight,
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
        {"VisualElement", ".ctor", 0, 0x55F50B0}, {"VisualElement", "set_visible", 1, 0x55FA170},
        {"DofusCursor", "SetCursor", 4, 0x280A070}, {"DofusCursor", "RemoveCursor", 2, 0x2809EA0},
        {"TextElement", "MeasureTextSize", 5, 0x5645AF0}};
    const std::vector<std::tuple<std::string, uintptr_t, uintptr_t>> styles{
        {"position", 0x55D1A30, 0x55D48B0}, {"left", 0x55D1610, 0x55D3EB0}, {"top", 0x55D1CD0, 0x55D4F70}, {"right", 0x55D1AC0, 0x55D4960},
        {"bottom", 0x55D1190, 0x55D3530}, {"paddingLeft", 0x55D1970, 0x55D46D0}, {"paddingRight", 0x55D19B0, 0x55D4770},
        {"paddingTop", 0x55D19F0, 0x55D4810}, {"paddingBottom", 0x55D1930, 0x55D4630}, {"borderTopLeftRadius", 0x55D1100, 0x55D33E0},
        {"borderTopRightRadius", 0x55D1140, 0x55D3440}, {"borderBottomLeftRadius", 0x55D0F90, 0x55D3050},
        {"borderBottomRightRadius", 0x55D0FD0, 0x55D30B0}, {"backgroundColor", 0x55D0D90, 0x55D2BA0}, {"color", 0x55D11D0, 0x55D35D0},
        {"fontSize", 0x55D1500, 0x55D3D00}, {"whiteSpace", 0x55D26B0, 0x55D6370}, {"minWidth", 0x55D1850, 0x55D4410},
        {"minHeight", 0x55D1810, 0x55D4370}, {"unityTextAlign", 0x55D23B0, 0x55D5ED0}};
    for (const auto& [property, get, set] : styles) {
        expected.insert({"InlineStyleAccess", "UnityEngine.UIElements.IStyle.get_" + property, 0, get});
        expected.insert({"InlineStyleAccess", "UnityEngine.UIElements.IStyle.set_" + property, 1, set});
    }
    Check(contracts == expected, "Exactly the pinned contracts: " + std::to_string(contracts.size()) + " of " + std::to_string(expected.size()));
    for (const uintptr_t rva : {0x5B9230, 0x5B9210, 0x5FE340, 0x5FDB20, 0x6194D0, 0x6194B0, 0x57A4050, 0x55F50B0, 0x55FA170, 0x280A070, 0x2809EA0, 0x5645AF0})
        Check(Guarded(rva), "The GC exports, the code behind them, the panel's pick, the grip's elements, the game's cursor manager and the text measure are guarded");
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
    Check(Pixels(label, "fontSize") == 12 && EnumStyle(label, "whiteSpace") == 2 && EnumStyle(label, "unityTextAlign") == 0,
        "Small text, lines broken at the newlines only (WhiteSpace.Pre), from its top-left (TextAnchor.UpperLeft)");
    Check(o.text == Measuring + "\n" + Memory + "\n" + Heap && o.texts == 1, "First text: no frame, no CPU share yet: " + o.text);
    Check(!Styled(label, "minWidth") && !Styled(label, "minHeight") && ui.naturalWidth == 0 && ui.naturalHeight == 0,
        "No natural size, no minimum size before a layout");
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
    // About twice a second: not at the next pump, at the second one. Its
    // items measured at its first layout (1.18.1: with its own font, each
    // shape once, the separator and the lines), its natural size their
    // groups' lines, its box at its sizes (100 %), its items on the three
    // lines its box gives the largest text.
    const auto measuredBefore = measures;
    Pump();
    Check(o.texts == 1, "No refresh 200 ms later");
    Check(ui.learned && ui.metrics.separator == 18 && ui.metrics.first == 12 && ui.metrics.pitch == 16 && measures == measuredBefore + 7 &&
        ui.lines == 3 && ui.font == 1 && ui.widths[SlotMeasuring] == 132 && ui.widths[SlotHeap] == 144, "Its items measured at its first layout, once");
    Check(ui.naturalWidth == MemoryLineWidth && ui.naturalHeight == 52 && Pixels(label, "minWidth") == MemoryLineWidth &&
        Pixels(label, "minHeight") == 52, "Its natural size at its first layout, its box at 100 %");
    Pump();
    Check(o.text == Steady && o.texts == 2, "Refreshed 400 ms later: " + o.text);
    Check(Pixels(label, "minWidth") == CpuLineWidth && ui.naturalWidth == CpuLineWidth && measures == measuredBefore + 10,
        "Widened by its new figures at once, only their new shapes measured");
    // Same figures: the text is left alone.
    Pump(4);
    Check(o.text == Steady && o.texts == 2 && measures == measuredBefore + 10, "Unchanged figures: no new text, nothing measured");
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
    Pump();
    Check(LabelBox(label)[3] == 52 && Pixels(label, "minHeight") == 52, "A line missing for a moment: its height kept, its free edge still");
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
        Check(Get(label).text == Steady && Pixels(label, "minWidth") == CpuLineWidth, "Then the CPU share, widening it at once: " + Get(label).text);
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
    Mouse(40, 20, true);
    Mouse(620, 400, true);
    Mouse(620, 400, false);
    Check(Pixels(label, "left") == 8 && Pixels(label, "top") == 8 && saves == 0 && picks == picked + 1,
        "Held from elsewhere onto the overlay: still nothing");
    // The game's window in the background: no press of its own.
    foreground = false;
    Mouse(40, 20, true);
    Mouse(320, 300, true);
    Mouse(320, 300, false);
    foreground = true;
    Check(Pixels(label, "left") == 8 && saves == 0 && picks == picked + 1, "Another window in the foreground: no drag");
    // A game element drawn above the overlay (a menu, a popup): the press is its own.
    blockers.push_back({Element("VisualElement", "GameMenu"), 0, 0, 100, 100});
    Mouse(40, 20, true);
    Mouse(320, 300, true);
    Mouse(320, 300, false);
    blockers.clear();
    Check(Pixels(label, "left") == 8 && saves == 0 && picks == picked + 2, "Under a game element: no drag");
    // The interface hidden by the game: no layout, no drag.
    collapsedWidth = 0;
    Mouse(40, 20, true);
    Mouse(320, 300, true);
    Mouse(320, 300, false);
    collapsedWidth = -1;
    Check(Pixels(label, "left") == 8 && saves == 0 && picks == picked + 2, "Interface hidden: no drag");
    // A click: pressed and released within 4 px.
    Mouse(40, 20, true);
    Mouse(42, 21, true);
    Mouse(42, 21, false);
    Check(Pixels(label, "left") == 8 && Pixels(label, "top") == 8 && saves == 0 && drag.phase == Drag::Idle, "A click moves nothing");
    // The drag: the overlay follows the cursor, keeping the grab offset (12, 12).
    Mouse(40, 20, true);
    Mouse(1020, 500, true);
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
    Mouse(1520, 900, true);
    // Released: the place saved once, as ten-thousandths of the free space.
    Mouse(1520, 900, false);
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
    Mouse(3056, 1796, true); // the panel's (1528, 898): on the overlay, beyond its top-left corner's handle
    Mouse(1060, 1000, true); // (530, 500): 998 and 398 panel pixels away
    Check(std::fabs(Pixels(label, "left") - (at[0] - 998)) < .01f && std::fabs(Pixels(label, "top") - (at[1] - 398)) < .01f,
        "Client pixels scaled to the panel's");
    Mouse(1060, 1000, false);
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
    Mouse(static_cast<long>(corner[0] + 40), static_cast<long>(corner[1] + 10), true);
    Mouse(740, 410, true);
    const float cornerWidth = LabelBox(label)[2], cornerHeight = LabelBox(label)[3];
    Check(Pixels(label, "right") == 1920 - 700 - cornerWidth && Pixels(label, "bottom") == 1080 - 400 - cornerHeight &&
        !Styled(label, "left") && !Styled(label, "top") && LabelBox(label)[0] == 700 && LabelBox(label)[1] == 400, "Its right and bottom sides moved");
    Mouse(740, 410, false);
    Check(saves == 3 && module->settings.perfOverlayX == PlaceOfEdge(700, cornerWidth, 1920) && module->settings.perfOverlayCorner == 3,
        "Saved, its corner kept");
    // The window losing the foreground mid-drag: the drag ends there, saved.
    Mouse(740, 410, true);
    Mouse(340, 310, true);
    foreground = false;
    Mouse(835, 805, true);
    Check(drag.phase == Drag::Idle && saves == 4 && LabelBox(label)[0] == 300 && LabelBox(label)[1] == 300,
        "Foreground lost: ended where it was, saved");
    foreground = true;
    Mouse(935, 605, true);
    Mouse(935, 605, false);
    Check(LabelBox(label)[0] == 300 && saves == 4, "The button still down when it comes back: no drag");
    // Something of the game drawn over the point held mid-drag (the loading
    // screen): ended, back at the last place where the panel picked it, saved.
    Mouse(340, 310, true);
    Mouse(440, 310, true);
    Mouse(540, 310, true);
    blockers.push_back({Element("VisualElement", "LoadingScreen"), 0, 0, 1920, 1080});
    Mouse(940, 310, true);
    Check(drag.phase == Drag::Idle && saves == 5 && LabelBox(label)[0] == 400 && LabelBox(label)[1] == 300,
        "Covered mid-drag: back at the last place seen, saved");
    Mouse(940, 310, false);
    Mouse(440, 310, true);
    Mouse(840, 310, true);
    Mouse(840, 310, false);
    blockers.clear();
    Check(LabelBox(label)[0] == 400 && saves == 5, "Under the loading screen: no drag");
    // Restore mid-drag: stopped, nothing saved.
    Mouse(440, 310, true);
    Mouse(640, 310, true);
    Check(drag.phase == Drag::Moving, "Dragging");
    RestorePerfOverlay();
    Check(drag.phase == Drag::Idle && saves == 5 && !Overlay(), "Restore: the drag stopped, nothing saved");
    Mouse(740, 310, true);
    Mouse(740, 310, false);
    Tick();
    label = Overlay();
    Step();
    Check(saves == 5 && std::fabs(LabelBox(label)[0] - EdgeOfPlace(module->settings.perfOverlayX, LabelBox(label)[2], 1920)) < .01f,
        "Recreated at the saved place");
    // The label taken away by the game mid-drag (its panel gone): stopped, nothing saved.
    const auto start = LabelBox(label);
    Mouse(static_cast<long>(start[0] + 40), static_cast<long>(start[1] + 10), true);
    Mouse(static_cast<long>(start[0] + 140), static_cast<long>(start[1] + 10), true);
    Check(drag.phase == Drag::Moving, "Dragging again");
    Get(document).fields["panel"] = nullptr;
    Tick();
    Check(!Overlay() && drag.phase == Drag::Idle, "Removed with its layer: the drag stopped");
    Mouse(static_cast<long>(start[0] + 240), static_cast<long>(start[1] + 10), true);
    Mouse(static_cast<long>(start[0] + 240), static_cast<long>(start[1] + 10), false);
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
    Mouse(static_cast<long>(held[0] + 40), static_cast<long>(held[1] + 10), true);
    Mouse(static_cast<long>(held[0] + 335), static_cast<long>(held[1] + 10), true);
    Mouse(static_cast<long>(held[0] + 335), static_cast<long>(held[1] + 10), false);
    Check(LabelBox(label)[0] == held[0] && saves == 5, "Held before the label: no drag");
    // A save that fails: logged, the place kept for this session.
    saveFails = true;
    Mouse(static_cast<long>(held[0] + 40), static_cast<long>(held[1] + 10), true);
    Mouse(static_cast<long>(held[0] + 90), static_cast<long>(held[1] + 10), true);
    Mouse(static_cast<long>(held[0] + 90), static_cast<long>(held[1] + 10), false);
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
    Mouse(static_cast<long>(held[0] + 90), static_cast<long>(held[1] + 10), true);
    Mouse(static_cast<long>(held[0] + 290), static_cast<long>(held[1] + 10), true);
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
    Mouse(40, 50, true); // the point held: (32, 42) in the overlay, between its bottom-left corner and its bottom edge
    Mouse(900, 53, true);
    Mouse(1800, 50, true);
    Mouse(1800, 50, false);
    const auto under = LabelBox(label);
    Check(under[0] == 1920 - 8 - width && under[1] == 8 && saves == saved + 1 &&
        Within({buttons.x, buttons.y, buttons.width, buttons.height}, under[0] + under[2] / 2, under[1] + under[3] / 2),
        "Dropped at the top right, its centre under the menu buttons");
    // Grabbed again by its visible part, above its bottom edge (6 px: a
    // resize) and beyond its bottom-left corner (18 px): it moves.
    Mouse(static_cast<long>(under[0]) + 34, 50, true);
    Mouse(1020, 500, true);
    Check(drag.phase == Drag::Moving && !drag.resize && LabelBox(label)[0] == 986 && LabelBox(label)[1] == 458, "Grabbed by its visible part");
    Mouse(1020, 500, false);
    // The point held taken under them: back at the last place where it was seen.
    Mouse(1020, 470, true); // the point held: (34, 12)
    Mouse(1220, 100, true);
    Mouse(1420, 20, true);  // drawn at (1386, 8): the point held, (1420, 20), under the buttons
    Mouse(1420, 20, true);
    Check(drag.phase == Drag::Idle && LabelBox(label)[0] == 1186 && LabelBox(label)[1] == 88 && saves == saved + 3 &&
        module->settings.perfOverlayX == PlaceOfEdge(1186, width, 1920) && module->settings.perfOverlayY == PlaceOfEdge(88, height, 1080),
        "The point held covered: back at the last place seen, saved there");
    Mouse(1420, 20, false);
    Check(LabelBox(label)[0] == 1186 && saves == saved + 3, "Released afterwards: nothing more");
    blockers.clear();
    // A borderless full screen at another aspect ratio than the display's:
    // the panel scaled alike on both axes and centred between black bars.
    clientWidth = 1920;
    clientHeight = 1200; // 60 px above and below the 1920 x 1080 panel
    Mouse(1226, 163, true); // the panel's (1226, 103): on the overlay
    Mouse(1326, 263, true);
    Check(drag.phase == Drag::Moving && LabelBox(label)[0] == 1286 && LabelBox(label)[1] == 188, "Letterboxed: the bars left out");
    Mouse(1326, 263, false);
    clientWidth = 2560;
    clientHeight = 1080; // 320 px left and right
    Mouse(1646, 203, true); // the panel's (1326, 203): on the overlay
    Mouse(1046, 403, true);
    Check(drag.phase == Drag::Moving && LabelBox(label)[0] == 686 && LabelBox(label)[1] == 388, "Pillarboxed: the bars left out");
    Mouse(1046, 403, false);
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
    Mouse(726, 403, true);
    Mouse(826, 503, true);
    Mouse(826, 503, false);
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
    Check(Get(Overlay()).text == Steady + "\n" + DiskLine, "Disk only: " + Get(Overlay()).text);
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
    Check(Get(Overlay()).text == Steady + "\n" + DiskLine && LoggedLine("PERF_OVERLAY_GPU_UNAVAILABLE counter=C0000BB8") == 1 &&
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
    Check(Get(label).text == "GPU 23 %" && !ui.provisional && ui.naturalWidth == 64 && ui.naturalHeight == 20 && Pixels(label, "minWidth") == 64 &&
        Pixels(label, "minHeight") == 20, "The GPU alone, sized again from its figure at once, the neutral text's size forgotten: " + Get(label).text);
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
    Check(Get(label).text == DiskLine, "The disk alone: " + Get(label).text);
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
    Mouse(static_cast<long>(box[0] + 40), static_cast<long>(box[1] + 10), true);
    Mouse(static_cast<long>(box[0] + 440), static_cast<long>(box[1] + 310), true);
    Mouse(static_cast<long>(box[0] + 440), static_cast<long>(box[1] + 310), false);
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
    Check(Get(label).text == DiskLine, "Then its figures: " + Get(label).text);
    module->settings.perfOverlayDisk = false;
    Tick();
    Check(!Overlay() && !devices.worker && !devices.thread && WaitFor(Retired), "Unticked again: removed, its worker stopped");
    module->settings.perfOverlayX = module->settings.perfOverlayY = -1;
}

// --------------------------------------------------------------- the resize
// 1.16.0: with the mouse placement on, resized from its bottom-right corner
// (its grip, the only corner left square); 1.17.1: as a window, its width and
// its height each (perfOverlayScaleX/Y), its text, insets and corners at the
// smaller one (the largest size its box holds, from its top-left), its box at
// each (its minimum width and height from its natural size), its top-left
// kept, entirely on screen, each within 50..300 %; saved once at the end with
// the place of its top-left for that box; a click changes nothing; never
// while locked.
void Resizing() {
    using namespace perfOverlay;
    using namespace mouse;
    using Box = std::array<float, 4>;
    auto& settings = module->settings;
    settings.perfOverlay = settings.perfOverlayDrag = settings.perfOverlayShown = true;
    settings.perfOverlayGpu = settings.perfOverlayDisk = false;
    settings.perfOverlayCorner = 0;
    settings.perfOverlayX = settings.perfOverlayY = -1;
    Check(settings.perfOverlayScaleX == 100 && settings.perfOverlayScaleY == 100, "Its natural size until resized");
    RestorePerfOverlay();
    Tick();
    Pump(4); // its figures, then its widest text
    auto label = Overlay();
    Step(); // the button seen up
    Check(label && ui.scales == Scales{100, 100} && LabelBox(label) == Box{8, 8, 226, 52} && Pixels(label, "fontSize") == 12 &&
        Pixels(label, "minWidth") == CpuLineWidth && Pixels(label, "minHeight") == 52 && ui.naturalWidth == CpuLineWidth && ui.naturalHeight == 52 &&
        Logged("PERF_OVERLAY_READY layer=RootHigh corner=0 heap=1 processors=8 drag=1 place=-1,-1 fps=1 gpu=0 disk=0 scaleX=100 scaleY=100") >= 1,
        "At its natural size, top left");
    for (const char* radius : {"borderTopLeftRadius", "borderTopRightRadius", "borderBottomLeftRadius"})
        Check(Pixels(label, radius) == 6, std::string("Rounded: ") + radius);
    Check(Pixels(label, "borderBottomRightRadius") == 0, "Its bottom-right corner square, where its grip shows");
    // A click on the grip: nothing.
    const auto saved = saves;
    Mouse(229, 55, true);
    Mouse(231, 57, true);
    Mouse(231, 57, false);
    Check(drag.phase == Drag::Idle && ui.scales == Scales{100, 100} && LabelBox(label) == Box{8, 8, 226, 52} && saves == saved,
        "A click on the grip changes nothing");
    // Its corner dragged across: wider only (1.18.1), its items flow onto two
    // lines, its text a little larger (the widest of them 328 px at 100 %).
    Mouse(229, 55, true);
    Mouse(342, 55, true);
    const float wider = 339.f / 328;
    Check(drag.phase == Drag::Moving && drag.resize && ui.scales == Scales{150, 100} && Near(Pixels(label, "fontSize"), 12 * wider, 1e-3) &&
        Near(Pixels(label, "paddingLeft"), 8 * wider, 1e-3) && Pixels(label, "minWidth") == 339 && Pixels(label, "minHeight") == 52 &&
        Get(label).text == TwoLines && ui.lines == 2 && Close(LabelBox(label), Box{8, 8, 339, 52}) && saves == saved,
        "Its corner across: 150 % wide, 100 % high, its items on two lines; nothing saved yet: " + Get(label).text);
    // Then down: both at 150 %, its text, insets, corners and box alike.
    Mouse(342, 81, true);
    Check(ui.scales == Scales{150, 150} && Pixels(label, "fontSize") == 18 && Pixels(label, "paddingLeft") == 12 && Pixels(label, "paddingRight") == 12 &&
        Pixels(label, "paddingTop") == 6 && Pixels(label, "paddingBottom") == 6 && Pixels(label, "borderTopLeftRadius") == 9 &&
        Pixels(label, "borderBottomLeftRadius") == 9 && Pixels(label, "borderBottomRightRadius") == 0 && Pixels(label, "minWidth") == 339 &&
        Pixels(label, "minHeight") == 78, "150 % both ways: its text, insets, corners and box alike");
    Check(LabelBox(label) == Box{8, 8, 339, 78} && Pixels(label, "left") == 8 && Pixels(label, "top") == 8 && saves == saved && Get(label).text == Steady,
        "Its top-left kept, its corner under the cursor (at the grab offset), its three lines back");
    // A layout of another size meanwhile (the game lays it out after the
    // frame): the pumps neither grow nor place it during a resize.
    collapsedWidth = 1000;
    Tick();
    Tick();
    collapsedWidth = -1;
    Check(ui.naturalWidth == CpuLineWidth && ui.naturalHeight == 52 && Pixels(label, "minWidth") == 339 && Pixels(label, "minHeight") == 78 &&
        Pixels(label, "left") == 8 && Pixels(label, "top") == 8 && drag.phase == Drag::Moving, "Never grown nor placed during a resize");
    // Much taller than wide: its items on more lines, the largest text its
    // box holds (five lines, the widest 196 px at 100 %), from its top-left.
    Mouse(342, 159, true);
    Check(ui.scales == Scales{150, 300} && Near(Pixels(label, "fontSize"), 12 * 339.f / 196, 1e-3) && Pixels(label, "minHeight") == 156 &&
        ui.lines == 5 && Close(LabelBox(label), Box{8, 8, 339, 156}), "150 % wide, 300 % high: five lines, its text larger: " + Get(label).text);
    Mouse(455, 107, true);
    Check(ui.scales == Scales{200, 200} && Pixels(label, "fontSize") == 24 && LabelBox(label) == Box{8, 8, 452, 104}, "200 %");
    Mouse(455, 107, false);
    Check(drag.phase == Drag::Idle && saves == saved + 1 && settings.perfOverlayScaleX == 200 && settings.perfOverlayScaleY == 200 &&
        settings.perfOverlayX == PlaceOfEdge(8, 452, 1920) && settings.perfOverlayY == PlaceOfEdge(8, 104, 1080) &&
        LoggedLine("PERF_OVERLAY_SCALED scaleX=200 scaleY=200 x=54 y=82 handle=bottom-right") == 1,
        "Saved once at the release: its sizes, and the place of its top-left for that box");
    Pump(4);
    Check(LabelBox(label) == Box{8, 8, 452, 104} && Pixels(label, "minWidth") == 452 && Pixels(label, "minHeight") == 104 && saves == saved + 1,
        "Stays at those sizes and that place");
    // Down to the smallest size, then back up: the point held covered on the
    // way, back at the last sizes where the panel picked it, saved there.
    Mouse(455, 107, true);
    Mouse(-300, -300, true);
    Check(ui.scales == Scales{50, 50} && Pixels(label, "fontSize") == 6 && LabelBox(label) == Box{8, 8, 113, 26} && Pixels(label, "minWidth") == 113 &&
        Pixels(label, "minHeight") == 26, "Never below 50 %");
    Mouse(229, 55, true);
    Check(ui.scales == Scales{100, 100} && LabelBox(label) == Box{8, 8, 226, 52}, "Back up to 100 %");
    blockers.push_back({Element("VisualElement", "Popup"), 200, 40, 200, 100});
    Mouse(229, 55, true);
    Check(drag.phase == Drag::Idle && ui.scales == Scales{50, 50} && LabelBox(label) == Box{8, 8, 113, 26} && saves == saved + 2 &&
        settings.perfOverlayScaleX == 50 && settings.perfOverlayScaleY == 50 && settings.perfOverlayX == 44 && settings.perfOverlayY == 76 &&
        LoggedLine("PERF_OVERLAY_SCALED scaleX=50 scaleY=50 x=44 y=76 handle=bottom-right") == 1,
        "The point held covered: back at the last sizes where it was picked, saved");
    // The pumps of that frame come before the game lays it out at those sizes
    // (its panels' update follows the compositor's): its layout still that of
    // 100 % neither grows it nor places it, placed at the next frame.
    collapsedWidth = 226;
    Tick();
    Tick();
    collapsedWidth = -1;
    Check(ui.rescaled && ui.pending && ui.naturalWidth == CpuLineWidth && ui.naturalHeight == 52 && Pixels(label, "minWidth") == 113,
        "Laid out at the sizes before: neither grown nor placed");
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
    Check(ui.scales == Scales{50, 50} && LabelBox(label) == Box{8, 8, 113, 26} && buttonReads == buttons && cursorReads == cursors && saves == saved + 2,
        "Locked: never resized, the mouse not read");
    settings.perfOverlayDrag = true;
    Tick();
    Step();
    Check(Get(label).picking == 0 && Pixels(label, "borderBottomRightRadius") == 0, "Unlocked: its grip's mark back");
    // The next start: at its sizes and its place.
    RestorePerfOverlay();
    Tick();
    label = Overlay();
    Step();
    Check(label && ui.scales == Scales{50, 50} && Pixels(label, "fontSize") == 6 && Pixels(label, "paddingLeft") == 4 && Pixels(label, "paddingTop") == 2 &&
        Pixels(label, "borderBottomRightRadius") == 0 && !ui.pending && std::fabs(LabelBox(label)[0] - 8) < .5f && std::fabs(LabelBox(label)[1] - 8) < .5f &&
        LoggedLine("PERF_OVERLAY_READY layer=RootHigh corner=0 heap=1 processors=8 drag=1 place=44,76 fps=1 gpu=0 disk=0 scaleX=50 scaleY=50") == 1,
        "Recreated at its sizes and its place");
    Pump(4);
    Check(LabelBox(label)[2] == 113 && Pixels(label, "minWidth") == 113 && ui.naturalWidth == CpuLineWidth, "Its widest text kept at 100 %, drawn at 50 %");
    // Sizes outside 50..300 % (never saved): the natural size on that axis;
    // the sizes saved again: back to them.
    settings.perfOverlayScaleX = 999;
    settings.perfOverlayScaleY = 20;
    Tick();
    Check(ui.scales == Scales{100, 100} && Pixels(label, "fontSize") == 12 && Pixels(label, "minWidth") == 226 && LabelBox(label)[2] == 226,
        "Anything else: its natural size");
    settings.perfOverlayScaleY = 200;
    Tick();
    Check(ui.scales == Scales{100, 200} && Near(Pixels(label, "fontSize"), 12 * 226.f / 196, 1e-3) && ui.lines == 5 &&
        Pixels(label, "minHeight") == 104 && LabelBox(label)[3] == 104, "A width out of 50..300 alone: 100 % wide, its height as asked (five lines)");
    settings.perfOverlayScaleX = settings.perfOverlayScaleY = 50;
    Tick();
    Check(ui.scales == Scales{50, 50} && Pixels(label, "fontSize") == 6 && LabelBox(label)[2] == 113 && LabelBox(label)[3] == 26, "Back at its sizes");
    // Unticked mid-resize: back at the sizes saved, nothing saved.
    Step();
    Mouse(116, 29, true);
    Mouse(229, 55, true);
    Check(drag.phase == Drag::Moving && ui.scales == Scales{100, 100}, "Resizing again");
    settings.perfOverlayDrag = false;
    collapsedWidth = 226; // still laid out at 100 % until the next frame
    Tick();
    Tick();
    collapsedWidth = -1;
    Check(drag.phase == Drag::Idle && ui.scales == Scales{50, 50} && Pixels(label, "fontSize") == 6 && LabelBox(label)[2] == 113 && saves == saved + 2 &&
        settings.perfOverlayScaleX == 50 && settings.perfOverlayScaleY == 50 && ui.naturalWidth == CpuLineWidth && ui.naturalHeight == 52 &&
        Pixels(label, "minWidth") == 113, "Unticked mid-resize: back at the sizes saved, nothing saved, never grown by the layout of the sizes before");
    Mouse(229, 55, false);
    settings.perfOverlayDrag = true;
    Tick();
    Step();
    // Restore mid-resize: nothing saved; recreated at the sizes saved.
    Mouse(116, 29, true);
    Mouse(229, 55, true);
    Check(drag.phase == Drag::Moving && ui.scales == Scales{100, 100}, "Resizing once more");
    RestorePerfOverlay();
    Check(!Overlay() && drag.phase == Drag::Idle && saves == saved + 2, "Restore mid-resize: nothing saved");
    Mouse(229, 55, false);
    Tick();
    label = Overlay();
    Step();
    Check(label && ui.scales == Scales{50, 50} && Pixels(label, "fontSize") == 6, "Recreated at the sizes saved");
    // A label anchored by its right and bottom sides (the corner an earlier
    // revision chose), without a place: resized, its top-left kept all the
    // same, never past the margins; saved with the place of its top-left.
    settings.perfOverlayX = settings.perfOverlayY = -1;
    settings.perfOverlayScaleX = settings.perfOverlayScaleY = 100;
    settings.perfOverlayCorner = 3;
    Tick();
    label = Overlay();
    Pump(4);
    Step();
    Check(label && ui.corner == 3 && ui.scales == Scales{100, 100} && LabelBox(label) == Box{1686, 1020, 226, 52} && Pixels(label, "right") == 8 &&
        Pixels(label, "bottom") == 8, "Bottom right, at its natural size");
    Mouse(1907, 1067, true);
    Mouse(2500, 1500, true);
    Check(drag.phase == Drag::Moving && ui.scales == Scales{100, 100} && LabelBox(label) == Box{1686, 1020, 226, 52}, "No room to grow: kept entirely on screen");
    Mouse(1794, 1067, true);
    Check(ui.scales == Scales{50, 100} && LabelBox(label) == Box{1686, 1020, 113, 52} && Pixels(label, "right") == 121 && Pixels(label, "bottom") == 8,
        "Narrower only: its right side moved, its bottom kept");
    Mouse(1794, 1041, true);
    Check(ui.scales == Scales{50, 50} && LabelBox(label) == Box{1686, 1020, 113, 26} && Pixels(label, "right") == 121 && Pixels(label, "bottom") == 34 &&
        !Styled(label, "left") && !Styled(label, "top"), "Its right and bottom sides moved: its top-left kept");
    Mouse(1794, 1041, false);
    Check(saves == saved + 3 && settings.perfOverlayScaleX == 50 && settings.perfOverlayScaleY == 50 &&
        settings.perfOverlayX == PlaceOfEdge(1686, 113, 1920) && settings.perfOverlayY == PlaceOfEdge(1020, 26, 1080) && settings.perfOverlayCorner == 3,
        "Saved: its sizes and the place of its top-left, its corner kept");
    Pump(4);
    Check(std::fabs(LabelBox(label)[0] - 1686) < .5f && std::fabs(LabelBox(label)[1] - 1020) < .5f && LabelBox(label)[2] == 113, "Stays there");
    // Grown back from there: up to the panel's margins only.
    Mouse(1794, 1041, true);
    Mouse(3000, 3000, true);
    const auto grown = LabelBox(label);
    Check(ui.scales == Scales{100, 100} && std::fabs(grown[0] - 1686) < .5f && std::fabs(grown[1] - 1020) < .5f && grown[0] + grown[2] <= 1920 - 8 + .5f &&
        grown[1] + grown[3] <= 1080 - 8 + .5f, "Grown up to the panel's margins only: entirely on screen");
    Mouse(3000, 3000, false);
    Check(saves == saved + 4 && settings.perfOverlayScaleX == 100 && settings.perfOverlayScaleY == 100, "Saved at that size");
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
// corner (at least 18 pixels of the screen) resizes it, and since 1.17.1 its
// right edge its width only and its bottom edge its height only, its top-left
// kept; each resize logged when it ends (PERF_OVERLAY_SCALED, with its width,
// its height and the handle used).
void GripAndEdges() {
    using namespace perfOverlay;
    using namespace mouse;
    using Box = std::array<float, 4>;
    auto& settings = module->settings;
    settings.perfOverlay = settings.perfOverlayDrag = settings.perfOverlayShown = true;
    settings.perfOverlayGpu = settings.perfOverlayDisk = false;
    settings.perfOverlayCorner = 0;
    settings.perfOverlayX = settings.perfOverlayY = -1;
    settings.perfOverlayScaleX = settings.perfOverlayScaleY = 100;
    RestorePerfOverlay();
    Tick();
    Pump(4); // its figures, then its widest text
    const auto label = Overlay(), grip = GripOf();
    Mouse(600, 600, false); // the cursor away from it
    Check(label && grip && LabelBox(label) == Box{8, 8, 226, 52} && !Get(grip).visible && !ui.gripShown, "Away from it: no grip");
    // Over it: shown at its bottom-right corner, within 50 ms; logged once a label.
    const auto reads = cursorReads, picked = picks;
    const std::string shown = "PERF_OVERLAY_GRIP shown scaleX=100 scaleY=100";
    const auto shownLogs = LoggedLine(shown);
    Mouse(100, 30, false);
    Check(!Get(grip).visible && cursorReads == reads && picks == picked, "Not looked at again within 50 ms");
    nowMs += HoverMs;
    Mouse(100, 30, false);
    Check(Get(grip).visible && ui.gripShown && cursorReads == reads + 1 && picks == picked + 1 && Pixels(grip, "left") == 225 &&
        Pixels(grip, "top") == 51 && LoggedLine(shown) == shownLogs + 1, "Over it: its grip at its bottom-right corner, logged");
    nowMs += HoverMs;
    Mouse(120, 40, false);
    Check(Get(grip).visible && Pixels(grip, "left") == 225 && LoggedLine(shown) == shownLogs + 1, "Still over it: kept, logged once");
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
    Check(drag.phase == Drag::Pressed && drag.resize && drag.handle == ResizeHandle::BottomRight && Get(grip).visible, "Pressed 17 px from its corner: a resize");
    Mouse(330, 69, true);
    Check(drag.phase == Drag::Moving && ui.scales == Scales{150, 150} && LabelBox(label) == Box{8, 8, 339, 78} && Get(grip).visible &&
        Pixels(grip, "left") == 333.5f && Pixels(grip, "top") == 72.5f && Pixels(grip, "paddingLeft") == 13.5f,
        "150 %: its grip at its new corner, at its size");
    std::set<std::pair<float, float>> dots;
    for (const auto dot : Get(grip).children) {
        Check(Pixels(dot, "paddingLeft") == 3 && Pixels(dot, "paddingTop") == 3, "Its dots at 150 %");
        dots.insert({Pixels(dot, "left"), Pixels(dot, "top")});
    }
    Check(dots == std::set<std::pair<float, float>>{{9.f, 0.f}, {4.5f, 4.5f}, {9.f, 4.5f}, {0.f, 9.f}, {4.5f, 9.f}, {9.f, 9.f}},
        "Their triangle at 150 %");
    const auto at = [](float width, float height) {
        return " x=" + std::to_string(PlaceOfEdge(8, width, 1920)) + " y=" + std::to_string(PlaceOfEdge(8, height, 1080));
    };
    const std::string cornerLine = "PERF_OVERLAY_SCALED scaleX=150 scaleY=150" + at(339, 78) + " handle=bottom-right";
    const std::string rightLine = "PERF_OVERLAY_SCALED scaleX=100 scaleY=150" + at(226, 78) + " handle=right";
    const std::string bottomLine = "PERF_OVERLAY_SCALED scaleX=100 scaleY=200" + at(226, 104) + " handle=bottom";
    const auto byCorner = LoggedLine(cornerLine), byRight = LoggedLine(rightLine), byBottom = LoggedLine(bottomLine);
    Mouse(330, 69, false);
    Check(drag.phase == Drag::Idle && settings.perfOverlayScaleX == 150 && settings.perfOverlayScaleY == 150 && LoggedLine(cornerLine) == byCorner + 1,
        "Released: saved, logged with its width, its height and the handle used");
    // Its right edge, anywhere along it between its corners: its width only,
    // back to 100 %, its height (150 %) and its top-left kept, its items on
    // four lines (the widest 208 px at 100 %), its text the largest they give.
    Mouse(345, 45, true);
    Check(drag.phase == Drag::Pressed && drag.resize && drag.handle == ResizeHandle::Right && Get(grip).visible, "Pressed on its right edge");
    Mouse(232, 300, true);
    const float fourLines = 226.f / 208;
    Check(drag.phase == Drag::Moving && ui.scales == Scales{100, 150} && Close(LabelBox(label), Box{8, 8, 226, 78}) && ui.lines == 4 &&
        Near(Pixels(label, "fontSize"), 12 * fourLines, 1e-3) && Near(Pixels(grip, "left"), 234 - 9 * fourLines, 1e-3) &&
        Near(Pixels(grip, "top"), 86 - 9 * fourLines, 1e-3) && Near(Pixels(grip, "paddingLeft"), 9 * fourLines, 1e-3),
        "Its right edge dragged: 100 % wide, still 150 % high, four lines, its grip at its corner at its text's size: " + Get(label).text);
    Mouse(232, 300, false);
    Check(settings.perfOverlayScaleX == 100 && settings.perfOverlayScaleY == 150 && LoggedLine(rightLine) == byRight + 1, "Saved, logged");
    // Its bottom edge, anywhere along it: its height only, 26 px lower: 200 %.
    Mouse(100, 84, true);
    Check(drag.phase == Drag::Pressed && drag.resize && drag.handle == ResizeHandle::Bottom, "Pressed on its bottom edge");
    Mouse(700, 110, true);
    const float fiveLines = 226.f / 196;
    Check(drag.phase == Drag::Moving && ui.scales == Scales{100, 200} && Close(LabelBox(label), Box{8, 8, 226, 104}) && ui.lines == 5 &&
        Near(Pixels(label, "fontSize"), 12 * fiveLines, 1e-3), "Its bottom edge dragged: 200 % high, its width (100 %) kept, five lines");
    Mouse(700, 110, false);
    Check(settings.perfOverlayScaleX == 100 && settings.perfOverlayScaleY == 200 && LoggedLine(bottomLine) == byBottom + 1, "Saved, logged");
    // Elsewhere: still a move, its grip along.
    Mouse(40, 30, true);
    Mouse(140, 130, true);
    Check(drag.phase == Drag::Moving && !drag.resize && LabelBox(label)[0] == 108 && LabelBox(label)[1] == 108 &&
        Near(Pixels(grip, "left"), 108 + 226 - 9 * fiveLines, 1e-3) && Near(Pixels(grip, "top"), 108 + 104 - 9 * fiveLines, 1e-3),
        "Elsewhere: moved, its grip along");
    Mouse(140, 130, false);
    // A small window (a panel pixel half a pixel of the screen): its corner
    // still 18 pixels of the screen, 36 panel pixels (18 otherwise at 100 %).
    clientWidth = 960;
    clientHeight = 540;
    Mouse(150, 89, true); // the panel's (300, 178): 34 px from its corner both ways
    Check(drag.phase == Drag::Pressed && drag.handle == ResizeHandle::BottomRight, "A small window: its corner 18 pixels of the screen");
    Mouse(150, 89, false);
    clientWidth = 1920;
    clientHeight = 1080;
    Check(drag.phase == Drag::Idle && ui.scales == Scales{100, 200} && LabelBox(label)[0] == 108, "A click: nothing");
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
    settings.perfOverlayScaleX = settings.perfOverlayScaleY = 100;
    settings.perfOverlayDrag = false;
}

// ------------------------------------------------------------- its cursors
// 1.17.1: with the mouse placement on, over the overlay, the game's cursor
// manager (DofusCursor, its UI layer, the label as the request's target, the
// plain state) shows the double arrow of the handle under the cursor
// (left-right on its right edge, up-down on its bottom edge, diagonal on its
// corner) or the move cursor elsewhere, as the game's resizable windows do;
// each asked once when it changes (the request of an element entered
// meanwhile stays on top), the handle pressed kept during its gesture, even
// outside the overlay; removed when the cursor leaves it, at the next frame
// when a gesture ends outside it, when the mouse placement is unticked and
// with the overlay (its parts unticked, Restore, the module stopping), a
// failed removal never keeping the overlay in its layer.
void Cursors() {
    using namespace perfOverlay;
    using namespace mouse;
    using Box = std::array<float, 4>;
    auto& settings = module->settings;
    settings.perfOverlay = settings.perfOverlayDrag = settings.perfOverlayShown = true;
    settings.perfOverlayGpu = settings.perfOverlayDisk = false;
    settings.perfOverlayCorner = 0;
    settings.perfOverlayX = settings.perfOverlayY = -1;
    settings.perfOverlayScaleX = settings.perfOverlayScaleY = 100;
    RestorePerfOverlay();
    Check(cursorRequests.empty(), "No cursor left asked by the overlays before");
    Tick();
    Pump(4);
    auto label = Overlay();
    Mouse(600, 600, false);
    Check(label && LabelBox(label) == Box{8, 8, 226, 52} && cursorRequests.empty() && ui.cursor == 0, "Away from it: no cursor asked");
    const auto sets = cursorSets, unasked = cursorRemovals;
    // Over its body: the move cursor, asked once.
    nowMs += HoverMs;
    Mouse(100, 30, false);
    Check(CursorShown() == 0x10000 && CursorAsked(label) == 0x10000 && ui.cursor == 0x10000 && cursorSets == sets + 1,
        "Over it: the move cursor (CursorType.Move) on the UI layer, for the label");
    nowMs += HoverMs;
    Mouse(120, 40, false);
    Check(cursorSets == sets + 1 && cursorRemovals == unasked, "Still over its body: never asked again");
    // Its right edge, its bottom edge, its corner: their double arrows, each in place of the last.
    nowMs += HoverMs;
    Mouse(232, 30, false);
    Check(CursorShown() == 0x20000 && cursorRequests.size() == 1 && cursorSets == sets + 2, "Its right edge: left-right (ResizeEW)");
    nowMs += HoverMs;
    Mouse(100, 58, false);
    Check(CursorShown() == 0x80000 && cursorRequests.size() == 1 && cursorSets == sets + 3, "Its bottom edge: up-down (ResizeNS)");
    nowMs += HoverMs;
    Mouse(229, 55, false);
    Check(CursorShown() == 0x100000 && cursorRequests.size() == 1 && cursorSets == sets + 4 && cursorRemovals == unasked,
        "Its corner: diagonal (ResizeNWSE)");
    // A game element's request made meanwhile: on top while the handle under the cursor stays the same.
    cursorRequests.push_back({Element("VisualElement", "GameButton"), 0x200000});
    nowMs += HoverMs;
    Mouse(228, 54, false);
    Check(CursorShown() == 0x200000 && CursorAsked(label) == 0x100000 && cursorSets == sets + 4, "Unchanged: never asked again, the game's request on top");
    cursorRequests.pop_back();
    // Left: removed.
    nowMs += HoverMs;
    Mouse(600, 600, false);
    Check(cursorRequests.empty() && ui.cursor == 0 && cursorRemovals == unasked + 1, "Left: its cursor removed");
    // A press: the cursor of the handle pressed at once, whatever the last
    // look saw; kept while resized, even outside the overlay.
    Mouse(232, 30, true);
    Check(drag.phase == Drag::Pressed && drag.handle == ResizeHandle::Right && CursorAsked(label) == 0x20000, "Pressed on its right edge: left-right at once");
    Mouse(800, 30, true);
    Check(drag.phase == Drag::Moving && ui.scales == Scales{300, 100} && LabelBox(label) == Box{8, 8, 678, 52} && CursorAsked(label) == 0x20000,
        "Resized beyond it: still left-right");
    Mouse(800, 30, false);
    Check(drag.phase == Drag::Idle && CursorAsked(label) == 0x20000 && settings.perfOverlayScaleX == 300, "Released outside it: saved");
    Step();
    Check(cursorRequests.empty() && ui.cursor == 0, "The gesture ended outside it: its cursor removed at the next frame");
    // Moved: the move cursor all along; dropped under the cursor, still the
    // move cursor, never asked again.
    Mouse(100, 30, true);
    Mouse(300, 230, true);
    Check(drag.phase == Drag::Moving && !drag.resize && CursorAsked(label) == 0x10000 && LabelBox(label)[0] == 208, "Moved: the move cursor");
    Mouse(300, 230, false);
    const auto dropped = cursorSets;
    nowMs += HoverMs;
    Step();
    Check(CursorAsked(label) == 0x10000 && cursorSets == dropped, "Dropped under the cursor: still the move cursor, never asked again");
    // Unticked: removed at once (the pump), never asked while locked; ticked again: back over it.
    settings.perfOverlayDrag = false;
    Tick();
    Check(cursorRequests.empty() && ui.cursor == 0 && Get(label).picking == 1, "Unticked: its cursor removed at once");
    for (int i = 0; i < 3; ++i) { nowMs += HoverMs; Mouse(300, 230, false); }
    Check(cursorRequests.empty() && cursorSets == dropped, "Locked: never asked");
    settings.perfOverlayDrag = true;
    Tick();
    nowMs += HoverMs;
    Mouse(300, 230, false);
    Check(CursorAsked(label) == 0x10000, "Ticked again: over it, the move cursor again");
    // Unticked between two frames (no pump yet): removed at the next frame.
    settings.perfOverlayDrag = false;
    Step();
    Check(cursorRequests.empty() && ui.cursor == 0, "Unticked: removed by the next frame as well");
    settings.perfOverlayDrag = true;
    Tick();
    // Removed with the overlay: its parts unticked, Restore, the module stopping.
    const auto over = [&] {
        Pump(2);
        nowMs += HoverMs;
        const auto box = LabelBox(Overlay());
        Mouse(static_cast<long>(box[0] + 20), static_cast<long>(box[1] + 20), false);
        return CursorAsked(Overlay());
    };
    Check(over() == 0x10000, "Over it again: the move cursor");
    settings.perfOverlay = false;
    Tick();
    Check(!Overlay() && cursorRequests.empty() && ui.cursor == 0, "Its parts unticked: removed with it");
    settings.perfOverlay = true;
    Tick();
    Check(over() == 0x10000, "Shown again: over it, the move cursor");
    RestorePerfOverlay();
    Check(!Overlay() && cursorRequests.empty(), "Restore: removed with it");
    Tick();
    Check(over() == 0x10000, "Recreated: over it, the move cursor");
    module->stopped = true;
    Tick();
    Check(!Overlay() && cursorRequests.empty(), "The module stopping: removed with it");
    module->stopped = false;
    // A removal that fails: logged, the overlay taken out of its layer all the
    // same; the game's next request replaces the one left.
    Tick();
    label = Overlay();
    Check(over() == 0x10000, "Over it once more");
    failMethod = "RemoveCursor"; failOwner = label; failCount = 1;
    RestorePerfOverlay();
    failCount = 0; failOwner = nullptr;
    Check(!Overlay() && !Get(label).fields["parent"] && !PerfOverlayCleanupPending() && Logged("PERF_OVERLAY_CURSOR_ERROR Injected failure: RemoveCursor") == 1,
        "A failed cursor removal: logged, the overlay removed all the same");
    cursorRequests.clear();
    settings.perfOverlayX = settings.perfOverlayY = -1;
    settings.perfOverlayScaleX = settings.perfOverlayScaleY = 100;
    settings.perfOverlayDrag = false;
}

// ------------------------------------------------ the texts without figures
// 1.17.1: a text without figures (« Mesure en cours… », « Mesure
// indisponible ») sizes the overlay only until its first figures,
// provisionally, its box at its width and its height all the same; its first
// figures size it again from scratch (narrower here), never such a text
// afterwards: 1.18.1, its box kept, that text drawn smaller to fit in it
// (never clipped). A resize pressed before its items are measured is let go
// (its first frame: no size to resize from); figures coming during a resize
// keep its box until it ends.
void NeutralTexts() {
    using namespace perfOverlay;
    using namespace mouse;
    using Box = std::array<float, 4>;
    auto& settings = module->settings;
    settings.perfOverlay = settings.perfOverlayDisk = false;
    settings.perfOverlayGpu = settings.perfOverlayDrag = settings.perfOverlayShown = true;
    settings.perfOverlayCorner = 0;
    settings.perfOverlayX = settings.perfOverlayY = -1;
    settings.perfOverlayScaleX = 100;
    settings.perfOverlayScaleY = 200;
    RestorePerfOverlay();
    Mouse(900, 900, false);
    pdhFake::Configure([] { pdhFake::readStatus = static_cast<PDH_STATUS>(PDH_CSTATUS_NO_OBJECT); });
    Tick();
    const auto label = Overlay();
    Check(label && Get(label).text == WaitingLine && Unmeasured() && !ui.learned && !Styled(label, "minWidth") && !Styled(label, "minHeight"),
        "The GPU alone, waiting: nothing measured before its first layout");
    // Its first layout: measured from that text, provisionally, its box at 100 % by 200 %.
    Step();
    Check(ui.learned && ui.provisional && ui.naturalWidth == 6 * 16 + 16 && ui.naturalHeight == 20 && Pixels(label, "minWidth") == 112 &&
        Pixels(label, "minHeight") == 40 && Pixels(label, "fontSize") == 12 && LabelBox(label) == Box{8, 8, 112, 40},
        "A text without figures: its box at its width and its height all the same");
    // Windows refusing the figure: « Mesure indisponible », wider, grows it, still provisionally.
    Check(WaitFor([] { return !Figures().gpuProblem.empty(); }), "The GPU refused");
    Pump(4);
    Check(Get(label).text == UnavailableLine && ui.provisional && ui.naturalWidth == 6 * 19 + 16 && Pixels(label, "minWidth") == 130 &&
        LabelBox(label) == Box{8, 8, 130, 40}, "A wider text without figures: grown, still provisionally");
    // Its first figures: its size forgotten, sized again from them at once.
    pdhFake::Configure([] { pdhFake::readStatus = ERROR_SUCCESS; });
    Check(WaitFor([] { return Figures().gpu >= 0; }), "The GPU's figure");
    Pump(2);
    Check(Get(label).text == "GPU 23 %" && !ui.provisional && ui.naturalWidth == 64 && ui.naturalHeight == 20 && Pixels(label, "minWidth") == 64 &&
        Pixels(label, "minHeight") == 40 && Pixels(label, "fontSize") == 12 && LabelBox(label) == Box{8, 8, 64, 40},
        "Its first figures: the size of the text without figures forgotten, its box from them");
    // Its bottom edge, between its bottom corners: its height only.
    const auto saved = saves;
    const auto scaledLine = [](int x, int y, float width, float height, const char* handle) {
        return "PERF_OVERLAY_SCALED scaleX=" + std::to_string(x) + " scaleY=" + std::to_string(y) + " x=" +
            std::to_string(PlaceOfEdge(8, width, 1920)) + " y=" + std::to_string(PlaceOfEdge(8, height, 1080)) + " handle=" + handle;
    };
    const auto tallerLines = LoggedLine(scaledLine(100, 250, 64, 50, "bottom"));
    Mouse(40, 47, true);
    Check(drag.phase == Drag::Pressed && drag.resize && drag.handle == ResizeHandle::Bottom && CursorAsked(label) == 0x80000, "Pressed on its bottom edge");
    Mouse(40, 57, true);
    Check(drag.phase == Drag::Moving && ui.scales == Scales{100, 250} && Pixels(label, "fontSize") == 12 && LabelBox(label) == Box{8, 8, 64, 50},
        "Its bottom edge under the cursor: 250 % high, its width kept");
    Mouse(40, 57, false);
    Check(drag.phase == Drag::Idle && saves == saved + 1 && settings.perfOverlayScaleX == 100 && settings.perfOverlayScaleY == 250 &&
        LoggedLine(scaledLine(100, 250, 64, 50, "bottom")) == tallerLines + 1, "Saved at those sizes");
    // A text without figures after them: never sizes it, its box kept, the
    // text drawn smaller to fit in it, never clipped; the figures back in that box.
    pdhFake::Configure([] { pdhFake::readStatus = static_cast<PDH_STATUS>(PDH_CSTATUS_NO_OBJECT); });
    Check(WaitFor([] { return !Figures().gpuProblem.empty(); }), "The GPU refused again");
    Pump(4);
    Check(Get(label).text == UnavailableLine && ui.naturalWidth == 64 && ui.naturalHeight == 20 && !ui.provisional && Pixels(label, "minWidth") == 64 &&
        Pixels(label, "minHeight") == 50 && Near(Pixels(label, "fontSize"), 12 * 64.f / 130, 1e-3) && Close(LabelBox(label), Box{8, 8, 64, 50}),
        "A text without figures after them: never sizes it, drawn smaller in its box, never clipped");
    pdhFake::Configure([] { pdhFake::readStatus = ERROR_SUCCESS; });
    Check(WaitFor([] { return Figures().gpu >= 0; }), "The GPU's figure again");
    Pump(4);
    Check(Get(label).text == "GPU 23 %" && ui.naturalWidth == 64 && Pixels(label, "fontSize") == 12 && LabelBox(label) == Box{8, 8, 64, 50} &&
        saves == saved + 1, "The figures back in their box");
    // A press on a new label's corner before its items could be measured (its
    // first layout not usable yet, measured again 50 ms later): let go, nothing
    // resized; the button held meanwhile never pressing again.
    settings.perfOverlayScaleX = settings.perfOverlayScaleY = 100;
    RestorePerfOverlay();
    Tick();
    const auto early = Overlay();
    collapsedWidth = 0;
    Step();
    collapsedWidth = -1;
    Check(early && !ui.learned && !Styled(early, "minWidth"), "A new label, its first layout not usable: nothing measured yet");
    const auto box = LabelBox(early);
    Mouse(static_cast<long>(box[0] + box[2] - 2), static_cast<long>(box[1] + box[3] - 2), true);
    Check(drag.phase == Drag::Idle && !ui.learned && ui.scales == Scales{100, 100} && saves == saved + 1, "Pressed before its items were measured: let go");
    nowMs += SettleMs;
    Mouse(static_cast<long>(box[0] + box[2] + 100), static_cast<long>(box[1] + box[3] + 100), true);
    Check(ui.learned && drag.phase == Drag::Idle && ui.scales == Scales{100, 100} && Pixels(early, "minWidth") == 112,
        "Measured 50 ms later; the button held: never a press");
    Mouse(static_cast<long>(box[0] + box[2] + 100), static_cast<long>(box[1] + box[3] + 100), false);
    // Its first figures during a resize from the size of its text without
    // figures: its box kept until the resize ends, then sized again.
    settings.perfOverlayX = settings.perfOverlayY = -1;
    RestorePerfOverlay();
    const HANDLE held = HoldCollections();
    Tick();
    const auto resized = Overlay();
    Check(resized && Get(resized).text == WaitingLine && WaitFor([] { return pdhFake::waiting.load() == 1; }), "Another label, waiting");
    Step();
    Check(ui.provisional && LabelBox(resized) == Box{8, 8, 112, 20}, "At 100 %");
    const auto cornerLines = LoggedLine(scaledLine(150, 150, 168, 30, "bottom-right"));
    Mouse(117, 25, true);
    Mouse(173, 35, true);
    Check(drag.phase == Drag::Moving && drag.handle == ResizeHandle::BottomRight && ui.scales == Scales{150, 150} && LabelBox(resized) == Box{8, 8, 168, 30},
        "Resized by its corner from the size of its text without figures");
    ReleaseCollections(held);
    Check(WaitFor([] { return Figures().gpu >= 0; }), "Its figure, during the resize");
    Pump(2);
    Check(Get(resized).text == "GPU 23 %" && drag.phase == Drag::Moving && ui.provisional && ui.naturalWidth == 112 &&
        LabelBox(resized) == Box{8, 8, 168, 30} && Pixels(resized, "fontSize") == 18, "Its first figures during the resize: its box kept until it ends");
    Mouse(173, 35, false);
    Check(drag.phase == Drag::Idle && saves == saved + 2 && LoggedLine(scaledLine(150, 150, 168, 30, "bottom-right")) == cornerLines + 1, "Saved");
    Pump(2);
    Step();
    const auto measured = LabelBox(resized);
    Check(!ui.provisional && ui.naturalWidth == 64 && ui.naturalHeight == 20 && measured[2] == 96 && measured[3] == 30 &&
        std::fabs(measured[0] - 8) < 1 && std::fabs(measured[1] - 8) < 1, "Then sized again from its figures, at its sizes");
    Mouse(900, 900, false);
    settings.perfOverlayGpu = settings.perfOverlayDrag = false;
    settings.perfOverlayX = settings.perfOverlayY = -1;
    settings.perfOverlayScaleX = settings.perfOverlayScaleY = 100;
    Tick();
    Check(!Overlay() && cursorRequests.empty() && WaitFor(Retired), "Unticked: removed, its worker stopped");
    settings.perfOverlay = true;
}

// ------------------------------------------------------ all its edges
// 1.18.1: resized as a window by all four edges and all four corners: the
// edge held follows the cursor and the opposite side stays put (a label
// anchored by its right and bottom sides moved by them), the game's cursor
// of each handle over it and during its gesture (left-right on its left
// edge, up-down on its top edge, one diagonal on its top-left corner, the
// other on its top-right and bottom-left ones), its items flowing again at
// each size, saved once at the end with the place of its top-left for that
// box, its handle logged.
void AllEdges() {
    using namespace perfOverlay;
    using namespace mouse;
    using Box = std::array<float, 4>;
    auto& settings = module->settings;
    settings.perfOverlay = settings.perfOverlayDrag = settings.perfOverlayShown = true;
    settings.perfOverlayGpu = settings.perfOverlayDisk = false;
    settings.perfOverlayCorner = 0;
    settings.perfOverlayX = settings.perfOverlayY = -1;
    settings.perfOverlayScaleX = settings.perfOverlayScaleY = 100;
    RestorePerfOverlay();
    Tick();
    Pump(4);
    auto label = Overlay();
    Step();
    Check(label && LabelBox(label) == Box{8, 8, 226, 52} && Get(label).text == Steady, "At its natural size, top left");
    // Moved by its middle to (500, 300) first.
    Mouse(48, 28, true);
    Mouse(540, 320, true);
    Mouse(540, 320, false);
    Check(LabelBox(label) == Box{500, 300, 226, 52} && !drag.resize, "Moved by its middle");
    const auto saved = saves;
    // Over its left and top edges and its other corners: their cursors.
    const auto cursorAt = [&](long x, long y) { nowMs += HoverMs; Mouse(x, y, false); return CursorAsked(label); };
    Check(cursorAt(503, 330) == 0x20000 && cursorAt(600, 302) == 0x80000 && cursorAt(502, 302) == 0x100000 && cursorAt(724, 302) == 0x40000 &&
        cursorAt(502, 350) == 0x40000 && cursorAt(724, 350) == 0x100000 && cursorAt(600, 326) == 0x10000,
        "Its left edge left-right, its top edge up-down, its top-left corner NW-SE, its top-right and bottom-left ones NE-SW, its middle the move cursor");
    const auto line = [](int x, int y, float left, float top, float width, float height, const char* handle) {
        return "PERF_OVERLAY_SCALED scaleX=" + std::to_string(x) + " scaleY=" + std::to_string(y) + " x=" +
            std::to_string(PlaceOfEdge(left, width, 1920)) + " y=" + std::to_string(PlaceOfEdge(top, height, 1080)) + " handle=" + handle;
    };
    // Its left edge: its width only, its right side (726) kept, its items on two lines.
    Mouse(503, 330, true);
    Check(drag.phase == Drag::Pressed && drag.resize && drag.handle == ResizeHandle::Left && CursorAsked(label) == 0x20000, "Pressed on its left edge");
    Mouse(390, 340, true);
    Check(drag.phase == Drag::Moving && ui.scales == Scales{150, 100} && Close(LabelBox(label), Box{387, 300, 339, 52}) && Pixels(label, "left") == 387 &&
        Get(label).text == TwoLines && saves == saved, "Its left edge dragged: 150 % wide, its right side and its top kept, two lines");
    Mouse(390, 340, false);
    Check(saves == saved + 1 && settings.perfOverlayScaleX == 150 && settings.perfOverlayScaleY == 100 &&
        settings.perfOverlayX == PlaceOfEdge(387, 339, 1920) && settings.perfOverlayY == PlaceOfEdge(300, 52, 1080) &&
        LoggedLine(line(150, 100, 387, 300, 339, 52, "left")) == 1, "Saved once: its sizes and the place of its top-left");
    // Its top edge: its height only, its bottom side (352) kept.
    Mouse(500, 302, true);
    Check(drag.phase == Drag::Pressed && drag.handle == ResizeHandle::Top && CursorAsked(label) == 0x80000, "Pressed on its top edge");
    Mouse(500, 276, true);
    Check(ui.scales == Scales{150, 150} && Close(LabelBox(label), Box{387, 274, 339, 78}) && Pixels(label, "fontSize") == 18 && Get(label).text == Steady,
        "Its top edge dragged: 150 % high, its bottom side kept, three lines at 150 %");
    Mouse(500, 276, false);
    Check(saves == saved + 2 && LoggedLine(line(150, 150, 387, 274, 339, 78, "top")) == 1, "Saved");
    // Its top-left corner: both, its bottom-right corner (726, 352) kept.
    Mouse(389, 276, true);
    Check(drag.phase == Drag::Pressed && drag.handle == ResizeHandle::TopLeft && CursorAsked(label) == 0x100000, "Pressed on its top-left corner");
    Mouse(502, 302, true);
    Check(ui.scales == Scales{100, 100} && Close(LabelBox(label), Box{500, 300, 226, 52}) && Pixels(label, "fontSize") == 12,
        "Its top-left corner dragged: back to 100 %, its bottom-right corner kept");
    Mouse(502, 302, false);
    Check(saves == saved + 3 && LoggedLine(line(100, 100, 500, 300, 226, 52, "top-left")) == 1, "Saved");
    // Its top-right corner: its left side and its bottom kept.
    Mouse(724, 302, true);
    Check(drag.phase == Drag::Pressed && drag.handle == ResizeHandle::TopRight && CursorAsked(label) == 0x40000, "Pressed on its top-right corner");
    Mouse(837, 276, true);
    Check(ui.scales == Scales{150, 150} && Close(LabelBox(label), Box{500, 274, 339, 78}) && CursorAsked(label) == 0x40000,
        "Its top-right corner dragged: its left side and its bottom kept");
    Mouse(837, 276, false);
    Check(saves == saved + 4 && LoggedLine(line(150, 150, 500, 274, 339, 78, "top-right")) == 1, "Saved");
    // Its bottom-left corner: its right side (839) and its top kept.
    Mouse(502, 350, true);
    Check(drag.phase == Drag::Pressed && drag.handle == ResizeHandle::BottomLeft && CursorAsked(label) == 0x40000, "Pressed on its bottom-left corner");
    Mouse(615, 324, true);
    Check(ui.scales == Scales{100, 100} && Close(LabelBox(label), Box{613, 274, 226, 52}), "Its bottom-left corner dragged: its right side and its top kept");
    Mouse(615, 324, false);
    Check(saves == saved + 5 && LoggedLine(line(100, 100, 613, 274, 226, 52, "bottom-left")) == 1, "Saved");
    // Far beyond its left side: 300 % at most, entirely on screen.
    Mouse(615, 300, true);
    Mouse(-500, 300, true);
    Check(drag.handle == ResizeHandle::Left && ui.scales == Scales{300, 100} && Close(LabelBox(label), Box{161, 274, 678, 52}) && Get(label).text.find('\n') != std::string::npos,
        "300 % wide at most, its right side kept");
    Mouse(-500, 300, false);
    Check(saves == saved + 6 && settings.perfOverlayScaleX == 300, "Saved");
    // A label anchored by its right and bottom sides (the corner an earlier
    // revision chose): its left edge and its top-left corner move them, its
    // right and bottom sides kept 8 px from the panel's.
    settings.perfOverlayX = settings.perfOverlayY = -1;
    settings.perfOverlayScaleX = settings.perfOverlayScaleY = 100;
    settings.perfOverlayCorner = 3;
    Tick();
    label = Overlay();
    Pump(4);
    Step();
    Check(label && LabelBox(label) == Box{1686, 1020, 226, 52}, "Bottom right, at its natural size");
    Mouse(1688, 1046, true);
    Mouse(1575, 1046, true);
    Check(drag.handle == ResizeHandle::Left && ui.scales == Scales{150, 100} && Close(LabelBox(label), Box{1573, 1020, 339, 52}) &&
        Pixels(label, "right") == 8 && Pixels(label, "bottom") == 8 && !Styled(label, "left"), "Its left edge: its right side kept 8 px from the panel's");
    Mouse(1575, 1046, false);
    Mouse(1575, 1022, true);
    Mouse(1462, 996, true);
    Check(drag.handle == ResizeHandle::TopLeft && ui.scales == Scales{200, 150} && Close(LabelBox(label), Box{1460, 994, 452, 78}) &&
        Pixels(label, "right") == 8 && Pixels(label, "bottom") == 8, "Its top-left corner: its right and bottom sides kept");
    Mouse(1462, 996, false);
    Check(saves == saved + 8 && settings.perfOverlayScaleX == 200 && settings.perfOverlayScaleY == 150 && settings.perfOverlayCorner == 3 &&
        settings.perfOverlayX == PlaceOfEdge(1460, 452, 1920) && settings.perfOverlayY == PlaceOfEdge(994, 78, 1080), "Saved, its corner kept");
    Check(changes == 0 && Logged("PERF_OVERLAY_MEASURE_ESTIMATED") == 0, "Never Module::Change; every item measured, never estimated");
    settings.perfOverlayCorner = 0;
    settings.perfOverlayX = settings.perfOverlayY = -1;
    settings.perfOverlayScaleX = settings.perfOverlayScaleY = 100;
    settings.perfOverlayDrag = false;
    RestorePerfOverlay();
}

// 1.18.1: the game unable to measure its items (MeasureTextSize giving no
// size, as without a font): estimated, 7 px a character and 16 px a line at
// 100 %, logged once a label; its items laid out all the same. The next label
// measures them again.
void MeasureFallback() {
    using namespace perfOverlay;
    auto& settings = module->settings;
    settings.perfOverlay = settings.perfOverlayShown = true;
    settings.perfOverlayGpu = settings.perfOverlayDisk = settings.perfOverlayDrag = false;
    settings.perfOverlayX = settings.perfOverlayY = -1;
    settings.perfOverlayScaleX = settings.perfOverlayScaleY = 100;
    RestorePerfOverlay();
    const auto before = Logged("PERF_OVERLAY_MEASURE_ESTIMATED");
    measureBroken = true;
    Tick();
    Pump(3);
    const auto label = Overlay();
    Check(label && ui.learned && ui.estimated && ui.metrics.first == 16 && ui.metrics.pitch == 16 && ui.metrics.separator == 21 &&
        ui.widths[SlotHeap] == 7 * 24 && LoggedLine("PERF_OVERLAY_MEASURE_ESTIMATED invalid") == 1 && Logged("PERF_OVERLAY_MEASURE_ESTIMATED") == before + 1,
        "Estimated, logged once");
    Check(Get(label).text == Steady && ui.naturalWidth == 7 * 35 + 16 && Pixels(label, "minWidth") == 7 * 35 + 16 && Pixels(label, "fontSize") == 12,
        "Laid out all the same, on its groups' lines: " + Get(label).text);
    measureBroken = false;
    RestorePerfOverlay();
    Tick();
    Pump(3);
    Check(Overlay() && ui.learned && !ui.estimated && ui.metrics.separator == 18 && Logged("PERF_OVERLAY_MEASURE_ESTIMATED") == before + 1 &&
        Pixels(Overlay(), "minWidth") == CpuLineWidth, "The next label measured again");
    settings.perfOverlay = false;
    Tick();
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
        FlowModel();
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
        Cursors();
        NeutralTexts();
        AllEdges();
        MeasureFallback();
        TopBarButton();
        RestorePerfOverlay();
        Check(!perfOverlay::devices.worker && !perfOverlay::devices.thread && WaitFor(Retired), "No worker left after Restore");
        mod.compositor.reset();
        module = nullptr;
        Check(handles.empty(), "No managed reference kept after Restore");
        Check(Logged("PERF_OVERLAY_MEASURE_ESTIMATED") == 1 && measures > 0, "Every item measured with the font drawn, estimated only when the game gave no size");
        std::cout << "PASS: performance overlay: frame rate and frame times over the last second (a slow frame forgotten after "
            "a second, 1024-frame ring), CPU share over the last two refreshes, Mo/Go and French decimals, three-line text and "
            "a fourth for the GPU and the disk, every native call verified and guarded (64 methods, 2 GC exports and the 4 "
            "functions behind them, the layer host's offset), inert when off or disabled, waits for its layer (loading screens, "
            "pq.pbs throwing), first child of RootHigh, ignores clicks and focus without the mouse placement, rounded dark "
            "background, its text from its top-left, never narrower or lower once laid out (its natural size from its items "
            "measured at its first layout with its own font, each shape once, never with a font drawn since), four corners "
            "(top right under the game menu), refreshed every second "
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
            "indisponible » when Windows refuses them, sized by those texts only until their first figures), only what is shown measured (no frame "
            "counter, memory, heap or CPU read without the frame rate, the "
            "mouse placement alone showing nothing), recreated for another choice of parts, and the mouse place kept and the "
            "overlay locked there when the mouse placement is unchecked, across restarts; resize (1.16.0, as a window since "
            "1.17.1): its width and its height each (perfOverlayScaleX/Y), its box at each from its natural size (the widest "
            "and tallest items kept at 100 %); reflow (1.18.1): each figure an item (« · » between those of a line), for each "
            "number of lines the narrowest width its items need, the largest text its box holds over them, its items then "
            "filling lines as wide as the box gives them (up on the line above as soon as they fit, back as it narrows: one "
            "line in a wide strip, an item a line in a tall one), a function of its box alone, each item's widest shape kept "
            "(no jitter), estimated and logged once when the game gives no size; its top-left kept by its right and bottom "
            "edges even when anchored by its right and bottom sides, its corner following "
            "the cursor freely, each side in whole percents within 50..300 and its own room to the panel's margins, a click "
            "changing nothing, never grown or placed meanwhile nor by the layout of the sizes before (the game lays it out "
            "after the frame), back at the last sizes seen when the point held is covered, saved once with the place of its "
            "top-left, back at the sizes saved when interrupted, recreated at those sizes, any other size read as 100 % on "
            "its axis, never while locked (corners all rounded); "
            "grip and edges (1.16.1): six dots of its text's colour in its bottom-right corner, elements of their own right "
            "above the label (never its children: its text measure kept), never picked, shown while the cursor is over it "
            "(looked at every 50 ms, the panel's pick only over it) and during a drag, hidden when it leaves, under a game "
            "element, with another window in the foreground and while locked, following the corner at the overlay's size; "
            "its corners (at least 18 pixels of the screen, 16 px at its text's size, never wider or higher than a third of "
            "it) and its edges (at least 6 pixels of the screen, 5 px at its text's size, never thicker than a quarter of it) "
            "resize it, all four of each since 1.18.1, the edge held following the cursor and the opposite side kept, the "
            "rest moving it; each resize "
            "logged as it ends (PERF_OVERLAY_SCALED, with its width, its height and its handle), the grip recreated with the "
            "label and removed with it; cursors (1.17.1): the game's cursor manager asked on its UI layer for the label, the "
            "double arrow of the handle under the cursor (left-right, up-down, both diagonals) or the move cursor, asked once a "
            "change (a game element's request left on top), the handle pressed kept during its gesture, removed when the "
            "cursor leaves, at the next frame after a gesture ended outside, when unticked and with the overlay (parts "
            "unticked, Restore, the module stopping), a failed removal logged without keeping the overlay; texts without "
            "figures (1.17.1): its box at its width and its height from them, provisionally, sized again from its first "
            "figures (narrower), never after them (drawn smaller in its box, never clipped), a resize pressed before its "
            "items are measured let go, figures during a resize keeping its box until it ends; "
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
