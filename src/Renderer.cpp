#include "processlens/Renderer.hpp"
#include "processlens/Formatting.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace processlens {
namespace {
template <typename T> void Release(T*& object) {
    if (object) {
        object->Release();
        object = nullptr;
    }
}
D2D1_COLOR_F Color(UINT32 rgb, float alpha = 1) {
    return D2D1::ColorF(rgb, alpha);
}
D2D1_COLOR_F Ink() {
    return Color(0x0E1218);
}
D2D1_COLOR_F Surface() {
    return Color(0x171D26);
}
D2D1_COLOR_F Raised() {
    return Color(0x202936);
}
D2D1_COLOR_F Muted() {
    return Color(0x8996A9);
}
D2D1_COLOR_F White() {
    return Color(0xEDF3FA);
}
D2D1_COLOR_F Mint() {
    return Color(0x6BE5BB);
}
D2D1_COLOR_F Blue() {
    return Color(0x75B9FF);
}
D2D1_COLOR_F Violet() {
    return Color(0xB3A0FF);
}
D2D1_COLOR_F Amber() {
    return Color(0xF1BA76);
}
D2D1_COLOR_F Red() {
    return Color(0xFF8191);
}
D2D1_COLOR_F Alpha(D2D1_COLOR_F c, float alpha) {
    c.a = alpha;
    return c;
}
D2D1_RECT_F Rect(float l, float t, float r, float b) {
    return D2D1::RectF(l, t, r, b);
}
double Blend(double from, double to, float progress) {
    return from + (to - from) * progress;
}
} // namespace

Renderer::~Renderer() {
    DiscardDeviceResources();
    Release(ellipsis_);
    Release(iconFont_);
    for (auto*& font : fonts_)
        Release(font);
    Release(writeFactory_);
    Release(factory_);
}

bool Renderer::Initialize(HWND window) {
    window_ = window;
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &factory_)) ||
        FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(&writeFactory_))))
        return false;
    const std::array<float, 7> sizes{11, 12, 14, 16, 24, 32, 40};
    for (std::size_t i = 0; i < fonts_.size(); ++i) {
        if (FAILED(writeFactory_->CreateTextFormat(
                L"Segoe UI", nullptr, i >= 3 ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, sizes[i], L"ru-RU", &fonts_[i])))
            return false;
        fonts_[i]->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        fonts_[i]->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
    if (FAILED(writeFactory_->CreateTextFormat(L"Segoe MDL2 Assets", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                               DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14,
                                               L"en-US", &iconFont_)))
        return false;
    iconFont_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    iconFont_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    if (FAILED(writeFactory_->CreateEllipsisTrimmingSign(fonts_[2], &ellipsis_)))
        return false;
    const DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
    for (auto* font : fonts_)
        font->SetTrimming(&trimming, ellipsis_);
    return CreateDeviceResources();
}

bool Renderer::CreateDeviceResources() {
    if (target_ && brush_)
        return true;
    if (!factory_)
        return false;
    DiscardDeviceResources();
    RECT client{};
    GetClientRect(window_, &client);
    const auto properties = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_IGNORE), dpi_,
        dpi_);
    if (FAILED(factory_->CreateHwndRenderTarget(
            properties,
            D2D1::HwndRenderTargetProperties(
                window_, D2D1::SizeU(std::max(1L, client.right), std::max(1L, client.bottom))),
            &target_)))
        return false;
    if (FAILED(target_->CreateSolidColorBrush(White(), &brush_))) {
        DiscardDeviceResources();
        return false;
    }
    return true;
}
void Renderer::DiscardDeviceResources() {
    Release(brush_);
    Release(target_);
}
void Renderer::Resize(UINT width, UINT height) {
    if (target_ && FAILED(target_->Resize(D2D1::SizeU(std::max(1U, width), std::max(1U, height)))))
        DiscardDeviceResources();
}
void Renderer::SetDpi(float dpi) {
    dpi_ = dpi;
    if (target_)
        target_->SetDpi(dpi, dpi);
}
const wchar_t* Renderer::T(const wchar_t* ru, const wchar_t* en) const {
    return Tr(settings_->language, ru, en);
}
D2D1_COLOR_F Renderer::Accent() const {
    return settings_->accent == 1 ? Blue() : settings_->accent == 2 ? Violet() : Mint();
}
bool Renderer::Hover(D2D1_RECT_F r) const {
    const float y = ui_->mouseY - contentOffset_;
    return ui_->mouseX >= r.left && ui_->mouseX < r.right && y >= r.top && y < r.bottom;
}
void Renderer::Text(std::wstring_view text, D2D1_RECT_F rect, int font, D2D1_COLOR_F color,
                    DWRITE_TEXT_ALIGNMENT align) {
    if (rect.right <= rect.left || rect.bottom <= rect.top)
        return;
    fonts_[font]->SetTextAlignment(align);
    brush_->SetColor(color);
    target_->DrawTextW(text.data(), static_cast<UINT32>(text.size()), fonts_[font], rect, brush_,
                       D2D1_DRAW_TEXT_OPTIONS_CLIP);
}
void Renderer::Icon(std::wstring_view glyph, D2D1_RECT_F rect, D2D1_COLOR_F color) {
    brush_->SetColor(color);
    target_->DrawTextW(glyph.data(), static_cast<UINT32>(glyph.size()), iconFont_, rect, brush_);
}
void Renderer::Round(D2D1_RECT_F rect, float radius, D2D1_COLOR_F fill, bool border) {
    brush_->SetColor(fill);
    target_->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), brush_);
    if (border) {
        brush_->SetColor(Color(0xFFFFFF, .055f));
        target_->DrawRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), brush_, 1);
    }
}
void Renderer::Line(float x1, float y1, float x2, float y2, D2D1_COLOR_F color, float width) {
    brush_->SetColor(color);
    target_->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), brush_, width);
}
void Renderer::Target(D2D1_RECT_F r, Action action, std::wstring_view label, unsigned value) {
    targets_.push_back({action, value, r.left, r.top, r.right, r.bottom, std::wstring(label)});
}
void Renderer::Button(D2D1_RECT_F r, Action action, std::wstring_view label, bool active,
                      std::wstring_view glyph, unsigned value) {
    const bool hover = Hover(r);
    Round(r, 8, active ? Alpha(Accent(), .14f) : hover ? Raised() : Surface(), !active);
    if (!glyph.empty())
        Icon(glyph, r, active || hover ? Accent() : Muted());
    else
        Text(label, r, 1, active ? Accent() : hover ? White() : Muted(), DWRITE_TEXT_ALIGNMENT_CENTER);
    Target(r, action, label, value);
}
HitTarget Renderer::HitTest(float x, float y) const {
    y -= contentOffset_;
    for (auto it = targets_.rbegin(); it != targets_.rend(); ++it) {
        if (it->Contains(x, y))
            return *it;
    }
    return {};
}

void Renderer::Graph(D2D1_RECT_F r, const std::vector<float>& values, float maximum, D2D1_COLOR_F color,
                     bool grid) {
    if (r.right <= r.left || r.bottom <= r.top)
        return;
    if (grid)
        for (int i = 0; i < 3; ++i) {
            const float y = r.top + (r.bottom - r.top) * static_cast<float>(i) / 2;
            Line(r.left, y, r.right, y, Color(0xFFFFFF, .045f));
        }
    if (values.empty() || maximum <= 0)
        return;
    ID2D1PathGeometry* geometry{};
    ID2D1GeometrySink* sink{};
    if (FAILED(factory_->CreatePathGeometry(&geometry)) || FAILED(geometry->Open(&sink))) {
        Release(sink);
        Release(geometry);
        return;
    }
    const float step = (r.right - r.left) / 119.0f;
    const std::size_t count = std::min<std::size_t>(120, values.size());
    const float start = r.right - step * static_cast<float>(count - 1);
    auto point = [&](std::size_t index) {
        float value = values[values.size() - count + index];
        if (index + 1 == count && count > 1)
            value = static_cast<float>(Blend(values[values.size() - 2], value, ui_->metricTransition));
        return D2D1::Point2F(start + step * static_cast<float>(index),
                             r.bottom - std::clamp(value / maximum, 0.0f, 1.0f) * (r.bottom - r.top));
    };
    sink->BeginFigure(point(0), D2D1_FIGURE_BEGIN_HOLLOW);
    for (std::size_t i = 1; i < count; ++i) {
        const auto a = point(i - 1), b = point(i);
        const float middle = (a.x + b.x) * .5f;
        sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(middle, a.y), D2D1::Point2F(middle, b.y), b));
    }
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();
    brush_->SetColor(Alpha(color, .10f));
    target_->DrawGeometry(geometry, brush_, 6);
    brush_->SetColor(color);
    target_->DrawGeometry(geometry, brush_, 1.8f);
    Release(sink);
    Release(geometry);

    // A translucent area makes the curve readable without obscuring the grid.
    if (SUCCEEDED(factory_->CreatePathGeometry(&geometry)) && SUCCEEDED(geometry->Open(&sink))) {
        sink->BeginFigure(D2D1::Point2F(start, r.bottom), D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine(point(0));
        for (std::size_t i = 1; i < count; ++i) {
            const auto a = point(i - 1), b = point(i);
            const float middle = (a.x + b.x) * .5f;
            sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(middle, a.y), D2D1::Point2F(middle, b.y), b));
        }
        sink->AddLine(D2D1::Point2F(r.right, r.bottom));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();
        brush_->SetColor(Alpha(color, .07f));
        target_->FillGeometry(geometry, brush_);
    }
    Release(sink);
    Release(geometry);
    brush_->SetColor(color);
    target_->FillEllipse(D2D1::Ellipse(point(count - 1), 2.5f, 2.5f), brush_);
}

void Renderer::Metric(D2D1_RECT_F r, std::wstring_view label, std::wstring_view value,
                      std::wstring_view subtitle, const std::vector<float>& history, D2D1_COLOR_F color,
                      float maximum) {
    Round(r, 14, Surface(), true);
    Round(Rect(r.left + 16, r.top + 18, r.left + 21, r.top + 23), 2, color);
    Text(label, Rect(r.left + 29, r.top + 10, r.right - 14, r.top + 32), 1, Muted());
    const int font = r.right - r.left < 210 ? 4 : 5;
    Text(value, Rect(r.left + 16, r.top + 34, r.right - 14, r.top + 80), font, White());
    Text(subtitle, Rect(r.left + 16, r.top + 78, r.right - 14, r.top + 99), 0, Muted());
    Graph(Rect(r.left + 16, r.top + 108, r.right - 16, r.bottom - 14), history, maximum, color);
}
void Renderer::Title() {
    Round(Rect(20, 18, 46, 44), 8, Alpha(Accent(), .13f));
    Line(25, 33, 30, 33, Accent(), 1.8f);
    Line(30, 33, 33, 25, Accent(), 1.8f);
    Line(33, 25, 37, 37, Accent(), 1.8f);
    Line(37, 37, 41, 29, Accent(), 1.8f);
    Text(L"ProcessLens", Rect(56, 16, 190, 45), 3, White());
    if (width_ > 650)
        Text(L"2.0", Rect(181, 18, 227, 44), 0, Muted());
    Button(Rect(width_ - 168, 16, width_ - 136, 46), Action::Pin, T(L"Поверх окон", L"Always on top"),
           settings_->alwaysOnTop, L"\uE718");
    Button(Rect(width_ - 130, 16, width_ - 98, 46), Action::Settings, T(L"Настройки", L"Settings"),
           ui_->settingsOpen, L"\uE713");
    Button(Rect(width_ - 92, 16, width_ - 60, 46), Action::Minimize, T(L"Свернуть", L"Minimize"), false,
           L"\uE921");
    Button(Rect(width_ - 54, 16, width_ - 22, 46), Action::Hide, T(L"Скрыть в трей", L"Hide to tray"), false,
           L"\uE8BB");
    Line(20, 61, width_ - 20, 61, Color(0xFFFFFF, .055f));
}
void Renderer::Compact(const MetricsSnapshot& s, const MetricsSnapshot& p) {
    const auto lang = settings_->language;
    Text(T(L"Ваш компьютер, в моменте", L"Your PC, at a glance"), Rect(22, 74, width_ - 150, 102), 2,
         White());
    Button(Rect(width_ - 137, 74, width_ - 22, 102), Action::Mode, T(L"Подробнее  ↗", L"Expand  ↗"), true);
    const auto cpu = FormatPercent(Blend(p.cpuPercent, s.cpuPercent, ui_->metricTransition), 0, lang);
    Round(Rect(20, 116, width_ - 20, 242), 14, Surface(), true);
    Text(T(L"ПРОЦЕССОР", L"PROCESSOR"), Rect(36, 126, 200, 146), 0, Muted());
    Text(cpu, Rect(34, 148, 190, 205), 6, White());
    Text(std::to_wstring(s.logicalProcessors) + T(L" логических ядер", L" logical cores"),
         Rect(36, 207, width_ - 36, 230), 0, Muted());
    Graph(Rect(width_ * .48f, 147, width_ - 38, 213), s.cpuHistory, 100, Accent());
    const float mid = width_ * .5f;
    Round(Rect(20, 254, mid - 6, 378), 14, Surface(), true);
    Round(Rect(mid + 6, 254, width_ - 20, 378), 14, Surface(), true);
    Text(T(L"ПАМЯТЬ", L"MEMORY"), Rect(36, 264, mid - 20, 285), 0, Muted());
    Text(FormatPercent(Blend(p.memoryPercent, s.memoryPercent, ui_->metricTransition), 0, lang),
         Rect(34, 286, mid - 20, 325), 5, Violet());
    Text(FormatBytes(s.memoryUsed, lang) + L" / " + FormatBytes(s.memoryTotal, lang),
         Rect(36, 325, mid - 20, 344), 0, Muted());
    Graph(Rect(36, 351, mid - 22, 368), s.memoryHistory, 100, Violet(), false);
    Text(T(L"ВИДЕОКАРТА", L"GRAPHICS"), Rect(mid + 22, 264, width_ - 36, 285), 0, Muted());
    Text(s.gpuPercent ? FormatPercent(*s.gpuPercent, 0, lang) : L"—", Rect(mid + 20, 286, width_ - 36, 325),
         5, Amber());
    Text(s.gpuPercent ? T(L"Самый занятый движок", L"Busiest GPU engine")
                      : T(L"Счётчик недоступен", L"Counter unavailable"),
         Rect(mid + 22, 325, width_ - 36, 344), 0, Muted());
    Graph(Rect(mid + 22, 351, width_ - 36, 368), s.gpuHistory, 100, Amber(), false);
    Round(Rect(20, 390, width_ - 20, 468), 14, Surface(), true);
    Text(T(L"СЕТЬ", L"NETWORK"), Rect(36, 398, 120, 418), 0, Muted());
    const float networkMax =
        s.networkHistory.empty()
            ? 1
            : std::max(1.0f, *std::max_element(s.networkHistory.begin(), s.networkHistory.end()) * 1.2f);
    Graph(Rect(width_ - 140, 401, width_ - 36, 420), s.networkHistory, networkMax, Blue(), false);
    Text(L"↓ " + FormatRate(s.networkDownloadBytesPerSecond, lang), Rect(36, 430, mid + 12, 454), 2, Blue());
    Text(L"↑ " + FormatRate(s.networkUploadBytesPerSecond, lang), Rect(mid + 12, 430, width_ - 36, 454), 2,
         Muted(), DWRITE_TEXT_ALIGNMENT_TRAILING);
    Text(T(L"Активные процессы", L"Top processes"), Rect(22, 480, width_ - 153, 510), 2, White());
    Button(Rect(width_ - 141, 482, width_ - 85, 508), Action::TopCpu, L"CPU", !ui_->topByMemory);
    Button(Rect(width_ - 79, 482, width_ - 22, 508), Action::TopMemory, L"RAM", ui_->topByMemory);
    const float compactRowHeight = std::clamp((height_ - 38 - 516) / 5, 24.0f, 28.0f);
    for (std::size_t i = 0; i < std::min<std::size_t>(5, ui_->processes.size()); ++i) {
        const auto& process = ui_->processes[i];
        const float y = 516 + static_cast<float>(i) * compactRowHeight;
        if (y + compactRowHeight > height_ - 37)
            break;
        const auto row = Rect(20, y, width_ - 20, y + compactRowHeight);
        if (Hover(row))
            Round(row, 7, Raised());
        Text(std::to_wstring(i + 1), Rect(27, y, 45, y + 28), 0, Muted());
        Text(process.name, Rect(52, y, width_ - 125, y + 28), 1, White());
        Text(ui_->topByMemory ? FormatBytes(process.workingSet, lang)
                              : FormatPercent(process.cpuPercent, 1, lang),
             Rect(width_ - 125, y, width_ - 31, y + 28), 1, ui_->topByMemory ? Violet() : Accent(),
             DWRITE_TEXT_ALIGNMENT_TRAILING);
        Target(row, Action::SelectProcess, process.name, process.pid);
    }
}
void Renderer::Expanded(const MetricsSnapshot& s, const MetricsSnapshot& p) {
    const auto lang = settings_->language;
    Text(T(L"Всё под контролем.", L"Everything in view."), Rect(24, 78, width_ - 370, 119), 5, White());
    Text(T(L"Живая картина производительности вашего компьютера",
           L"A live view of your computer's performance"),
         Rect(25, 116, width_ - 370, 141), 1, Muted());
    Button(Rect(width_ - 278, 89, width_ - 159, 121), Action::Pause,
           ui_->paused ? T(L"Продолжить", L"Resume") : T(L"Пауза", L"Pause"), ui_->paused);
    Button(Rect(width_ - 149, 89, width_ - 24, 121), Action::Mode, T(L"Мини-виджет", L"Mini widget"));
    const float card = (width_ - 48 - 36) / 4;
    Metric(Rect(24, 154, 24 + card, 314), T(L"Процессор", L"Processor"),
           FormatPercent(Blend(p.cpuPercent, s.cpuPercent, ui_->metricTransition), 0, lang),
           std::to_wstring(s.logicalProcessors) + T(L" логических ядер", L" logical cores"), s.cpuHistory,
           Accent());
    Metric(Rect(36 + card, 154, 36 + card * 2, 314), T(L"Оперативная память", L"Memory"),
           FormatPercent(Blend(p.memoryPercent, s.memoryPercent, ui_->metricTransition), 0, lang),
           FormatBytes(s.memoryUsed, lang) + L" / " + FormatBytes(s.memoryTotal, lang), s.memoryHistory,
           Violet());
    Metric(Rect(48 + card * 2, 154, 48 + card * 3, 314), T(L"Видеокарта", L"Graphics"),
           s.gpuPercent ? FormatPercent(*s.gpuPercent, 0, lang) : L"—",
           s.gpuPercent ? T(L"Самый занятый движок", L"Busiest GPU engine")
                        : T(L"Счётчик недоступен", L"Counter unavailable"),
           s.gpuHistory, Amber());
    const float networkMax =
        s.networkHistory.empty()
            ? 1
            : std::max(1.0f, *std::max_element(s.networkHistory.begin(), s.networkHistory.end()) * 1.2f);
    Metric(Rect(60 + card * 3, 154, width_ - 24, 314), T(L"Сеть · получение", L"Network · download"),
           FormatRate(s.networkDownloadBytesPerSecond, lang),
           L"↑ " + FormatRate(s.networkUploadBytesPerSecond, lang), s.networkHistory, Blue(), networkMax);
    Text(T(L"БЕЗ ПЕРЕЗАГРУЗКИ", L"SYSTEM UPTIME"), Rect(26, 324, 177, 351), 0, Muted());
    Text(FormatUptime(s.uptimeSeconds, lang), Rect(178, 324, 355, 351), 1, White());
    Text(T(L"СВОБОДНО ПАМЯТИ", L"AVAILABLE MEMORY"), Rect(370, 324, 520, 351), 0, Muted());
    Text(FormatBytes(s.memoryAvailable, lang), Rect(525, 324, 657, 351), 1, Violet());
    Text(std::to_wstring(s.processes.size()) + T(L" процессов", L" processes"),
         Rect(width_ - 211, 324, width_ - 25, 351), 1, Muted(), DWRITE_TEXT_ALIGNMENT_TRAILING);

    Text(T(L"Процессы", L"Processes"), Rect(25, 370, 188, 404), 4, White());
    const auto searchRect = Rect(225, 367, width_ - 24, 405);
    Round(searchRect, 9, ui_->searchFocused ? Raised() : Surface(), true);
    if (ui_->searchFocused) {
        brush_->SetColor(Alpha(Accent(), .7f));
        target_->DrawRoundedRectangle(D2D1::RoundedRect(searchRect, 9, 9), brush_, 1);
    }
    Icon(L"\uE721", Rect(233, 372, 265, 400), Muted());
    if (ui_->searchSelectAll && !ui_->search.empty())
        Round(Rect(272, 373, width_ - 68, 400), 3, Alpha(Accent(), .18f));
    Text(ui_->search.empty() ? T(L"Поиск по имени или PID", L"Search by name or PID") : ui_->search,
         Rect(273, 370, width_ - 75, 402), 2, ui_->search.empty() ? Muted() : White());
    if (ui_->search.empty())
        Text(L"Ctrl+F", Rect(width_ - 100, 373, width_ - 36, 399), 0, Muted(),
             DWRITE_TEXT_ALIGNMENT_TRAILING);
    Target(searchRect, Action::Search, T(L"Поиск процессов", L"Search processes"));
    if (!ui_->search.empty())
        Button(Rect(width_ - 60, 372, width_ - 31, 400), Action::ClearSearch,
               T(L"Очистить поиск", L"Clear search"), false, L"\uE894");

    const float right = ui_->selectedPid ? width_ - 344 : width_ - 24;
    const float tableWidth = right - 24;
    const std::array<float, 7> columns{24,
                                       24 + tableWidth * .34f,
                                       24 + tableWidth * .43f,
                                       24 + tableWidth * .56f,
                                       24 + tableWidth * .71f,
                                       24 + tableWidth * .855f,
                                       right};
    Round(Rect(24, 420, right, 452), 8, Raised());
    const std::array labels{
        T(L"Процесс", L"Process"), L"PID", L"CPU", T(L"Память", L"Memory"), T(L"Чтение", L"Read"),
        T(L"Запись", L"Write")};
    for (unsigned i = 0; i < 6; ++i) {
        const bool active = static_cast<unsigned>(ui_->sortColumn) == i;
        const std::wstring label =
            std::wstring(labels[i]) + (active ? (ui_->ascending ? L" ↑" : L" ↓") : L"");
        Text(label, Rect(columns[i] + 10, 420, columns[i + 1] - 4, 452), 0, active ? Accent() : Muted());
        Target(Rect(columns[i], 420, columns[i + 1], 452), Action::Sort, labels[i], i);
    }
    const int rows = VisibleRowCount(height_);
    const int begin = ClampScrollOffset(ui_->scrollOffset, ui_->processes.size(), rows);
    for (int i = 0; i < rows && begin + i < static_cast<int>(ui_->processes.size()); ++i) {
        const auto& process = ui_->processes[static_cast<std::size_t>(begin + i)];
        const float y = 458 + static_cast<float>(i) * 38;
        const auto row = Rect(24, y, right, y + 36);
        const bool selected = ui_->selectedPid && *ui_->selectedPid == process.pid;
        if (selected)
            Round(row, 7, Alpha(Accent(), .10f));
        else if (Hover(row))
            Round(row, 7, Raised());
        if (selected)
            Round(Rect(25, y + 8, 28, y + 28), 1, Accent());
        if (process.cpuPercent > 0.1) {
            Round(Rect(columns[2] + 5, y + 5,
                       columns[2] + 5 +
                           (columns[3] - columns[2] - 10) *
                               static_cast<float>(std::clamp(process.cpuPercent / 100, 0.0, 1.0)),
                       y + 31),
                  3, Alpha(Accent(), .10f));
        }
        const std::wstring unavailable = L"—";
        const std::array<std::wstring, 6> values{
            process.name,
            std::to_wstring(process.pid),
            process.accessible ? FormatPercent(process.cpuPercent, 1, lang) : unavailable,
            process.workingSet ? FormatBytes(process.workingSet, lang) : unavailable,
            process.accessible ? FormatRate(process.readBytesPerSecond, lang) : unavailable,
            process.accessible ? FormatRate(process.writeBytesPerSecond, lang) : unavailable};
        for (std::size_t c = 0; c < 6; ++c)
            Text(values[c], Rect(columns[c] + 10, y, columns[c + 1] - 6, y + 36), 1,
                 c == 0   ? White()
                 : c == 2 ? Accent()
                          : Muted());
        Target(row, Action::SelectProcess, process.name, process.pid);
    }
    if (ui_->processes.empty()) {
        Icon(L"\uE721", Rect(24, 475, right, 512), Muted());
        Text(T(L"Ничего не найдено", L"No matching processes"), Rect(24, 516, right, 547), 3, White(),
             DWRITE_TEXT_ALIGNMENT_CENTER);
        Text(T(L"Попробуйте другое имя или PID", L"Try a different name or PID"), Rect(24, 548, right, 574),
             1, Muted(), DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    if (static_cast<int>(ui_->processes.size()) > rows && rows > 0) {
        const float track = static_cast<float>(rows) * 38;
        const float thumb =
            std::max(24.0f, track * static_cast<float>(rows) / static_cast<float>(ui_->processes.size()));
        const float y = 458 + (track - thumb) * static_cast<float>(begin) /
                                  static_cast<float>(ui_->processes.size() - rows);
        Round(Rect(right - 3, y, right, y + thumb), 1.5f, Color(0x58667A));
    }
    if (ui_->selectedPid)
        Details(s);
}

void Renderer::Details(const MetricsSnapshot& s) {
    const float l = width_ - 330, r = width_ - 24;
    const auto lang = settings_->language;
    Round(Rect(l, 420, r, height_ - 44), 14, Surface(), true);
    const auto found = std::find_if(s.processes.begin(), s.processes.end(), [&](const auto& p) {
        return p.pid == *ui_->selectedPid && p.creationTime == ui_->selectedCreation;
    });
    Text(T(L"О ПРОЦЕССЕ", L"PROCESS DETAILS"), Rect(l + 16, 431, r - 50, 453), 0, Muted());
    Button(Rect(r - 43, 427, r - 12, 457), Action::CloseDetails, T(L"Закрыть", L"Close"), false, L"\uE8BB");
    if (found == s.processes.end()) {
        Text(T(L"Процесс завершён", L"Process has exited"), Rect(l + 16, 472, r - 16, 510), 3, White());
        return;
    }
    Text(found->name, Rect(l + 16, 461, r - 16, 495), 3, White());
    Text(L"PID " + std::to_wstring(found->pid) + L"  ·  " +
             (found->accessible ? T(L"Выполняется", L"Running") : T(L"Ограничен доступ", L"Limited access")),
         Rect(l + 16, 496, r - 16, 516), 0, Muted());
    const std::array<std::pair<std::wstring, std::wstring>, 6> data{
        {{L"CPU", FormatPercent(found->cpuPercent, 1, lang)},
         {T(L"Память", L"Memory"), FormatBytes(found->workingSet, lang)},
         {T(L"Потоки", L"Threads"), std::to_wstring(found->threadCount)},
         {T(L"Запущен", L"Started"), found->startTime.value_or(L"—")},
         {T(L"Чтение", L"Read"), FormatRate(found->readBytesPerSecond, lang)},
         {T(L"Запись", L"Write"), FormatRate(found->writeBytesPerSecond, lang)}}};
    float y = 529;
    for (const auto& [label, value] : data) {
        Text(label, Rect(l + 16, y, l + 112, y + 23), 1, Muted());
        Text(value, Rect(l + 112, y, r - 16, y + 23), 1, White(), DWRITE_TEXT_ALIGNMENT_TRAILING);
        y += 22;
    }
    const float endY = height_ - 96;
    if (endY > 738) {
        Text(found->executablePath.empty() ? T(L"Путь недоступен", L"Path unavailable")
                                           : found->executablePath,
             Rect(l + 16, 675, r - 16, endY - 46), 0, Muted());
    }
    if (!found->executablePath.empty()) {
        Button(Rect(l + 16, endY - 40, r - 58, endY - 8), Action::OpenFolder,
               T(L"Открыть папку", L"Open folder"));
        Button(Rect(r - 50, endY - 40, r - 16, endY - 8), Action::CopyPath,
               T(L"Копировать путь", L"Copy path"), false, L"\uE8C8");
    }
    if (found->accessible && found->pid != 0 && found->pid != 4 && found->pid != GetCurrentProcessId()) {
        Round(Rect(l + 16, endY, r - 16, endY + 34), 8,
              Alpha(Red(), Hover(Rect(l + 16, endY, r - 16, endY + 34)) ? .21f : .11f));
        Text(T(L"Завершить процесс", L"End process"), Rect(l + 16, endY, r - 16, endY + 34), 1, Red(),
             DWRITE_TEXT_ALIGNMENT_CENTER);
        Target(Rect(l + 16, endY, r - 16, endY + 34), Action::EndProcess,
               T(L"Завершить процесс", L"End process"));
    }
}
void Renderer::SettingsPanel() {
    targets_.resize(std::min<std::size_t>(targets_.size(), 4));
    Round(Rect(0, 62, width_, height_), 0, Color(0x05080C, .78f));
    const float w = std::min(448.0f, width_ - 32);
    const float l = (width_ - w) / 2, r = l + w, top = 76;
    Round(Rect(l, top, r, std::min(height_ - 42, 658.0f)), 18, Surface(), true);
    Text(T(L"Настройки", L"Settings"), Rect(l + 22, top + 15, r - 65, top + 51), 4, White());
    Button(Rect(r - 53, top + 17, r - 21, top + 49), Action::Settings,
           T(L"Закрыть настройки", L"Close settings"), false, L"\uE8BB");
    const auto section = [&](const wchar_t* ru, const wchar_t* en, float y) {
        Text(T(ru, en), Rect(l + 22, y, r - 22, y + 22), 0, Muted());
    };
    section(L"ЯЗЫК / LANGUAGE", L"LANGUAGE / ЯЗЫК", 142);
    Button(Rect(l + 22, 170, l + w * .5f - 5, 204), Action::LanguageRu, L"Русский",
           settings_->language == Language::Russian);
    Button(Rect(l + w * .5f + 5, 170, r - 22, 204), Action::LanguageEn, L"English",
           settings_->language == Language::English);
    section(L"ЦВЕТ АКЦЕНТА", L"ACCENT COLOR", 220);
    const std::array accents{Mint(), Blue(), Violet()};
    const std::array names{T(L"Мята", L"Mint"), T(L"Лёд", L"Ice"), T(L"Ирис", L"Iris")};
    const float seg = (w - 60) / 3;
    for (unsigned i = 0; i < 3; ++i) {
        const float x = l + 22 + static_cast<float>(i) * (seg + 8);
        Button(Rect(x, 246, x + seg, 279), static_cast<Action>(static_cast<int>(Action::AccentMint) + i),
               names[i], settings_->accent == static_cast<int>(i));
        Round(Rect(x + 9, 258, x + 14, 263), 2, accents[i]);
    }
    section(L"ЧАСТОТА ОБНОВЛЕНИЯ", L"UPDATE INTERVAL", 294);
    const std::array labels{L"250", L"500", L"1000", L"2000"};
    const std::array intervals{250, 500, 1000, 2000};
    const float cell = (w - 62) / 4;
    for (unsigned i = 0; i < 4; ++i) {
        const float x = l + 22 + static_cast<float>(i) * (cell + 6);
        Button(Rect(x, 320, x + cell, 352), static_cast<Action>(static_cast<int>(Action::Refresh250) + i),
               std::wstring(labels[i]) + T(L" мс", L" ms"), settings_->refreshIntervalMs == intervals[i]);
    }
    Text(settings_->refreshIntervalMs == 250
             ? T(L"250 мс увеличивает нагрузку на CPU", L"250 ms increases CPU usage")
             : T(L"Баланс плавности и нагрузки на систему", L"Balance smoothness and system overhead"),
         Rect(l + 22, 355, r - 22, 376), 0, Muted());
    Text(T(L"Непрозрачность", L"Opacity"), Rect(l + 22, 390, r - 154, 418), 2, White());
    Button(Rect(r - 148, 388, r - 116, 420), Action::OpacityDown, L"−");
    Text(std::to_wstring(settings_->opacityPercent) + L"%", Rect(r - 111, 388, r - 61, 420), 1, White(),
         DWRITE_TEXT_ALIGNMENT_CENTER);
    Button(Rect(r - 54, 388, r - 22, 420), Action::OpacityUp, L"+");
    const auto toggle = [&](float y, Action action, const wchar_t* label, bool enabled) {
        Text(label, Rect(l + 22, y, r - 82, y + 36), 2, White());
        const auto box = Rect(r - 66, y + 7, r - 22, y + 29);
        Round(box, 11, enabled ? Accent() : Raised());
        brush_->SetColor(enabled ? Ink() : Muted());
        target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(enabled ? r - 33 : r - 55, y + 18), 7, 7), brush_);
        Target(Rect(l + 22, y, r - 22, y + 36), action, label);
    };
    toggle(434, Action::Animations, T(L"Плавные анимации", L"Smooth animations"), settings_->animations);
    toggle(478, Action::Startup, T(L"Запуск вместе с Windows", L"Start with Windows"),
           settings_->startWithWindows);
    toggle(522, Action::ClickThrough, T(L"Пропускать клики сквозь окно", L"Click through the window"),
           settings_->clickThrough);
    Line(l + 22, 574, r - 22, 574, Color(0xFFFFFF, .06f));
    Text(L"Ctrl + Alt + O", Rect(l + 22, 583, r - 22, 610), 3, Accent());
    Text(T(L"Открыть виджет и вернуть управление", L"Reveal the widget and restore interaction"),
         Rect(l + 22, 612, r - 22, 637), 0, Muted());
}
void Renderer::Confirmation(const MetricsSnapshot& s) {
    targets_.resize(std::min<std::size_t>(targets_.size(), 4));
    Round(Rect(0, 62, width_, height_), 0, Color(0x05080C, .84f));
    const float w = std::min(440.0f, width_ - 40), l = (width_ - w) / 2, top = height_ * .5f - 132;
    Round(Rect(l, top, l + w, top + 264), 16, Surface(), true);
    Text(T(L"Завершить процесс?", L"End this process?"), Rect(l + 24, top + 22, l + w - 24, top + 61), 4,
         White());
    const auto found = std::find_if(s.processes.begin(), s.processes.end(), [&](const auto& p) {
        return p.pid == ui_->selectedPid && p.creationTime == ui_->selectedCreation;
    });
    Text(found == s.processes.end() ? T(L"Процесс уже завершён", L"Process already exited") : found->name,
         Rect(l + 24, top + 72, l + w - 24, top + 100), 3, Red());
    Text(T(L"Несохранённые данные могут быть потеряны.", L"Unsaved work in this process may be lost."),
         Rect(l + 24, top + 111, l + w - 24, top + 138), 1, Muted());
    Text(T(L"Действие нельзя отменить.", L"This action cannot be undone."),
         Rect(l + 24, top + 139, l + w - 24, top + 164), 1, Muted());
    Button(Rect(l + 24, top + 196, l + w * .5f - 5, top + 234), Action::CancelEnd, T(L"Отмена", L"Cancel"),
           true);
    Round(Rect(l + w * .5f + 5, top + 196, l + w - 24, top + 234), 8, Alpha(Red(), .15f));
    Text(T(L"Завершить", L"End process"), Rect(l + w * .5f + 5, top + 196, l + w - 24, top + 234), 1, Red(),
         DWRITE_TEXT_ALIGNMENT_CENTER);
    Target(Rect(l + w * .5f + 5, top + 196, l + w - 24, top + 234), Action::ConfirmEnd,
           T(L"Завершить", L"End process"));
}
void Renderer::Footer(const MetricsSnapshot& s) {
    Line(22, height_ - 35, width_ - 22, height_ - 35, Color(0xFFFFFF, .06f));
    Round(Rect(24, height_ - 21, 29, height_ - 16), 2, ui_->paused ? Amber() : Accent());
    Text(ui_->paused ? T(L"Просмотр на паузе", L"View paused") : T(L"В реальном времени", L"Live monitoring"),
         Rect(37, height_ - 32, 205, height_ - 4), 0, Muted());
    const auto label = settings_->mode == ViewMode::Compact
                           ? L"Ctrl + Alt + O"
                           : std::to_wstring(ui_->processes.size()) + L" / " +
                                 std::to_wstring(s.processes.size()) +
                                 T(L" процессов   ·   Ctrl+F поиск", L" processes   ·   Ctrl+F search");
    Text(label, Rect(207, height_ - 32, width_ - 24, height_ - 4), 0, Muted(),
         DWRITE_TEXT_ALIGNMENT_TRAILING);
}
void Renderer::Render(const MetricsSnapshot& s, const MetricsSnapshot& previous, const Settings& settings,
                      const UiState& ui) {
    if (!CreateDeviceResources())
        return;
    settings_ = &settings;
    ui_ = &ui;
    width_ = target_->GetSize().width;
    height_ = target_->GetSize().height;
    targets_.clear();
    target_->BeginDraw();
    target_->SetTransform(D2D1::Matrix3x2F::Identity());
    target_->Clear(Ink());
    // A restrained entrance, with no ongoing animation timer once it settles.
    contentOffset_ = (1 - ui.reveal) * 12;
    target_->SetTransform(D2D1::Matrix3x2F::Translation(0, contentOffset_));
    Title();
    if (settings.mode == ViewMode::Compact)
        Compact(s, previous);
    else
        Expanded(s, previous);
    Footer(s);
    if (ui.settingsOpen)
        SettingsPanel();
    if (ui.confirmEnd)
        Confirmation(s);
    if (!ui.notice.empty() || !ui.hotkeyAvailable) {
        const std::wstring label = !ui.notice.empty() ? ui.notice
                                                      : T(L"Ctrl+Alt+O занят. Открывайте через трей.",
                                                          L"Ctrl+Alt+O is in use. Open from the tray.");
        const float w = std::min(520.0f, width_ - 40), l = (width_ - w) / 2;
        Round(Rect(l, height_ - 86, l + w, height_ - 43), 10, Raised(), true);
        Text(label, Rect(l + 14, height_ - 82, l + w - 14, height_ - 47), 1, White(),
             DWRITE_TEXT_ALIGNMENT_CENTER);
    } else if (!ui.settingsOpen && !ui.confirmEnd) {
        const auto hit = HitTest(ui.mouseX, ui.mouseY);
        if (!hit.label.empty() && (hit.top < 63 || hit.action == Action::CopyPath)) {
            const float tooltipWidth = std::min(240.0f, width_ - 40);
            const float x = std::clamp(hit.right - tooltipWidth, 20.0f, width_ - tooltipWidth - 20);
            Round(Rect(x, hit.bottom + 6, x + tooltipWidth, hit.bottom + 34), 6, Raised(), true);
            Text(hit.label, Rect(x + 8, hit.bottom + 6, x + tooltipWidth - 8, hit.bottom + 34), 0, White(),
                 DWRITE_TEXT_ALIGNMENT_CENTER);
        }
    }
    target_->SetTransform(D2D1::Matrix3x2F::Identity());
    brush_->SetColor(Color(0xFFFFFF, .08f));
    target_->DrawRoundedRectangle(D2D1::RoundedRect(Rect(.5f, .5f, width_ - .5f, height_ - .5f), 16, 16),
                                  brush_);
    if (target_->EndDraw() == D2DERR_RECREATE_TARGET)
        DiscardDeviceResources();
}

} // namespace processlens
