#pragma once

#include "processlens/Calculations.hpp"
#include "processlens/MetricsCollector.hpp"
#include "processlens/Renderer.hpp"
#include "processlens/Settings.hpp"

#include <Windows.h>

#include <cstdint>
#include <memory>
#include <string>

namespace processlens {

class MainWindow {
public:
    explicit MainWindow(HINSTANCE instance);
    ~MainWindow();

    bool Create();
    void Show(int commandShow);
    HWND Handle() const noexcept { return window_; }

private:
    static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    void ToggleMode();
    void SetMode(ViewMode mode);
    void SetAlwaysOnTop(bool enabled);
    void SetClickThrough(bool enabled);
    void SetRefreshInterval(int milliseconds);
    void ShowTrayMenu(POINT screenPoint);
    void AddTrayIcon();
    void RemoveTrayIcon();
    void SaveWindowState();
    void RestoreVisiblePosition();
    void HandleClick(float x, float y);
    void HandleCharacter(wchar_t character);
    void SelectProcessAt(float x, float y);
    void EndSelectedProcess();
    std::vector<ProcessMetrics> VisibleProcesses() const;
    float Scale() const noexcept;

    HINSTANCE instance_{};
    HWND window_{};
    Renderer renderer_;
    Settings settings_;
    std::unique_ptr<MetricsCollector> collector_;
    ProcessSortColumn sortColumn_{ProcessSortColumn::Cpu};
    bool sortAscending_{false};
    bool topByMemory_{false};
    bool searchFocused_{false};
    std::wstring search_;
    std::uint32_t selectedPid_{};
    int scrollOffset_{};
    UINT taskbarCreatedMessage_{};
    bool trayAdded_{};
    bool exiting_{};
};

} // namespace processlens
