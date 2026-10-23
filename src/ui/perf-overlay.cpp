#include "ui/perf-overlay.hpp"
#include "core/module.hpp"
#include <psapi.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

namespace perfOverlay {
// ===================================================================== model
// No game access in this part: frame times, CPU share, memory figures, the
// overlay's text and its corner (perf-overlay-tests covers them).

// Start times of the main compositor's last frames, in performance counter
// ticks: 1024 of them cover the last second up to 1024 frames per second.
struct FrameRing {
    static constexpr size_t Capacity = 1024;
    std::array<int64_t, Capacity> stamps{};
    size_t next = 0, count = 0;
    void Clear() { next = count = 0; }
    void Push(int64_t stamp) {
        stamps[next] = stamp;
        next = (next + 1) % Capacity;
        if (count < Capacity) ++count;
    }
    int64_t At(size_t back) const { return stamps[(next + Capacity - 1 - back) % Capacity]; } // 0: the newest
};
static_assert((FrameRing::Capacity & (FrameRing::Capacity - 1)) == 0, "Push and At reduce to a mask");

struct FrameStats { bool valid = false; double fps = 0, averageMs = 0, worstMs = 0; };
// The last second of frames: from the newest one back to the first frame at
// least `window` ticks before it (all of them while fewer cover a second).
// The rate is the frames over that span, the average the span over the
// frames, the worst the longest frame in it.
FrameStats Frames(const FrameRing& ring, int64_t frequency, int64_t window) {
    FrameStats stats;
    if (ring.count < 2 || frequency <= 0 || window <= 0) return stats;
    const int64_t newest = ring.At(0);
    int64_t worst = 0, span = 0;
    size_t frames = 0;
    for (size_t back = 1; back < ring.count; ++back) {
        const int64_t length = ring.At(back - 1) - ring.At(back);
        if (length <= 0) break; // out of order (never with a monotonic counter): the newer frames only
        worst = std::max(worst, length);
        span = newest - ring.At(back);
        ++frames;
        if (span >= window) break;
    }
    if (frames == 0) return stats;
    const double seconds = static_cast<double>(span) / static_cast<double>(frequency);
    stats.valid = true;
    stats.fps = static_cast<double>(frames) / seconds;
    stats.averageMs = seconds * 1000.0 / static_cast<double>(frames);
    stats.worstMs = static_cast<double>(worst) * 1000.0 / static_cast<double>(frequency);
    return stats;
}

// The process's CPU time (kernel and user, every thread) and the wall clock,
// both in 100 ns units.
struct CpuSample { uint64_t cpu = 0, wall = 0; };
// The process's share of all the logical processors between two samples, in
// percent (Task Manager's figure); -1 when it cannot be told.
double CpuPercent(const CpuSample& from, const CpuSample& to, unsigned processors) {
    if (processors == 0 || to.wall <= from.wall || to.cpu < from.cpu) return -1;
    const double share = static_cast<double>(to.cpu - from.cpu) /
        (static_cast<double>(to.wall - from.wall) * static_cast<double>(processors));
    return std::clamp(share * 100.0, 0.0, 100.0);
}
// The samples of the last two refreshes: each figure covers both (about a
// second at the overlay's pace); the first sample gives none.
struct CpuMeter {
    std::array<CpuSample, 2> kept{};
    size_t count = 0;
    void Clear() { count = 0; }
    double Add(const CpuSample& sample, unsigned processors) {
        const double percent = count > 0 ? CpuPercent(kept[0], sample, processors) : -1;
        if (count < kept.size()) kept[count++] = sample;
        else { kept[0] = kept[1]; kept[1] = sample; }
        return percent;
    }
};
// Performance counter ticks as 100 ns units, without overflow.
uint64_t Hundreds(int64_t counter, int64_t frequency) {
    if (counter <= 0 || frequency <= 0) return 0;
    const auto ticks = static_cast<uint64_t>(counter), rate = static_cast<uint64_t>(frequency);
    return ticks / rate * 10000000ULL + ticks % rate * 10000000ULL / rate;
}

// French figures: a decimal comma, a space before the unit.
std::string Decimal(double value, int decimals) {
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
    std::string text(buffer);
    std::replace(text.begin(), text.end(), '.', ',');
    return text;
}
std::string Rate(double fps) { return Decimal(fps, fps < 9.95 ? 1 : 0); }
std::string Milliseconds(double ms) {
    if (ms < 99.95) return Decimal(ms, 1) + " ms";
    if (ms < 999.5) return Decimal(ms, 0) + " ms";
    return Decimal(ms / 1000.0, 1) + " s";
}
// Windows' units: Mo and Go of 1024.
std::string Bytes(uint64_t bytes) {
    const double mebibytes = static_cast<double>(bytes) / (1024.0 * 1024.0);
    if (mebibytes < 1023.5) return Decimal(mebibytes, 0) + " Mo";
    return Decimal(mebibytes / 1024.0, 2) + " Go";
}

// One refresh's figures; a part that cannot be read is left out of the text.
struct Sample {
    FrameStats frames;
    bool memory = false;
    uint64_t workingSet = 0, privateBytes = 0;
    bool heap = false;
    uint64_t heapUsed = 0, heapSize = 0;
    double cpu = -1;
};
constexpr const char* MeasuringText = "FPS : mesure en cours…";
// Three short lines:
//   60 FPS · 16,7 ms (max 33,4 ms)
//   Mémoire 1,52 Go · privée 1,31 Go
//   Tas géré 312 Mo / 420 Mo · CPU 12 %
std::string Text(const Sample& sample) {
    std::string text = sample.frames.valid ? Rate(sample.frames.fps) + " FPS · " + Milliseconds(sample.frames.averageMs) +
        " (max " + Milliseconds(sample.frames.worstMs) + ")" : std::string(MeasuringText);
    if (sample.memory) text += "\nMémoire " + Bytes(sample.workingSet) + " · privée " + Bytes(sample.privateBytes);
    std::string last;
    if (sample.heap) last = "Tas géré " + Bytes(sample.heapUsed) + " / " + Bytes(sample.heapSize);
    if (sample.cpu >= 0) last += (last.empty() ? "CPU " : " · CPU ") + Decimal(sample.cpu, 0) + " %";
    if (!last.empty()) text += "\n" + last;
    return text;
}

// settings.perfOverlayCorner: 0 top left, 1 top right, 2 bottom left, 3
// bottom right. Panel pixels, so the game's interface scale applies. At the
// top right the overlay goes under the game's menu buttons (RootPopup, 41 px
// high, drawn above it).
constexpr float EdgeMargin = 8, MenuClearance = 48;
struct Placement { bool right = false, bottom = false; float horizontal = EdgeMargin, vertical = EdgeMargin; };
Placement Place(int corner) {
    Placement placement;
    placement.right = corner == 1 || corner == 3;
    placement.bottom = corner == 2 || corner == 3;
    if (corner == 1) placement.vertical = MenuClearance;
    return placement;
}

// =================================================================== natives
using namespace managed;
constexpr const char* UiImage = "UnityEngine.UIElementsModule.dll";
constexpr const char* UiSpace = "UnityEngine.UIElements";
constexpr const char* OwnedName = "dofus_miniatures_perf_overlay";
// gmo builds the named native layers in draw order: RootWorld, RootLow (HUD),
// RootMedium (windows), RootHigh, RootTop (notifications), RootMenu,
// RootPopup, RootTooltip, RootLoadingScreen, RootCursor. First in RootHigh:
// above the HUD and the windows, under every game element of RootHigh and
// under menus, popups, tooltips and loading screens; hidden with the HUD when
// the game hides its interface (gmo.blkk hides RootWorld to RootHigh).
constexpr const char* LayerName = "RootHigh";
constexpr uint64_t RefreshMs = 400;   // every second pump: the text changes about twice a second
constexpr uint64_t CheckMs = 2000;    // the whole layer chain checked again
constexpr uint64_t RetryMs = 5000;    // after an error
// MaxFailures errors within FailureWindowMs: the overlay gives up (a success in
// between does not count: an error at each first refresh would blink forever).
constexpr unsigned MaxFailures = 3;
constexpr uint64_t FailureWindowMs = 300000;
// A label the game does not let go (Restore runs every frame once stopped):
// tried again twice a second, forgotten after ten seconds, as the options page.
constexpr uint64_t CleanupRetryMs = 500, CleanupGiveUpMs = 10000;
constexpr float PaddingX = 8, PaddingY = 4, Radius = 6, FontPixels = 12;
struct Rgba { float r, g, b, a; };
constexpr Rgba BackgroundTint{0.f, 0.f, 0.f, .55f}, TextTint{.93f, .93f, .93f, 1.f};
// WhiteSpace.Pre: lines broken at the text's newlines only (TextUtilities.
// toTextWrappingMode: PreserveWhitespaceNoWrap); Normal and NoWrap collapse
// whitespace.
constexpr int PositionAbsolute = 1, PickingIgnore = 1, WhiteSpacePre = 2, PixelUnit = 0;

namespace prop {
enum Property : size_t {
    Position, Left, Top, Right, Bottom, PaddingLeft, PaddingRight, PaddingTop, PaddingBottom,
    RadiusTopLeft, RadiusTopRight, RadiusBottomLeft, RadiusBottomRight, Background, Color, FontSize, WhiteSpace, MinWidth, Count
};
}
using prop::Property;
struct StyleContract { const char* name; uintptr_t get, set; };
// InlineStyleAccess's IStyle accessors (pinned 3.7.12.12 snapshot), in Property order.
constexpr std::array<StyleContract, prop::Count> StyleContracts{{
    {"position", 0x55D1A30, 0x55D48B0}, {"left", 0x55D1610, 0x55D3EB0}, {"top", 0x55D1CD0, 0x55D4F70},
    {"right", 0x55D1AC0, 0x55D4960}, {"bottom", 0x55D1190, 0x55D3530},
    {"paddingLeft", 0x55D1970, 0x55D46D0}, {"paddingRight", 0x55D19B0, 0x55D4770},
    {"paddingTop", 0x55D19F0, 0x55D4810}, {"paddingBottom", 0x55D1930, 0x55D4630},
    {"borderTopLeftRadius", 0x55D1100, 0x55D33E0}, {"borderTopRightRadius", 0x55D1140, 0x55D3440},
    {"borderBottomLeftRadius", 0x55D0F90, 0x55D3050}, {"borderBottomRightRadius", 0x55D0FD0, 0x55D30B0},
    {"backgroundColor", 0x55D0D90, 0x55D2BA0}, {"color", 0x55D11D0, 0x55D35D0},
    {"fontSize", 0x55D1500, 0x55D3D00}, {"whiteSpace", 0x55D26B0, 0x55D6370}, {"minWidth", 0x55D1850, 0x55D4410}}};
// il2cpp_gc_get_used_size / il2cpp_gc_get_heap_size: thunks to Boehm's
// counters (GC_get_heap_size, minus GC_get_free_bytes for the used part),
// plain reads without the GC lock.
constexpr uintptr_t GcUsedExport = 0x5B9230, GcHeapExport = 0x5B9210;
// The code behind them, guarded too: GetUsedHeapSize (0x5FE340), the heap
// size thunk (0x5FDB20), GC_get_heap_size (0x6194D0), GC_get_free_bytes (0x6194B0).
constexpr std::array<uintptr_t, 4> GcBodies{0x5FE340, 0x5FDB20, 0x6194D0, 0x6194B0};
using GcSize = int64_t (*)();

struct Natives {
    Klass element{}, label{}, length{}, style{}, layerService{}, layerHost{};
    Method parent{}, panel{}, name{}, hierarchy{}, insert{}, remove{}, picking{}, setName{}, getStyle{}, layout{};
    Method childCount{}, childAt{}, focusable{}, labelCtor{}, setText{}, richText{}, documentRoot{};
    std::array<Method, prop::Count> get{}, set{};
    GcSize gcUsed = nullptr, gcHeap = nullptr;
} n;

// Windows' clocks and counters; the tests replace them.
int64_t Counter() { LARGE_INTEGER value{}; QueryPerformanceCounter(&value); return value.QuadPart; }
int64_t CounterFrequency() { LARGE_INTEGER value{}; return QueryPerformanceFrequency(&value) ? value.QuadPart : 0; }
uint64_t TickMilliseconds() { return GetTickCount64(); }
unsigned LogicalProcessors() {
    const DWORD count = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    if (count) return count;
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    return info.dwNumberOfProcessors;
}
// Working set (the RAM the game occupies) and private bytes (its commit).
bool ProcessMemory(uint64_t& workingSet, uint64_t& privateBytes) {
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (!K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters))) return false;
    workingSet = counters.WorkingSetSize;
    privateBytes = counters.PrivateUsage;
    return true;
}
bool ProcessTime(uint64_t& cpuTime) {
    FILETIME creation{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &creation, &exited, &kernel, &user)) return false;
    const auto value = [](const FILETIME& t) { return (static_cast<uint64_t>(t.dwHighDateTime) << 32) | t.dwLowDateTime; };
    cpuTime = value(kernel) + value(user);
    return true;
}
FARPROC GameExport(const char* name) { return GetProcAddress(reinterpret_cast<HMODULE>(gameBase), name); }
struct System {
    int64_t (*counter)() = Counter;
    int64_t (*frequency)() = CounterFrequency;
    uint64_t (*milliseconds)() = TickMilliseconds;
    unsigned (*processors)() = LogicalProcessors;
    bool (*memory)(uint64_t&, uint64_t&) = ProcessMemory;
    bool (*cpu)(uint64_t&) = ProcessTime;
    FARPROC (*lookup)(const char*) = GameExport;
} sys;

// The overlay: one game label, owned until removed.
struct Ui {
    Ref label, layer, compositor;
    int corner = 0;
    std::string text;
    float minWidth = 0; // the widest it has been laid out (panel pixels)
    uint64_t nextCheck = 0, nextRefresh = 0;
};
struct Throttle { std::string last; uint64_t until = 0; };
bool installed = false, sampling = false, broken = false;
int64_t frequency = 0;
unsigned processors = 1, failures = 0, cleanupFailures = 0;
uint64_t retryAt = 0, firstFailureAt = 0, cleanupSince = 0, cleanupRetryAt = 0;
FrameRing ring;
CpuMeter cpuMeter;
Ui ui;
Throttle readyLog, removedLog, waitingLog, errorLog;

// The same line at most once per interval; another line at once.
void Report(Throttle& throttle, const std::string& line, uint64_t now, uint64_t interval = 60000) {
    if (line == throttle.last && now < throttle.until) return;
    throttle.last = line;
    throttle.until = now + interval;
    Log(line);
}
bool Session() {
    return installed && module && module->ready && module->servicesReady && !module->stopped && !module->stopRequested &&
        module->compositor.get();
}
void RequireField(Klass type, const char* name, size_t offset) {
    auto field = api.class_get_field_from_name(type, name);
    if (!field || api.field_get_offset(field) != offset) throw std::runtime_error(std::string("Performance overlay field mismatch: ") + name);
}
// A game export, only at its pinned address and with its pinned bytes.
GcSize Export(const char* name, uintptr_t rva) {
    const auto address = reinterpret_cast<uintptr_t>(sys.lookup(name));
    if (!address || address != gameBase + rva) throw std::runtime_error(std::string("Unexpected IL2CPP export ") + name);
    return reinterpret_cast<GcSize>(api.NativeAt(rva));
}
void Install() {
    frequency = sys.frequency();
    if (frequency <= 0) throw std::runtime_error("No performance counter");
    processors = std::max(1u, sys.processors());
    n.element = api.Class(UiImage, UiSpace, "VisualElement");
    n.parent = api.FindVerified(n.element, "get_parent", 0, 0x55F7180);
    n.panel = api.FindVerified(n.element, "get_panel", 0, 0x55F7170);
    n.name = api.FindVerified(n.element, "get_name", 0, 0x3158710);
    n.hierarchy = api.FindVerified(n.element, "get_hierarchy", 0, 0x185C4A0);
    n.insert = api.FindVerified(n.element, "Insert", 2, 0x55E5710);
    n.remove = api.FindVerified(n.element, "RemoveFromHierarchy", 0, 0x55E7BD0);
    n.picking = api.FindVerified(n.element, "set_pickingMode", 1, 0x55F97B0);
    n.setName = api.FindVerified(n.element, "set_name", 1, 0x55F9670);
    n.getStyle = api.FindVerified(n.element, "get_style", 0, 0x55F74A0);
    n.layout = api.FindVerified(n.element, "get_layout", 0, 0x55F6930);
    const auto hierarchy = api.NestedClass(n.element, "Hierarchy");
    n.childCount = api.FindVerified(hierarchy, "get_childCount", 0, 0x55FFA80);
    n.childAt = api.FindVerified(hierarchy, "get_Item", 1, 0x55FE670);
    n.focusable = api.FindVerified(api.Class(UiImage, UiSpace, "Focusable"), "set_focusable", 1, 0x5777EF0);
    n.label = api.Class(UiImage, UiSpace, "Label");
    n.labelCtor = api.FindVerified(n.label, ".ctor", 0, 0x5729760);
    const auto text = api.Class(UiImage, UiSpace, "TextElement");
    n.setText = api.FindVerified(text, "set_text", 1, 0x564B780);
    n.richText = api.FindVerified(text, "set_enableRichText", 1, 0x564B1E0);
    n.style = api.Class(UiImage, UiSpace, "InlineStyleAccess");
    const std::string accessor = "UnityEngine.UIElements.IStyle.";
    for (size_t i = 0; i < prop::Count; ++i) {
        n.get[i] = api.FindVerified(n.style, (accessor + "get_" + StyleContracts[i].name).c_str(), 0, StyleContracts[i].get);
        n.set[i] = api.FindVerified(n.style, (accessor + "set_" + StyleContracts[i].name).c_str(), 1, StyleContracts[i].set);
    }
    n.length = api.Class(UiImage, UiSpace, "Length");
    api.FindVerified(n.length, ".ctor", 2, 0x561D3D0);
    // The UIDocument root, the layer service and its host (soul::GetUiRoot's chain).
    n.documentRoot = api.FindVerified(api.Class("Core.dll", "", "pq"), "pbs", 0, 0x2E93CC0);
    n.layerService = api.Class("Core.dll", "", "gmo");
    n.layerHost = api.Class("Core.dll", "", "gmy");
    RequireField(n.layerService, "dzrv", 0x70);
    // The managed heap is optional: without it, the rest of the overlay.
    try {
        const auto used = Export("il2cpp_gc_get_used_size", GcUsedExport);
        const auto heap = Export("il2cpp_gc_get_heap_size", GcHeapExport);
        for (const auto body : GcBodies) api.NativeAt(body);
        n.gcUsed = used;
        n.gcHeap = heap;
    } catch (const std::exception& error) {
        n.gcUsed = n.gcHeap = nullptr;
        Log(std::string("PERF_OVERLAY_HEAP_UNAVAILABLE ") + error.what());
    }
}

// ----------------------------------------------------------------- figures
void StartSampling() {
    ring.Clear();
    cpuMeter.Clear();
    sampling = true;
}
Sample Measure() {
    Sample sample;
    sample.frames = Frames(ring, frequency, frequency);
    sample.memory = sys.memory(sample.workingSet, sample.privateBytes);
    if (n.gcUsed && n.gcHeap) {
        const int64_t used = n.gcUsed(), size = n.gcHeap();
        if (size > 0 && used >= 0 && used <= size) {
            sample.heap = true;
            sample.heapUsed = static_cast<uint64_t>(used);
            sample.heapSize = static_cast<uint64_t>(size);
        }
    }
    uint64_t cpuTime = 0;
    if (sys.cpu(cpuTime)) sample.cpu = cpuMeter.Add({cpuTime, Hundreds(sys.counter(), frequency)}, processors);
    return sample;
}

// --------------------------------------------------------------- the label
Obj StyleOf(Obj element) {
    Obj style = api.Invoke(n.getStyle, element);
    if (!style || api.object_get_class(style) != n.style) throw std::runtime_error("Performance overlay inline style unavailable");
    return style;
}
void Write(Obj style, Property property, void* value) {
    Ref box(api.Invoke(n.get[property], style));
    if (!box.get() || !api.class_is_valuetype(api.object_get_class(box.get()))) throw std::runtime_error("Performance overlay expected a boxed style");
    api.Call(box.get(), "set_value", {value});
    api.Invoke(n.set[property], style, {api.object_unbox(box.get())});
}
void Pixels(Obj style, Property property, float pixels) {
    int unit = PixelUnit;
    Ref length(api.New(n.length, {&pixels, &unit}));
    Write(style, property, api.object_unbox(length.get()));
}
void Paint(Obj style, Property property, Rgba color) { Write(style, property, &color); }
void Enum(Obj style, Property property, int value) { Write(style, property, &value); }
void SetText(Obj label, const std::string& text) {
    Ref value(api.string_new(text.c_str()));
    api.Invoke(n.setText, label, {value.get()});
}

// The layer chain, checked link by link; a missing link means "wait" (the
// interface is not built yet or is being replaced: loading screens).
struct Layer { Ref document, service, host, layer; const char* missing = nullptr; };
Layer FindLayer() {
    Layer found;
    // pq.pbs caches the UIDocument root once found; before that, without a
    // UIDocument, it throws (NullReferenceException): no interface yet, wait.
    try { found.document.reset(api.Invoke(n.documentRoot, nullptr)); }
    catch (const std::exception&) { found.document.reset(); }
    if (!found.document.get()) { found.missing = "document"; return found; }
    found.service.reset(module->FindService("fqp"));
    if (!found.service.get() || api.object_get_class(found.service.get()) != n.layerService) { found.missing = "layer-service"; return found; }
    found.host.reset(api.Field(found.service.get(), "dzrv"));
    if (!found.host.get() || api.object_get_class(found.host.get()) != n.layerHost ||
        api.Invoke(n.parent, found.host.get()) != found.document.get()) { found.missing = "layer-host"; return found; }
    Ref children(api.Invoke(n.hierarchy, found.host.get()));
    if (!children.get()) { found.missing = "layers"; return found; }
    void* list = api.object_unbox(children.get());
    const int count = api.Value<int>(api.Invoke(n.childCount, list));
    if (count < 0 || count > 64) { found.missing = "layers"; return found; }
    for (int i = 0; i < count; ++i) {
        Ref child(api.Invoke(n.childAt, list, {&i}));
        if (!child.get() || api.String(api.Invoke(n.name, child.get())) != LayerName) continue;
        if (found.layer.get() || api.Invoke(n.parent, child.get()) != found.host.get()) {
            found.layer.reset();
            found.missing = "ambiguous-layer";
            return found;
        }
        found.layer.reset(child.get());
    }
    if (!found.layer.get()) found.missing = "layer";
    else if (!api.Invoke(n.panel, found.layer.get())) { found.layer.reset(); found.missing = "panel"; }
    return found;
}
// The label, styled and filled before it enters the game's layer (first
// child: under the game's own RootHigh elements). Owned before the Insert, so
// that a failing Insert is still undone.
void Create(Layer& found, int corner, uint64_t now) {
    Ref label(api.object_new(n.label));
    if (!label.get()) throw std::runtime_error("Performance overlay allocation failed");
    api.Invoke(n.labelCtor, label.get());
    Ref name(api.string_new(OwnedName));
    api.Invoke(n.setName, label.get(), {name.get()});
    int picking = PickingIgnore;
    api.Invoke(n.picking, label.get(), {&picking}); // never a click or a hover
    bool no = false;
    api.Invoke(n.focusable, label.get(), {&no});    // never the keyboard focus
    api.Invoke(n.richText, label.get(), {&no});
    Ref style(StyleOf(label.get()));
    const auto placement = Place(corner);
    Enum(style.get(), prop::Position, PositionAbsolute);
    Pixels(style.get(), placement.right ? prop::Right : prop::Left, placement.horizontal);
    Pixels(style.get(), placement.bottom ? prop::Bottom : prop::Top, placement.vertical);
    Pixels(style.get(), prop::PaddingLeft, PaddingX);
    Pixels(style.get(), prop::PaddingRight, PaddingX);
    Pixels(style.get(), prop::PaddingTop, PaddingY);
    Pixels(style.get(), prop::PaddingBottom, PaddingY);
    for (auto radius : {prop::RadiusTopLeft, prop::RadiusTopRight, prop::RadiusBottomLeft, prop::RadiusBottomRight})
        Pixels(style.get(), radius, Radius);
    Paint(style.get(), prop::Background, BackgroundTint);
    Paint(style.get(), prop::Color, TextTint);
    Pixels(style.get(), prop::FontSize, FontPixels);
    Enum(style.get(), prop::WhiteSpace, WhiteSpacePre);
    const auto text = Text(Measure());
    SetText(label.get(), text);
    ui = Ui{};
    ui.label = std::move(label);
    ui.layer = std::move(found.layer);
    ui.compositor.reset(module->compositor.get());
    ui.corner = corner;
    ui.text = text;
    ui.nextCheck = now + CheckMs;
    ui.nextRefresh = now + RefreshMs;
    int first = 0;
    api.Invoke(n.insert, ui.layer.get(), {&first, ui.label.get()});
    Report(readyLog, "PERF_OVERLAY_READY layer=RootHigh corner=" + std::to_string(corner) + " heap=" + (n.gcUsed ? "1" : "0") +
        " processors=" + std::to_string(processors), now);
}
// Still in its layer, shown, in this session, in the corner chosen; every two
// seconds its layer is still the one the whole chain leads to (the game may
// rebuild its interface).
bool Current(int corner, uint64_t now) {
    if (ui.corner != corner || ui.compositor.get() != module->compositor.get()) return false;
    if (api.Invoke(n.parent, ui.label.get()) != ui.layer.get() || !api.Invoke(n.panel, ui.label.get())) return false;
    if (now < ui.nextCheck) return true;
    ui.nextCheck = now + CheckMs;
    return FindLayer().layer.get() == ui.layer.get();
}
// Takes the label out of the game's layer (when still in it) and forgets it;
// on a failure everything is kept for another try. The CPU samples go with
// it: the next label's first figure never spans the time without one.
void RemoveUi() {
    cpuMeter.Clear();
    if (ui.label.get() && api.Invoke(n.parent, ui.label.get())) api.Invoke(n.remove, ui.label.get());
    ui = Ui{};
}
// The label only ever widens, to the width its text was last laid out at:
// its free edge (the left one in the right corners) stays still while the
// figures change width.
void Widen() {
    struct Rect { float x, y, width, height; };
    const auto box = api.Value<Rect>(api.Invoke(n.layout, ui.label.get()));
    if (!std::isfinite(box.width) || box.width > 4096 || box.width <= ui.minWidth + .5f) return;
    ui.minWidth = std::ceil(box.width);
    Ref style(StyleOf(ui.label.get()));
    Pixels(style.get(), prop::MinWidth, ui.minWidth);
}
void Refresh(uint64_t now) {
    ui.nextRefresh = now + RefreshMs;
    Widen();
    const auto text = Text(Measure());
    if (text == ui.text) return;
    SetText(ui.label.get(), text);
    ui.text = text;
}
// Never throws: stop, logout and the option turned off. A label the game does
// not let go is tried again every CleanupRetryMs (Restore runs every frame
// once the mod stops) and forgotten CleanupGiveUpMs after the first failure.
void RestoreAll(uint64_t now) {
    sampling = false;
    ring.Clear();
    cpuMeter.Clear();
    if (!ui.label.get()) { ui = Ui{}; cleanupFailures = 0; return; }
    if (cleanupFailures > 0 && now < cleanupRetryAt) return;
    try {
        RemoveUi();
        cleanupFailures = 0;
    } catch (const std::exception& error) {
        if (cleanupFailures++ == 0) {
            cleanupSince = now;
            Log(std::string("PERF_OVERLAY_CLEANUP_ERROR ") + error.what());
        }
        if (now - cleanupSince >= CleanupGiveUpMs) {
            Log(std::string("PERF_OVERLAY_CLEANUP_ABANDONED ") + error.what());
            ui = Ui{};
            cleanupFailures = 0;
        } else {
            cleanupRetryAt = now + CleanupRetryMs;
        }
    }
}
void Tick(uint64_t now) {
    const bool option = module && module->settings.perfOverlay;
    if (!option) { broken = false; failures = 0; retryAt = 0; } // checking the box again tries again
    if (!Session() || !option || broken) {
        if (ui.label.get()) Report(removedLog, std::string("PERF_OVERLAY_REMOVED ") + (!option ? "option-off" : broken ? "errors" : "session"), now);
        RestoreAll(now);
        return;
    }
    if (!sampling) StartSampling();
    if (now < retryAt) return;
    try {
        const int setting = module->settings.perfOverlayCorner;
        const int corner = Settings::ValidPerfOverlayCorner(setting) ? setting : 0;
        if (ui.label.get() && !Current(corner, now)) {
            Report(removedLog, std::string("PERF_OVERLAY_REMOVED ") + (ui.corner != corner ? "corner" : "replaced"), now);
            RemoveUi();
        }
        if (!ui.label.get()) {
            auto found = FindLayer();
            if (!found.layer.get()) {
                cpuMeter.Clear();
                Report(waitingLog, std::string("PERF_OVERLAY_WAITING ") + found.missing, now, 30000);
                return;
            }
            Create(found, corner, now);
        } else if (now >= ui.nextRefresh) {
            Refresh(now);
        }
    } catch (const std::exception& error) {
        Report(errorLog, std::string("PERF_OVERLAY_ERROR ") + error.what(), now);
        try { RemoveUi(); } catch (...) { /* RestoreAll tries again */ }
        retryAt = now + RetryMs;
        if (failures == 0 || now - firstFailureAt > FailureWindowMs) { failures = 0; firstFailureAt = now; }
        if (++failures >= MaxFailures) {
            broken = true;
            Log("PERF_OVERLAY_DISABLED repeated errors; unchecking and checking the option tries again");
            RestoreAll(now);
        }
    }
}
}

void InstallPerfOverlay() {
    using namespace perfOverlay;
    if (installed) return;
    // Optional: a contract mismatch disables only the overlay, never the mod.
    try { Install(); }
    catch (const std::exception& error) { Log(std::string("PERF_OVERLAY_DISABLED ") + error.what()); return; }
    installed = true;
}
void TickPerfOverlay() {
    using namespace perfOverlay;
    if (!installed) return;
    Tick(sys.milliseconds());
}
// Every frame of the main compositor: one counter read while the option is on.
void PerfOverlayFrame(unsigned long long) {
    using namespace perfOverlay;
    if (!sampling) return;
    ring.Push(sys.counter());
}
void RestorePerfOverlay() {
    using namespace perfOverlay;
    if (!installed) return;
    RestoreAll(sys.milliseconds());
}
bool PerfOverlayCleanupPending() { return perfOverlay::installed && perfOverlay::ui.label.get() != nullptr; }
