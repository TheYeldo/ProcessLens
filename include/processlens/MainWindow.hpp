#pragma once
#include "processlens/MetricsCollector.hpp"
#include "processlens/Renderer.hpp"
#include "processlens/Settings.hpp"
#include "processlens/UiState.hpp"

#include <Windows.h>
#include <chrono>
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
    void Activate();
    void Hide();
    void SetMode(ViewMode mode);
    void SetAlwaysOnTop(bool enabled);
    void SetClickThrough(bool enabled);
    void SetStartWithWindows(bool enabled);
    void SetRefreshInterval(int milliseconds);
    void ShowTrayMenu(POINT point);
    void AddTrayIcon();
    void RemoveTrayIcon();
    void SaveWindowState();
    void RestoreVisiblePosition();
    void RefreshProcesses();
    void HandleAction(const HitTarget& hit);
    void HandleCharacter(wchar_t character);
    void HandleKey(WPARAM key);
    void SelectProcess(unsigned pid);
    void PrepareEndProcess();
    void ConfirmEndProcess();
    void CancelEndProcess();
    void OpenProcessFolder();
    void CopyProcessPath();
    void CopyText(const std::wstring& text);
    void Notice(const std::wstring& text);
    void StartAnimation(bool reveal);
    void TickAnimation();
    void ApplyOpacity();
    void SettingsChanged();
    bool AnimationsEnabled() const;
    const ProcessMetrics* SelectedProcess() const;
    const wchar_t* T(const wchar_t* ru, const wchar_t* en) const;
    float Scale() const noexcept;

    HINSTANCE instance_{};
    HWND window_{};
    Renderer renderer_;
    Settings settings_;
    UiState ui_;
    std::unique_ptr<MetricsCollector> collector_;
    std::shared_ptr<const MetricsSnapshot> snapshot_{std::make_shared<const MetricsSnapshot>()};
    std::shared_ptr<const MetricsSnapshot> previous_{snapshot_};
    std::chrono::steady_clock::time_point revealStart_{}, metricStart_{};
    bool revealAnimating_{}, metricAnimating_{};
    int keyboardIndex_{-1};
    HANDLE pendingTermination_{};
    UINT taskbarCreatedMessage_{};
    bool trayAdded_{};
};
} // namespace processlens
