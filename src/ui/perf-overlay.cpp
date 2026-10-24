#include "ui/perf-overlay.hpp"
#include "core/module.hpp"
#include "ui/options-ui.hpp"
#include "ui/tools-ui.hpp"
#include <psapi.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cwctype>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace perfOverlay {
// ===================================================================== model
// No game access in this part: frame times, CPU share, memory figures, the
// graphics card and disk figures, the parts shown, the overlay's text, its
// corner, its mouse placement and its size (perf-overlay-tests covers them).

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
// Transfer rates in the same units: whole Ko/s, then Mo/s and Go/s with a
// decimal below 10.
std::string Throughput(double bytesPerSecond) {
    const double kibibytes = std::max(0.0, bytesPerSecond) / 1024.0;
    if (kibibytes < 1023.5) return Decimal(kibibytes, 0) + " Ko/s";
    const double mebibytes = kibibytes / 1024.0;
    if (mebibytes < 1023.5) return Decimal(mebibytes, mebibytes < 9.95 ? 1 : 0) + " Mo/s";
    const double gibibytes = mebibytes / 1024.0;
    return Decimal(gibibytes, gibibytes < 9.95 ? 1 : 0) + " Go/s";
}

// ------------------------------------------------- graphics card and disk
// Windows' performance counters (PDH), read by the device worker: each
// instance of a counter with its value, valid or not (a rate needs two
// collections: the first one gives no value).
struct CounterItem { std::wstring name; double value = 0; bool valid = false; };
using CounterItems = std::vector<CounterItem>;

// "GPU Engine" instances are pid_<pid>_luid_<adapter>_phys_<n>_eng_<n>_engtype_<type>,
// some types with spaces ("3D", "Copy", "Video Decode", "High Priority 3D").
// The type of one of this process's engines; empty for another process
// (pid_12_ is not pid_123_) or another name.
std::wstring EngineType(const std::wstring& instance, unsigned pid) {
    const std::wstring prefix = L"pid_" + std::to_wstring(pid) + L"_", marker = L"_engtype_";
    if (instance.compare(0, prefix.size(), prefix) != 0) return {};
    const auto at = instance.find(marker, prefix.size() - 1);
    return at == std::wstring::npos ? std::wstring() : instance.substr(at + marker.size());
}
// The game's share of the graphics card, as the Task Manager's GPU column:
// the utilization of its busiest engine, as a whole percentage. Each instance
// is one engine of one adapter: two Copy engines, or the 3D engines of two
// adapters, are never added up. -1 without a valid value of this process (the
// first sample, or no engine of its own).
int GpuPercent(const CounterItems& engines, unsigned pid) {
    double busiest = -1;
    for (const auto& item : engines)
        if (item.valid && std::isfinite(item.value) && !EngineType(item.name, pid).empty()) busiest = std::max(busiest, std::max(0.0, item.value));
    return busiest < 0 ? -1 : static_cast<int>(std::lround(std::min(busiest, 100.0)));
}
// The drive letter of the game's path ("C:\…", "\\?\C:\…"), upper case; 0
// for a network path.
wchar_t DriveOf(const std::wstring& path) {
    std::wstring_view rest(path);
    if (rest.starts_with(L"\\\\?\\")) rest.remove_prefix(4);
    if (rest.size() < 2 || rest[1] != L':') return 0;
    const auto letter = static_cast<wchar_t>(std::towupper(rest[0]));
    return letter >= L'A' && letter <= L'Z' ? letter : 0;
}
// "PhysicalDisk" instances: the disk's number, then the letters of its
// volumes ("0 C:", "1 D: E:"); "_Total" holds none.
bool HoldsDrive(const std::wstring& instance, wchar_t drive) {
    if (drive < L'A' || drive > L'Z') return false;
    for (size_t space = instance.find(L' '); space != std::wstring::npos;) {
        const size_t start = space + 1;
        space = instance.find(L' ', start);
        const auto token = instance.substr(start, space == std::wstring::npos ? std::wstring::npos : space - start);
        if (token.size() == 2 && token[1] == L':' && static_cast<wchar_t>(std::towupper(token[0])) == drive) return true;
    }
    return false;
}
// The physical disk holding the game: its active time (100 − % Idle Time,
// the Task Manager's) and its transfer rate (the bytes every program reads
// and writes on it each second); -1 for a figure without a valid value.
struct DiskFigures { int busy = -1; double rate = -1; };
DiskFigures Disk(const CounterItems& idle, const CounterItems& bytes, wchar_t drive) {
    DiskFigures figures;
    for (const auto& item : idle)
        if (item.valid && std::isfinite(item.value) && HoldsDrive(item.name, drive)) {
            figures.busy = static_cast<int>(std::lround(std::clamp(100.0 - item.value, 0.0, 100.0)));
            break;
        }
    for (const auto& item : bytes)
        if (item.valid && std::isfinite(item.value) && item.value >= 0 && HoldsDrive(item.name, drive)) {
            figures.rate = item.value;
            break;
        }
    return figures;
}

// The overlay's parts, each its own box: the frame rate and times, the memory,
// the managed heap and the CPU share (perfOverlay), the graphics card
// (perfOverlayGpu), the disk (perfOverlayDisk). The overlay shows with any of
// them, those only.
struct Parts {
    bool fps = false, gpu = false, disk = false;
    bool Any() const { return fps || gpu || disk; }
    bool operator==(const Parts&) const = default;
};
// One refresh's figures, of the parts shown only; a figure that cannot be
// read is left out of the text.
struct Sample {
    bool fps = false;      // perfOverlay: the frame figures, the memory, the managed heap and the CPU share below
    FrameStats frames;
    bool memory = false;
    uint64_t workingSet = 0, privateBytes = 0;
    bool heap = false;
    uint64_t heapUsed = 0, heapSize = 0;
    double cpu = -1;
    int gpu = -1;          // perfOverlayGpu
    wchar_t drive = 0;     // perfOverlayDisk: the game's drive, its disk's active time and transfer rate
    int diskBusy = -1;
    double diskRate = -1;
    bool waiting = false;  // the GPU or the disk shown without a figure yet, rather than without one to give
};
constexpr const char* MeasuringText = "FPS : mesure en cours…";
// Without the frame rate, the text while nothing else is shown: the first
// figures of the GPU or the disk to come, or none to come (Windows refusing
// them).
constexpr const char* WaitingText = "Mesure en cours…";
constexpr const char* UnavailableText = "Mesure indisponible";
// Three short lines with the frame rate, then one for the graphics card and
// the disk (alone without the frame rate):
//   60 FPS · 16,7 ms (max 33,4 ms)
//   Mémoire 1,52 Go · privée 1,31 Go
//   Tas géré 312 Mo / 420 Mo · CPU 12 %
//   GPU 23 % · Disque C: 4 % · 1,2 Mo/s
std::string Text(const Sample& sample) {
    std::string text;
    if (sample.fps) {
        text = sample.frames.valid ? Rate(sample.frames.fps) + " FPS · " + Milliseconds(sample.frames.averageMs) +
            " (max " + Milliseconds(sample.frames.worstMs) + ")" : std::string(MeasuringText);
        if (sample.memory) text += "\nMémoire " + Bytes(sample.workingSet) + " · privée " + Bytes(sample.privateBytes);
        std::string last;
        if (sample.heap) last = "Tas géré " + Bytes(sample.heapUsed) + " / " + Bytes(sample.heapSize);
        if (sample.cpu >= 0) last += (last.empty() ? "CPU " : " · CPU ") + Decimal(sample.cpu, 0) + " %";
        if (!last.empty()) text += "\n" + last;
    }
    std::string deviceLine;
    if (sample.gpu >= 0) deviceLine = "GPU " + std::to_string(sample.gpu) + " %";
    if (sample.drive && (sample.diskBusy >= 0 || sample.diskRate >= 0)) {
        std::string disk = std::string("Disque ") + static_cast<char>(sample.drive) + ":";
        if (sample.diskBusy >= 0) disk += " " + std::to_string(sample.diskBusy) + " %";
        if (sample.diskRate >= 0) disk += (sample.diskBusy >= 0 ? " · " : " ") + Throughput(sample.diskRate);
        deviceLine += (deviceLine.empty() ? "" : " · ") + disk;
    }
    if (!deviceLine.empty()) text += (text.empty() ? "" : "\n") + deviceLine;
    if (text.empty()) text = sample.waiting ? WaitingText : UnavailableText;
    return text;
}
// A text without any figure: it never sets the overlay's width (Widen), the
// figures that follow can be narrower.
bool Placeholder(const std::string& text) { return text == WaitingText || text == UnavailableText; }

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

// The mouse's place (perfOverlayX/Y): on each axis, the overlay's top-left
// edge as ten-thousandths of the free space (the panel minus the overlay), so
// it survives resolution and interface scale changes; whatever the sizes,
// the overlay stays EdgeMargin inside the panel (at the first margin when
// the panel is too small for it).
constexpr int PlaceScale = 10000;
float ClampEdge(float edge, float size, float space) { return std::max(EdgeMargin, std::min(edge, space - size - EdgeMargin)); }
float EdgeOfPlace(int place, float size, float space) {
    const float share = static_cast<float>(std::clamp(place, 0, PlaceScale)) / static_cast<float>(PlaceScale);
    return ClampEdge(share * std::max(0.f, space - size), size, space);
}
int PlaceOfEdge(float edge, float size, float space) {
    const float free = space - size;
    if (!(free > 0) || !std::isfinite(edge)) return 0;
    return static_cast<int>(std::lround(std::clamp(edge / free, 0.f, 1.f) * static_cast<float>(PlaceScale)));
}
// The place the settings ask for: the corner (-1, -1), or the mouse's place.
struct Spot { int x = -1, y = -1; bool operator==(const Spot&) const = default; };
// VisualElement.layout: panel pixels, relative to the parent.
struct Rect { float x, y, width, height; };
// A point of the game window's client area (pixels from its top left) in the
// panel's pixels. Unity shows the panel scaled alike on both axes and
// centred: in a borderless full screen at another aspect ratio than the
// display's, black bars fill the rest, outside the panel.
struct Point { float x, y; };
Point PanelPoint(float clientX, float clientY, float clientWidth, float clientHeight, float panelWidth, float panelHeight) {
    const float scale = std::min(clientWidth / panelWidth, clientHeight / panelHeight);
    return {(clientX - (clientWidth - panelWidth * scale) / 2) / scale, (clientY - (clientHeight - panelHeight * scale) / 2) / scale};
}

// The overlay's size (perfOverlayScale): a percentage of its natural size,
// applied alike to its text, insets, corners and minimum width, so that its
// text, drawn again at its new size, stays crisp. Its grip: its last
// GripPixels towards its bottom-right corner (never more than half of it: the
// rest still moves it), where a press resizes it rather than moves it.
constexpr int BaseScale = 100, MinScale = 50, MaxScale = 300; // Settings::ValidPerfOverlayScale
constexpr float GripPixels = 12;
// A size laid out at `from` percent, at `to` percent.
float Scaled(float size, int from, int to) { return size * static_cast<float>(to) / static_cast<float>(from); }
bool InGrip(const Rect& box, float x, float y) {
    const float grip = std::min({GripPixels, box.width / 2, box.height / 2});
    return x >= box.x + box.width - grip && y >= box.y + box.height - grip;
}

// The overlay dragged with the left button (perfOverlayDrag): a press on it
// (the panel picks the overlay itself, not a game element above it), then
// the cursor moving DragThreshold panel pixels. It follows the cursor,
// keeping the grab offset, inside the margins. Pressed in its grip, it is
// resized instead: its top-left stays put and its bottom-right corner follows
// the cursor (ResizedScale). The release (anywhere), the game's window losing
// the foreground or the interface hidden ends it and saves the place (and
// the size) reached. Something of the game drawn over the point held (the
// loading screen, a popup, the menu buttons) ends it too, back at the last
// place (or size) where the panel picked that point: the overlay is never
// left where it cannot be grabbed again. A click changes nothing.
constexpr float DragThreshold = 4;
struct Drag {
    enum Phase { Idle, Pressed, Moving } phase = Idle;
    bool down = true;            // the button as last seen; true until seen up, so that a press is always seen starting
    bool resize = false;         // pressed in the grip: resized, its top-left kept, rather than moved
    float grabX = 0, grabY = 0;  // the cursor within the overlay at the press, the point held: from its top-left (from its bottom-right corner when resized)
    float pressX = 0, pressY = 0;
    float left = 0, top = 0;     // the overlay's top-left edge at the last move (at the press when resized)
    float seenLeft = 0, seenTop = 0; // its top-left edge when the panel last picked it at the point held
    float width = 0, height = 0, spaceWidth = 0, spaceHeight = 0; // its size and the panel's at the last move
    // Resized: its size and scale at the press, the scale drawn and the one at
    // which the panel last picked the point held.
    float startWidth = 0, startHeight = 0;
    int startScale = BaseScale, scale = BaseScale, seenScale = BaseScale;
};
enum class DragStep { None, Move, Scale, End, Back };
// The point held in an overlay laid out from `edge` over `size`: the grab
// offset, kept inside the overlay (its text may shrink it meanwhile).
float Held(float edge, float size, float grab) { return edge + std::clamp(grab, 0.f, std::max(0.f, size - 1)); }
// The point held in the overlay laid out as `box`: from its top-left, or from
// its bottom-right corner when resized.
Point HeldPoint(const Drag& drag, const Rect& box) {
    if (drag.resize) return {Held(box.x, box.width, box.width - drag.grabX), Held(box.y, box.height, box.height - drag.grabY)};
    return {Held(box.x, box.width, drag.grabX), Held(box.y, box.height, drag.grabY)};
}
// A resize's scale with the cursor at (x, y): the overlay's bottom-right
// corner (at the grab offset) brought closest to the cursor along its
// diagonal, its top-left staying put; whole percents within
// MinScale..MaxScale, never past the panel's margins, as a move keeps it (a
// panel too small for the smallest size: the smallest size).
int ResizedScale(const Drag& drag, float x, float y, float spaceWidth, float spaceHeight) {
    const float width = drag.startWidth, height = drag.startHeight, start = static_cast<float>(drag.startScale);
    if (!(width > 0) || !(height > 0)) return drag.scale;
    const float ratio = ((x + drag.grabX - drag.left) * width + (y + drag.grabY - drag.top) * height) / (width * width + height * height);
    const float fit = std::min((spaceWidth - EdgeMargin - drag.left) / width, (spaceHeight - EdgeMargin - drag.top) / height);
    if (!std::isfinite(ratio) || !std::isfinite(fit)) return drag.scale;
    const float wanted = std::clamp(ratio * start, static_cast<float>(MinScale), static_cast<float>(MaxScale));
    const float most = std::clamp(std::floor(fit * start + .001f), static_cast<float>(MinScale), static_cast<float>(MaxScale));
    return static_cast<int>(std::lround(std::min(wanted, most)));
}
// One frame of a press or a drag: `press` the button going down this frame,
// `located` the cursor at (x, y) in panel pixels with the game's window in
// the foreground and both layouts known, `reached` the panel picking the
// overlay: under the cursor at a press, at the point held afterwards;
// `scale` the overlay's. Move: draw the overlay at (left, top); Scale: draw
// it at `scale`, its top-left at (left, top); End: save that place (and
// scale); Back: the point held covered, draw the overlay back at (left, top),
// where the panel last picked it (or at the scale it had then), and save it.
DragStep Advance(Drag& drag, bool press, bool down, bool located, bool reached, float x, float y, const Rect& box,
    float spaceWidth, float spaceHeight, int scale = BaseScale) {
    switch (drag.phase) {
    case Drag::Idle:
        if (!press || !located || !reached) return DragStep::None;
        drag.phase = Drag::Pressed;
        drag.resize = InGrip(box, x, y);
        drag.grabX = drag.resize ? box.x + box.width - x : x - box.x;
        drag.grabY = drag.resize ? box.y + box.height - y : y - box.y;
        drag.pressX = x;
        drag.pressY = y;
        drag.left = drag.seenLeft = box.x;
        drag.top = drag.seenTop = box.y;
        drag.startWidth = box.width;
        drag.startHeight = box.height;
        drag.startScale = drag.scale = drag.seenScale = scale;
        return DragStep::None;
    case Drag::Pressed:
        if (!down || !located || !reached) { drag.phase = Drag::Idle; return DragStep::None; } // a click, or interrupted: nothing moved
        if (std::hypot(x - drag.pressX, y - drag.pressY) < DragThreshold) return DragStep::None;
        drag.phase = Drag::Moving;
        [[fallthrough]];
    case Drag::Moving:
        // Another window in the foreground or the interface hidden (nothing
        // measured: the sizes of the last move kept), or the release.
        if (!located || (!down && reached)) { drag.phase = Drag::Idle; return DragStep::End; }
        drag.width = box.width;
        drag.height = box.height;
        drag.spaceWidth = spaceWidth;
        drag.spaceHeight = spaceHeight;
        if (!reached) { // the point held covered, released or not
            drag.phase = Drag::Idle;
            if (drag.resize) { drag.scale = drag.seenScale; return DragStep::Back; }
            drag.left = ClampEdge(drag.seenLeft, box.width, spaceWidth);
            drag.top = ClampEdge(drag.seenTop, box.height, spaceHeight);
            return DragStep::Back;
        }
        if (drag.resize) {
            drag.seenScale = drag.scale; // the scale of the layout seen now
            drag.scale = ResizedScale(drag, x, y, spaceWidth, spaceHeight);
            return DragStep::Scale;
        }
        drag.seenLeft = box.x;
        drag.seenTop = box.y;
        drag.left = ClampEdge(x - drag.grabX, box.width, spaceWidth);
        drag.top = ClampEdge(y - drag.grabY, box.height, spaceHeight);
        return DragStep::Move;
    }
    return DragStep::None;
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
constexpr uint64_t SettleMs = 50;     // the mouse's place of a label not laid out yet, tried again
// MaxFailures errors within FailureWindowMs: the overlay gives up (a success in
// between does not count: an error at each first refresh would blink forever).
constexpr unsigned MaxFailures = 3;
constexpr uint64_t FailureWindowMs = 300000;
// A label the game does not let go (Restore runs every frame once stopped):
// tried again twice a second, forgotten after ten seconds, as the options page.
constexpr uint64_t CleanupRetryMs = 500, CleanupGiveUpMs = 10000;
// The device worker: a sample a second; the game's thread waits at most
// JoinMs for it to stop (it then ends by itself after its PDH call).
constexpr DWORD DevicePeriodMs = 1000, JoinMs = 250;
constexpr float PaddingX = 8, PaddingY = 4, Radius = 6, FontPixels = 12;
struct Rgba { float r, g, b, a; };
constexpr Rgba BackgroundTint{0.f, 0.f, 0.f, .55f}, TextTint{.93f, .93f, .93f, 1.f};
// WhiteSpace.Pre: lines broken at the text's newlines only (TextUtilities.
// toTextWrappingMode: PreserveWhitespaceNoWrap); Normal and NoWrap collapse
// whitespace. PickingMode: Position (the mouse placement: the label takes
// its clicks) or Ignore.
constexpr int PositionAbsolute = 1, PickingPosition = 0, PickingIgnore = 1, WhiteSpacePre = 2, PixelUnit = 0;

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
    Klass element{}, label{}, length{}, style{}, layerService{}, layerHost{}, panelType{};
    Method parent{}, panel{}, name{}, hierarchy{}, insert{}, remove{}, picking{}, setName{}, getStyle{}, layout{};
    Method childCount{}, childAt{}, focusable{}, labelCtor{}, setText{}, richText{}, documentRoot{}, pick{};
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
// The left button (the right one when the buttons are swapped: Windows'
// primary button), whatever window has the foreground.
bool LeftButton() { return (GetAsyncKeyState(GetSystemMetrics(SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON) & 0x8000) != 0; }
// The cursor over the game: false unless a window of this process has the
// foreground; then the cursor in its client area (pixels from its top left,
// beyond its edges when outside) and the client area's size.
struct Cursor { long x = 0, y = 0, width = 0, height = 0; };
bool GameCursor(Cursor& cursor) {
    const HWND window = GetForegroundWindow();
    DWORD process = 0;
    if (!window || !GetWindowThreadProcessId(window, &process) || process != GetCurrentProcessId() || IsIconic(window)) return false;
    RECT client{};
    POINT point{};
    if (!GetClientRect(window, &client) || client.right <= 0 || client.bottom <= 0 || !GetCursorPos(&point) || !ScreenToClient(window, &point))
        return false;
    cursor = {point.x, point.y, client.right, client.bottom};
    return true;
}
unsigned ProcessId() { return GetCurrentProcessId(); }
// The game's executable: its drive is the disk shown.
std::wstring GamePath() {
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    return length && length < buffer.size() ? std::wstring(buffer.data(), length) : std::wstring();
}
HANDLE SpawnThread(LPTHREAD_START_ROUTINE routine, void* parameter) { return CreateThread(nullptr, 0, routine, parameter, 0, nullptr); }
// PDH, Windows' performance counters, by their English names: a French
// Windows answers too.
PDH_STATUS CounterOpen(PDH_HQUERY* query) { return PdhOpenQueryW(nullptr, 0, query); }
PDH_STATUS CounterAdd(PDH_HQUERY query, const wchar_t* path, PDH_HCOUNTER* counter) { return PdhAddEnglishCounterW(query, path, 0, counter); }
PDH_STATUS CounterCollect(PDH_HQUERY query) { return PdhCollectQueryData(query); }
PDH_STATUS CounterClose(PDH_HQUERY query) { return PdhCloseQuery(query); }
// Every instance of a wildcard counter, as uncapped doubles, from
// PdhGetFormattedCounterArrayW (`format`; the tests give a fake): the size
// asked first, again when instances came meanwhile, three times at most.
using FormatArray = decltype(&PdhGetFormattedCounterArrayW);
PDH_STATUS ReadArray(FormatArray format, PDH_HCOUNTER counter, CounterItems& items) {
    items.clear();
    std::vector<unsigned char> buffer;
    for (int attempt = 0; attempt < 3; ++attempt) {
        DWORD size = static_cast<DWORD>(buffer.size()), count = 0;
        const PDH_STATUS status = format(counter, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &size, &count,
            buffer.empty() ? nullptr : reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data()));
        if (static_cast<DWORD>(status) == PDH_MORE_DATA) { buffer.assign(size, 0); continue; }
        if (status != ERROR_SUCCESS) return status;
        const auto* values = reinterpret_cast<const PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
        for (DWORD i = 0; values && i < count; ++i) {
            const DWORD state = values[i].FmtValue.CStatus;
            items.push_back({values[i].szName ? values[i].szName : L"", values[i].FmtValue.doubleValue,
                state == static_cast<DWORD>(PDH_CSTATUS_VALID_DATA) || state == static_cast<DWORD>(PDH_CSTATUS_NEW_DATA)});
        }
        return ERROR_SUCCESS;
    }
    return static_cast<PDH_STATUS>(PDH_MORE_DATA);
}
PDH_STATUS CounterRead(PDH_HCOUNTER counter, CounterItems& items) { return ReadArray(PdhGetFormattedCounterArrayW, counter, items); }
struct Pdh {
    PDH_STATUS (*open)(PDH_HQUERY*) = CounterOpen;
    PDH_STATUS (*add)(PDH_HQUERY, const wchar_t*, PDH_HCOUNTER*) = CounterAdd;
    PDH_STATUS (*collect)(PDH_HQUERY) = CounterCollect;
    PDH_STATUS (*read)(PDH_HCOUNTER, CounterItems&) = CounterRead;
    PDH_STATUS (*close)(PDH_HQUERY) = CounterClose;
};
struct System {
    int64_t (*counter)() = Counter;
    int64_t (*frequency)() = CounterFrequency;
    uint64_t (*milliseconds)() = TickMilliseconds;
    unsigned (*processors)() = LogicalProcessors;
    bool (*memory)(uint64_t&, uint64_t&) = ProcessMemory;
    bool (*cpu)(uint64_t&) = ProcessTime;
    FARPROC (*lookup)(const char*) = GameExport;
    bool (*button)() = LeftButton;
    bool (*cursor)(Cursor&) = GameCursor;
    unsigned (*pid)() = ProcessId;
    std::wstring (*gamePath)() = GamePath;
    HANDLE (*spawn)(LPTHREAD_START_ROUTINE, void*) = SpawnThread;
    Pdh pdh;
    DWORD devicePeriod = DevicePeriodMs;
} sys;

// The overlay: one game label, owned until removed.
struct Ui {
    Ref label, layer, compositor;
    int corner = 0, picking = PickingIgnore;
    Parts parts;                         // the parts it shows
    Spot spot;                           // the place applied: its corner, or the mouse's place
    bool pending = false;                // the mouse's place waits for the label's layout
    float horizontal = 0, vertical = 0;  // the values written on its anchored sides (those of its corner)
    int scale = BaseScale;               // the size drawn: perfOverlayScale, or a resize's in progress
    bool rescaled = false;               // drawn at another size since the last frame: still laid out at the size before
    std::string text;
    float minWidth = 0; // the widest it has been laid out, at 100 % (panel pixels): written scaled (MinWidthAt)
    uint64_t nextCheck = 0, nextRefresh = 0, settleAt = 0;
};
struct Throttle { std::string last; uint64_t until = 0; };
// sampling: the overlay on in a usable session (the frame call works);
// timing: the frame times read too (the frame rate shown).
bool installed = false, sampling = false, timing = false, broken = false;
int64_t frequency = 0;
unsigned processors = 1, failures = 0, cleanupFailures = 0;
uint64_t retryAt = 0, firstFailureAt = 0, cleanupSince = 0, cleanupRetryAt = 0;
FrameRing ring;
CpuMeter cpuMeter;
Ui ui;
Drag drag;
float lastWidth = 0, lastHeight = 0; // the last label's laid-out size: a new label's first mouse placement
Throttle readyLog, removedLog, waitingLog, errorLog, dragLog, devicesLog;

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
// The parts ticked in the options.
Parts Ticked() {
    const auto& settings = module->settings;
    return {settings.perfOverlay, settings.perfOverlayGpu, settings.perfOverlayDisk};
}
// The overlay asked for: a part ticked, and not hidden with its top-bar
// button (perfOverlayShown).
bool Asked() { return module->settings.perfOverlayShown && Ticked().Any(); }
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
    // The panel's own pick (IPanel.Pick: Pick(point, mouse pointer), Panel's
    // override): what a press at a point reaches, the mouse placement's test.
    n.panelType = api.Class(UiImage, UiSpace, "BaseVisualElementPanel");
    n.pick = api.FindVerified(n.panelType, "Pick", 1, 0x57A4050);
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

// ------------------------------------------------------- the device worker
// The GPU and disk figures come from PDH, which can take tens of
// milliseconds a call: a worker thread samples them about once a second,
// never the game's thread. It starts while the label is shown with one of
// the two options on, with the counters asked for only, and stops when the
// label goes (hidden, Restore, the mod stopping). It touches nothing of the
// game and shares only `figures`, under `lock` while they are copied.
// What the worker publishes; -1 for a figure not known. A problem (why a
// figure stays missing) is logged once by the game's thread.
struct DeviceFigures {
    int gpu = -1;
    DiskFigures disk;
    std::string gpuProblem, diskProblem;
};
struct DeviceWorker {
    bool gpu = false, disk = false;
    unsigned pid = 0;
    wchar_t drive = 0;
    DWORD period = DevicePeriodMs;
    Pdh pdh;               // the worker's own copy: the tests' fakes, or PDH
    HANDLE stop = nullptr; // manual-reset event, set to stop
    std::mutex lock;
    DeviceFigures figures; // under lock
    DeviceWorker() = default;
    DeviceWorker(const DeviceWorker&) = delete;
    DeviceWorker& operator=(const DeviceWorker&) = delete;
    ~DeviceWorker() { if (stop) CloseHandle(stop); }
};
std::string Code(const char* what, PDH_STATUS status) {
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "%s=%08lX", what, static_cast<unsigned long>(status));
    return buffer;
}
// The worker's PDH query, opened, read and closed on its thread.
struct DeviceQuery {
    const DeviceWorker& worker;
    PDH_HQUERY query = nullptr;
    PDH_HCOUNTER engines = nullptr, idle = nullptr, bytes = nullptr;
    std::string gpuProblem, diskProblem; // the query or a counter refused: for good
    unsigned collections = 0;
    explicit DeviceQuery(const DeviceWorker& owner) : worker(owner) {
        const PDH_STATUS opened = worker.pdh.open(&query);
        if (opened != ERROR_SUCCESS) {
            query = nullptr;
            if (worker.gpu) gpuProblem = Code("query", opened);
            if (worker.disk) diskProblem = Code("query", opened);
            return;
        }
        if (worker.gpu) {
            // This process's engines only (PDH expands the wildcard at each collection).
            const std::wstring path = L"\\GPU Engine(pid_" + std::to_wstring(worker.pid) + L"_*)\\Utilization Percentage";
            const PDH_STATUS status = worker.pdh.add(query, path.c_str(), &engines);
            if (status != ERROR_SUCCESS) { engines = nullptr; gpuProblem = Code("counter", status); }
        }
        if (worker.disk && !worker.drive) diskProblem = "no-drive";
        else if (worker.disk) {
            PDH_STATUS status = worker.pdh.add(query, L"\\PhysicalDisk(*)\\% Idle Time", &idle);
            if (status == ERROR_SUCCESS) status = worker.pdh.add(query, L"\\PhysicalDisk(*)\\Disk Bytes/sec", &bytes);
            if (status != ERROR_SUCCESS) { idle = bytes = nullptr; diskProblem = Code("counter", status); }
        }
    }
    DeviceQuery(const DeviceQuery&) = delete;
    DeviceQuery& operator=(const DeviceQuery&) = delete;
    ~DeviceQuery() { if (query) worker.pdh.close(query); }
    // One collection. The first one gives no rate: a figure still missing is
    // a problem from the second one on.
    DeviceFigures Sample() {
        DeviceFigures figures;
        figures.gpuProblem = gpuProblem;
        figures.diskProblem = diskProblem;
        if (!query || (!engines && !idle)) return figures;
        const PDH_STATUS collected = worker.pdh.collect(query);
        const bool told = ++collections >= 2;
        if (collected != ERROR_SUCCESS) {
            if (told && engines) figures.gpuProblem = Code("collect", collected);
            if (told && idle) figures.diskProblem = Code("collect", collected);
            return figures;
        }
        CounterItems items;
        if (engines) {
            const PDH_STATUS status = worker.pdh.read(engines, items);
            figures.gpu = status == ERROR_SUCCESS ? GpuPercent(items, worker.pid) : -1;
            if (figures.gpu < 0 && told) figures.gpuProblem = status == ERROR_SUCCESS ? std::string("no-instance") : Code("read", status);
        }
        if (idle) {
            CounterItems rates;
            const PDH_STATUS idleStatus = worker.pdh.read(idle, items), rateStatus = worker.pdh.read(bytes, rates);
            if (idleStatus != ERROR_SUCCESS) items.clear();
            if (rateStatus != ERROR_SUCCESS) rates.clear();
            figures.disk = Disk(items, rates, worker.drive);
            if (figures.disk.busy < 0 && figures.disk.rate < 0 && told)
                figures.diskProblem = idleStatus != ERROR_SUCCESS ? Code("read", idleStatus) : rateStatus != ERROR_SUCCESS ?
                    Code("read", rateStatus) : std::string("no-instance ") + static_cast<char>(worker.drive) + ":";
        }
        return figures;
    }
};
// The thread owns a reference to its worker: a worker the game's thread
// stopped waiting for still has it until its last PDH call returns.
DWORD WINAPI DeviceThread(void* parameter) {
    const std::unique_ptr<std::shared_ptr<DeviceWorker>> owner(static_cast<std::shared_ptr<DeviceWorker>*>(parameter));
    DeviceWorker& worker = **owner;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    try {
        DeviceQuery query(worker);
        do {
            DeviceFigures figures = query.Sample();
            const std::lock_guard<std::mutex> guard(worker.lock);
            worker.figures = std::move(figures);
        } while (WaitForSingleObject(worker.stop, worker.period) == WAIT_TIMEOUT);
    } catch (...) {
        // Out of memory only: the figures stay as last published.
    }
    return 0;
}
struct Devices {
    std::shared_ptr<DeviceWorker> worker;
    HANDLE thread = nullptr;
    HANDLE retiring = nullptr; // a worker that did not stop within JoinMs: never a second one before it ends
    uint64_t retryAt = 0;      // after a thread that could not start
} devices;
bool gpuReported = false, diskReported = false;
// Never throws: Restore and the mod stopping call it.
void StopDevices(uint64_t now) {
    if (!devices.thread) { devices.worker.reset(); return; }
    SetEvent(devices.worker->stop);
    if (WaitForSingleObject(devices.thread, JoinMs) == WAIT_OBJECT_0) {
        CloseHandle(devices.thread);
    } else {
        if (devices.retiring) CloseHandle(devices.retiring);
        devices.retiring = devices.thread;
        Report(devicesLog, "PERF_OVERLAY_DEVICES_SLOW the worker ends after its current reading", now);
    }
    devices.thread = nullptr;
    devices.worker.reset();
}
void StartDevices(bool gpu, bool disk, uint64_t now) {
    if (devices.retiring) {
        if (WaitForSingleObject(devices.retiring, 0) != WAIT_OBJECT_0) return; // still in its last PDH call
        CloseHandle(devices.retiring);
        devices.retiring = nullptr;
    }
    auto worker = std::make_shared<DeviceWorker>();
    worker->gpu = gpu;
    worker->disk = disk;
    worker->pid = sys.pid();
    worker->drive = disk ? DriveOf(sys.gamePath()) : 0;
    worker->period = sys.devicePeriod;
    worker->pdh = sys.pdh;
    worker->stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    auto parameter = std::make_unique<std::shared_ptr<DeviceWorker>>(worker);
    const HANDLE thread = worker->stop ? sys.spawn(DeviceThread, parameter.get()) : nullptr;
    if (!thread) {
        const DWORD error = GetLastError();
        devices.retryAt = now + RetryMs;
        Report(devicesLog, "PERF_OVERLAY_DEVICES_ERROR thread " + std::to_string(error), now);
        return;
    }
    parameter.release(); // the thread's now
    devices.worker = std::move(worker);
    devices.thread = thread;
}
// After every pump: the worker runs while the label is shown with the GPU or
// the disk asked for; another choice restarts it with only those counters.
void UpdateDevices(uint64_t now) {
    const bool gpu = module->settings.perfOverlayGpu, disk = module->settings.perfOverlayDisk;
    if (!ui.label.get() || (!gpu && !disk)) { StopDevices(now); return; }
    if (devices.worker && devices.worker->gpu == gpu && devices.worker->disk == disk) return;
    StopDevices(now);
    if (now >= devices.retryAt) StartDevices(gpu, disk, now);
}
// The worker's last figures, those of the parts shown (a choice reaches the
// text at once, the worker at the next pump). A part shown is waiting while
// the worker does not read it yet or has no figure of it yet, not once
// Windows refuses it (its problem).
void ReadDevices(Sample& sample, const Parts& parts) {
    if (!parts.gpu && !parts.disk) return;
    DeviceWorker* const worker = devices.worker.get();
    DeviceFigures figures;
    if (worker) {
        const std::lock_guard<std::mutex> guard(worker->lock);
        figures = worker->figures;
    }
    const bool gpu = parts.gpu && worker && worker->gpu, disk = parts.disk && worker && worker->disk;
    if (gpu) sample.gpu = figures.gpu;
    if (disk) {
        sample.drive = worker->drive;
        sample.diskBusy = figures.disk.busy;
        sample.diskRate = figures.disk.rate;
    }
    if (parts.gpu && (!gpu || (figures.gpu < 0 && figures.gpuProblem.empty()))) sample.waiting = true;
    if (parts.disk && (!disk || (figures.disk.busy < 0 && figures.disk.rate < 0 && figures.diskProblem.empty()))) sample.waiting = true;
    if (!figures.gpuProblem.empty() && !gpuReported) { gpuReported = true; Log("PERF_OVERLAY_GPU_UNAVAILABLE " + figures.gpuProblem); }
    if (!figures.diskProblem.empty() && !diskReported) { diskReported = true; Log("PERF_OVERLAY_DISK_UNAVAILABLE " + figures.diskProblem); }
}

// ----------------------------------------------------------------- figures
void StartSampling() {
    ring.Clear();
    cpuMeter.Clear();
    sampling = true;
}
// The frame times only with the frame rate shown (one counter read a frame),
// from no frame when it is shown again: neither they nor the CPU share span
// the time without it.
void Time(bool on) {
    if (timing == on) return;
    timing = on;
    ring.Clear();
    cpuMeter.Clear();
}
// The figures of the parts shown, read for them only: the frame times, the
// memory, the managed heap and the CPU time with the frame rate; the worker's
// with the GPU or the disk.
Sample Measure(const Parts& parts) {
    Sample sample;
    if (parts.fps) {
        sample.fps = true;
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
    }
    ReadDevices(sample, parts);
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
// The minimum width written at `scale`: the widest text so far, laid out at
// 100 %, scaled alike.
float MinWidthAt(float width, int scale) { return std::ceil(Scaled(width, BaseScale, scale) - .001f); }
// The overlay at `scale`: its text, insets, corners and minimum width alike;
// its bottom-right corner left square while the mouse can move and resize it
// (`grip`), the only mark of its grip.
void StyleScale(Obj style, int scale, bool grip, float minWidth) {
    const float factor = static_cast<float>(scale) / static_cast<float>(BaseScale);
    Pixels(style, prop::PaddingLeft, PaddingX * factor);
    Pixels(style, prop::PaddingRight, PaddingX * factor);
    Pixels(style, prop::PaddingTop, PaddingY * factor);
    Pixels(style, prop::PaddingBottom, PaddingY * factor);
    for (auto radius : {prop::RadiusTopLeft, prop::RadiusTopRight, prop::RadiusBottomLeft})
        Pixels(style, radius, Radius * factor);
    Pixels(style, prop::RadiusBottomRight, grip ? 0.f : Radius * factor);
    Pixels(style, prop::FontSize, FontPixels * factor);
    if (minWidth > 0) Pixels(style, prop::MinWidth, MinWidthAt(minWidth, scale));
}
// The label drawn at `scale`. The game lays it out again after the frame (its
// panels' update comes after the compositor's): until the next frame, its
// layout is that of the size before, which neither widens nor places it.
void DrawAt(int scale) {
    Ref style(StyleOf(ui.label.get()));
    StyleScale(style.get(), scale, ui.picking == PickingPosition, ui.minWidth);
    ui.scale = scale;
    ui.rescaled = true;
}
void Enum(Obj style, Property property, int value) { Write(style, property, &value); }
void SetText(Obj label, const std::string& text) {
    Ref value(api.string_new(text.c_str()));
    api.Invoke(n.setText, label, {value.get()});
}
Rect LayoutOf(Obj element) { return api.Value<Rect>(api.Invoke(n.layout, element)); }
// A layout to place with: none while the game hides its interface (0 or NaN)
// or before the first layout pass.
bool Usable(const Rect& box) {
    return std::isfinite(box.x) && std::isfinite(box.y) && std::isfinite(box.width) && std::isfinite(box.height) &&
        box.width > .5f && box.height > .5f && box.width <= 16384 && box.height <= 16384;
}
void Remember(const Rect& box) { lastWidth = box.width; lastHeight = box.height; }

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
// The place the settings ask for: the mouse's place once one is saved, the
// mouse placement on or off (the overlay then locked there); the corner
// otherwise.
Spot Wanted() {
    const auto& settings = module->settings;
    if (settings.perfOverlayX < 0 || !Settings::ValidPerfOverlayPlace(settings.perfOverlayX, settings.perfOverlayY)) return {};
    return {settings.perfOverlayX, settings.perfOverlayY};
}
// The size the settings ask for: perfOverlayScale, the natural size for
// anything else.
int WantedScale() {
    const int scale = module->settings.perfOverlayScale;
    return Settings::ValidPerfOverlayScale(scale) ? scale : BaseScale;
}
// The values on the label's anchored sides: those of the corner it was
// created in (left or right, top or bottom), whatever its place.
void Anchor(float horizontal, float vertical) {
    const bool moveX = std::fabs(horizontal - ui.horizontal) >= .25f, moveY = std::fabs(vertical - ui.vertical) >= .25f;
    if (!moveX && !moveY) return;
    const auto placement = Place(ui.corner);
    Ref style(StyleOf(ui.label.get()));
    if (moveX) { Pixels(style.get(), placement.right ? prop::Right : prop::Left, horizontal); ui.horizontal = horizontal; }
    if (moveY) { Pixels(style.get(), placement.bottom ? prop::Bottom : prop::Top, vertical); ui.vertical = vertical; }
}
// The label's top-left edge at (left, top) in the panel.
void MoveTo(float left, float top, const Rect& layer, float width, float height) {
    const auto placement = Place(ui.corner);
    Anchor(placement.right ? layer.width - left - width : left, placement.bottom ? layer.height - top - height : top);
}
// The label at the place the settings ask for: its corner's margins, or the
// mouse's place from both layouts (pending until the label has one).
void Settle() {
    const Spot spot = Wanted();
    ui.spot = spot;
    ui.pending = false;
    if (spot.x < 0) {
        const auto placement = Place(ui.corner);
        Anchor(placement.horizontal, placement.vertical);
        return;
    }
    if (ui.rescaled) { ui.pending = true; return; } // laid out at the size before: placed at the next frame
    const Rect layer = LayoutOf(ui.layer.get()), box = LayoutOf(ui.label.get());
    if (!Usable(layer) || !Usable(box)) { ui.pending = true; return; }
    Remember(box);
    MoveTo(EdgeOfPlace(spot.x, box.width, layer.width), EdgeOfPlace(spot.y, box.height, layer.height), layer, box.width, box.height);
}
// The label, styled and filled before it enters the game's layer (first
// child: under the game's own RootHigh elements). Owned before the Insert, so
// that a failing Insert is still undone. With the mouse placement on, it
// takes the clicks on it (they never reach the game); never the keyboard. At
// the size saved (perfOverlayScale).
void Create(Layer& found, int corner, const Parts& parts, uint64_t now) {
    Ref label(api.object_new(n.label));
    if (!label.get()) throw std::runtime_error("Performance overlay allocation failed");
    api.Invoke(n.labelCtor, label.get());
    Ref name(api.string_new(OwnedName));
    api.Invoke(n.setName, label.get(), {name.get()});
    int picking = module->settings.perfOverlayDrag ? PickingPosition : PickingIgnore;
    api.Invoke(n.picking, label.get(), {&picking});
    bool no = false;
    api.Invoke(n.focusable, label.get(), {&no});    // never the keyboard focus
    api.Invoke(n.richText, label.get(), {&no});
    Ref style(StyleOf(label.get()));
    const auto placement = Place(corner);
    const Spot spot = Wanted();
    const int scale = WantedScale();
    float horizontal = placement.horizontal, vertical = placement.vertical;
    if (spot.x >= 0) {
        // The mouse's place, with the last label's size until this one is laid out.
        const Rect layer = LayoutOf(found.layer.get());
        if (Usable(layer)) {
            const float left = EdgeOfPlace(spot.x, lastWidth, layer.width), top = EdgeOfPlace(spot.y, lastHeight, layer.height);
            horizontal = placement.right ? layer.width - left - lastWidth : left;
            vertical = placement.bottom ? layer.height - top - lastHeight : top;
        }
    }
    Enum(style.get(), prop::Position, PositionAbsolute);
    Pixels(style.get(), placement.right ? prop::Right : prop::Left, horizontal);
    Pixels(style.get(), placement.bottom ? prop::Bottom : prop::Top, vertical);
    StyleScale(style.get(), scale, picking == PickingPosition, 0);
    Paint(style.get(), prop::Background, BackgroundTint);
    Paint(style.get(), prop::Color, TextTint);
    Enum(style.get(), prop::WhiteSpace, WhiteSpacePre);
    const auto text = Text(Measure(parts));
    SetText(label.get(), text);
    ui = Ui{};
    ui.label = std::move(label);
    ui.layer = std::move(found.layer);
    ui.compositor.reset(module->compositor.get());
    ui.corner = corner;
    ui.parts = parts;
    ui.picking = picking;
    ui.spot = spot;
    ui.pending = spot.x >= 0;
    ui.horizontal = horizontal;
    ui.vertical = vertical;
    ui.scale = scale;
    ui.text = text;
    ui.nextCheck = now + CheckMs;
    ui.nextRefresh = now + RefreshMs;
    drag = Drag{};
    int first = 0;
    api.Invoke(n.insert, ui.layer.get(), {&first, ui.label.get()});
    Report(readyLog, "PERF_OVERLAY_READY layer=RootHigh corner=" + std::to_string(corner) + " heap=" + (n.gcUsed ? "1" : "0") +
        " processors=" + std::to_string(processors) + " drag=" + (picking == PickingPosition ? "1" : "0") +
        " place=" + std::to_string(spot.x) + "," + std::to_string(spot.y) + " fps=" + (parts.fps ? "1" : "0") +
        " gpu=" + (parts.gpu ? "1" : "0") + " disk=" + (parts.disk ? "1" : "0") + " scale=" + std::to_string(scale), now);
}
// Still in its layer, shown, in this session, in its corner, with the parts
// ticked (another choice: a new label, sized for them from the start); every
// two seconds its layer is still the one the whole chain leads to (the game
// may rebuild its interface).
bool Current(int corner, const Parts& parts, uint64_t now) {
    if (ui.corner != corner || !(ui.parts == parts) || ui.compositor.get() != module->compositor.get()) return false;
    if (api.Invoke(n.parent, ui.label.get()) != ui.layer.get() || !api.Invoke(n.panel, ui.label.get())) return false;
    if (now < ui.nextCheck) return true;
    ui.nextCheck = now + CheckMs;
    return FindLayer().layer.get() == ui.layer.get();
}
// The mouse placement checked or unchecked: the label takes the clicks, or
// lets them through and stays locked at its place and size, its bottom-right
// corner rounded again; a drag or a resize in progress stops (unsaved: back
// at the place and the size saved).
void UpdatePicking() {
    int picking = module->settings.perfOverlayDrag ? PickingPosition : PickingIgnore;
    if (picking == ui.picking) return;
    api.Invoke(n.picking, ui.label.get(), {&picking});
    ui.picking = picking;
    drag = Drag{};
    Ref style(StyleOf(ui.label.get()));
    Pixels(style.get(), prop::RadiusBottomRight, picking == PickingPosition ? 0.f : Scaled(Radius, BaseScale, ui.scale));
}
// Takes the label out of the game's layer (when still in it) and forgets it;
// on a failure everything is kept for another try. The CPU samples go with
// it: the next label's first figure never spans the time without one. A
// drag stops without saving.
void RemoveUi() {
    cpuMeter.Clear();
    drag = Drag{};
    if (ui.label.get() && api.Invoke(n.parent, ui.label.get())) api.Invoke(n.remove, ui.label.get());
    ui = Ui{};
}
// The label only ever widens, to the width its text was last laid out at:
// its free edge (the left one in the right corners) stays still while the
// figures change width. A text without figures (Placeholder) sets none. Kept
// at 100 %: a resize scales it with the rest.
void Widen(const Rect& box) {
    if (!std::isfinite(box.width) || box.width > 4096 || box.width <= MinWidthAt(ui.minWidth, ui.scale) + .5f) return;
    ui.minWidth = Scaled(box.width, ui.scale, BaseScale);
    Ref style(StyleOf(ui.label.get()));
    Pixels(style.get(), prop::MinWidth, MinWidthAt(ui.minWidth, ui.scale));
}
void Refresh(uint64_t now) {
    ui.nextRefresh = now + RefreshMs;
    // Never during a resize, nor before the game lays it out at a new size:
    // its layout may still be that of the size before.
    if (!Placeholder(ui.text) && !ui.rescaled && !(drag.resize && drag.phase != Drag::Idle)) Widen(LayoutOf(ui.label.get()));
    // At the mouse's place, placed again for the label's and the panel's
    // sizes (new figures, resolution or interface scale).
    if (ui.spot.x >= 0 && drag.phase == Drag::Idle) Settle();
    const auto text = Text(Measure(ui.parts));
    if (text == ui.text) return;
    SetText(ui.label.get(), text);
    ui.text = text;
}

// -------------------------------------------------------- the mouse placement
// The cursor in the layer's pixels: the game's window client area shows the
// whole panel (black bars aside), and the layer covers the panel from its
// origin.
bool Locate(const Rect& layer, float& x, float& y) {
    Cursor cursor;
    if (!sys.cursor(cursor) || cursor.width <= 0 || cursor.height <= 0) return false;
    const auto point = PanelPoint(static_cast<float>(cursor.x), static_cast<float>(cursor.y), static_cast<float>(cursor.width),
        static_cast<float>(cursor.height), layer.width, layer.height);
    x = point.x;
    y = point.y;
    return std::isfinite(x) && std::isfinite(y);
}
// The panel picks the label at this point: the press is on the overlay, not
// on a game element drawn above it (menus, popups, the loading screen).
bool Picked(float x, float y) {
    const Obj panel = api.Invoke(n.panel, ui.label.get());
    if (!panel || !api.class_is_assignable_from(n.panelType, api.object_get_class(panel))) return false;
    std::array<float, 2> point{x, y}; // Vector2
    return api.Invoke(n.pick, panel, {point.data()}) == ui.label.get();
}
// The overlay's own settings (its place and size, perfOverlayShown and the
// part its top-bar button ticks), saved once in the settings file, so they
// survive restarts. Saved as Module::Change saves (another client's shared
// Metamob choice kept, the options page in step), without its appearance
// refresh: no look reads them, and in combat that refresh reloads every
// fighter's look. A file that cannot be written keeps them for the session.
void Commit(Settings next, const std::string& done, const char* failed) {
    try {
        const bool withMetamobSync = module->KeepSharedMetamobSync(next);
        module->settings = next;
        if (module->ui) module->ui->Sync();
        module->SaveSettings(withMetamobSync);
        Log(done);
    } catch (const std::exception& error) {
        Log(std::string(failed) + error.what());
    }
}
// A resize step: the overlay drawn at `scale`, its top-left kept where it was
// at the press (a label anchored by its right or bottom side moved by them,
// for its size at that scale).
void Rescale(int scale, const Rect& layer) {
    if (scale != ui.scale) DrawAt(scale);
    MoveTo(drag.left, drag.top, layer, Scaled(drag.startWidth, drag.startScale, scale), Scaled(drag.startHeight, drag.startScale, scale));
}
// The drop, or the end of a resize: the place reached (perfOverlayX/Y) for
// the size drawn then, and that size (perfOverlayScale).
void SavePlace() {
    const int scale = drag.resize ? drag.scale : ui.scale;
    const float width = drag.resize ? Scaled(drag.startWidth, drag.startScale, scale) : drag.width;
    const float height = drag.resize ? Scaled(drag.startHeight, drag.startScale, scale) : drag.height;
    const Spot spot{PlaceOfEdge(drag.left, width, drag.spaceWidth), PlaceOfEdge(drag.top, height, drag.spaceHeight)};
    ui.spot = spot;
    ui.pending = false;
    if (spot.x == module->settings.perfOverlayX && spot.y == module->settings.perfOverlayY && scale == module->settings.perfOverlayScale) return;
    Settings next = module->settings;
    next.perfOverlayX = spot.x;
    next.perfOverlayY = spot.y;
    next.perfOverlayScale = scale;
    const std::string place = "x=" + std::to_string(spot.x) + " y=" + std::to_string(spot.y);
    Commit(next, drag.resize ? "PERF_OVERLAY_RESIZED scale=" + std::to_string(scale) + " " + place : "PERF_OVERLAY_PLACED " + place,
        "PERF_OVERLAY_PLACE_ERROR ");
}
// Every frame while the label is shown with the mouse placement: the left
// button (one read); at a press, or while a press, a drag or a resize goes
// on, the cursor, both layouts and the panel's pick. Outside a usable
// session nothing is read and the button is forgotten: a press is only ever
// seen starting.
void DragFrame() {
    if (!Session() || !Asked() || !module->settings.perfOverlayDrag || broken ||
        ui.picking != PickingPosition || ui.compositor.get() != module->compositor.get()) {
        drag = Drag{};
        return;
    }
    const bool down = sys.button(), press = down && !drag.down;
    drag.down = down;
    if (drag.phase == Drag::Idle && !press) return;
    const Rect layer = LayoutOf(ui.layer.get()), box = LayoutOf(ui.label.get());
    float x = 0, y = 0;
    const bool located = Usable(layer) && Usable(box) && Locate(layer, x, y);
    // The panel's pick under the cursor at a press; afterwards at the point
    // held, which the user sees, whatever covers the rest of the overlay.
    bool reached = false;
    if (located) {
        const Point held = drag.phase == Drag::Idle ? Point{x, y} : HeldPoint(drag, box);
        reached = Picked(held.x, held.y);
    }
    switch (Advance(drag, press, down, located, reached, x, y, box, layer.width, layer.height, ui.scale)) {
    case DragStep::Move:
        Remember(box);
        MoveTo(drag.left, drag.top, layer, box.width, box.height);
        break;
    case DragStep::Scale:
        Rescale(drag.scale, layer);
        break;
    case DragStep::Back:
        if (drag.resize) Rescale(drag.scale, layer);
        else MoveTo(drag.left, drag.top, layer, box.width, box.height);
        [[fallthrough]];
    case DragStep::End:
        SavePlace();
        break;
    case DragStep::None:
        break;
    }
}
// Never throws (the compositor's frame calls it): an error stops the drag.
void FrameUi(uint64_t now) {
    ui.rescaled = false; // laid out since the last frame, at the size drawn
    try {
        DragFrame();
        if (ui.pending && drag.phase == Drag::Idle && now >= ui.settleAt) {
            ui.settleAt = now + SettleMs;
            Settle();
        }
    } catch (const std::exception& error) {
        drag = Drag{};
        Report(dragLog, std::string("PERF_OVERLAY_DRAG_ERROR ") + error.what(), now);
    } catch (...) {
        drag = Drag{};
    }
}

// Never throws: stop, logout and the option turned off. A label the game does
// not let go is tried again every CleanupRetryMs (Restore runs every frame
// once the mod stops) and forgotten CleanupGiveUpMs after the first failure.
void RestoreAll(uint64_t now) {
    sampling = timing = false;
    ring.Clear();
    cpuMeter.Clear();
    drag = Drag{};
    StopDevices(now);
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
// The label shown, refreshed and placed; on an error it goes, and comes back
// RetryMs later.
void Show(uint64_t now) {
    try {
        // The corner an earlier revision chose (the options no longer offer
        // one), the top left otherwise.
        const int setting = module->settings.perfOverlayCorner;
        const int corner = Settings::ValidPerfOverlayCorner(setting) ? setting : 0;
        const Parts parts = Ticked();
        if (ui.label.get() && !Current(corner, parts, now)) {
            Report(removedLog, std::string("PERF_OVERLAY_REMOVED ") +
                (ui.corner != corner ? "corner" : !(ui.parts == parts) ? "parts" : "replaced"), now);
            RemoveUi();
        }
        if (!ui.label.get()) {
            auto found = FindLayer();
            if (!found.layer.get()) {
                cpuMeter.Clear();
                Report(waitingLog, std::string("PERF_OVERLAY_WAITING ") + found.missing, now, 30000);
                return;
            }
            Create(found, corner, parts, now);
            return;
        }
        UpdatePicking();
        if (drag.phase == Drag::Idle && ui.scale != WantedScale()) {
            // Another size asked for (a resize stopped unsaved: the size
            // saved): drawn at it, and placed again for it once laid out.
            DrawAt(WantedScale());
            Settle();
        }
        if (drag.phase == Drag::Idle && !(Wanted() == ui.spot)) Settle();
        if (now >= ui.nextRefresh) Refresh(now);
    } catch (const std::exception& error) {
        Report(errorLog, std::string("PERF_OVERLAY_ERROR ") + error.what(), now);
        try { RemoveUi(); } catch (...) { /* RestoreAll tries again */ }
        retryAt = now + RetryMs;
        if (failures == 0 || now - firstFailureAt > FailureWindowMs) { failures = 0; firstFailureAt = now; }
        if (++failures >= MaxFailures) {
            broken = true;
            Log("PERF_OVERLAY_DISABLED repeated errors; hiding it (its boxes or its top-bar button) and showing it again tries again");
            RestoreAll(now);
        }
    }
}
void Tick(uint64_t now) {
    const Parts parts = module ? Ticked() : Parts{};
    const bool option = module && Asked();
    if (!option) { broken = false; failures = 0; retryAt = 0; } // showing it again (a box or the top-bar button) tries again
    if (!Session() || !option || broken) {
        if (ui.label.get())
            Report(removedLog, std::string("PERF_OVERLAY_REMOVED ") + (!parts.Any() ? "option-off" : !option ? "hidden" : broken ? "errors" : "session"), now);
        RestoreAll(now);
        return;
    }
    if (!sampling) StartSampling();
    Time(parts.fps);
    if (now >= retryAt) Show(now);
    if (!broken) UpdateDevices(now);
}

// ------------------------------------------------------- the top-bar button
// Its button among the mod's in the top bar (toolsUi, drawn by the soul
// widget), pressed while the overlay is asked for. A click hides it, its
// parts kept ticked, or shows it again, with « Afficher les FPS et la
// mémoire » ticked when no part is, so that the click shows something.
// Hidden, it stays hidden at the next start. Saved as a drop is (Commit).
// FigmaIcons.activity (1 in the pinned snapshot): what the overlay shows, the
// game's activity (its frames, memory, processor, graphics card and disk).
// FigmaIcons has no gauge, speed or FPS icon; chart (77) is the statistics
// window's.
constexpr int ButtonIcon = 1;
bool ButtonPressed() { return module && Asked(); }
bool ButtonClick() {
    if (!module) return false;
    Settings next = module->settings;
    const bool hide = Asked(), fps = !hide && !Ticked().Any();
    next.perfOverlayShown = !hide;
    if (fps) next.perfOverlay = true;
    Commit(next, std::string("PERF_OVERLAY_BUTTON ") + (hide ? "hidden" : fps ? "shown fps" : "shown"), "PERF_OVERLAY_BUTTON_ERROR ");
    return true;
}
}

void InstallPerfOverlay() {
    using namespace perfOverlay;
    if (installed) return;
    // Optional: a contract mismatch disables only the overlay, never the mod.
    try { Install(); }
    catch (const std::exception& error) { Log(std::string("PERF_OVERLAY_DISABLED ") + error.what()); return; }
    installed = true;
    // Its top-bar button, after those of the treasure hunt, the notebook and
    // the statistics window, registered before it on this thread (before the
    // hooks, as theirs); perfOverlayButton removes it. No window and no
    // header: the overlay keeps its own discreet look.
    try {
        toolsUi::Register("perf", "Performances", ButtonIcon, ButtonPressed, ButtonClick, {},
            [] { return module && module->settings.perfOverlayButton; });
    } catch (const std::exception& error) { Log(std::string("PERF_OVERLAY_BUTTON_UNAVAILABLE ") + error.what()); }
}
void TickPerfOverlay() {
    using namespace perfOverlay;
    if (!installed) return;
    Tick(sys.milliseconds());
}
// Every frame of the main compositor while the overlay is on: one counter
// read with the frame rate shown; with the mouse placement, the left button
// and, only at a press on the overlay or during its drag, the cursor and the
// layouts.
void PerfOverlayFrame(unsigned long long now) {
    using namespace perfOverlay;
    if (!sampling) return;
    if (timing) ring.Push(sys.counter());
    if (ui.label.get()) FrameUi(now);
    else drag = Drag{};
}
void RestorePerfOverlay() {
    using namespace perfOverlay;
    if (!installed) return;
    RestoreAll(sys.milliseconds());
}
bool PerfOverlayCleanupPending() { return perfOverlay::installed && perfOverlay::ui.label.get() != nullptr; }
