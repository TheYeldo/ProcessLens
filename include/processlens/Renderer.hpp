#pragma once

#include "processlens/MetricsSnapshot.hpp"
#include "processlens/Settings.hpp"
#include "processlens/UiState.hpp"

#include <Windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <array>
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
    void Render(const MetricsSnapshot& snapshot, const MetricsSnapshot& previous, const Settings& settings,
                const UiState& ui);
    HitTarget HitTest(float x, float y) const;
    const std::vector<HitTarget>& Targets() const noexcept { return targets_; }

  private:
    bool CreateDeviceResources();
    void Text(std::wstring_view text, D2D1_RECT_F rect, int font, D2D1_COLOR_F color,
              DWRITE_TEXT_ALIGNMENT alignment = DWRITE_TEXT_ALIGNMENT_LEADING);
    void Round(D2D1_RECT_F rect, float radius, D2D1_COLOR_F fill, bool border = false);
    void Line(float x1, float y1, float x2, float y2, D2D1_COLOR_F color, float width = 1);
    void Icon(std::wstring_view glyph, D2D1_RECT_F rect, D2D1_COLOR_F color);
    void Button(D2D1_RECT_F rect, Action action, std::wstring_view label, bool active = false,
                std::wstring_view glyph = {}, unsigned value = 0);
    void Target(D2D1_RECT_F rect, Action action, std::wstring_view label, unsigned value = 0);
    void Graph(D2D1_RECT_F rect, const std::vector<float>& values, float maximum, D2D1_COLOR_F color,
               bool grid = true);
    void Metric(D2D1_RECT_F rect, std::wstring_view label, std::wstring_view value,
                std::wstring_view subtitle, const std::vector<float>& history, D2D1_COLOR_F color,
                float maximum = 100);
    void Title();
    void Compact(const MetricsSnapshot& snapshot, const MetricsSnapshot& previous);
    void Expanded(const MetricsSnapshot& snapshot, const MetricsSnapshot& previous);
    void Details(const MetricsSnapshot& snapshot);
    void SettingsPanel();
    void Confirmation(const MetricsSnapshot& snapshot);
    void Footer(const MetricsSnapshot& snapshot);
    const wchar_t* T(const wchar_t* ru, const wchar_t* en) const;
    D2D1_COLOR_F Accent() const;
    bool Hover(D2D1_RECT_F rect) const;

    HWND window_{};
    float dpi_{96};
    float width_{}, height_{};
    float contentOffset_{};
    ID2D1Factory* factory_{};
    ID2D1HwndRenderTarget* target_{};
    ID2D1SolidColorBrush* brush_{};
    IDWriteFactory* writeFactory_{};
    IDWriteInlineObject* ellipsis_{};
    std::array<IDWriteTextFormat*, 7> fonts_{};
    IDWriteTextFormat* iconFont_{};
    const Settings* settings_{};
    const UiState* ui_{};
    std::vector<HitTarget> targets_;
};

} // namespace processlens
