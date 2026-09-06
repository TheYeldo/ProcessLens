#pragma once

#include "processlens/Calculations.hpp"
#include "processlens/MetricsSnapshot.hpp"
#include "processlens/Settings.hpp"

#include <Windows.h>
#include <d2d1.h>
#include <dwrite.h>

#include <cstdint>
#include <string_view>
#include <vector>

namespace processlens {

class Renderer {
public:
    Renderer() = default;
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool Initialize(HWND window);
    void Resize(UINT width, UINT height);
    void SetDpi(float dpi);
    void DiscardDeviceResources();
    void Render(const MetricsSnapshot& snapshot,
                ViewMode mode,
                std::wstring_view search,
                ProcessSortColumn sortColumn,
                bool ascending,
                bool topByMemory,
                std::uint32_t selectedPid,
                int scrollOffset);

private:
    bool CreateDeviceResources();
    void DrawCompact(const MetricsSnapshot& snapshot, bool topByMemory);
    void DrawExpanded(const MetricsSnapshot& snapshot,
                      std::wstring_view search,
                      ProcessSortColumn sortColumn,
                      bool ascending,
                      std::uint32_t selectedPid,
                      int scrollOffset);
    void DrawTitleBar(ViewMode mode);
    void DrawMetricCard(D2D1_RECT_F rect, std::wstring_view label, std::wstring_view value,
                        const std::vector<float>& history, float maximum, D2D1_COLOR_F accent);
    void DrawGraph(D2D1_RECT_F rect, const std::vector<float>& values, float maximum, D2D1_COLOR_F color);
    void DrawText(std::wstring_view text, D2D1_RECT_F rect, IDWriteTextFormat* format,
                  D2D1_COLOR_F color, DWRITE_TEXT_ALIGNMENT alignment = DWRITE_TEXT_ALIGNMENT_LEADING);
    void FillRoundRect(D2D1_RECT_F rect, float radius, D2D1_COLOR_F color);

    HWND window_{};
    float dpi_{96.0f};
    ID2D1Factory* factory_{};
    ID2D1HwndRenderTarget* target_{};
    ID2D1SolidColorBrush* brush_{};
    IDWriteFactory* writeFactory_{};
    IDWriteTextFormat* titleFormat_{};
    IDWriteTextFormat* headingFormat_{};
    IDWriteTextFormat* bodyFormat_{};
    IDWriteTextFormat* smallFormat_{};
};

} // namespace processlens
