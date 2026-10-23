// PerfOverlayTests (offline): perf-overlay.cpp on a fake IL2CPP runtime with
// fake Windows counters and clocks. No client binary is loaded, no process
// opened. Every native call the module makes must have been verified at
// install (FindVerified at its pinned RVA, every RVA guarded in
// signatures.hpp), except the style value setters every module uses.
#include "core/module.hpp"
#include "core/signatures.hpp"
#include "appearance/features.hpp"
#include "ui/options-ui.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
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
    {"VisualElement", "Focusable"}, {"gmy", "VisualElement"}, {"UIDocumentRootElement", "VisualElement"}};
std::vector<std::string> logs;
// Failure injection: this method on this object throws (failCount times; -1:
// always), only while the object is shown when failAttachedOnly.
std::string failMethod;
Obj failOwner = nullptr;
int failCount = 0;
bool failAttachedOnly = false;
int labelsBuilt = 0, removals = 0;
float collapsedWidth = -1; // >= 0 or NaN: the layout's width while the game hides its interface
std::set<uintptr_t> natives;  // the native helpers checked (NativeAt)
uintptr_t nativeMismatch = 0; // a helper whose bytes differ from the snapshot
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
// The fake text layout: 6 px a character of the longest line, plus the
// padding; never narrower than the label's minimum width.
float LaidOutWidth(Obj o) {
    if (!(collapsedWidth < 0)) return collapsedWidth;
    size_t longest = 0, line = 0;
    for (const unsigned char c : Get(o).text) {
        if (c == '\n') { line = 0; continue; }
        if ((c & 0xC0) != 0x80) longest = std::max(longest, ++line);
    }
    float width = 6.f * static_cast<float>(longest) + 16.f;
    if (Get(o).styles.contains("minWidth")) {
        float minimum = 0; std::memcpy(&minimum, Get(o).styles.at("minWidth").data(), 4);
        width = std::max(width, minimum);
    }
    return width;
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
int64_t FakeCounter() { return counter; }
int64_t FakeFrequency() { return fakeFrequency; }
uint64_t FakeMilliseconds() { return nowMs; }
unsigned FakeProcessors() { return fakeProcessors; }
bool FakeMemory(uint64_t& w, uint64_t& p) { if (!memoryReadable) return false; w = workingSet; p = privateBytes; return true; }
bool FakeCpu(uint64_t& t) { t = cpuTime; return true; }
int64_t FakeGcUsed() { return heapUsed; }
int64_t FakeGcHeap() { return heapSize; }
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
    api.object_new = [](Klass k) -> Obj { return Make(static_cast<Class*>(k)->name); };
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
    panel = Make("Panel");
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
    if (info.name == "RemoveFromHierarchy") ++removals;
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
    if (m == ".ctor") { Check(o.klass->name == "Label" && info.type == "Label", "Label constructor"); o.constructed = true; ++labelsBuilt; return nullptr; }
    Check(o.klass->name != "Label" || o.constructed, "Label used before its constructor");
    if (m == "get_parent") return o.fields["parent"];
    if (m == "get_panel") return Panel(self);
    if (m == "get_name") return fixture::String(o.name);
    if (m == "get_hierarchy") { auto box = Make("Hierarchy"); std::memcpy(Get(box).bytes.data(), &self, sizeof(self)); return box; }
    if (m == "get_layout") { struct Rect { float x, y, width, height; }; return Box(Rect{0, 0, LaidOutWidth(self), 52}); }
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
    throw std::runtime_error("Unexpected fake method " + info.type + "." + m);
}
}
Module* module = nullptr;
Module::Module(std::filesystem::path path) : folder(std::move(path)) {}
managed::Obj Module::FindService(const char* name) const {
    fixture::Check(std::string(name) == "fqp", "Only the layer service is looked up");
    return fixture::service;
}
void Log(const std::string& message) { fixture::logs.push_back(message); }
#include "ui/perf-overlay.cpp"

namespace {
using namespace fixture;
size_t Logged(const std::string& prefix) {
    return static_cast<size_t>(std::count_if(logs.begin(), logs.end(), [&](const std::string& line) { return line.rfind(prefix, 0) == 0; }));
}
size_t LoggedLine(const std::string& text) { return static_cast<size_t>(std::count(logs.begin(), logs.end(), text)); }
Obj Overlay() { return perfOverlay::ui.label.get(); }
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
bool Near(double value, double expected, double tolerance = 1e-6) { return std::fabs(value - expected) <= tolerance; }
const std::string Measuring = "FPS : mesure en cours…";
const std::string Memory = "Mémoire 1,52 Go · privée 1,31 Go";
const std::string Heap = "Tas géré 312 Mo / 420 Mo";
const std::string Steady = "60 FPS · 16,7 ms (max 16,7 ms)\n" + Memory + "\n" + Heap + " · CPU 12 %";
// A new label's first text: its CPU share needs a second sample.
const std::string SteadyNoCpu = "60 FPS · 16,7 ms (max 16,7 ms)\n" + Memory + "\n" + Heap;
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
    Sample sample;
    sample.frames = {true, 60, 16.6667, 33.4};
    sample.memory = true; sample.workingSet = 1632087573ULL; sample.privateBytes = 1406601790ULL;
    sample.heap = true; sample.heapUsed = 312 * MiB; sample.heapSize = 420 * MiB;
    sample.cpu = 12.4;
    Check(Text(sample) == "60 FPS · 16,7 ms (max 33,4 ms)\n" + Memory + "\n" + Heap + " · CPU 12 %", "Three lines");
    sample.heap = false;
    Check(Text(sample) == "60 FPS · 16,7 ms (max 33,4 ms)\n" + Memory + "\nCPU 12 %", "Without the managed heap");
    sample.heap = true; sample.cpu = -1;
    Check(Text(sample) == "60 FPS · 16,7 ms (max 33,4 ms)\n" + Memory + "\n" + Heap, "Without the CPU share yet");
    sample.heap = false;
    Check(Text(sample) == "60 FPS · 16,7 ms (max 33,4 ms)\n" + Memory, "Two lines");
    sample.memory = false; sample.frames.valid = false; sample.cpu = 3;
    Check(Text(sample) == Measuring + "\nCPU 3 %", "No frame yet, memory unreadable");
    const auto left = Place(0), right = Place(1), bottomLeft = Place(2), bottomRight = Place(3), other = Place(7);
    Check(!left.right && !left.bottom && left.horizontal == 8 && left.vertical == 8, "Top left");
    Check(right.right && !right.bottom && right.horizontal == 8 && right.vertical == 48, "Top right: under the game's menu buttons");
    Check(!bottomLeft.right && bottomLeft.bottom && bottomLeft.horizontal == 8 && bottomLeft.vertical == 8, "Bottom left");
    Check(bottomRight.right && bottomRight.bottom && bottomRight.horizontal == 8 && bottomRight.vertical == 8, "Bottom right");
    Check(!other.right && !other.bottom && other.vertical == 8, "Anything else: top left");
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
    // Every contract at its pinned RVA (an independent copy of the snapshot's).
    std::set<std::tuple<std::string, std::string, int, uintptr_t>> expected{
        {"VisualElement", "get_parent", 0, 0x55F7180}, {"VisualElement", "get_panel", 0, 0x55F7170}, {"VisualElement", "get_name", 0, 0x3158710},
        {"VisualElement", "get_hierarchy", 0, 0x185C4A0}, {"VisualElement", "Insert", 2, 0x55E5710},
        {"VisualElement", "RemoveFromHierarchy", 0, 0x55E7BD0}, {"VisualElement", "set_pickingMode", 1, 0x55F97B0},
        {"VisualElement", "set_name", 1, 0x55F9670}, {"VisualElement", "get_style", 0, 0x55F74A0}, {"VisualElement", "get_layout", 0, 0x55F6930},
        {"Hierarchy", "get_childCount", 0, 0x55FFA80}, {"Hierarchy", "get_Item", 1, 0x55FE670}, {"Focusable", "set_focusable", 1, 0x5777EF0},
        {"Label", ".ctor", 0, 0x5729760}, {"TextElement", "set_text", 1, 0x564B780}, {"TextElement", "set_enableRichText", 1, 0x564B1E0},
        {"Length", ".ctor", 2, 0x561D3D0}, {"pq", "pbs", 0, 0x2E93CC0}};
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
    for (const uintptr_t rva : {0x5B9230, 0x5B9210, 0x5FE340, 0x5FDB20, 0x6194D0, 0x6194B0})
        Check(Guarded(rva), "The GC exports and the code behind them are guarded");
    InstallPerfOverlay();
    Check(contracts == expected && Logged("PERF_OVERLAY_DISABLED") == 2, "Installed once");
}

// --------------------------------------------------------------- lifecycle
void OptionOff() {
    using namespace perfOverlay;
    module->settings.perfOverlay = false;
    for (int i = 0; i < 5; ++i) Pump();
    Check(Owned(high).empty() && ring.count == 0 && !sampling && !PerfOverlayCleanupPending(), "Off: nothing created, nothing measured");
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
    Check(Logged("PERF_OVERLAY_READY layer=RootHigh corner=0 heap=1 processors=8") == 1, "Ready logged");
    const auto label = owned[0];
    auto& o = Get(label);
    Check(o.klass->name == "Label" && o.constructed && o.picking == 1 && !o.focusable && !o.rich, "Never a click, a hover or the keyboard focus");
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
    Check(!Overlay() && Owned(high).empty() && Logged("PERF_OVERLAY_ERROR Injected failure: set_enableRichText") == 1 && failures == 1,
        "Creation error logged, nothing attached");
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
    const auto label = Overlay();
    Check(label && PerfOverlayCleanupPending(), "Shown before the stop");
    RestorePerfOverlay();
    Check(!Overlay() && !Get(label).fields["parent"] && Owned(high).empty() && !PerfOverlayCleanupPending() && !sampling, "Restored");
    Frame();
    Check(ring.count == 0, "No frame measured after Restore");
    // A detach that fails: pending, tried again 500 ms later (Restore runs every frame once the mod stops).
    Tick();
    const auto shown = Overlay();
    Check(shown && Owned(high).size() == 1, "Shown again");
    failMethod = "RemoveFromHierarchy"; failOwner = shown; failCount = 1;
    auto tries = removals;
    RestorePerfOverlay();
    Check(PerfOverlayCleanupPending() && Get(shown).fields["parent"] && Logged("PERF_OVERLAY_CLEANUP_ERROR") == 1, "Failed detach: pending");
    for (int frame = 0; frame < 30; ++frame) { nowMs += 16; RestorePerfOverlay(); }
    Check(PerfOverlayCleanupPending() && removals - tries == 1, "Not tried again within 500 ms");
    for (int frame = 0; frame < 10; ++frame) { nowMs += 16; RestorePerfOverlay(); }
    Check(!PerfOverlayCleanupPending() && !Get(shown).fields["parent"] && removals - tries == 2, "Done at the next try, 500 ms later");
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
}

int main() {
    try {
        Setup();
        Module mod({});
        module = &mod;
        mod.ready = true;
        mod.servicesReady = true;
        mod.compositor.reset(Make("Compositor"));
        BuildInterface();
        FramesModel();
        CpuModel();
        Formatting();
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
        RestorePerfOverlay();
        mod.compositor.reset();
        module = nullptr;
        Check(handles.empty(), "No managed reference kept after Restore");
        std::cout << "PASS: performance overlay: frame rate and frame times over the last second (a slow frame forgotten after "
            "a second, 1024-frame ring), CPU share over the last two refreshes, Mo/Go and French decimals, three-line text, "
            "every native call verified and guarded (54 methods, 2 GC exports and the 4 functions behind them, the layer "
            "host's offset), inert when off or disabled, waits for its layer (loading screens, pq.pbs throwing), first child "
            "of RootHigh, ignores clicks and focus, rounded dark background, never narrower once laid out, four corners "
            "(top right under the game menu), refreshed every second pump and only when changed, no CPU share spanning a "
            "wait, recreated when the game rebuilds its layers or the session changes, removed at once when unchecked or "
            "restored, 3 errors within 5 minutes give up (errors at each first refresh included), cleanup retried twice "
            "a second for 10 s\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        for (const auto& line : fixture::logs) std::cerr << "  log: " << line << '\n';
        return 1;
    }
}
