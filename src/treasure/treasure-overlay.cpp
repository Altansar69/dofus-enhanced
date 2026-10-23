#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wrl.h>
#include <WebView2.h>
#include "treasure/treasure-overlay-policy.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;
namespace Policy = TreasureOverlayPolicy;

namespace {
constexpr int HomeId = 101, ReloadId = 102, PinId = 103;
constexpr UINT SelfTestComplete = WM_APP + 1;
constexpr UINT_PTR TimeoutId = 1;
constexpr wchar_t RuntimeUrl[] =
    L"https://developer.microsoft.com/microsoft-edge/webview2/#download-section";

std::wstring ErrorCode(HRESULT result) {
    std::wostringstream out;
    out << L"0x" << std::hex << static_cast<unsigned long>(result);
    return out.str();
}

struct ScopedHandle {
    HANDLE value = nullptr;
    ~ScopedHandle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};

class Overlay {
public:
    explicit Overlay(bool selfTest) : selfTest_(selfTest) {}

    int Run(HINSTANCE instance) {
        instance_ = instance;
        const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(initialized)) return ReportStartupError(L"Initialisation COM impossible.", 2);
        const int result = RunInitialized();
        CoUninitialize();
        return result;
    }

private:
    HINSTANCE instance_{};
    HWND window_{}, home_{}, reload_{}, pinControl_{}, statusControl_{};
    HFONT font_{};
    fs::path root_, profile_, settingsFile_, selfTestParent_;
    ComPtr<ICoreWebView2Environment> environment_;
    ComPtr<ICoreWebView2Controller> controller_;
    ComPtr<ICoreWebView2> webview_;
    ScopedHandle mutex_;
    bool selfTest_{}, pinned_ = true, closing_{}, initializing_{}, ready_{}, browserClosed_{};
    bool navigationVerified_{}, boundsVerified_{}, closeVerified_{};
    int exitCode_ = 1;
    UINT activateMessage_{};
    UINT browserProcessId_{};
    std::wstring status_ = L"Chargement de DofusDB…";

    int Scale(int logical) const {
        const UINT dpi = window_ ? GetDpiForWindow(window_) : 96;
        return MulDiv(logical, static_cast<int>(dpi), 96);
    }

    void Log(const std::wstring& text) const {
        if (selfTest_) { std::wcerr << text << L'\n'; return; }
        if (root_.empty()) return;
        std::wofstream log(root_ / L"overlay.log", std::ios::app);
        SYSTEMTIME now{};
        GetLocalTime(&now);
        log << now.wYear << L'-' << now.wMonth << L'-' << now.wDay << L' '
            << now.wHour << L':' << now.wMinute << L':' << now.wSecond << L' '
            << text << L'\n';
    }

    int ReportStartupError(const std::wstring& message, int code) const {
        if (selfTest_) std::wcerr << message << L'\n';
        else MessageBoxW(nullptr, message.c_str(), L"Chasse au trésor — DofusDB",
            MB_OK | MB_ICONERROR);
        return code;
    }

    bool PrepareProfile() {
        PWSTR localData = nullptr;
        const HRESULT result = SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localData);
        if (FAILED(result) || !localData) return false;
        root_ = fs::path(localData) / L"DofusMiniatures" / L"TreasureOverlay";
        CoTaskMemFree(localData);
        if (selfTest_) {
            GUID id{};
            if (FAILED(CoCreateGuid(&id))) return false;
            wchar_t text[40]{};
            if (!StringFromGUID2(id, text, 40)) return false;
            selfTestParent_ = fs::absolute(root_ / L"SelfTests").lexically_normal();
            root_ = selfTestParent_ / text;
        }
        profile_ = root_ / L"Profile";
        settingsFile_ = root_ / L"window.ini";
        std::error_code error;
        fs::create_directories(profile_, error);
        return !error;
    }

    bool ActivateExisting() {
        if (selfTest_) return false;
        mutex_.value = CreateMutexW(nullptr, FALSE, Policy::MutexName);
        const DWORD result = GetLastError();
        if (!mutex_.value) {
            exitCode_ = ReportStartupError(L"Impossible d’ouvrir la fenêtre DofusDB.", 2);
            return true;
        }
        if (result != ERROR_ALREADY_EXISTS) return false;
        // Another launch may still be creating its window.
        for (int attempt = 0; attempt < 20; ++attempt) {
            if (const HWND existing = FindWindowW(Policy::WindowClass, nullptr)) {
                DWORD processId = 0;
                GetWindowThreadProcessId(existing, &processId);
                AllowSetForegroundWindow(processId);
                PostMessageW(existing, activateMessage_, 0, 0);
                exitCode_ = 0;
                return true;
            }
            Sleep(100);
        }
        exitCode_ = ReportStartupError(
            L"La fenêtre DofusDB est déjà en cours d’ouverture. Réessaie dans quelques secondes.", 2);
        return true;
    }

    Policy::Rect InitialBounds() {
        Policy::Rect rect{ 120, 100, 960, 860 };
        if (!selfTest_) {
            rect.left = static_cast<int>(GetPrivateProfileIntW(L"Window", L"Left", rect.left,
                settingsFile_.c_str()));
            rect.top = static_cast<int>(GetPrivateProfileIntW(L"Window", L"Top", rect.top,
                settingsFile_.c_str()));
            const auto width = std::clamp(static_cast<int>(GetPrivateProfileIntW(
                L"Window", L"Width", 840, settingsFile_.c_str())), 1, 20000);
            const auto height = std::clamp(static_cast<int>(GetPrivateProfileIntW(
                L"Window", L"Height", 760, settingsFile_.c_str())), 1, 20000);
            rect.left = std::clamp(rect.left, -100000, 100000);
            rect.top = std::clamp(rect.top, -100000, 100000);
            rect.right = rect.left + width;
            rect.bottom = rect.top + height;
            pinned_ = GetPrivateProfileIntW(L"Window", L"Pinned", 1, settingsFile_.c_str()) != 0;
        }
        const RECT native{ rect.left, rect.top, rect.right, rect.bottom };
        MONITORINFO monitor{ sizeof(MONITORINFO) };
        if (GetMonitorInfoW(MonitorFromRect(&native, MONITOR_DEFAULTTONEAREST), &monitor))
            return Policy::FitToWorkArea(rect, { monitor.rcWork.left, monitor.rcWork.top,
                monitor.rcWork.right, monitor.rcWork.bottom });
        return rect;
    }

    void SaveWindow() {
        if (selfTest_ || !window_ || settingsFile_.empty()) return;
        RECT rect{};
        if (IsIconic(window_) || IsZoomed(window_)) {
            WINDOWPLACEMENT placement{ sizeof(WINDOWPLACEMENT) };
            if (!GetWindowPlacement(window_, &placement)) return;
            rect = placement.rcNormalPosition;
            // WINDOWPLACEMENT uses workspace coordinates for an ordinary top-level window.
            MONITORINFO monitor{ sizeof(MONITORINFO) };
            if (GetMonitorInfoW(MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST), &monitor))
                OffsetRect(&rect, monitor.rcWork.left - monitor.rcMonitor.left,
                    monitor.rcWork.top - monitor.rcMonitor.top);
        } else if (!GetWindowRect(window_, &rect)) return;
        const auto write = [this](const wchar_t* key, int value) {
            const auto text = std::to_wstring(value);
            WritePrivateProfileStringW(L"Window", key, text.c_str(), settingsFile_.c_str());
        };
        write(L"Left", rect.left); write(L"Top", rect.top);
        write(L"Width", rect.right - rect.left); write(L"Height", rect.bottom - rect.top);
        write(L"Pinned", pinned_ ? 1 : 0);
    }

    HWND Control(const wchar_t* text, DWORD style, int id, const wchar_t* type = L"BUTTON") {
        const HWND control = CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style,
            0, 0, 1, 1, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            instance_, nullptr);
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        return control;
    }

    void SetStatus(const std::wstring& text) {
        status_ = text;
        if (statusControl_) SetWindowTextW(statusControl_, status_.c_str());
        if (window_) InvalidateRect(window_, nullptr, TRUE);
    }

    RECT BrowserBounds() const {
        RECT rect{};
        GetClientRect(window_, &rect);
        rect.top = Scale(42);
        rect.bottom = std::max(rect.top, rect.bottom - Scale(28));
        return rect;
    }

    void Resize() {
        if (!window_) return;
        RECT client{};
        GetClientRect(window_, &client);
        MoveWindow(home_, Scale(8), Scale(7), Scale(82), Scale(28), TRUE);
        MoveWindow(reload_, Scale(98), Scale(7), Scale(90), Scale(28), TRUE);
        MoveWindow(pinControl_, Scale(202), Scale(7), Scale(205), Scale(28), TRUE);
        MoveWindow(statusControl_, Scale(8), std::max(0L, client.bottom - Scale(24)),
            std::max(1L, client.right - Scale(16)), Scale(22), TRUE);
        if (controller_) {
            const RECT requested = BrowserBounds();
            const HRESULT result = controller_->put_Bounds(requested);
            RECT actual{};
            boundsVerified_ = SUCCEEDED(result) && SUCCEEDED(controller_->get_Bounds(&actual)) &&
                EqualRect(&requested, &actual);
            if (selfTest_ && !boundsVerified_) Fail(L"Bornes WebView2 invalides.", E_FAIL);
        }
    }

    void Fail(const std::wstring& message, HRESULT result) {
        const auto detail = message + L" (" + ErrorCode(result) + L")";
        Log(detail);
        initializing_ = false;
        ready_ = false;
        if (controller_) controller_->put_IsVisible(FALSE);
        SetStatus(detail);
        if (selfTest_) {
            exitCode_ = 3;
            PostMessageW(window_, WM_CLOSE, 0, 0);
        }
    }

    HRESULT ConfigureBrowser() {
        ComPtr<ICoreWebView2Settings> settings;
        HRESULT result = webview_->get_Settings(&settings);
        if (FAILED(result)) return result;
        if (FAILED(result = settings->put_IsWebMessageEnabled(FALSE))) return result;
        if (FAILED(result = settings->put_AreHostObjectsAllowed(FALSE))) return result;
        settings->put_AreDevToolsEnabled(FALSE);
        settings->put_AreDefaultContextMenusEnabled(FALSE);
        EventRegistrationToken token{};
        result = webview_->add_NavigationStarting(
            Callback<ICoreWebView2NavigationStartingEventHandler>(
                [this](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                    LPWSTR uri = nullptr;
                    const HRESULT got = args->get_Uri(&uri);
                    const bool allowed = SUCCEEDED(got) && uri && Policy::AllowedNavigation(uri) &&
                        (!selfTest_ || std::wstring_view(uri) == L"about:blank");
                    CoTaskMemFree(uri);
                    if (!allowed) {
                        args->put_Cancel(TRUE);
                        SetStatus(L"Lien externe bloqué : cette fenêtre est réservée à DofusDB.");
                    }
                    return S_OK;
                }).Get(), &token);
        if (FAILED(result)) return result;
        result = webview_->add_NewWindowRequested(
            Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                [this](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                    args->put_Handled(TRUE);
                    LPWSTR uri = nullptr;
                    if (!selfTest_ && SUCCEEDED(args->get_Uri(&uri)) && uri) {
                        if (Policy::AllowedNavigation(uri)) webview_->Navigate(uri);
                        else SetStatus(L"Lien externe bloqué : cette fenêtre est réservée à DofusDB.");
                    }
                    CoTaskMemFree(uri);
                    return S_OK;
                }).Get(), &token);
        if (FAILED(result)) return result;
        result = webview_->add_PermissionRequested(
            Callback<ICoreWebView2PermissionRequestedEventHandler>(
                [](ICoreWebView2*, ICoreWebView2PermissionRequestedEventArgs* args) -> HRESULT {
                    return args->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY);
                }).Get(), &token);
        if (FAILED(result)) return result;
        ComPtr<ICoreWebView2_4> downloadBrowser;
        if (SUCCEEDED(webview_.As(&downloadBrowser))) {
            result = downloadBrowser->add_DownloadStarting(
                Callback<ICoreWebView2DownloadStartingEventHandler>(
                    [this](ICoreWebView2*, ICoreWebView2DownloadStartingEventArgs* args) -> HRESULT {
                        args->put_Cancel(TRUE);
                        args->put_Handled(TRUE);
                        SetStatus(L"Les téléchargements sont désactivés dans cette fenêtre.");
                        return S_OK;
                    }).Get(), &token);
            if (FAILED(result)) return result;
        } else return E_NOINTERFACE;
        result = webview_->add_ProcessFailed(
            Callback<ICoreWebView2ProcessFailedEventHandler>(
                [this](ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs*) -> HRESULT {
                    Fail(L"Le navigateur s’est arrêté. Ferme puis rouvre cette fenêtre.", E_FAIL);
                    return S_OK;
                }).Get(), &token);
        if (FAILED(result)) return result;
        return webview_->add_NavigationCompleted(
            Callback<ICoreWebView2NavigationCompletedEventHandler>(
                [this](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                    if (closing_) return S_OK;
                    BOOL success = FALSE;
                    args->get_IsSuccess(&success);
                    if (!success) {
                        COREWEBVIEW2_WEB_ERROR_STATUS error{};
                        args->get_WebErrorStatus(&error);
                        // A rejected external link must not hide the existing DofusDB page.
                        if (error == COREWEBVIEW2_WEB_ERROR_STATUS_OPERATION_CANCELED && ready_)
                            return S_OK;
                        Fail(L"Page indisponible. Vérifie la connexion puis clique sur Actualiser.",
                            HRESULT_FROM_WIN32(static_cast<DWORD>(error)));
                        return S_OK;
                    }
                    navigationVerified_ = true;
                    ready_ = true;
                    controller_->put_IsVisible(!selfTest_ && !IsIconic(window_) ? TRUE : FALSE);
                    SetStatus(L"DofusDB · Saisis les coordonnées, la direction et l’indice.");
                    if (selfTest_) {
                        LPWSTR source = nullptr;
                        const bool blank = SUCCEEDED(webview_->get_Source(&source)) && source &&
                            std::wstring_view(source) == L"about:blank";
                        CoTaskMemFree(source);
                        if (!blank || IsWindowVisible(window_) || !boundsVerified_)
                            Fail(L"Le test doit rester masqué et limité à about:blank.", E_FAIL);
                        else PostMessageW(window_, SelfTestComplete, 0, 0);
                    }
                    return S_OK;
                }).Get(), &token);
    }

    void InitializeBrowser() {
        if (closing_ || initializing_) return;
        initializing_ = true;
        SetStatus(selfTest_ ? L"Test local about:blank…" : L"Chargement de DofusDB…");
        LPWSTR version = nullptr;
        const HRESULT runtime = GetAvailableCoreWebView2BrowserVersionString(nullptr, &version);
        CoTaskMemFree(version);
        if (FAILED(runtime)) {
            Fail(L"Microsoft Edge WebView2 Runtime manque. Installe-le puis rouvre cette fenêtre.",
                runtime);
            if (!selfTest_ && MessageBoxW(window_,
                L"Le moteur Microsoft Edge WebView2 Runtime est nécessaire.\n\n"
                L"Ouvrir la page officielle de téléchargement ?",
                L"Chasse au trésor — DofusDB", MB_YESNO | MB_ICONINFORMATION) == IDYES)
                ShellExecuteW(window_, L"open", RuntimeUrl, nullptr, nullptr, SW_SHOWNORMAL);
            return;
        }
        const HRESULT result = CreateCoreWebView2EnvironmentWithOptions(nullptr,
            profile_.c_str(), nullptr,
            Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [this](HRESULT status, ICoreWebView2Environment* environment) -> HRESULT {
                    if (closing_) return S_OK;
                    if (FAILED(status) || !environment) {
                        Fail(L"Impossible de créer le navigateur. Ferme puis rouvre cette fenêtre.",
                            FAILED(status) ? status : E_POINTER);
                        return S_OK;
                    }
                    environment_ = environment;
                    const HRESULT create = environment_->CreateCoreWebView2Controller(window_,
                        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                            [this](HRESULT created, ICoreWebView2Controller* controller) -> HRESULT {
                                if (closing_) {
                                    if (controller) controller->Close();
                                    return S_OK;
                                }
                                if (FAILED(created) || !controller) {
                                    Fail(L"Impossible d’afficher le navigateur.",
                                        FAILED(created) ? created : E_POINTER);
                                    return S_OK;
                                }
                                controller_ = controller;
                                const HRESULT got = controller_->get_CoreWebView2(&webview_);
                                if (FAILED(got) || !webview_) {
                                    Fail(L"Navigateur indisponible.", FAILED(got) ? got : E_POINTER);
                                    return S_OK;
                                }
                                webview_->get_BrowserProcessId(&browserProcessId_);
                                controller_->put_IsVisible(FALSE);
                                const HRESULT configured = ConfigureBrowser();
                                if (FAILED(configured)) {
                                    Fail(L"Configuration du navigateur impossible.", configured);
                                    return S_OK;
                                }
                                Resize();
                                if (closing_) return S_OK;
                                initializing_ = false;
                                const HRESULT navigated = webview_->Navigate(
                                    selfTest_ ? L"about:blank" : Policy::HomeUrl);
                                if (FAILED(navigated)) Fail(L"Ouverture de la page impossible.", navigated);
                                return S_OK;
                            }).Get());
                    if (FAILED(create)) Fail(L"Création de la fenêtre du navigateur impossible.", create);
                    return S_OK;
                }).Get());
        if (FAILED(result)) Fail(L"Initialisation du navigateur impossible.", result);
    }

    void CloseBrowser() {
        if (browserClosed_) return;
        browserClosed_ = true;
        closing_ = true;
        ScopedHandle browser;
        // This PID is provided by our WebView2 instance, never by a game process.
        if (selfTest_ && browserProcessId_)
            browser.value = OpenProcess(SYNCHRONIZE, FALSE, browserProcessId_);
        closeVerified_ = controller_ && SUCCEEDED(controller_->Close());
        webview_.Reset();
        controller_.Reset();
        environment_.Reset();
        if (selfTest_ && browser.value) WaitForSingleObject(browser.value, 2000);
    }

    void CleanupSelfTest() {
        if (!selfTest_ || root_.empty() || selfTestParent_.empty()) return;
        // Remove only the generated, absolute GUID directory under our test root.
        const auto absolute = fs::absolute(root_).lexically_normal();
        if (absolute.parent_path() != selfTestParent_ ||
            absolute.filename().wstring().size() != 38 ||
            absolute.filename().wstring().front() != L'{') {
            exitCode_ = 4;
            return;
        }
        std::error_code error;
        fs::remove_all(absolute, error);
        if (error) {
            Log(L"Nettoyage du profil de test impossible : " + absolute.wstring());
            exitCode_ = 4;
        }
    }

    int RunInitialized() {
        activateMessage_ = RegisterWindowMessageW(L"DofusMiniatures.TreasureOverlay.Activate.v1");
        if (ActivateExisting()) return exitCode_;
        if (!PrepareProfile()) return ReportStartupError(L"Profil du navigateur inaccessible.", 2);
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        WNDCLASSEXW definition{ sizeof(WNDCLASSEXW) };
        definition.lpfnWndProc = WindowProc;
        definition.hInstance = instance_;
        definition.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        definition.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        definition.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        definition.lpszClassName = Policy::WindowClass;
        if (!RegisterClassExW(&definition)) return ReportStartupError(L"Fenêtre indisponible.", 2);
        const auto bounds = InitialBounds();
        window_ = CreateWindowExW(!selfTest_ && pinned_ ? WS_EX_TOPMOST : 0,
            Policy::WindowClass, L"Chasse au trésor — DofusDB", WS_OVERLAPPEDWINDOW,
            bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top,
            nullptr, nullptr, instance_, this);
        if (!window_) {
            CleanupSelfTest();
            return ReportStartupError(L"Création de la fenêtre impossible.", 2);
        }
        font_ = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        home_ = Control(L"Chasse", BS_PUSHBUTTON | WS_TABSTOP, HomeId);
        reload_ = Control(L"Actualiser", BS_PUSHBUTTON | WS_TABSTOP, ReloadId);
        pinControl_ = Control(L"Toujours au-dessus", BS_AUTOCHECKBOX | WS_TABSTOP, PinId);
        SendMessageW(pinControl_, BM_SETCHECK, pinned_ ? BST_CHECKED : BST_UNCHECKED, 0);
        statusControl_ = Control(status_.c_str(), SS_LEFT | SS_NOPREFIX, 0, L"STATIC");
        Resize();
        if (selfTest_) SetTimer(window_, TimeoutId, 20000, nullptr);
        else {
            ShowWindow(window_, SW_SHOWNORMAL);
            UpdateWindow(window_);
        }
        InitializeBrowser();
        MSG message{};
        BOOL read = 0;
        while ((read = GetMessageW(&message, nullptr, 0, 0)) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (read < 0) exitCode_ = 2;
        CloseBrowser();
        if (selfTest_) {
            if (exitCode_ == 0 && (!navigationVerified_ || !boundsVerified_ || !closeVerified_))
                exitCode_ = 3;
            CleanupSelfTest();
            std::wcerr << L"TreasureOverlay hidden self-test: "
                << (exitCode_ == 0 ? L"PASS" : L"FAIL") << L'\n';
        }
        UnregisterClassW(Policy::WindowClass, instance_);
        return exitCode_;
    }

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        auto* self = reinterpret_cast<Overlay*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Overlay*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
            self->window_ = window;
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(window, message, wparam, lparam);
        if (message == self->activateMessage_ && !self->selfTest_) {
            ShowWindow(window, IsIconic(window) ? SW_RESTORE : SW_SHOW);
            SetForegroundWindow(window);
            return 0;
        }
        switch (message) {
        case WM_SIZE:
            if (wparam != SIZE_MINIMIZED) self->Resize();
            if (self->controller_)
                self->controller_->put_IsVisible(!self->selfTest_ && self->ready_ &&
                    wparam != SIZE_MINIMIZED ? TRUE : FALSE);
            return 0;
        case WM_MOVE:
        case WM_MOVING:
            if (self->controller_) self->controller_->NotifyParentWindowPositionChanged();
            break;
        case WM_GETMINMAXINFO: {
            auto* limits = reinterpret_cast<MINMAXINFO*>(lparam);
            limits->ptMinTrackSize = { self->Scale(Policy::MinimumWidth),
                self->Scale(Policy::MinimumHeight) };
            return 0;
        }
        case WM_DPICHANGED: {
            const auto* rect = reinterpret_cast<RECT*>(lparam);
            SetWindowPos(window, nullptr, rect->left, rect->top,
                rect->right - rect->left, rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
            self->Resize();
            return 0;
        }
        case WM_EXITSIZEMOVE:
            self->SaveWindow();
            return 0;
        case WM_COMMAND:
            switch (LOWORD(wparam)) {
            case HomeId:
                if (self->webview_) self->webview_->Navigate(Policy::HomeUrl);
                return 0;
            case ReloadId:
                if (self->webview_) self->webview_->Reload();
                else self->InitializeBrowser();
                return 0;
            case PinId:
                self->pinned_ = SendMessageW(self->pinControl_, BM_GETCHECK, 0, 0) == BST_CHECKED;
                SetWindowPos(window, self->pinned_ ? HWND_TOPMOST : HWND_NOTOPMOST,
                    0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                self->SaveWindow();
                return 0;
            }
            break;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            const HDC dc = BeginPaint(window, &paint);
            if (!self->ready_) {
                RECT rect = self->BrowserBounds();
                InflateRect(&rect, -self->Scale(28), -self->Scale(32));
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
                SelectObject(dc, self->font_ ? self->font_ : GetStockObject(DEFAULT_GUI_FONT));
                DrawTextW(dc, self->status_.c_str(), -1, &rect,
                    DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
            }
            EndPaint(window, &paint);
            return 0;
        }
        case WM_TIMER:
            if (self->selfTest_ && wparam == TimeoutId)
                self->Fail(L"Délai du test local dépassé.", HRESULT_FROM_WIN32(ERROR_TIMEOUT));
            return 0;
        case SelfTestComplete:
            if (self->selfTest_) { self->exitCode_ = 0; PostMessageW(window, WM_CLOSE, 0, 0); }
            return 0;
        case WM_CLOSE:
            self->SaveWindow();
            self->closing_ = true;
            KillTimer(window, TimeoutId);
            self->CloseBrowser();
            DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            if (!self->selfTest_) self->exitCode_ = 0;
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(window, message, wparam, lparam);
    }
};
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    int count = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments) return 2;
    const bool selfTest = count == 2 && std::wstring_view(arguments[1]) == L"--self-test";
    const bool valid = count == 1 || selfTest;
    LocalFree(arguments);
    if (!valid) return 2;
    Overlay app(selfTest);
    return app.Run(instance);
}
