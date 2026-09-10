#pragma once

#include "processlens/Calculations.hpp"
#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace processlens {

enum class Action {
    None,
    Mode,
    Hide,
    Minimize,
    Settings,
    Pin,
    Pause,
    Search,
    ClearSearch,
    TopCpu,
    TopMemory,
    Sort,
    SelectProcess,
    CloseDetails,
    OpenFolder,
    CopyPath,
    EndProcess,
    LanguageRu,
    LanguageEn,
    AccentMint,
    AccentBlue,
    AccentViolet,
    OpacityDown,
    OpacityUp,
    Animations,
    Startup,
    ClickThrough,
    Refresh250,
    Refresh500,
    Refresh1000,
    Refresh2000,
    CancelEnd,
    ConfirmEnd
};

struct HitTarget {
    Action action{Action::None};
    unsigned value{};
    float left{}, top{}, right{}, bottom{};
    std::wstring label;
    bool Contains(float x, float y) const noexcept {
        return x >= left && x < right && y >= top && y < bottom;
    }
};

struct UiState {
    std::wstring search;
    bool searchFocused{};
    bool searchSelectAll{};
    bool paused{};
    bool settingsOpen{};
    bool confirmEnd{};
    bool topByMemory{};
    bool hotkeyAvailable{true};
    ProcessSortColumn sortColumn{ProcessSortColumn::Cpu};
    bool ascending{};
    std::optional<std::uint32_t> selectedPid;
    std::uint64_t selectedCreation{};
    int scrollOffset{};
    float mouseX{-1}, mouseY{-1};
    float reveal{1};
    float metricTransition{1};
    std::wstring notice;
    std::vector<ProcessMetrics> processes;
};

inline float EaseOutCubic(float progress) noexcept {
    const float p = 1.0f - std::clamp(progress, 0.0f, 1.0f);
    return 1.0f - p * p * p;
}

inline int VisibleRowCount(float height) noexcept {
    return std::max(0, static_cast<int>((height - 498.0f) / 38.0f));
}

inline int ClampScrollOffset(int offset, std::size_t count, int visibleRows) noexcept {
    return std::clamp(offset, 0, std::max(0, static_cast<int>(count) - visibleRows));
}

} // namespace processlens
