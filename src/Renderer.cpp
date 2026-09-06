#include "processlens/Renderer.hpp"

#include "processlens/Formatting.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <string>

namespace processlens {
namespace {

template <typename T>
void SafeRelease(T*& value) {
    if (value) {
        value->Release();
        value = nullptr;
    }
}

D2D1_COLOR_F Background() { return D2D1::ColorF(0x11151D); }
D2D1_COLOR_F Surface() { return D2D1::ColorF(0x1A202B); }
D2D1_COLOR_F SurfaceAlt() { return D2D1::ColorF(0x202837); }
D2D1_COLOR_F PrimaryText() { return D2D1::ColorF(0xEDF2F7); }
D2D1_COLOR_F SecondaryText() { return D2D1::ColorF(0x8F9BAD); }
D2D1_COLOR_F Cyan() { return D2D1::ColorF(0x4CC9F0); }
D2D1_COLOR_F Purple() { return D2D1::ColorF(0x9B8AFB); }
D2D1_COLOR_F Green() { return D2D1::ColorF(0x53D6A2); }
D2D1_COLOR_F Orange() { return D2D1::ColorF(0xFFB86B); }

std::wstring WithArrow(std::wstring text, bool active, bool ascending) {
    if (active) text += ascending ? L"  ^" : L"  v";
    return text;
}

} // namespace

Renderer::~Renderer() {
    DiscardDeviceResources();
    SafeRelease(titleFormat_);
    SafeRelease(headingFormat_);
    SafeRelease(bodyFormat_);
    SafeRelease(smallFormat_);
    SafeRelease(writeFactory_);
    SafeRelease(factory_);
}

bool Renderer::Initialize(HWND window) {
    window_ = window;
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &factory_))) return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&writeFactory_)))) return false;

    auto makeFormat = [&](float size, DWRITE_FONT_WEIGHT weight, IDWriteTextFormat** output) {
        return writeFactory_->CreateTextFormat(L"Segoe UI Variable", nullptr, weight,
                                                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                                size, L"en-US", output);
    };
    if (FAILED(makeFormat(16.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, &titleFormat_)) ||
        FAILED(makeFormat(13.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, &headingFormat_)) ||
        FAILED(makeFormat(12.0f, DWRITE_FONT_WEIGHT_NORMAL, &bodyFormat_)) ||
        FAILED(makeFormat(10.5f, DWRITE_FONT_WEIGHT_NORMAL, &smallFormat_))) return false;
    bodyFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    smallFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    return CreateDeviceResources();
}

bool Renderer::CreateDeviceResources() {
    if (target_) return true;
    RECT rect{};
    GetClientRect(window_, &rect);
    const auto size = D2D1::SizeU(std::max<LONG>(rect.right, 1), std::max<LONG>(rect.bottom, 1));
    const auto properties = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_IGNORE), dpi_, dpi_);
    if (FAILED(factory_->CreateHwndRenderTarget(properties,
            D2D1::HwndRenderTargetProperties(window_, size, D2D1_PRESENT_OPTIONS_NONE), &target_))) return false;
    return SUCCEEDED(target_->CreateSolidColorBrush(PrimaryText(), &brush_));
}

void Renderer::DiscardDeviceResources() {
    SafeRelease(brush_);
    SafeRelease(target_);
}

void Renderer::Resize(UINT width, UINT height) {
    if (target_) target_->Resize(D2D1::SizeU(std::max(width, 1U), std::max(height, 1U)));
}

void Renderer::SetDpi(float dpi) {
    dpi_ = dpi;
    if (target_) target_->SetDpi(dpi_, dpi_);
}

void Renderer::DrawText(std::wstring_view text, D2D1_RECT_F rect, IDWriteTextFormat* format,
                        D2D1_COLOR_F color, DWRITE_TEXT_ALIGNMENT alignment) {
    format->SetTextAlignment(alignment);
    brush_->SetColor(color);
    target_->DrawTextW(text.data(), static_cast<UINT32>(text.size()), format, rect, brush_,
                       D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void Renderer::FillRoundRect(D2D1_RECT_F rect, float radius, D2D1_COLOR_F color) {
    brush_->SetColor(color);
    target_->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), brush_);
}

void Renderer::DrawGraph(D2D1_RECT_F rect, const std::vector<float>& values, float maximum, D2D1_COLOR_F color) {
    if (values.size() < 2 || maximum <= 0.0f) return;
    ID2D1PathGeometry* geometry = nullptr;
    ID2D1GeometrySink* sink = nullptr;
    if (FAILED(factory_->CreatePathGeometry(&geometry)) || FAILED(geometry->Open(&sink))) {
        SafeRelease(sink);
        SafeRelease(geometry);
        return;
    }
    const float width = rect.right - rect.left;
    const float height = rect.bottom - rect.top;
    const float step = width / static_cast<float>(values.size() - 1);
    auto point = [&](std::size_t i) {
        const float normalized = std::clamp(values[i] / maximum, 0.0f, 1.0f);
        return D2D1::Point2F(rect.left + step * static_cast<float>(i), rect.bottom - normalized * height);
    };
    sink->BeginFigure(point(0), D2D1_FIGURE_BEGIN_HOLLOW);
    for (std::size_t i = 1; i < values.size(); ++i) sink->AddLine(point(i));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();
    brush_->SetColor(color);
    target_->DrawGeometry(geometry, brush_, 1.6f);
    sink->Release();
    geometry->Release();
}

void Renderer::DrawMetricCard(D2D1_RECT_F rect, std::wstring_view label, std::wstring_view value,
                              const std::vector<float>& history, float maximum, D2D1_COLOR_F accent) {
    FillRoundRect(rect, 10.0f, Surface());
    DrawText(label, D2D1::RectF(rect.left + 12, rect.top + 9, rect.right - 12, rect.top + 30),
             smallFormat_, SecondaryText());
    DrawText(value, D2D1::RectF(rect.left + 12, rect.top + 27, rect.right - 12, rect.top + 52),
             headingFormat_, PrimaryText());
    DrawGraph(D2D1::RectF(rect.left + 12, rect.bottom - 31, rect.right - 12, rect.bottom - 10),
              history, maximum, accent);
}

void Renderer::DrawTitleBar(ViewMode mode) {
    DrawText(L"ProcessLens", D2D1::RectF(18, 14, 180, 39), titleFormat_, PrimaryText());
    const auto size = target_->GetSize();
    FillRoundRect(D2D1::RectF(size.width - 112, 10, size.width - 80, 38), 7, SurfaceAlt());
    DrawText(mode == ViewMode::Compact ? L"+" : L"-",
             D2D1::RectF(size.width - 112, 12, size.width - 80, 36), bodyFormat_, PrimaryText(), DWRITE_TEXT_ALIGNMENT_CENTER);
    DrawText(L"_", D2D1::RectF(size.width - 74, 8, size.width - 42, 35), bodyFormat_, SecondaryText(), DWRITE_TEXT_ALIGNMENT_CENTER);
    DrawText(L"x", D2D1::RectF(size.width - 38, 10, size.width - 8, 36), bodyFormat_, SecondaryText(), DWRITE_TEXT_ALIGNMENT_CENTER);
}

void Renderer::DrawCompact(const MetricsSnapshot& snapshot, bool topByMemory) {
    const auto size = target_->GetSize();
    const float left = 16.0f;
    const float right = size.width - 16.0f;
    DrawMetricCard(D2D1::RectF(left, 54, right, 126), L"CPU", FormatPercent(snapshot.cpuPercent),
                   snapshot.cpuHistory, 100.0f, Cyan());
    DrawMetricCard(D2D1::RectF(left, 134, right, 206), L"MEMORY",
                   FormatBytes(snapshot.memoryUsed) + L" / " + FormatBytes(snapshot.memoryTotal),
                   snapshot.memoryHistory, 100.0f, Purple());

    FillRoundRect(D2D1::RectF(left, 214, right, 284), 10, Surface());
    DrawText(L"NETWORK", D2D1::RectF(left + 12, 223, right - 12, 244), smallFormat_, SecondaryText());
    DrawText(L"Down  " + FormatRate(snapshot.networkDownloadBytesPerSecond),
             D2D1::RectF(left + 12, 247, (left + right) / 2, 270), bodyFormat_, Green());
    DrawText(L"Up  " + FormatRate(snapshot.networkUploadBytesPerSecond),
             D2D1::RectF((left + right) / 2, 247, right - 12, 270), bodyFormat_, Orange(), DWRITE_TEXT_ALIGNMENT_TRAILING);

    FillRoundRect(D2D1::RectF(left, 292, right, 329), 10, Surface());
    DrawText(L"GPU", D2D1::RectF(left + 12, 302, left + 100, 324), smallFormat_, SecondaryText());
    DrawText(snapshot.gpuPercent ? FormatPercent(*snapshot.gpuPercent) : L"N/A",
             D2D1::RectF(left + 100, 300, right - 12, 324), bodyFormat_, PrimaryText(), DWRITE_TEXT_ALIGNMENT_TRAILING);

    DrawText(L"TOP PROCESSES", D2D1::RectF(left + 2, 345, right - 110, 367), headingFormat_, PrimaryText());
    DrawText(topByMemory ? L"by RAM" : L"by CPU", D2D1::RectF(right - 110, 345, right - 2, 367),
             smallFormat_, Cyan(), DWRITE_TEXT_ALIGNMENT_TRAILING);
    auto processes = snapshot.processes;
    SortProcesses(processes, topByMemory ? ProcessSortColumn::Memory : ProcessSortColumn::Cpu, false);
    const std::size_t count = std::min<std::size_t>(5, processes.size());
    for (std::size_t i = 0; i < count; ++i) {
        const float y = 376.0f + static_cast<float>(i) * 34.0f;
        if (y + 29 > size.height - 8) break;
        if (i % 2 == 0) FillRoundRect(D2D1::RectF(left, y, right, y + 29), 7, Surface());
        DrawText(processes[i].name, D2D1::RectF(left + 10, y + 6, right - 105, y + 27), bodyFormat_, PrimaryText());
        const auto value = topByMemory ? FormatBytes(processes[i].workingSet) : FormatPercent(processes[i].cpuPercent, 1);
        DrawText(value, D2D1::RectF(right - 105, y + 6, right - 10, y + 27), bodyFormat_, SecondaryText(), DWRITE_TEXT_ALIGNMENT_TRAILING);
    }
}

void Renderer::DrawExpanded(const MetricsSnapshot& snapshot,
                            std::wstring_view search,
                            ProcessSortColumn sortColumn,
                            bool ascending,
                            std::uint32_t selectedPid,
                            int scrollOffset) {
    const auto size = target_->GetSize();
    const float margin = 18.0f;
    const float gap = 10.0f;
    const float cardWidth = (size.width - margin * 2 - gap * 3) / 4;
    DrawMetricCard(D2D1::RectF(margin, 55, margin + cardWidth, 154), L"CPU", FormatPercent(snapshot.cpuPercent), snapshot.cpuHistory, 100, Cyan());
    DrawMetricCard(D2D1::RectF(margin + cardWidth + gap, 55, margin + cardWidth * 2 + gap, 154), L"MEMORY", FormatPercent(snapshot.memoryPercent), snapshot.memoryHistory, 100, Purple());
    DrawMetricCard(D2D1::RectF(margin + (cardWidth + gap) * 2, 55, margin + cardWidth * 3 + gap * 2, 154), L"NETWORK", FormatRate(snapshot.networkDownloadBytesPerSecond), snapshot.networkHistory, 25, Green());
    DrawMetricCard(D2D1::RectF(margin + (cardWidth + gap) * 3, 55, size.width - margin, 154), L"GPU", snapshot.gpuPercent ? FormatPercent(*snapshot.gpuPercent) : L"N/A", snapshot.gpuHistory, 100, Orange());

    DrawText(L"SYSTEM", D2D1::RectF(margin, 169, margin + 80, 190), smallFormat_, SecondaryText());
    DrawText(FormatBytes(snapshot.memoryUsed) + L" used   " + FormatBytes(snapshot.memoryAvailable) + L" available",
             D2D1::RectF(margin + 80, 167, size.width - margin, 191), bodyFormat_, PrimaryText());

    FillRoundRect(D2D1::RectF(margin, 201, size.width - margin, 237), 9, Surface());
    const std::wstring searchLabel = search.empty() ? L"Search processes by name or PID..." : std::wstring(search);
    DrawText(searchLabel, D2D1::RectF(margin + 12, 210, size.width - margin - 12, 232), bodyFormat_,
             search.empty() ? SecondaryText() : PrimaryText());

    const float panelRight = selectedPid ? size.width - 300.0f : size.width - margin;
    const float tableWidth = panelRight - margin;
    const std::array<float, 7> positions{
        margin, margin + tableWidth * 0.34f, margin + tableWidth * 0.44f,
        margin + tableWidth * 0.56f, margin + tableWidth * 0.70f,
        margin + tableWidth * 0.85f, panelRight};
    FillRoundRect(D2D1::RectF(margin, 249, panelRight, 281), 8, SurfaceAlt());
    const std::array labels{L"Process", L"PID", L"CPU", L"RAM", L"Read", L"Write"};
    const std::array columns{ProcessSortColumn::Name, ProcessSortColumn::Pid, ProcessSortColumn::Cpu,
                             ProcessSortColumn::Memory, ProcessSortColumn::Read, ProcessSortColumn::Write};
    for (std::size_t i = 0; i < labels.size(); ++i) {
        DrawText(WithArrow(labels[i], sortColumn == columns[i], ascending),
                 D2D1::RectF(positions[i] + 8, 257, positions[i + 1] - 4, 278), smallFormat_, SecondaryText());
    }

    auto processes = FilterProcesses(snapshot.processes, search);
    SortProcesses(processes, sortColumn, ascending);
    const float rowHeight = 29.0f;
    const int visibleRows = std::max(0, static_cast<int>((size.height - 295) / rowHeight));
    const int begin = std::clamp(scrollOffset, 0, std::max(0, static_cast<int>(processes.size()) - visibleRows));
    for (int row = 0; row < visibleRows && begin + row < static_cast<int>(processes.size()); ++row) {
        const auto& process = processes[static_cast<std::size_t>(begin + row)];
        const float y = 288.0f + row * rowHeight;
        if (process.pid == selectedPid) FillRoundRect(D2D1::RectF(margin, y, panelRight, y + 26), 6, D2D1::ColorF(0x26364A));
        else if (row % 2 == 0) FillRoundRect(D2D1::RectF(margin, y, panelRight, y + 26), 6, Surface());
        const std::array<std::wstring, 6> values{process.name, std::to_wstring(process.pid), FormatPercent(process.cpuPercent, 1),
                                                FormatBytes(process.workingSet), FormatRate(process.readBytesPerSecond),
                                                FormatRate(process.writeBytesPerSecond)};
        for (std::size_t column = 0; column < values.size(); ++column) {
            DrawText(values[column], D2D1::RectF(positions[column] + 8, y + 5, positions[column + 1] - 4, y + 25),
                     smallFormat_, column == 0 ? PrimaryText() : SecondaryText());
        }
    }

    if (selectedPid) {
        const auto found = std::find_if(snapshot.processes.begin(), snapshot.processes.end(),
                                        [&](const auto& process) { return process.pid == selectedPid; });
        if (found != snapshot.processes.end()) {
            const float left = size.width - 284.0f;
            FillRoundRect(D2D1::RectF(left, 249, size.width - margin, size.height - margin), 12, Surface());
            DrawText(found->name, D2D1::RectF(left + 16, 268, size.width - 36, 294), headingFormat_, PrimaryText());
            DrawText(L"x", D2D1::RectF(size.width - 50, 264, size.width - 25, 291), bodyFormat_, SecondaryText(), DWRITE_TEXT_ALIGNMENT_CENTER);
            const std::array<std::pair<std::wstring, std::wstring>, 7> details{{
                {L"PID", std::to_wstring(found->pid)}, {L"CPU", FormatPercent(found->cpuPercent, 1)},
                {L"RAM", FormatBytes(found->workingSet)}, {L"Threads", std::to_wstring(found->threadCount)},
                {L"Started", found->startTime.value_or(L"N/A")}, {L"Read", FormatRate(found->readBytesPerSecond)},
                {L"Write", FormatRate(found->writeBytesPerSecond)}}};
            float y = 311;
            for (const auto& [label, value] : details) {
                DrawText(label, D2D1::RectF(left + 16, y, left + 95, y + 22), smallFormat_, SecondaryText());
                DrawText(value, D2D1::RectF(left + 95, y, size.width - 34, y + 22), bodyFormat_, PrimaryText(), DWRITE_TEXT_ALIGNMENT_TRAILING);
                y += 31;
            }
            FillRoundRect(D2D1::RectF(left + 16, size.height - 66, size.width - 34, size.height - 31), 8, D2D1::ColorF(0xB6495B));
            DrawText(L"End Process", D2D1::RectF(left + 16, size.height - 59, size.width - 34, size.height - 34),
                     bodyFormat_, PrimaryText(), DWRITE_TEXT_ALIGNMENT_CENTER);
        }
    }
}

void Renderer::Render(const MetricsSnapshot& snapshot,
                      ViewMode mode,
                      std::wstring_view search,
                      ProcessSortColumn sortColumn,
                      bool ascending,
                      bool topByMemory,
                      std::uint32_t selectedPid,
                      int scrollOffset) {
    if (!CreateDeviceResources()) return;
    target_->BeginDraw();
    target_->Clear(Background());
    DrawTitleBar(mode);
    if (mode == ViewMode::Compact) DrawCompact(snapshot, topByMemory);
    else DrawExpanded(snapshot, search, sortColumn, ascending, selectedPid, scrollOffset);
    if (target_->EndDraw() == D2DERR_RECREATE_TARGET) DiscardDeviceResources();
}

} // namespace processlens
