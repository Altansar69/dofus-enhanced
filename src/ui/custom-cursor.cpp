#include "ui/custom-cursor.hpp"
#include "ui/custom-cursor-policy.hpp"
#include "ui/custom-cursor-player.hpp"
#include "core/module.hpp"
#include <intrin.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace customCursor {
using namespace managed;
constexpr const char* CoreImage = "UnityEngine.CoreModule.dll";
constexpr const char* UiImage = "UnityEngine.UIElementsModule.dll";
constexpr const char* UiSpace = "UnityEngine.UIElements";
// DofusCursor.UpdateCursorTexture stores its decision in m_currentCursor, then
// calls ApplyCursor here (0x280A73C); every path of ApplyCursor ends with a
// tail jump to UnityEngine.Cursor.SetCursor (0x28088F9). A DofusCursor request
// thus reaches SetCursor with the return address after that 5-byte call. Both
// sites are guarded.
constexpr uintptr_t ApplyCursorCall = 0x280A73C, ApplyCursorJump = 0x28088F9;
constexpr uintptr_t ApplyCursorReturn = ApplyCursorCall + 5;
// TextureFormat.RGBA32; HideFlags.DontUnloadUnusedAsset (the game unloads its
// unused assets: never the mod's cursors); CursorMode.Auto (a Windows cursor,
// as the game's, at every size: never ForceSoftware, custom-cursor-player.hpp);
// PointerId.mousePointerId.
constexpr int Rgba32 = 4, DontUnloadUnusedAsset = 0x20, CursorModeAuto = 0, MousePointerId = 0;
// In milliseconds: the frame work at most every FrameMs; the UIDocument's
// panel, and the drag service while missing, looked up every LookupMs; the
// theme's colours read every ThemeMs (« Couleur du thème du jeu » only); the
// window's scale and the Windows pointer size every SizeMs, the textures
// checked as often; a texture of the mod sent again within DedupeMs skipped; a
// failed destroy tried again every CleanupRetryMs; FaultErrors frame errors
// within FaultWindowMs turn the cursors off.
constexpr uint64_t FrameMs = 8, LookupMs = 2000, ThemeMs = 500, SizeMs = 1000, DedupeMs = 250, CleanupRetryMs = 500,
    FaultWindowMs = 10000, LogRepeatMs = 60000;
// Tries before the doomed textures are left to the game; requests logged
// (CUSTOM_CURSOR_SEEN); the ancestors looked at for a text field and for the
// theme's colours.
constexpr unsigned CleanupTries = 20, FaultErrors = 3, SeenLimit = 64, TextFieldDepth = 12, ThemeDepth = 64;

struct Natives {
    Klass dofusCursor{}, cursor{}, texture{}, object{}, element{}, panel{}, textField{}, styleAccess{}, dragService{};
    void* currentCursor = nullptr; // DofusCursor.m_currentCursor (static)
    size_t layer = 0, type = 0, state = 0, cachedPtr = 0, dragging = 0;
    Method setCursor{}, update{}, textureCtor{}, loadRaw{}, apply{}, hideFlags{}, destroy{};
    Method documentRoot{}, parent{}, panelOf{}, topElement{}, enabled{}, customStyle{}, readColor{};
    // The optional parts installed; sized: the player loads the mod's cursors at
    // their own size (without it, Windows' size, as 1.17.0).
    bool textHover = false, theme = false, drag = false, sized = false;
} n;

// UnityEngine.Vector2: SetCursor's hotspot, by value in RDX.
struct Vector2 {
    float x = 0, y = 0;
    bool operator==(const Vector2&) const = default;
};
static_assert(sizeof(Vector2) == 8, "UnityEngine.Vector2 travels by value in one register");
using SetCursorFn = void (*)(Obj, Vector2, int32_t, Method);
SetCursorFn originalSetCursor = nullptr;

// One texture of the mod per shape, with its hotspot in pixels from the top left.
struct Drawn { Ref texture; Vector2 hotspot; };
// What a set is drawn for: the colour choice, the theme's accent and dark
// colours (« Couleur du thème du jeu » only, 0 otherwise) and the size.
struct Look {
    int colour = -1, size = 0;
    uint32_t accent = 0, dark = 0;
    bool operator==(const Look&) const = default;
};
struct Set {
    Look look;
    std::array<Drawn, cursorArt::ShapeCount> shapes;
    size_t built = 0; // uploaded, in Shape order
    bool loadLogged = false; // CUSTOM_CURSOR_LOADED written for this set
    bool Complete() const { return built == shapes.size(); }
};
// A call going on to Unity: the texture, its hotspot and the mode; for a
// cursor of the mod, the size it is drawn at (0 otherwise).
struct Call { Obj texture = nullptr; Vector2 hotspot; int32_t mode = CursorModeAuto; int pixels = 0; };
// A request logged once: caller, layer, type, state, no texture.
using Seen = std::tuple<uintptr_t, int32_t, int64_t, int32_t, bool>;
struct Throttle { std::string last; uint64_t until = 0; };
struct State {
    bool installed = false;
    bool active = false;    // the mod's cursors stand in: the option on, a complete set
    bool faulted = false;   // an error in the detour, or repeated frame errors: off until unticked
    bool forceNext = false; // a refresh: its call is never skipped
    Set current, building;  // in use; the next one, one shape a frame
    std::vector<Ref> doomed; // textures to destroy, once the game took another cursor
    unsigned cleanupFailures = 0;
    uint64_t cleanupRetryAt = 0;
    // The last call that went on: a texture of the mod sent again within
    // DedupeMs, with no other call between, is skipped.
    Call last;
    uint64_t lastAt = 0;
    bool lastOurs = false;
    // The hover inputs: the UIDocument's root and panel, the element last
    // under the pointer and whether it is in an enabled text field, the drag
    // service.
    Ref root, panel, element, drag;
    bool elementText = false;
    // The last decision: its hover inputs and what it asked for (the frame
    // reads the button for Move, the text field for the arrow).
    Hover applied;
    bool askedArrow = false, askedMove = false;
    // The look's inputs, and the theme's variable names.
    unsigned dpi = 96, base = 32;
    uint32_t accent = StandardAccent, dark = StandardDark;
    int themeDepth = -2;
    Ref accentName, darkName;
    uint64_t nextFrame = 0, nextRoot = 0, nextDrag = 0, nextTheme = 0, nextSize = 0;
    unsigned frameErrors = 0;
    uint64_t firstFrameError = 0;
    bool hookErrorLogged = false;
    std::set<Seen> seen;
    Throttle frameLog, refreshLog;
} s;

// ------------------------------------------------------------ Windows' side
// Clock, primary button, window scale and pointer size; the tests replace them.
uint64_t Milliseconds() { return GetTickCount64(); }
// A window of this process in the foreground: the game's.
HWND GameForeground() {
    const HWND window = GetForegroundWindow();
    DWORD process = 0;
    return window && GetWindowThreadProcessId(window, &process) && process == GetCurrentProcessId() ? window : nullptr;
}
// Windows' primary button (the right one when swapped), the game in the foreground.
bool PrimaryButton() {
    return GameForeground() && (GetAsyncKeyState(GetSystemMetrics(SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON) & 0x8000) != 0;
}
// The game window's DPI (its monitor's, when the game is DPI aware): the
// window last seen in the foreground, else the system's, else 96.
unsigned WindowDpi() {
    using WindowDpiFn = UINT(WINAPI*)(HWND);
    using SystemDpiFn = UINT(WINAPI*)();
    static const HMODULE user = GetModuleHandleW(L"user32.dll");
    static const auto forWindow = user ? reinterpret_cast<WindowDpiFn>(GetProcAddress(user, "GetDpiForWindow")) : nullptr;
    static const auto forSystem = user ? reinterpret_cast<SystemDpiFn>(GetProcAddress(user, "GetDpiForSystem")) : nullptr;
    static HWND window = nullptr;
    if (const HWND foreground = GameForeground()) window = foreground;
    if (window && !IsWindow(window)) window = nullptr;
    if (window && forWindow)
        if (const UINT dpi = forWindow(window)) return dpi;
    if (forSystem)
        if (const UINT dpi = forSystem()) return dpi;
    return 96;
}
// The Windows pointer's base size, larger once enlarged in Windows' settings
// (CursorBaseSize; 32 without it).
unsigned PointerBase() {
    DWORD value = 0, bytes = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Control Panel\\Cursors", L"CursorBaseSize", RRF_RT_REG_DWORD, nullptr, &value, &bytes) != ERROR_SUCCESS)
        return 32;
    return value;
}
// The player's code at a site of custom-cursor-player.hpp, or null when
// UnityPlayer.dll is not loaded or smaller than that.
const unsigned char* PlayerCode(uintptr_t rva, size_t size) {
    const auto image = reinterpret_cast<const unsigned char*>(GetModuleHandleW(L"UnityPlayer.dll"));
    if (!image) return nullptr;
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) return nullptr;
    const auto headers = reinterpret_cast<const IMAGE_NT_HEADERS64*>(image + dos->e_lfanew);
    if (headers->Signature != IMAGE_NT_SIGNATURE || headers->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        rva + size > headers->OptionalHeader.SizeOfImage)
        return nullptr;
    return image + rva;
}
// user32's LoadImageW, the function the player's import reaches.
void* LoadImageAddress() {
    const HMODULE user = GetModuleHandleW(L"user32.dll");
    return user ? reinterpret_cast<void*>(GetProcAddress(user, "LoadImageW")) : nullptr;
}
// Windows' own cursor size, the one the player's load would give (logged only).
int WindowsCursor() { return GetSystemMetrics(SM_CXCURSOR); }
struct System {
    uint64_t (*milliseconds)() = Milliseconds;
    bool (*button)() = PrimaryButton;
    unsigned (*dpi)() = WindowDpi;
    unsigned (*pointerBase)() = PointerBase;
    const unsigned char* (*playerCode)(uintptr_t, size_t) = PlayerCode;
    void* (*loadImage)() = LoadImageAddress;
    int (*windowsCursor)() = WindowsCursor;
} sys;

// ------------------------------------------------------ the cursors' size
// The player makes the Windows cursor of a new texture at Windows' own size
// (custom-cursor-player.hpp). While the mod's own cursor goes on to Unity, on
// this thread, that load gets the size the mod drew it at instead: a Windows
// cursor still, at its own size. Any other load (another thread, the game's
// own textures, other arguments, once the call is over) goes on as it is.
using LoadImageFn = HANDLE(WINAPI*)(HINSTANCE, LPCWSTR, UINT, int, int, UINT);
LoadImageFn originalLoadImage = nullptr;
std::atomic<DWORD> sizedThread{0};
std::atomic<int> sizedPixels{0};
std::atomic<unsigned> sizedLoads{0}; // the loads given the mod's size, ever
HANDLE WINAPI LoadImageHook(HINSTANCE instance, LPCWSTR name, UINT type, int cx, int cy, UINT flags) {
    const int pixels = sizedPixels.load(std::memory_order_relaxed);
    if (pixels > 0 && sizedThread.load(std::memory_order_relaxed) == GetCurrentThreadId() &&
        player::PlayerLoad(instance, type, cx, cy, flags)) {
        cx = cy = pixels;
        flags &= ~player::DefaultSize;
        sizedLoads.fetch_add(1, std::memory_order_relaxed);
    }
    return originalLoadImage(instance, name, type, cx, cy, flags);
}
// For as long as the mod's cursor of `pixels` goes on to Unity, on this thread
// (0: another cursor, nothing changed).
class SizedLoad {
public:
    explicit SizedLoad(int pixels) : on(pixels > 0) {
        if (!on) return;
        sizedThread.store(GetCurrentThreadId(), std::memory_order_relaxed);
        sizedPixels.store(pixels, std::memory_order_relaxed);
    }
    ~SizedLoad() {
        if (!on) return;
        sizedPixels.store(0, std::memory_order_relaxed);
        sizedThread.store(0, std::memory_order_relaxed);
    }
    SizedLoad(const SizedLoad&) = delete;
    SizedLoad& operator=(const SizedLoad&) = delete;
private:
    bool on;
};

// ------------------------------------------------------------------ logging
// The same line at most once a minute; another line at once.
void Report(Throttle& throttle, const std::string& line, uint64_t now) {
    if (line == throttle.last && now < throttle.until) return;
    throttle.last = line;
    throttle.until = now + LogRepeatMs;
    Log(line);
}
std::string Hex(uint64_t value) {
    char text[24];
    std::snprintf(text, sizeof(text), "0x%llX", static_cast<unsigned long long>(value));
    return text;
}
std::string Rgb(uint32_t value) {
    char text[16];
    std::snprintf(text, sizeof(text), "#%06X", static_cast<unsigned>(value));
    return text;
}
const char* ShapeName(Shape shape) {
    static constexpr const char* Names[] = {"arrow", "hand", "beam", "open", "closed", "resizeEW", "resizeNS", "resizeNWSE",
        "resizeNESW", "arrowForbidden", "handForbidden", "arrowBusy", "handBusy"};
    static_assert(std::size(Names) == cursorArt::ShapeCount, "A name per shape");
    return Names[static_cast<size_t>(shape)];
}
// The first SeenLimit distinct requests: what the game asks for, where, and
// what stood in (settles in game which types the game sends, and whether its
// textures exist).
void Note(const Request& request, uintptr_t caller, const std::optional<Shape>& shape) {
    if (!caller || s.seen.size() >= SeenLimit) return;
    if (!s.seen.emplace(caller - gameBase, request.layer, request.type, request.state, request.textureNull).second) return;
    Log("CUSTOM_CURSOR_SEEN caller=" + (request.origin == Origin::ApplyCursor ? std::string("DofusCursor") : Hex(caller - gameBase)) +
        " layer=" + std::to_string(request.layer) +
        " type=" + (request.type < 0 ? std::to_string(request.type) : Hex(static_cast<uint64_t>(request.type))) +
        " state=" + std::to_string(request.state) + " texture=" + (request.textureNull ? "none" : "game") +
        " cursor=" + (shape ? ShapeName(*shape) : "game"));
}

// ---------------------------------------------------------------- contracts
// A field at its pinned offset (in the object, or in the class's static data).
size_t RequireField(Klass type, const char* name, size_t offset) {
    const auto field = api.class_get_field_from_name(type, name);
    if (!field || api.field_get_offset(field) != offset) throw std::runtime_error(std::string("Cursor field layout: ") + name);
    return offset;
}
// An optional part: a missing contract turns it off, never the cursors.
void Optional(const char* part, bool& installed, void (*install)()) {
    try {
        install();
        installed = true;
    } catch (const std::exception& error) {
        installed = false;
        Log(std::string("CUSTOM_CURSOR_PART_UNAVAILABLE ") + part + " " + error.what());
    }
}
void SetCursorHook(Obj texture, Vector2 hotspot, int32_t mode, Method method);
void Install() {
    // Required: the game's requests and their refresh, the textures.
    n.dofusCursor = api.Class("Core.dll", "Core.UILogic", "DofusCursor");
    n.update = api.FindVerified(n.dofusCursor, "UpdateCursorTexture", 0, 0x280A4C0);
    RequireField(n.dofusCursor, "m_currentCursor", 0x8);
    n.currentCursor = api.class_get_field_from_name(n.dofusCursor, "m_currentCursor");
    api.NativeAt(ApplyCursorCall);
    api.NativeAt(ApplyCursorJump);
    n.cursor = api.Class("Core.dll", "Core.UILogic", "Cursor");
    n.layer = RequireField(n.cursor, "layerEnum", 0x10);
    n.type = RequireField(n.cursor, "cursorType", 0x18);
    n.state = RequireField(n.cursor, "cursorState", 0x20);
    n.texture = api.Class(CoreImage, "UnityEngine", "Texture2D");
    n.textureCtor = api.FindVerified(n.texture, ".ctor", 5, 0x5410E20); // (width, height, format, mipChain, linear)
    n.loadRaw = api.FindVerified(n.texture, "LoadRawTextureData", 2, 0x540E4C0); // (IntPtr, Int32)
    n.apply = api.FindVerified(n.texture, "Apply", 2, 0x540BCE0); // (updateMipmaps, makeNoLongerReadable)
    n.object = api.Class(CoreImage, "UnityEngine", "Object");
    n.hideFlags = api.FindVerified(n.object, "set_hideFlags", 1, 0x5439360);
    n.destroy = api.FindVerified(n.object, "Destroy", 1, 0x5435EA0);
    n.cachedPtr = RequireField(n.object, "m_CachedPtr", 0x10);
    n.setCursor = api.FindVerified(api.Class(CoreImage, "UnityEngine", "Cursor"), "SetCursor", 3, 0x54024F0);
    // Optional: the UIDocument's root and the parents, then the text cursor over
    // the inputs the game does not mark (the chat, the searches: every input is
    // a TextField) and the theme's colours; the closed hand while an item is
    // dragged.
    bool ui = false;
    Optional("ui", ui, [] {
        n.element = api.Class(UiImage, UiSpace, "VisualElement");
        n.parent = api.FindVerified(n.element, "get_parent", 0, 0x55F7180);
        n.documentRoot = api.FindVerified(api.Class("Core.dll", "", "pq"), "pbs", 0, 0x2E93CC0);
    });
    if (ui) {
        Optional("text", n.textHover, [] {
            n.panelOf = api.FindVerified(n.element, "get_panel", 0, 0x55F7170);
            n.enabled = api.FindVerified(n.element, "get_enabledInHierarchy", 0, 0x55F5EF0);
            n.panel = api.Class(UiImage, UiSpace, "BaseVisualElementPanel");
            n.topElement = api.FindVerified(n.panel, "GetTopElementUnderPointer", 1, 0x57A3FC0);
            n.textField = api.Class(UiImage, UiSpace, "TextField");
        });
        Optional("theme", n.theme, [] {
            n.customStyle = api.FindVerified(n.element, "get_customStyle", 0, 0x55F5C30);
            n.styleAccess = api.NestedClass(n.element, "CustomStyleAccess");
            n.readColor = api.FindVerified(n.styleAccess, "TryGetValue", 2, 0x55FCCB0); // (CustomStyleProperty<Color>, out Color)
        });
    }
    Optional("drag", n.drag, [] {
        n.dragService = api.Class("Core.dll", "Core.UILogic.Components.DragAndDrop", "DragAndDropService");
        n.dragging = RequireField(n.dragService, "<isDragging>k__BackingField", 0x10);
    });
    // Last of the game's: once applied, SetCursor's bytes no longer match its guard.
    AddHook(n.setCursor, 0x54024F0, reinterpret_cast<void*>(SetCursorHook), reinterpret_cast<void**>(&originalSetCursor));
    // Optional, once the detour exists (never without it): the player loads the
    // mod's cursors at their own size, its loading checked first
    // (custom-cursor-player.hpp). Without it, they keep Windows' size.
    Optional("size", n.sized, [] {
        if (!player::SiteMatches(sys.playerCode(player::LoadSite, player::LoadBytes.size())))
            throw std::runtime_error("UnityPlayer.dll does not load cursors as the supported client's");
        void* const target = sys.loadImage();
        if (!target) throw std::runtime_error("LoadImageW unavailable");
        AddSystemHook(target, reinterpret_cast<void*>(LoadImageHook), reinterpret_cast<void**>(&originalLoadImage));
    });
}

// ------------------------------------------------------------ hover inputs
bool Alive(Obj texture) { return texture && Read<void*>(texture, n.cachedPtr) != nullptr; }
bool Dragging() {
    const Obj service = n.drag ? s.drag.get() : nullptr;
    return service && Read<bool>(service, n.dragging);
}
// The element, or one of its first ancestors, is an enabled TextField (the
// chat, the searches, the game's own inputs: all derive from it).
bool InTextField(Obj element) {
    Ref scope(element);
    for (unsigned depth = 0; scope.get(); ++depth) {
        if (api.class_is_assignable_from(n.textField, api.object_get_class(scope.get())))
            return api.Value<bool>(api.Invoke(n.enabled, scope.get()));
        if (depth == TextFieldDepth) break;
        scope.reset(api.Invoke(n.parent, scope.get()));
    }
    return false;
}
// An enabled text field under the pointer: the panel's top element, its
// ancestors looked at again only when it changes. A failure is not the
// cursors': the plain arrow, until the panel is found again.
bool OverText() {
    if (!n.textHover || !s.panel.get()) return false;
    try {
        int pointer = MousePointerId;
        const Obj top = api.Invoke(n.topElement, s.panel.get(), {&pointer});
        if (top != s.element.get()) {
            s.element.reset(top);
            s.elementText = top && InTextField(top);
        }
        return s.elementText;
    } catch (...) {
        s.panel.reset();
        s.element.reset();
        s.elementText = false;
        return false;
    }
}
// The inputs a request needs: the drag always, the button for Move, the text
// field for the arrow (none of them while an item is dragged).
Hover ReadHover(bool arrow, bool move) {
    Hover hover;
    hover.dragging = Dragging();
    if (hover.dragging) return hover;
    if (move) hover.button = sys.button();
    if (arrow) hover.overText = OverText();
    return hover;
}

// ---------------------------------------------------------------- the detour
// The request behind a call: DofusCursor's when it comes back from ApplyCursor
// with a Cursor in m_currentCursor (stored right before), another caller's
// otherwise.
Request Classify(Obj texture, uintptr_t caller) {
    Request request;
    request.textureNull = texture == nullptr;
    if (caller != gameBase + ApplyCursorReturn) return request;
    Obj current = nullptr;
    api.field_static_get_value(n.currentCursor, &current);
    if (!current || api.object_get_class(current) != n.cursor) return request;
    request.origin = Origin::ApplyCursor;
    request.layer = Read<int32_t>(current, n.layer);
    request.type = Read<int64_t>(current, n.type);
    request.state = Read<int32_t>(current, n.state);
    return request;
}
void Remember(const Call& call, uint64_t now, bool ours) {
    s.last = call;
    s.lastAt = now;
    s.lastOurs = ours;
}
// While the mod's cursors are on: what goes on to Unity, the mod's cursor in
// place of the game's (Choose) or the call unchanged; false skips it (the same
// cursor of the mod sent within DedupeMs).
bool Decide(Obj texture, uintptr_t caller, Call& call) {
    const Request request = Classify(texture, caller);
    const Hover hover = ReadHover(AsksArrow(request), AsksMove(request));
    const auto shape = Choose(request, hover);
    s.applied = hover;
    s.askedArrow = AsksArrow(request);
    s.askedMove = AsksMove(request);
    Note(request, caller, shape);
    const uint64_t now = sys.milliseconds();
    const bool forced = s.forceNext;
    s.forceNext = false;
    if (shape) {
        const auto& drawn = s.current.shapes[static_cast<size_t>(*shape)];
        const Obj ours = drawn.texture.get();
        // A texture of the mod destroyed meanwhile: the game's cursor, until the
        // frame draws the set again.
        if (Alive(ours)) {
            if (!forced && s.lastOurs && s.last.texture == ours && s.last.hotspot == drawn.hotspot && now - s.lastAt < DedupeMs)
                return false;
            call = {ours, drawn.hotspot, CursorModeAuto, s.current.look.size};
            Remember(call, now, true);
            return true;
        }
    }
    Remember(call, now, false);
    return true;
}
// An error in the detour: the call goes on unchanged, the cursors off at the
// next frame until the option is ticked again.
void HookFault(const char* what) {
    s.faulted = true;
    if (s.hookErrorLogged) return;
    s.hookErrorLogged = true;
    Log(std::string("CUSTOM_CURSOR_HOOK_ERROR ") + what);
}
// The player made a Windows cursor of the set in use at its own size: logged
// once a set, with the size Windows would have given it.
void NoteSizedLoad(int pixels) {
    if (s.current.loadLogged) return;
    s.current.loadLogged = true;
    try { Log("CUSTOM_CURSOR_LOADED size=" + std::to_string(pixels) + " windows=" + std::to_string(sys.windowsCursor())); }
    catch (...) {}
}
// Every call of UnityEngine.Cursor.SetCursor, on the main thread. Never throws
// into the game: a call goes on as the game made it unless the mod's cursor
// stands in; the mod's own calls go through the trampoline, a cursor of the
// mod loaded at its own size if the player makes its Windows cursor then.
void OnSetCursor(Obj texture, Vector2 hotspot, int32_t mode, Method method, uintptr_t caller) {
    Call call{texture, hotspot, mode};
    bool send = true;
    if (s.active && !s.faulted && module && !module->stopped) {
        try {
            send = Decide(texture, caller, call);
        } catch (const std::exception& error) {
            HookFault(error.what());
            call = {texture, hotspot, mode};
            send = true;
        } catch (...) {
            HookFault("unknown error");
            call = {texture, hotspot, mode};
            send = true;
        }
    }
    if (!send) return;
    const unsigned loads = sizedLoads.load(std::memory_order_relaxed);
    {
        const SizedLoad sized(n.sized ? call.pixels : 0);
        originalSetCursor(call.texture, call.hotspot, call.mode, method);
    }
    if (call.pixels && sizedLoads.load(std::memory_order_relaxed) != loads) NoteSizedLoad(call.pixels);
}
void SetCursorHook(Obj texture, Vector2 hotspot, int32_t mode, Method method) {
    OnSetCursor(texture, hotspot, mode, method, reinterpret_cast<uintptr_t>(_ReturnAddress()));
}

// ------------------------------------------------------------- the textures
// The game applies its current cursor again (DofusCursor.UpdateCursorTexture),
// through the detour: the mod's cursor while on, the game's own once off. If
// it fails, the plain arrow (the mod's, or Windows').
void Refresh() {
    s.forceNext = true;
    try {
        api.Invoke(n.update, nullptr);
        return;
    } catch (const std::exception& error) {
        Report(s.refreshLog, std::string("CUSTOM_CURSOR_REFRESH_ERROR ") + error.what(), sys.milliseconds());
    } catch (...) {
        Report(s.refreshLog, "CUSTOM_CURSOR_REFRESH_ERROR unknown error", sys.milliseconds());
    }
    try { OnSetCursor(nullptr, {}, CursorModeAuto, n.setCursor, 0); } catch (...) {}
}
// The texture of one shape: RGBA32 without mipmaps, kept from the game's
// unloads, readable (Apply(false, false)) as the game's own cursors. A failure
// leaves the texture to destroy.
Ref Upload(const cursorArt::Image& image) {
    Ref texture(api.object_new(n.texture));
    if (!texture.get()) throw std::runtime_error("Cursor texture allocation failed");
    try {
        int width = image.size, height = image.size, format = Rgba32, flags = DontUnloadUnusedAsset;
        bool mipChain = false, linear = false, updateMipmaps = false, noLongerReadable = false;
        api.Invoke(n.textureCtor, texture.get(), {&width, &height, &format, &mipChain, &linear});
        api.Invoke(n.hideFlags, texture.get(), {&flags});
        void* data = const_cast<uint8_t*>(image.rgba.data());
        int bytes = static_cast<int>(image.rgba.size());
        api.Invoke(n.loadRaw, texture.get(), {&data, &bytes});
        api.Invoke(n.apply, texture.get(), {&updateMipmaps, &noLongerReadable});
    } catch (...) {
        s.doomed.push_back(std::move(texture));
        throw;
    }
    return texture;
}
void Doom(Set& set) {
    for (auto& drawn : set.shapes)
        if (drawn.texture.get()) s.doomed.push_back(std::move(drawn.texture));
    set = Set{};
}
// Destroys the doomed textures (Object.Destroy), the game having taken
// another cursor. A failure keeps them, tried again CleanupRetryMs later;
// after CleanupTries failures they are left to the game: a few hundred KB
// until it closes. Stopping before the services are ready, nothing would try
// again (no compositor's frame ends the stop): tried at once whatever the
// wait, then left to the game on a failure.
void DestroyDoomed(uint64_t now, bool stopping) {
    const bool lastChance = stopping && (!module || !module->compositor.get());
    if (s.doomed.empty() || (!lastChance && s.cleanupFailures && now < s.cleanupRetryAt)) return;
    std::string failure;
    for (auto it = s.doomed.begin(); it != s.doomed.end();) {
        try {
            // Never made (its constructor failed) or destroyed already: nothing native left.
            if (Alive(it->get())) api.Invoke(n.destroy, nullptr, {it->get()});
            it = s.doomed.erase(it);
        } catch (const std::exception& error) {
            if (failure.empty()) failure = error.what();
            ++it;
        } catch (...) {
            if (failure.empty()) failure = "unknown error";
            ++it;
        }
    }
    if (s.doomed.empty()) { s.cleanupFailures = 0; return; }
    if (s.cleanupFailures++ == 0) Log("CUSTOM_CURSOR_CLEANUP_ERROR " + failure);
    s.cleanupRetryAt = now + CleanupRetryMs;
    if (s.cleanupFailures >= CleanupTries || lastChance) {
        Log("CUSTOM_CURSOR_RESTORE_ABANDONED " + std::to_string(s.doomed.size()) + " textures left to the game: " + failure);
        s.doomed.clear();
        s.cleanupFailures = 0;
    }
}
// The game's own cursor back (its request again, unchanged), then the mod's
// textures destroyed.
void TurnOff(const char* reason, uint64_t now, bool stopping) {
    if (!s.active && !s.current.built && !s.building.built && s.doomed.empty()) return;
    const bool wasActive = s.active;
    s.active = false;
    Doom(s.building);
    Doom(s.current);
    if (wasActive) {
        Refresh();
        Log(std::string("CUSTOM_CURSOR_OFF ") + reason);
    }
    DestroyDoomed(now, stopping);
}
cursorArt::Palette PaletteFor(const Look& look) {
    return cursorArt::PaletteOf(static_cast<cursorArt::Colour>(look.colour), look.accent, look.dark);
}
// One shape more of the set for `look`. Complete, the set is in use at once
// and the one before destroyed once the game took the new cursor.
void Build(const Look& look, uint64_t now) {
    if (!(s.building.look == look)) {
        Doom(s.building);
        s.building.look = look;
    }
    const size_t index = s.building.built;
    const auto image = cursorArt::Render(static_cast<Shape>(index), PaletteFor(look), look.size);
    const auto pixels = static_cast<size_t>(look.size) * static_cast<size_t>(look.size);
    if (image.size != look.size || image.rgba.size() != pixels * 4 || image.hotX < 0 || image.hotY < 0 || image.hotX >= image.size ||
        image.hotY >= image.size)
        throw std::runtime_error("Cursor image malformed");
    s.building.shapes[index] = {Upload(image), {static_cast<float>(image.hotX), static_cast<float>(image.hotY)}};
    if (++s.building.built < s.building.shapes.size()) return;
    Set previous = std::move(s.current);
    s.current = std::move(s.building);
    s.building = Set{};
    const bool first = !s.active;
    s.active = true;
    s.lastOurs = false;
    // Always Windows cursors (mode=hardware): loaded at the set's size by the
    // player (load=texture), or at Windows' own size without the size part
    // (load=windows). Logged before the refresh that sends the first of them.
    Log(std::string(first ? "CUSTOM_CURSOR_ON" : "CUSTOM_CURSOR_REDRAWN") + " size=" + std::to_string(look.size) + " dpi=" +
        std::to_string(s.dpi) + " base=" + std::to_string(s.base) + " color=" + std::to_string(look.colour) +
        " mid=" + Rgb(PaletteFor(look).mid) + " mode=hardware load=" + (n.sized ? "texture" : "windows"));
    Refresh();
    Doom(previous);
    DestroyDoomed(now, false);
}
// A texture of the set in use destroyed by something else: the game's cursor
// until the set is drawn again.
void CheckTextures() {
    for (const auto& drawn : s.current.shapes)
        if (!Alive(drawn.texture.get())) {
            Log("CUSTOM_CURSOR_LOST a texture of the mod was destroyed; drawing them again");
            const bool wasActive = s.active;
            s.active = false;
            Doom(s.current);
            if (wasActive) Refresh();
            return;
        }
}

// ------------------------------------------------------- the frame's inputs
// pq.pbs, the UIDocument's root (it throws before any UIDocument exists), and
// its panel.
void FindPanel() {
    try {
        Ref root(api.Invoke(n.documentRoot, nullptr));
        Obj panel = nullptr;
        if (root.get() && n.textHover) {
            panel = api.Invoke(n.panelOf, root.get());
            if (panel && !api.class_is_assignable_from(n.panel, api.object_get_class(panel))) panel = nullptr;
        }
        if (panel != s.panel.get()) {
            s.panel.reset(panel);
            s.element.reset();
            s.elementText = false;
        }
        s.root = std::move(root);
    } catch (...) {
        s.root.reset();
        s.panel.reset();
        s.element.reset();
        s.elementText = false;
    }
}
// The game's DragAndDropService (registered as fqa), once the services are ready.
void FindDrag() {
    if (!module->servicesReady) return;
    try {
        const Obj service = module->FindService("fqa");
        if (!service || api.object_get_class(service) != n.dragService) return;
        s.drag.reset(service);
        Log("CUSTOM_CURSOR_DRAG_SERVICE found");
    } catch (...) {}
}
bool ReadColour(Obj custom, Obj name, uint32_t& rgb) {
    // CustomStyleProperty<Color>: a value type holding its name.
    Obj property = name;
    std::array<float, 4> colour{};
    if (!api.Value<bool>(api.Invoke(n.readColor, custom, {&property, &colour}))) return false;
    for (const float channel : colour)
        if (!std::isfinite(channel) || channel < 0 || channel > 1) return false;
    rgb = RgbOf(colour[0], colour[1], colour[2]);
    return true;
}
// The theme's accent (--highlight_primary100) and dark background
// (--background_dark97), declared by its sheet on the panel's root: from the
// UIDocument's root up, the nearest declaration of each. depth: the accent's
// level, -1 without it.
void ReadTheme(uint32_t& accent, uint32_t& dark, int& depth) {
    if (!s.accentName.get()) s.accentName.reset(api.string_new("--highlight_primary100"));
    if (!s.darkName.get()) s.darkName.reset(api.string_new("--background_dark97"));
    bool accentFound = false, darkFound = false;
    Ref scope(s.root.get());
    for (unsigned level = 0; scope.get() && level < ThemeDepth && !(accentFound && darkFound); ++level) {
        Ref custom(api.Invoke(n.customStyle, scope.get()));
        if (custom.get() && api.class_is_assignable_from(n.styleAccess, api.object_get_class(custom.get()))) {
            if (!accentFound && ReadColour(custom.get(), s.accentName.get(), accent)) {
                accentFound = true;
                depth = static_cast<int>(level);
            }
            if (!darkFound) darkFound = ReadColour(custom.get(), s.darkName.get(), dark);
        }
        scope.reset(api.Invoke(n.parent, scope.get()));
    }
}
// « Couleur du thème du jeu »: the theme's colours, the Standard theme's until
// they are read; followed when the player changes the theme.
void UpdateTheme(uint64_t now) {
    if (!n.theme || now < s.nextTheme) return;
    s.nextTheme = now + ThemeMs;
    uint32_t accent = StandardAccent, dark = StandardDark;
    int depth = -1;
    try { ReadTheme(accent, dark, depth); }
    catch (...) { accent = StandardAccent; dark = StandardDark; depth = -1; }
    if (accent != s.accent || dark != s.dark || depth != s.themeDepth)
        Log("CUSTOM_CURSOR_THEME accent=" + Rgb(accent) + " dark=" + Rgb(dark) + " depth=" + std::to_string(depth));
    s.accent = accent;
    s.dark = dark;
    s.themeDepth = depth;
}
// A hover input changed since the last decision: the game's cursor again,
// through the detour.
void Poll() {
    if (!(ReadHover(s.askedArrow, s.askedMove) == s.applied)) Refresh();
}
void Tick(uint64_t now) {
    if (now < s.nextFrame) return;
    s.nextFrame = now + FrameMs;
    const auto& settings = module->settings;
    if (!settings.customCursor && s.faulted) { // unticked: ticking it again tries again
        s.faulted = s.hookErrorLogged = false;
        s.frameErrors = 0;
    }
    if (!settings.customCursor || s.faulted) {
        TurnOff(s.faulted ? "faulted" : "option", now, false);
        return;
    }
    if ((n.textHover || n.theme) && now >= s.nextRoot) {
        s.nextRoot = now + LookupMs;
        FindPanel();
    }
    if (n.drag && !s.drag.get() && now >= s.nextDrag) {
        s.nextDrag = now + LookupMs;
        FindDrag();
    }
    if (now >= s.nextSize) {
        s.nextSize = now + SizeMs;
        s.dpi = sys.dpi();
        s.base = sys.pointerBase();
        if (s.current.Complete()) CheckTextures();
    }
    Look look;
    look.colour = Settings::ValidCustomCursorColor(settings.customCursorColor) ? settings.customCursorColor : 0;
    look.size = CursorPixels(s.dpi, s.base, settings.customCursorSize);
    if (look.colour == static_cast<int>(cursorArt::Colour::Theme)) {
        UpdateTheme(now);
        look.accent = s.accent;
        look.dark = s.dark;
    }
    if (!(s.current.Complete() && s.current.look == look)) Build(look, now);
    else if (s.building.built) Doom(s.building); // back to the look in use
    if (s.active) Poll();
    DestroyDoomed(now, false);
}
// Three frame errors within ten seconds turn the cursors off until the option
// is ticked again.
void FrameError(const std::string& what, uint64_t now) {
    Report(s.frameLog, "CUSTOM_CURSOR_FRAME_ERROR " + what, now);
    if (s.frameErrors == 0 || now - s.firstFrameError > FaultWindowMs) {
        s.frameErrors = 0;
        s.firstFrameError = now;
    }
    if (++s.frameErrors < FaultErrors || s.faulted) return;
    s.faulted = true;
    Log("CUSTOM_CURSOR_FAULTED repeated errors; the game's cursor until the option is ticked again");
}
}

void InstallCustomCursor() {
    using namespace customCursor;
    if (s.installed) return;
    try { Install(); }
    catch (const std::exception& error) { Log(std::string("CUSTOM_CURSOR_DISABLED ") + error.what()); return; }
    s.installed = true;
    Log(std::string("CUSTOM_CURSOR_READY textHover=") + (n.textHover ? "1" : "0") + " theme=" + (n.theme ? "1" : "0") +
        " drag=" + (n.drag ? "1" : "0") + " size=" + (n.sized ? "1" : "0"));
}
void CustomCursorFrame(unsigned long long now) {
    using namespace customCursor;
    if (!s.installed || !module || !module->ready.load() || module->stopped) return;
    try { Tick(now); }
    catch (const std::exception& error) { FrameError(error.what(), now); }
    catch (...) { FrameError("unknown error", now); }
}
// At Stop (module->stopped already set: the detour forwards unchanged), first
// of the restores: the game's cursor back, the textures destroyed, every
// reference released.
void RestoreCustomCursor() {
    using namespace customCursor;
    if (!s.installed) return;
    try {
        TurnOff("stop", sys.milliseconds(), true);
        s.root.reset();
        s.panel.reset();
        s.element.reset();
        s.drag.reset();
        s.accentName.reset();
        s.darkName.reset();
        s.elementText = false;
    } catch (...) {}
}
bool CustomCursorCleanupPending() { return customCursor::s.installed && !customCursor::s.doomed.empty(); }
