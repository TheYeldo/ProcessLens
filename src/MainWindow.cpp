#include "processlens/MainWindow.hpp"
#include "processlens/Formatting.hpp"
#include "processlens/LauncherShortcut.hpp"
#include "processlens/Logging.hpp"
#include "processlens/WindowProtocol.hpp"

#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <windowsx.h>
#include <algorithm>
#include <array>

namespace processlens {
namespace {
constexpr UINT MessageTray = WM_APP + 42, MessageMetrics = WM_APP + 43;
constexpr UINT_PTR AnimationTimer = 1, NoticeTimer = 2;
constexpr UINT TrayIconId = 1, HotkeyRestore = 1;
enum Command : UINT {
    CommandOpen = 100,
    CommandCompact,
    CommandExpanded,
    CommandAlwaysOnTop,
    CommandClickThrough,
    CommandStartup,
    CommandSettings,
    CommandPause,
    CommandExit
};
void ApplyCorners(HWND window, float scale) {
    const DWM_WINDOW_CORNER_PREFERENCE preference = DWMWCP_ROUND;
    DwmSetWindowAttribute(window, DWMWA_WINDOW_CORNER_PREFERENCE, &preference, sizeof(preference));
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(window, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    RECT r{};
    GetClientRect(window, &r);
    HRGN region = CreateRoundRectRgn(0, 0, r.right + 1, r.bottom + 1, static_cast<int>(32 * scale),
                                     static_cast<int>(32 * scale));
    if (!SetWindowRgn(window, region, TRUE))
        DeleteObject(region);
}
std::uint64_t FileTimeValue(const FILETIME& value) {
    return (static_cast<std::uint64_t>(value.dwHighDateTime) << 32) | value.dwLowDateTime;
}
} // namespace
MainWindow::MainWindow(HINSTANCE instance) : instance_(instance), settings_(Settings::Load()) {
    settings_.startWithWindows = IsStartupEnabled();
    settings_.width = std::max(settings_.width, settings_.mode == ViewMode::Compact ? 400 : 1000);
    settings_.height = std::max(settings_.height, settings_.mode == ViewMode::Compact ? 690 : 800);
}
MainWindow::~MainWindow() {
    if (collector_)
        collector_->Stop();
    CancelEndProcess();
    RemoveTrayIcon();
}
const wchar_t* MainWindow::T(const wchar_t* ru, const wchar_t* en) const {
    return Tr(settings_.language, ru, en);
}
float MainWindow::Scale() const noexcept {
    return window_ ? static_cast<float>(GetDpiForWindow(window_)) / 96 : 1;
}

bool MainWindow::Create() {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = WindowProcedure;
    wc.hInstance = instance_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(101));
    if (!wc.hIcon)
        wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIconSm = wc.hIcon;
    wc.lpszClassName = WindowClassName;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return false;
    RestoreVisiblePosition();
    DWORD ex = WS_EX_APPWINDOW | WS_EX_LAYERED;
    if (settings_.alwaysOnTop)
        ex |= WS_EX_TOPMOST;
    if (settings_.clickThrough)
        ex |= WS_EX_TRANSPARENT;
    window_ = CreateWindowExW(ex, WindowClassName, L"ProcessLens", WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX,
                              settings_.x, settings_.y, settings_.width, settings_.height, nullptr, nullptr,
                              instance_, this);
    if (!window_)
        return false;
    // CreateWindowEx establishes the actual monitor DPI. Enforce logical minimum
    // dimensions here too, not just during interactive resizing (WM_GETMINMAXINFO).
    settings_.width = std::max(
        settings_.width, static_cast<int>((settings_.mode == ViewMode::Compact ? 400 : 1000) * Scale()));
    settings_.height = std::max(
        settings_.height, static_cast<int>((settings_.mode == ViewMode::Compact ? 690 : 800) * Scale()));
    RestoreVisiblePosition();
    SetWindowPos(window_, nullptr, settings_.x, settings_.y, settings_.width, settings_.height,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    renderer_.SetDpi(static_cast<float>(GetDpiForWindow(window_)));
    if (!renderer_.Initialize(window_)) {
        DestroyWindow(window_);
        return false;
    }
    ApplyOpacity();
    ApplyCorners(window_, Scale());
    taskbarCreatedMessage_ = RegisterWindowMessageW(L"TaskbarCreated");
    AddTrayIcon();
    ui_.hotkeyAvailable =
        RegisterHotKey(window_, HotkeyRestore, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'O') != FALSE;
    collector_ = std::make_unique<MetricsCollector>([this] {
        if (window_)
            PostMessageW(window_, MessageMetrics, 0, 0);
    });
    collector_->SetInterval(std::chrono::milliseconds(settings_.refreshIntervalMs));
    collector_->Start();
    return true;
}
void MainWindow::Show(int commandShow) {
    if (commandShow == SW_HIDE) {
        ShowWindow(window_, SW_HIDE);
        return;
    }
    Activate();
}
void MainWindow::Activate() {
    if (settings_.clickThrough)
        SetClickThrough(false);
    const bool wasHidden = !IsWindowVisible(window_) || IsIconic(window_);
    if (wasHidden)
        StartAnimation(true);
    ShowWindow(window_, IsIconic(window_) ? SW_RESTORE : SW_SHOWNORMAL);
    SetForegroundWindow(window_);
    UpdateWindow(window_);
}
void MainWindow::Hide() {
    CancelEndProcess();
    SaveWindowState();
    KillTimer(window_, AnimationTimer);
    revealAnimating_ = metricAnimating_ = false;
    ui_.reveal = ui_.metricTransition = 1;
    ShowWindow(window_, SW_HIDE);
    ApplyOpacity();
}
bool MainWindow::AnimationsEnabled() const {
    BOOL enabled = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0);
    return settings_.animations && enabled != FALSE;
}
void MainWindow::StartAnimation(bool reveal) {
    if (!AnimationsEnabled()) {
        revealAnimating_ = metricAnimating_ = false;
        ui_.reveal = ui_.metricTransition = 1;
        KillTimer(window_, AnimationTimer);
        ApplyOpacity();
        InvalidateRect(window_, nullptr, FALSE);
        return;
    }
    if (reveal) {
        revealStart_ = std::chrono::steady_clock::now();
        revealAnimating_ = true;
        ui_.reveal = 0;
        ApplyOpacity();
    } else {
        metricStart_ = std::chrono::steady_clock::now();
        metricAnimating_ = true;
        ui_.metricTransition = 0;
    }
    if (!SetTimer(window_, AnimationTimer, 16, nullptr)) {
        revealAnimating_ = metricAnimating_ = false;
        ui_.reveal = ui_.metricTransition = 1;
        ApplyOpacity();
    }
}
void MainWindow::TickAnimation() {
    const auto now = std::chrono::steady_clock::now();
    const auto progress = [&](auto started, float duration) {
        return std::clamp(std::chrono::duration<float, std::milli>(now - started).count() / duration, 0.0f,
                          1.0f);
    };
    if (revealAnimating_) {
        const float p = progress(revealStart_, 240);
        ui_.reveal = EaseOutCubic(p);
        revealAnimating_ = p < 1;
        ApplyOpacity();
    }
    if (metricAnimating_) {
        const float p = progress(metricStart_, 180);
        ui_.metricTransition = EaseOutCubic(p);
        metricAnimating_ = p < 1;
    }
    InvalidateRect(window_, nullptr, FALSE);
    if (!revealAnimating_ && !metricAnimating_)
        KillTimer(window_, AnimationTimer);
}
void MainWindow::ApplyOpacity() {
    if (window_)
        SetLayeredWindowAttributes(
            window_, 0, static_cast<BYTE>(255 * settings_.opacityPercent / 100.0f * (.2f + .8f * ui_.reveal)),
            LWA_ALPHA);
}
LRESULT CALLBACK MainWindow::WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<MainWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->HandleMessage(message, wParam, lParam)
                : DefWindowProcW(window, message, wParam, lParam);
}
LRESULT MainWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    if (taskbarCreatedMessage_ && message == taskbarCreatedMessage_) {
        trayAdded_ = false;
        AddTrayIcon();
        return 0;
    }
    switch (message) {
    case WM_NCCALCSIZE:
        return 0;
    case WM_NCHITTEST: {
        POINT p{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(window_, &p);
        RECT r{};
        GetClientRect(window_, &r);
        const int border = static_cast<int>(6 * Scale());
        const bool l = p.x < border, right = p.x >= r.right - border, t = p.y < border,
                   b = p.y >= r.bottom - border;
        if (t && l)
            return HTTOPLEFT;
        if (t && right)
            return HTTOPRIGHT;
        if (b && l)
            return HTBOTTOMLEFT;
        if (b && right)
            return HTBOTTOMRIGHT;
        if (l)
            return HTLEFT;
        if (right)
            return HTRIGHT;
        if (t)
            return HTTOP;
        if (b)
            return HTBOTTOM;
        if (p.y / Scale() < 62 && p.x / Scale() < r.right / Scale() - 180)
            return HTCAPTION;
        return HTCLIENT;
    }
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST), &monitor);
        info->ptMinTrackSize.x =
            std::min(static_cast<LONG>((settings_.mode == ViewMode::Compact ? 400 : 1000) * Scale()),
                     monitor.rcWork.right - monitor.rcWork.left);
        info->ptMinTrackSize.y =
            std::min(static_cast<LONG>((settings_.mode == ViewMode::Compact ? 690 : 800) * Scale()),
                     monitor.rcWork.bottom - monitor.rcWork.top);
        return 0;
    }
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) {
            KillTimer(window_, AnimationTimer);
            return 0;
        }
        renderer_.Resize(LOWORD(lParam), HIWORD(lParam));
        ApplyCorners(window_, Scale());
        RefreshProcesses();
        InvalidateRect(window_, nullptr, FALSE);
        return 0;
    case WM_DPICHANGED: {
        const auto* rect = reinterpret_cast<RECT*>(lParam);
        renderer_.SetDpi(static_cast<float>(HIWORD(wParam)));
        SetWindowPos(window_, nullptr, rect->left, rect->top, rect->right - rect->left,
                     rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_DISPLAYCHANGE:
        SaveWindowState();
        RestoreVisiblePosition();
        SetWindowPos(window_, nullptr, settings_.x, settings_.y, settings_.width, settings_.height,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        BeginPaint(window_, &paint);
        renderer_.Render(*snapshot_, *previous_, settings_, ui_);
        EndPaint(window_, &paint);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case MessageMetrics:
        if (!ui_.paused) {
            previous_ = snapshot_;
            snapshot_ = collector_->Snapshot();
            RefreshProcesses();
            if (IsWindowVisible(window_) && !IsIconic(window_))
                StartAnimation(false);
            else
                ui_.metricTransition = 1;
        }
        return 0;
    case WM_TIMER:
        if (wParam == AnimationTimer)
            TickAnimation();
        else if (wParam == NoticeTimer) {
            ui_.notice.clear();
            KillTimer(window_, NoticeTimer);
            InvalidateRect(window_, nullptr, FALSE);
        }
        return 0;
    case MessageActivate:
        Activate();
        return 0;
    case WM_NCLBUTTONDBLCLK:
        if (wParam == HTCAPTION) {
            SetMode(settings_.mode == ViewMode::Compact ? ViewMode::Expanded : ViewMode::Compact);
            return 0;
        }
        break;
    case WM_LBUTTONDBLCLK:
        if (renderer_.HitTest(GET_X_LPARAM(lParam) / Scale(), GET_Y_LPARAM(lParam) / Scale()).action ==
                Action::None &&
            !ui_.settingsOpen && !ui_.confirmEnd)
            SetMode(settings_.mode == ViewMode::Compact ? ViewMode::Expanded : ViewMode::Compact);
        return 0;
    case WM_LBUTTONDOWN:
        SetFocus(window_);
        HandleAction(renderer_.HitTest(GET_X_LPARAM(lParam) / Scale(), GET_Y_LPARAM(lParam) / Scale()));
        return 0;
    case WM_MOUSEMOVE: {
        ui_.mouseX = GET_X_LPARAM(lParam) / Scale();
        ui_.mouseY = GET_Y_LPARAM(lParam) / Scale();
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window_, 0};
        TrackMouseEvent(&track);
        InvalidateRect(window_, nullptr, FALSE);
        return 0;
    }
    case WM_MOUSELEAVE:
        ui_.mouseX = ui_.mouseY = -1;
        InvalidateRect(window_, nullptr, FALSE);
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            const auto action = renderer_.HitTest(ui_.mouseX, ui_.mouseY).action;
            SetCursor(LoadCursorW(nullptr, action == Action::Search ? IDC_IBEAM
                                           : action == Action::None ? IDC_ARROW
                                                                    : IDC_HAND));
            return TRUE;
        }
        break;
    case WM_CHAR:
        HandleCharacter(static_cast<wchar_t>(wParam));
        return 0;
    case WM_KEYDOWN:
        HandleKey(wParam);
        return 0;
    case WM_MOUSEWHEEL:
        if (settings_.mode == ViewMode::Expanded && !ui_.settingsOpen && !ui_.confirmEnd) {
            ui_.scrollOffset -= GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA * 3;
            RefreshProcesses();
            InvalidateRect(window_, nullptr, FALSE);
        }
        return 0;
    case WM_EXITSIZEMOVE:
        SaveWindowState();
        return 0;
    case WM_CLOSE:
        Hide();
        return 0;
    case WM_HOTKEY:
        if (wParam == HotkeyRestore)
            Activate();
        return 0;
    case MessageTray: {
        // NOTIFYICON_VERSION_4 puts the event in LOWORD, the icon id in HIWORD.
        const auto event = LOWORD(lParam);
        if (event == NIN_SELECT || event == NIN_KEYSELECT || event == WM_LBUTTONUP ||
            event == WM_LBUTTONDBLCLK)
            Activate();
        else if (event == WM_CONTEXTMENU || event == WM_RBUTTONUP) {
            POINT point{};
            GetCursorPos(&point);
            ShowTrayMenu(point);
        }
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case CommandOpen:
            Activate();
            break;
        case CommandCompact:
            SetMode(ViewMode::Compact);
            Activate();
            break;
        case CommandExpanded:
            SetMode(ViewMode::Expanded);
            Activate();
            break;
        case CommandAlwaysOnTop:
            SetAlwaysOnTop(!settings_.alwaysOnTop);
            break;
        case CommandClickThrough:
            SetClickThrough(!settings_.clickThrough);
            break;
        case CommandStartup:
            SetStartWithWindows(!settings_.startWithWindows);
            break;
        case CommandSettings:
            ui_.settingsOpen = true;
            Activate();
            break;
        case CommandPause:
            HandleAction({Action::Pause});
            break;
        case CommandExit:
            DestroyWindow(window_);
            break;
        }
        return 0;
    case WM_DESTROY:
        KillTimer(window_, AnimationTimer);
        KillTimer(window_, NoticeTimer);
        UnregisterHotKey(window_, HotkeyRestore);
        if (collector_)
            collector_->Stop();
        CancelEndProcess();
        SaveWindowState();
        RemoveTrayIcon();
        window_ = nullptr;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window_, message, wParam, lParam);
}
void MainWindow::RefreshProcesses() {
    ui_.processes =
        FilterProcesses(snapshot_->processes, settings_.mode == ViewMode::Compact ? L"" : ui_.search);
    SortProcesses(ui_.processes,
                  settings_.mode == ViewMode::Compact
                      ? (ui_.topByMemory ? ProcessSortColumn::Memory : ProcessSortColumn::Cpu)
                      : ui_.sortColumn,
                  settings_.mode == ViewMode::Expanded && ui_.ascending);
    RECT r{};
    if (window_)
        GetClientRect(window_, &r);
    ui_.scrollOffset =
        ClampScrollOffset(ui_.scrollOffset, ui_.processes.size(), VisibleRowCount(r.bottom / Scale()));
}
void MainWindow::HandleAction(const HitTarget& hit) {
    using enum Action;
    if (hit.action != Search && hit.action != ClearSearch) {
        ui_.searchFocused = false;
        ui_.searchSelectAll = false;
    }
    switch (hit.action) {
    case Mode:
        SetMode(settings_.mode == ViewMode::Compact ? ViewMode::Expanded : ViewMode::Compact);
        break;
    case Hide:
        this->Hide();
        break;
    case Minimize:
        ShowWindow(window_, SW_MINIMIZE);
        break;
    case Settings:
        CancelEndProcess();
        ui_.settingsOpen = !ui_.settingsOpen;
        break;
    case Pin:
        SetAlwaysOnTop(!settings_.alwaysOnTop);
        break;
    case Pause:
        ui_.paused = !ui_.paused;
        if (!ui_.paused) {
            previous_ = snapshot_;
            snapshot_ = collector_->Snapshot();
            RefreshProcesses();
            StartAnimation(false);
        } else {
            ui_.metricTransition = 1;
            metricAnimating_ = false;
        }
        break;
    case Search:
        ui_.searchFocused = true;
        break;
    case ClearSearch:
        ui_.search.clear();
        ui_.searchFocused = true;
        ui_.searchSelectAll = false;
        ui_.scrollOffset = 0;
        RefreshProcesses();
        break;
    case TopCpu:
        ui_.topByMemory = false;
        RefreshProcesses();
        break;
    case TopMemory:
        ui_.topByMemory = true;
        RefreshProcesses();
        break;
    case Sort:
        if (ui_.sortColumn == static_cast<ProcessSortColumn>(hit.value))
            ui_.ascending = !ui_.ascending;
        else {
            ui_.sortColumn = static_cast<ProcessSortColumn>(hit.value);
            ui_.ascending = hit.value <= 1;
        }
        ui_.scrollOffset = 0;
        RefreshProcesses();
        break;
    case SelectProcess:
        this->SelectProcess(hit.value);
        break;
    case CloseDetails:
        ui_.selectedPid.reset();
        CancelEndProcess();
        break;
    case OpenFolder:
        OpenProcessFolder();
        break;
    case CopyPath:
        CopyProcessPath();
        break;
    case EndProcess:
        PrepareEndProcess();
        break;
    case CancelEnd:
        CancelEndProcess();
        break;
    case ConfirmEnd:
        ConfirmEndProcess();
        break;
    case LanguageRu:
        settings_.language = Language::Russian;
        SettingsChanged();
        break;
    case LanguageEn:
        settings_.language = Language::English;
        SettingsChanged();
        break;
    case AccentMint:
    case AccentBlue:
    case AccentViolet:
        settings_.accent = static_cast<int>(hit.action) - static_cast<int>(AccentMint);
        SettingsChanged();
        break;
    case OpacityDown:
        settings_.opacityPercent = std::max(70, settings_.opacityPercent - 5);
        SettingsChanged();
        break;
    case OpacityUp:
        settings_.opacityPercent = std::min(100, settings_.opacityPercent + 5);
        SettingsChanged();
        break;
    case Animations:
        settings_.animations = !settings_.animations;
        SettingsChanged();
        break;
    case Startup:
        SetStartWithWindows(!settings_.startWithWindows);
        break;
    case ClickThrough:
        ui_.settingsOpen = false;
        SetClickThrough(!settings_.clickThrough);
        break;
    case Refresh250:
        SetRefreshInterval(250);
        break;
    case Refresh500:
        SetRefreshInterval(500);
        break;
    case Refresh1000:
        SetRefreshInterval(1000);
        break;
    case Refresh2000:
        SetRefreshInterval(2000);
        break;
    default:
        break;
    }
    InvalidateRect(window_, nullptr, FALSE);
}
void MainWindow::HandleCharacter(wchar_t ch) {
    if (!ui_.searchFocused || ui_.settingsOpen || ui_.confirmEnd || settings_.mode != ViewMode::Expanded)
        return;
    if (GetKeyState(VK_CONTROL) & 0x8000)
        return;
    if (ch == L'\b') {
        if (ui_.searchSelectAll)
            ui_.search.clear();
        else if (!ui_.search.empty())
            ui_.search.pop_back();
    } else if (ch >= L' ' && ch != 127 && ui_.search.size() < 128) {
        if (ui_.searchSelectAll)
            ui_.search.clear();
        ui_.search.push_back(ch);
    } else
        return;
    ui_.searchSelectAll = false;
    ui_.scrollOffset = 0;
    RefreshProcesses();
    InvalidateRect(window_, nullptr, FALSE);
}
void MainWindow::HandleKey(WPARAM key) {
    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    if (key == VK_ESCAPE) {
        if (ui_.confirmEnd)
            CancelEndProcess();
        else if (ui_.settingsOpen)
            ui_.settingsOpen = false;
        else if (ui_.selectedPid)
            ui_.selectedPid.reset();
        else if (!ui_.search.empty()) {
            ui_.search.clear();
            RefreshProcesses();
        } else
            Hide();
    } else if (ctrl && key == 'F' && !ui_.settingsOpen && !ui_.confirmEnd) {
        if (settings_.mode == ViewMode::Compact)
            SetMode(ViewMode::Expanded);
        ui_.searchFocused = true;
        ui_.searchSelectAll = true;
    } else if (ui_.searchFocused && ctrl && key == 'A')
        ui_.searchSelectAll = true;
    else if (ui_.searchFocused && ctrl && key == 'C')
        CopyText(ui_.search);
    else if (ui_.searchFocused && ctrl && key == 'V') {
        if (OpenClipboard(window_)) {
            HANDLE data = GetClipboardData(CF_UNICODETEXT);
            if (data) {
                const auto* text = static_cast<const wchar_t*>(GlobalLock(data));
                if (text) {
                    if (ui_.searchSelectAll)
                        ui_.search.clear();
                    for (std::size_t i = 0; text[i] && ui_.search.size() < 128; ++i)
                        if (text[i] >= L' ')
                            ui_.search += text[i];
                    GlobalUnlock(data);
                }
            }
            CloseClipboard();
        }
        ui_.searchSelectAll = false;
        ui_.scrollOffset = 0;
        RefreshProcesses();
    } else if (key == VK_SPACE && !ui_.searchFocused && !ui_.settingsOpen && !ui_.confirmEnd)
        HandleAction({Action::Pause});
    else if (ctrl && key == VK_OEM_COMMA)
        HandleAction({Action::Settings});
    else if (key == VK_TAB) {
        const auto& targets = renderer_.Targets();
        if (!targets.empty()) {
            const int direction = (GetKeyState(VK_SHIFT) & 0x8000) ? -1 : 1;
            keyboardIndex_ = (keyboardIndex_ + direction + static_cast<int>(targets.size())) %
                             static_cast<int>(targets.size());
            const auto& hit = targets[static_cast<std::size_t>(keyboardIndex_)];
            ui_.mouseX = (hit.left + hit.right) / 2;
            ui_.mouseY = (hit.top + hit.bottom) / 2;
            ui_.searchFocused = hit.action == Action::Search;
        }
    } else if (key == VK_RETURN && !ui_.searchFocused) {
        // Destructive confirmation requires an explicit click on its labelled button.
        const auto hit = renderer_.HitTest(ui_.mouseX, ui_.mouseY);
        if (hit.action != Action::ConfirmEnd)
            HandleAction(hit);
    } else if ((key == VK_DOWN || key == VK_UP) && !ui_.settingsOpen && !ui_.confirmEnd &&
               !ui_.searchFocused) {
        if (!ui_.processes.empty()) {
            auto found = std::find_if(ui_.processes.begin(), ui_.processes.end(),
                                      [&](const auto& p) { return ui_.selectedPid == p.pid; });
            int index = found == ui_.processes.end()
                            ? 0
                            : static_cast<int>(std::distance(ui_.processes.begin(), found)) +
                                  (key == VK_DOWN ? 1 : -1);
            index = std::clamp(index, 0, static_cast<int>(ui_.processes.size()) - 1);
            SelectProcess(ui_.processes[static_cast<std::size_t>(index)].pid);
            RECT r{};
            GetClientRect(window_, &r);
            const int rows = VisibleRowCount(r.bottom / Scale());
            if (index < ui_.scrollOffset)
                ui_.scrollOffset = index;
            if (index >= ui_.scrollOffset + rows)
                ui_.scrollOffset = index - rows + 1;
            RefreshProcesses();
        }
    }
    InvalidateRect(window_, nullptr, FALSE);
}
void MainWindow::SelectProcess(unsigned pid) {
    CancelEndProcess();
    const auto found = std::find_if(snapshot_->processes.begin(), snapshot_->processes.end(),
                                    [&](const auto& p) { return p.pid == pid; });
    if (found == snapshot_->processes.end())
        return;
    const auto creation = found->creationTime;
    if (settings_.mode == ViewMode::Compact)
        SetMode(ViewMode::Expanded);
    ui_.selectedPid = pid;
    ui_.selectedCreation = creation;
}
const ProcessMetrics* MainWindow::SelectedProcess() const {
    if (!ui_.selectedPid)
        return nullptr;
    const auto found =
        std::find_if(snapshot_->processes.begin(), snapshot_->processes.end(), [&](const auto& p) {
            return p.pid == *ui_.selectedPid && p.creationTime == ui_.selectedCreation;
        });
    return found == snapshot_->processes.end() ? nullptr : &*found;
}
void MainWindow::PrepareEndProcess() {
    CancelEndProcess();
    const auto* p = SelectedProcess();
    if (!p || p->pid == 0 || p->pid == 4 || p->pid == GetCurrentProcessId() || !p->creationTime) {
        Notice(T(L"Этот процесс нельзя завершить здесь.", L"This process cannot be ended here."));
        return;
    }
    HANDLE process = OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, p->pid);
    if (!process) {
        Notice(T(L"Windows запретила доступ к процессу.", L"Windows denied access to this process."));
        return;
    }
    FILETIME created{}, ended{}, kernel{}, user{};
    BOOL critical = FALSE;
    if (!GetProcessTimes(process, &created, &ended, &kernel, &user) ||
        FileTimeValue(created) != p->creationTime || !IsProcessCritical(process, &critical) || critical) {
        CloseHandle(process);
        Notice(T(L"Процесс завершён или защищён Windows.",
                 L"The process has exited or is protected by Windows."));
        return;
    }
    pendingTermination_ = process;
    ui_.confirmEnd = true;
    ui_.mouseX = ui_.mouseY = -1;
}
void MainWindow::CancelEndProcess() {
    if (pendingTermination_) {
        CloseHandle(pendingTermination_);
        pendingTermination_ = nullptr;
    }
    ui_.confirmEnd = false;
}
void MainWindow::ConfirmEndProcess() {
    if (!pendingTermination_)
        return;
    const bool success = TerminateProcess(pendingTermination_, 1) != FALSE;
    CancelEndProcess();
    if (success) {
        ui_.selectedPid.reset();
        Notice(T(L"Процесс завершён.", L"Process ended."));
    } else
        Notice(T(L"Windows не разрешила завершить процесс.", L"Windows did not allow the process to end."));
}
void MainWindow::OpenProcessFolder() {
    const auto* process = SelectedProcess();
    if (!process || process->executablePath.empty())
        return;
    PIDLIST_ABSOLUTE path{};
    if (SUCCEEDED(SHParseDisplayName(process->executablePath.c_str(), nullptr, &path, 0, nullptr))) {
        const HRESULT result = SHOpenFolderAndSelectItems(path, 0, nullptr, 0);
        CoTaskMemFree(path);
        if (FAILED(result))
            Notice(T(L"Не удалось открыть папку.", L"Unable to open the folder."));
    } else
        Notice(T(L"Файл больше не доступен.", L"The file is no longer available."));
}
void MainWindow::CopyText(const std::wstring& value) {
    if (value.empty() || !OpenClipboard(window_))
        return;
    const SIZE_T bytes = (value.size() + 1) * sizeof(wchar_t);
    HGLOBAL data = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (data) {
        void* memory = GlobalLock(data);
        if (memory) {
            memcpy(memory, value.c_str(), bytes);
            GlobalUnlock(data);
            if (EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, data))
                data = nullptr;
        }
        if (data)
            GlobalFree(data);
    }
    CloseClipboard();
}
void MainWindow::CopyProcessPath() {
    if (const auto* process = SelectedProcess(); process && !process->executablePath.empty()) {
        CopyText(process->executablePath);
        Notice(T(L"Путь скопирован.", L"Path copied."));
    }
}
void MainWindow::Notice(const std::wstring& text) {
    ui_.notice = text;
    SetTimer(window_, NoticeTimer, 4000, nullptr);
    InvalidateRect(window_, nullptr, FALSE);
}
void MainWindow::SetMode(ViewMode mode) {
    if (settings_.mode == mode)
        return;
    CancelEndProcess();
    settings_.mode = mode;
    ui_.settingsOpen = false;
    ui_.searchFocused = false;
    ui_.selectedPid.reset();
    settings_.width = static_cast<int>((mode == ViewMode::Compact ? 420 : 1180) * Scale());
    settings_.height = static_cast<int>((mode == ViewMode::Compact ? 700 : 820) * Scale());
    RECT rect{};
    GetWindowRect(window_, &rect);
    settings_.x = rect.left;
    settings_.y = rect.top;
    RestoreVisiblePosition();
    SetWindowPos(window_, nullptr, settings_.x, settings_.y, settings_.width, settings_.height,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    RefreshProcesses();
    SaveWindowState();
    StartAnimation(true);
}
void MainWindow::SettingsChanged() {
    settings_.Save();
    ApplyOpacity();
    if (!AnimationsEnabled()) {
        KillTimer(window_, AnimationTimer);
        revealAnimating_ = metricAnimating_ = false;
        ui_.reveal = ui_.metricTransition = 1;
        ApplyOpacity();
    }
    RemoveTrayIcon();
    AddTrayIcon();
}
void MainWindow::SetAlwaysOnTop(bool enabled) {
    settings_.alwaysOnTop = enabled;
    SetWindowPos(window_, enabled ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    settings_.Save();
}
void MainWindow::SetClickThrough(bool enabled) {
    settings_.clickThrough = enabled;
    LONG_PTR style = GetWindowLongPtrW(window_, GWL_EXSTYLE);
    if (enabled)
        style |= WS_EX_TRANSPARENT;
    else
        style &= ~static_cast<LONG_PTR>(WS_EX_TRANSPARENT);
    SetWindowLongPtrW(window_, GWL_EXSTYLE, style);
    SetWindowPos(window_, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    settings_.Save();
}
void MainWindow::SetStartWithWindows(bool enabled) {
    if (!SetStartupEnabled(enabled)) {
        Notice(T(L"Не удалось изменить автозапуск.", L"Unable to update startup."));
        return;
    }
    settings_.startWithWindows = enabled;
    settings_.Save();
}
void MainWindow::SetRefreshInterval(int milliseconds) {
    settings_.refreshIntervalMs = milliseconds;
    if (collector_)
        collector_->SetInterval(std::chrono::milliseconds(milliseconds));
    settings_.Save();
}
void MainWindow::AddTrayIcon() {
    if (!window_)
        return;
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = window_;
    data.uID = TrayIconId;
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    data.uCallbackMessage = MessageTray;
    data.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(101));
    if (!data.hIcon)
        data.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(data.szTip, T(L"ProcessLens · Ctrl+Alt+O — открыть", L"ProcessLens · Ctrl+Alt+O to open"));
    trayAdded_ = Shell_NotifyIconW(NIM_ADD, &data) != FALSE;
    if (trayAdded_) {
        data.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &data);
    }
}
void MainWindow::RemoveTrayIcon() {
    if (!trayAdded_ || !window_)
        return;
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = window_;
    data.uID = TrayIconId;
    Shell_NotifyIconW(NIM_DELETE, &data);
    trayAdded_ = false;
}
void MainWindow::ShowTrayMenu(POINT point) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, CommandOpen, T(L"Открыть ProcessLens", L"Open ProcessLens"));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, CommandCompact, T(L"Мини-виджет", L"Mini widget"));
    AppendMenuW(menu, MF_STRING, CommandExpanded, T(L"Подробный монитор", L"Expanded monitor"));
    AppendMenuW(menu, MF_STRING | (settings_.alwaysOnTop ? MF_CHECKED : 0), CommandAlwaysOnTop,
                T(L"Поверх окон", L"Always on top"));
    AppendMenuW(menu, MF_STRING | (settings_.clickThrough ? MF_CHECKED : 0), CommandClickThrough,
                T(L"Пропускать клики · Ctrl+Alt+O", L"Click through · Ctrl+Alt+O"));
    AppendMenuW(menu, MF_STRING | (settings_.startWithWindows ? MF_CHECKED : 0), CommandStartup,
                T(L"Запуск вместе с Windows", L"Start with Windows"));
    AppendMenuW(menu, MF_STRING, CommandPause,
                ui_.paused ? T(L"Продолжить просмотр", L"Resume view")
                           : T(L"Приостановить просмотр", L"Pause view"));
    AppendMenuW(menu, MF_STRING, CommandSettings, T(L"Настройки…", L"Settings…"));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, CommandExit, T(L"Выйти", L"Exit"));
    SetForegroundWindow(window_);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, point.x, point.y, 0, window_, nullptr);
    PostMessageW(window_, WM_NULL, 0, 0);
    DestroyMenu(menu);
}
void MainWindow::SaveWindowState() {
    if (!window_ || IsIconic(window_))
        return;
    RECT r{};
    if (GetWindowRect(window_, &r)) {
        settings_.x = r.left;
        settings_.y = r.top;
        settings_.width = r.right - r.left;
        settings_.height = r.bottom - r.top;
    }
    settings_.Save();
}
void MainWindow::RestoreVisiblePosition() {
    RECT desired{settings_.x, settings_.y, settings_.x + settings_.width, settings_.y + settings_.height};
    MONITORINFO monitor{sizeof(monitor)};
    if (!GetMonitorInfoW(MonitorFromRect(&desired, MONITOR_DEFAULTTONEAREST), &monitor))
        return;
    settings_.width = std::min(settings_.width, static_cast<int>(monitor.rcWork.right - monitor.rcWork.left));
    settings_.height =
        std::min(settings_.height, static_cast<int>(monitor.rcWork.bottom - monitor.rcWork.top));
    settings_.x = std::clamp(settings_.x, static_cast<int>(monitor.rcWork.left),
                             static_cast<int>(monitor.rcWork.right) - settings_.width);
    settings_.y = std::clamp(settings_.y, static_cast<int>(monitor.rcWork.top),
                             static_cast<int>(monitor.rcWork.bottom) - settings_.height);
}
} // namespace processlens
