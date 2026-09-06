#include "processlens/MainWindow.hpp"

#include "processlens/Formatting.hpp"
#include "processlens/LauncherShortcut.hpp"
#include "processlens/Logging.hpp"
#include "processlens/WindowProtocol.hpp"

#include <dwmapi.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <chrono>

namespace processlens {
namespace {

constexpr UINT MessageTray = WM_APP + 42;
constexpr UINT MessageMetrics = WM_APP + 43;
constexpr UINT TrayIconId = 1;
constexpr UINT HotkeyRestore = 1;

enum Command : UINT {
    CommandOpen = 100,
    CommandCompact,
    CommandExpanded,
    CommandAlwaysOnTop,
    CommandClickThrough,
    CommandStartWithWindows,
    CommandRefresh250,
    CommandRefresh500,
    CommandRefresh1000,
    CommandRefresh2000,
    CommandExit
};

void ApplyRoundedCorners(HWND window, float scale) {
    const auto preference = DWMWCP_ROUND;
    DwmSetWindowAttribute(window, DWMWA_WINDOW_CORNER_PREFERENCE, &preference, sizeof(preference));
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(window, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

    RECT rect{};
    GetClientRect(window, &rect);
    HRGN region = CreateRoundRectRgn(0, 0, rect.right + 1, rect.bottom + 1,
                                     static_cast<int>(18 * scale), static_cast<int>(18 * scale));
    SetWindowRgn(window, region, TRUE); // The system owns region after success.
}

} // namespace

MainWindow::MainWindow(HINSTANCE instance)
    : instance_(instance), settings_(Settings::Load()) {
    settings_.startWithWindows = IsStartupEnabled();
}

MainWindow::~MainWindow() {
    if (collector_) collector_->Stop();
    RemoveTrayIcon();
}

bool MainWindow::Create() {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    windowClass.lpfnWndProc = WindowProcedure;
    windowClass.hInstance = instance_;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hbrBackground = nullptr;
    windowClass.lpszClassName = WindowClassName;
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    RestoreVisiblePosition();
    DWORD extendedStyle = WS_EX_APPWINDOW;
    if (settings_.alwaysOnTop) extendedStyle |= WS_EX_TOPMOST;
    if (settings_.clickThrough) extendedStyle |= WS_EX_TRANSPARENT;
    window_ = CreateWindowExW(extendedStyle, WindowClassName, L"ProcessLens",
                              WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX,
                              settings_.x, settings_.y, settings_.width, settings_.height,
                              nullptr, nullptr, instance_, this);
    if (!window_) return false;

    renderer_.Initialize(window_);
    renderer_.SetDpi(static_cast<float>(GetDpiForWindow(window_)));
    ApplyRoundedCorners(window_, Scale());
    taskbarCreatedMessage_ = RegisterWindowMessageW(L"TaskbarCreated");
    AddTrayIcon();
    if (!RegisterHotKey(window_, HotkeyRestore, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'O')) {
        LogWarningOnce(L"global-hotkey", L"Ctrl+Alt+O is already registered by another application");
    }
    collector_ = std::make_unique<MetricsCollector>([this] {
        if (window_) PostMessageW(window_, MessageMetrics, 0, 0);
    });
    collector_->SetInterval(std::chrono::milliseconds(settings_.refreshIntervalMs));
    collector_->Start();
    return true;
}

void MainWindow::Show(int commandShow) {
    ShowWindow(window_, commandShow);
    if (commandShow != SW_HIDE) UpdateWindow(window_);
}

float MainWindow::Scale() const noexcept {
    return window_ ? static_cast<float>(GetDpiForWindow(window_)) / 96.0f : 1.0f;
}

LRESULT CALLBACK MainWindow::WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    MainWindow* self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<MainWindow*>(create->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->HandleMessage(message, wParam, lParam)
                : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT MainWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == taskbarCreatedMessage_ && taskbarCreatedMessage_ != 0) {
        trayAdded_ = false;
        AddTrayIcon();
        return 0;
    }
    switch (message) {
    case WM_NCCALCSIZE:
        return 0;
    case WM_NCHITTEST: {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(window_, &point);
        RECT rect{};
        GetClientRect(window_, &rect);
        const int border = std::max(6, static_cast<int>(7 * Scale()));
        const bool left = point.x < border;
        const bool right = point.x >= rect.right - border;
        const bool top = point.y < border;
        const bool bottom = point.y >= rect.bottom - border;
        if (top && left) return HTTOPLEFT;
        if (top && right) return HTTOPRIGHT;
        if (bottom && left) return HTBOTTOMLEFT;
        if (bottom && right) return HTBOTTOMRIGHT;
        if (left) return HTLEFT;
        if (right) return HTRIGHT;
        if (top) return HTTOP;
        if (bottom) return HTBOTTOM;
        const float x = point.x / Scale();
        const float y = point.y / Scale();
        const float width = rect.right / Scale();
        if (y < 48.0f && x < width - 120.0f) return HTCAPTION;
        return HTCLIENT;
    }
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize.x = static_cast<LONG>((settings_.mode == ViewMode::Compact ? 340 : 760) * Scale());
        info->ptMinTrackSize.y = static_cast<LONG>((settings_.mode == ViewMode::Compact ? 530 : 560) * Scale());
        return 0;
    }
    case WM_SIZE: {
        renderer_.Resize(LOWORD(lParam), HIWORD(lParam));
        ApplyRoundedCorners(window_, Scale());
        InvalidateRect(window_, nullptr, FALSE);
        return 0;
    }
    case WM_DPICHANGED: {
        const auto* suggested = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(window_, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left, suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        renderer_.SetDpi(static_cast<float>(HIWORD(wParam)));
        ApplyRoundedCorners(window_, Scale());
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        BeginPaint(window_, &paint);
        const auto snapshot = collector_ ? collector_->Snapshot() : std::make_shared<const MetricsSnapshot>();
        renderer_.Render(*snapshot, settings_.mode, search_, sortColumn_, sortAscending_, topByMemory_, selectedPid_, scrollOffset_);
        EndPaint(window_, &paint);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case MessageMetrics:
        InvalidateRect(window_, nullptr, FALSE);
        return 0;
    case MessageActivate:
        if (settings_.clickThrough) SetClickThrough(false);
        ShowWindow(window_, IsIconic(window_) ? SW_RESTORE : SW_SHOWNORMAL);
        SetForegroundWindow(window_);
        FlashWindow(window_, TRUE);
        return 0;
    case WM_LBUTTONDBLCLK:
        ToggleMode();
        return 0;
    case WM_LBUTTONDOWN: {
        SetFocus(window_);
        const float scale = Scale();
        HandleClick(static_cast<float>(GET_X_LPARAM(lParam)) / scale,
                    static_cast<float>(GET_Y_LPARAM(lParam)) / scale);
        return 0;
    }
    case WM_CHAR:
        HandleCharacter(static_cast<wchar_t>(wParam));
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            if (selectedPid_) selectedPid_ = 0;
            else search_.clear();
            InvalidateRect(window_, nullptr, FALSE);
        }
        return 0;
    case WM_MOUSEWHEEL:
        if (settings_.mode == ViewMode::Expanded) {
            scrollOffset_ = std::max(0, scrollOffset_ - GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA * 3);
            InvalidateRect(window_, nullptr, FALSE);
        }
        return 0;
    case WM_EXITSIZEMOVE:
        SaveWindowState();
        return 0;
    case WM_CLOSE:
        ShowWindow(window_, SW_HIDE);
        return 0;
    case WM_HOTKEY:
        if (wParam == HotkeyRestore) {
            if (settings_.clickThrough) SetClickThrough(false);
            ShowWindow(window_, SW_SHOWNORMAL);
            SetForegroundWindow(window_);
        }
        return 0;
    case MessageTray:
        if (lParam == WM_LBUTTONUP || lParam == WM_LBUTTONDBLCLK) {
            if (settings_.clickThrough) SetClickThrough(false);
            ShowWindow(window_, SW_SHOWNORMAL);
            SetForegroundWindow(window_);
        } else if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
            POINT point{};
            GetCursorPos(&point);
            ShowTrayMenu(point);
        }
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case CommandOpen: SetClickThrough(false); ShowWindow(window_, SW_SHOWNORMAL); SetForegroundWindow(window_); break;
        case CommandCompact: SetMode(ViewMode::Compact); break;
        case CommandExpanded: SetMode(ViewMode::Expanded); break;
        case CommandAlwaysOnTop: SetAlwaysOnTop(!settings_.alwaysOnTop); break;
        case CommandClickThrough: SetClickThrough(!settings_.clickThrough); break;
        case CommandStartWithWindows: SetStartWithWindows(!settings_.startWithWindows); break;
        case CommandRefresh250: SetRefreshInterval(250); break;
        case CommandRefresh500: SetRefreshInterval(500); break;
        case CommandRefresh1000: SetRefreshInterval(1000); break;
        case CommandRefresh2000: SetRefreshInterval(2000); break;
        case CommandExit: exiting_ = true; DestroyWindow(window_); break;
        }
        return 0;
    case WM_DESTROY:
        UnregisterHotKey(window_, HotkeyRestore);
        if (collector_) collector_->Stop();
        SaveWindowState();
        RemoveTrayIcon();
        window_ = nullptr;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window_, message, wParam, lParam);
}

void MainWindow::HandleClick(float x, float y) {
    RECT pixels{};
    GetClientRect(window_, &pixels);
    const float width = pixels.right / Scale();
    const float height = pixels.bottom / Scale();
    if (y < 45 && x >= width - 42) {
        ShowWindow(window_, SW_HIDE);
    } else if (y < 45 && x >= width - 78) {
        ShowWindow(window_, SW_MINIMIZE);
    } else if (y < 45 && x >= width - 116) {
        ToggleMode();
    } else if (settings_.mode == ViewMode::Compact) {
        if (y >= 335 && y <= 375) {
            topByMemory_ = !topByMemory_;
            InvalidateRect(window_, nullptr, FALSE);
        }
    } else {
        if (selectedPid_ && x >= width - 58 && y >= 249 && y <= 302) {
            selectedPid_ = 0;
        } else if (selectedPid_ && x >= width - 284 && y >= height - 76) {
            EndSelectedProcess();
        } else if (y >= 201 && y <= 239) {
            searchFocused_ = true;
        } else if (y >= 249 && y <= 282) {
            const float panelRight = selectedPid_ ? width - 300.0f : width - 18.0f;
            const float tableWidth = panelRight - 18.0f;
            const std::array<float, 7> positions{18.0f, 18.0f + tableWidth * .34f, 18.0f + tableWidth * .44f,
                18.0f + tableWidth * .56f, 18.0f + tableWidth * .70f, 18.0f + tableWidth * .85f, panelRight};
            const std::array columns{ProcessSortColumn::Name, ProcessSortColumn::Pid, ProcessSortColumn::Cpu,
                                     ProcessSortColumn::Memory, ProcessSortColumn::Read, ProcessSortColumn::Write};
            for (std::size_t i = 0; i < columns.size(); ++i) {
                if (x >= positions[i] && x < positions[i + 1]) {
                    if (sortColumn_ == columns[i]) sortAscending_ = !sortAscending_;
                    else { sortColumn_ = columns[i]; sortAscending_ = columns[i] == ProcessSortColumn::Name; }
                    scrollOffset_ = 0;
                    break;
                }
            }
        } else if (y >= 288) {
            SelectProcessAt(x, y);
        } else {
            searchFocused_ = false;
        }
        InvalidateRect(window_, nullptr, FALSE);
    }
}

void MainWindow::HandleCharacter(wchar_t character) {
    if (!searchFocused_ || settings_.mode != ViewMode::Expanded) return;
    if (character == L'\b') {
        if (!search_.empty()) search_.pop_back();
    } else if (character >= L' ' && character != 127 && search_.size() < 128) {
        search_.push_back(character);
    }
    scrollOffset_ = 0;
    InvalidateRect(window_, nullptr, FALSE);
}

std::vector<ProcessMetrics> MainWindow::VisibleProcesses() const {
    const auto snapshot = collector_ ? collector_->Snapshot() : std::make_shared<const MetricsSnapshot>();
    auto processes = FilterProcesses(snapshot->processes, search_);
    SortProcesses(processes, sortColumn_, sortAscending_);
    return processes;
}

void MainWindow::SelectProcessAt(float x, float y) {
    RECT pixels{};
    GetClientRect(window_, &pixels);
    const float width = pixels.right / Scale();
    const float panelRight = selectedPid_ ? width - 300.0f : width - 18.0f;
    if (x > panelRight) return;
    const int row = static_cast<int>((y - 288.0f) / 29.0f) + scrollOffset_;
    const auto processes = VisibleProcesses();
    if (row >= 0 && row < static_cast<int>(processes.size())) selectedPid_ = processes[static_cast<std::size_t>(row)].pid;
}

void MainWindow::EndSelectedProcess() {
    const auto snapshot = collector_ ? collector_->Snapshot() : std::make_shared<const MetricsSnapshot>();
    const auto found = std::find_if(snapshot->processes.begin(), snapshot->processes.end(),
                                    [&](const auto& process) { return process.pid == selectedPid_; });
    if (found == snapshot->processes.end()) {
        MessageBoxW(window_, L"The process is no longer running.", L"ProcessLens", MB_OK | MB_ICONINFORMATION);
        selectedPid_ = 0;
        return;
    }
    const std::wstring question = L"End " + found->name + L"?\n\nPID: " + std::to_wstring(found->pid) +
                                  L"\n\nUnsaved data in this process may be lost.";
    if (MessageBoxW(window_, question.c_str(), L"Confirm End Process", MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) != IDOK) return;
    HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, found->pid);
    if (!process) {
        const DWORD error = GetLastError();
        MessageBoxW(window_, error == ERROR_ACCESS_DENIED ? L"Access denied." : L"Unable to open the process.",
                    L"ProcessLens", MB_OK | MB_ICONERROR);
        return;
    }
    const BOOL terminated = TerminateProcess(process, 1);
    CloseHandle(process);
    if (!terminated) MessageBoxW(window_, L"Windows did not allow this process to be ended.", L"ProcessLens", MB_OK | MB_ICONERROR);
    else selectedPid_ = 0;
}

void MainWindow::ToggleMode() {
    SetMode(settings_.mode == ViewMode::Compact ? ViewMode::Expanded : ViewMode::Compact);
}

void MainWindow::SetMode(ViewMode mode) {
    if (settings_.mode == mode) return;
    settings_.mode = mode;
    searchFocused_ = false;
    selectedPid_ = 0;
    RECT rect{};
    GetWindowRect(window_, &rect);
    const int width = static_cast<int>((mode == ViewMode::Compact ? 370 : 920) * Scale());
    const int height = static_cast<int>((mode == ViewMode::Compact ? 570 : 680) * Scale());
    SetWindowPos(window_, settings_.alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
                 rect.left, rect.top, width, height, SWP_NOACTIVATE);
    SaveWindowState();
    InvalidateRect(window_, nullptr, FALSE);
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
    if (enabled) style |= WS_EX_TRANSPARENT;
    else style &= ~static_cast<LONG_PTR>(WS_EX_TRANSPARENT);
    SetWindowLongPtrW(window_, GWL_EXSTYLE, style);
    SetWindowPos(window_, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    settings_.Save();
}

void MainWindow::SetStartWithWindows(bool enabled) {
    if (!SetStartupEnabled(enabled)) {
        MessageBoxW(window_, L"Windows could not update the per-user startup shortcut.",
                    L"ProcessLens", MB_OK | MB_ICONERROR);
        return;
    }
    settings_.startWithWindows = enabled;
    settings_.Save();
}

void MainWindow::SetRefreshInterval(int milliseconds) {
    settings_.refreshIntervalMs = milliseconds;
    if (collector_) collector_->SetInterval(std::chrono::milliseconds(milliseconds));
    settings_.Save();
}

void MainWindow::AddTrayIcon() {
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = window_;
    data.uID = TrayIconId;
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    data.uCallbackMessage = MessageTray;
    data.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(data.szTip, L"ProcessLens — Ctrl+Alt+O restores interaction");
    trayAdded_ = Shell_NotifyIconW(NIM_ADD, &data) != FALSE;
    if (trayAdded_) {
        data.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &data);
    }
}

void MainWindow::RemoveTrayIcon() {
    if (!trayAdded_ || !window_) return;
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = window_;
    data.uID = TrayIconId;
    Shell_NotifyIconW(NIM_DELETE, &data);
    trayAdded_ = false;
}

void MainWindow::ShowTrayMenu(POINT screenPoint) {
    HMENU menu = CreatePopupMenu();
    HMENU refresh = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, CommandOpen, L"Open ProcessLens");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (settings_.mode == ViewMode::Compact ? MF_CHECKED : 0), CommandCompact, L"Compact Mode");
    AppendMenuW(menu, MF_STRING | (settings_.mode == ViewMode::Expanded ? MF_CHECKED : 0), CommandExpanded, L"Expanded Mode");
    AppendMenuW(menu, MF_STRING | (settings_.alwaysOnTop ? MF_CHECKED : 0), CommandAlwaysOnTop, L"Always on Top");
    AppendMenuW(menu, MF_STRING | (settings_.clickThrough ? MF_CHECKED : 0), CommandClickThrough, L"Click-through (restore: Ctrl+Alt+O)");
    AppendMenuW(menu, MF_STRING | (settings_.startWithWindows ? MF_CHECKED : 0), CommandStartWithWindows, L"Start with Windows");
    AppendMenuW(refresh, MF_STRING | (settings_.refreshIntervalMs == 250 ? MF_CHECKED : 0), CommandRefresh250, L"250 ms (higher CPU use)");
    AppendMenuW(refresh, MF_STRING | (settings_.refreshIntervalMs == 500 ? MF_CHECKED : 0), CommandRefresh500, L"500 ms");
    AppendMenuW(refresh, MF_STRING | (settings_.refreshIntervalMs == 1000 ? MF_CHECKED : 0), CommandRefresh1000, L"1000 ms (default)");
    AppendMenuW(refresh, MF_STRING | (settings_.refreshIntervalMs == 2000 ? MF_CHECKED : 0), CommandRefresh2000, L"2000 ms");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(refresh), L"Settings / Refresh rate");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, CommandExit, L"Exit");
    SetForegroundWindow(window_);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, screenPoint.x, screenPoint.y, 0, window_, nullptr);
    PostMessageW(window_, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

void MainWindow::SaveWindowState() {
    if (!window_ || IsIconic(window_)) return;
    RECT rect{};
    if (GetWindowRect(window_, &rect)) {
        settings_.x = rect.left;
        settings_.y = rect.top;
        settings_.width = rect.right - rect.left;
        settings_.height = rect.bottom - rect.top;
    }
    settings_.Save();
}

void MainWindow::RestoreVisiblePosition() {
    RECT desired{settings_.x, settings_.y, settings_.x + settings_.width, settings_.y + settings_.height};
    HMONITOR monitor = MonitorFromRect(&desired, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) return;
    const int workWidth = static_cast<int>(info.rcWork.right - info.rcWork.left);
    const int workHeight = static_cast<int>(info.rcWork.bottom - info.rcWork.top);
    const int width = std::min(settings_.width, workWidth);
    const int height = std::min(settings_.height, workHeight);
    settings_.width = width;
    settings_.height = height;
    settings_.x = std::clamp(settings_.x, static_cast<int>(info.rcWork.left), static_cast<int>(info.rcWork.right) - width);
    settings_.y = std::clamp(settings_.y, static_cast<int>(info.rcWork.top), static_cast<int>(info.rcWork.bottom) - height);
}

} // namespace processlens
