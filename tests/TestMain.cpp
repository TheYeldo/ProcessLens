#include "processlens/Calculations.hpp"
#include "processlens/Formatting.hpp"
#include "processlens/GpuCollector.hpp"
#include "processlens/GraphBuffer.hpp"
#include "processlens/Settings.hpp"
#include "processlens/UiState.hpp"

#include <chrono>
#include <cmath>
#include <exception>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void CheckNear(double actual, double expected, double tolerance, const char* message) {
    Check(std::abs(actual - expected) <= tolerance, message);
}

void TestCpuDelta() {
    CheckNear(processlens::CalculateSystemCpuPercent({100, 300, 100}, {150, 400, 200}), 75.0, 0.001,
              "system CPU uses idle/kernel/user deltas");
    CheckNear(processlens::CalculateSystemCpuPercent({100, 300, 100}, {90, 400, 200}), 0.0, 0.001,
              "system CPU rejects counter rollback");
}

void TestProcessCpu() {
    CheckNear(processlens::CalculateProcessCpuPercent(10'000, 2'010'000, 10'000'000, 4), 5.0, 0.001,
              "process CPU normalizes by elapsed time and logical processors");
    CheckNear(processlens::CalculateProcessCpuPercent(200, 100, 1000, 8), 0.0, 0.001,
              "process CPU rejects PID/counter rollback");
}

void TestFormatting() {
    Check(processlens::FormatBytes(824) == L"824 B", "formats bytes");
    Check(processlens::FormatBytes(12'700) == L"12.4 KB", "formats kibibyte-sized values");
    Check(processlens::FormatBytes(5'054'644'019ULL) == L"4.71 GB", "formats gibibyte-sized values");
    Check(processlens::FormatRate(1024) == L"1.00 KB/s", "formats transfer rates");
    using processlens::Language;
    Check(processlens::FormatBytes(5'054'644'019ULL, Language::Russian) == L"4,71 ГБ",
          "Russian byte units and decimal separator");
    Check(processlens::FormatRate(1024, Language::Russian) == L"1,00 КБ/с", "Russian rate units");
    Check(processlens::FormatPercent(12.5, 1, Language::Russian) == L"12,5%", "Russian percentages");
    Check(processlens::FormatUptime(90060, Language::Russian) == L"1 д 1 ч 1 мин", "uptime across a day boundary");
    Check(processlens::FormatUptime(0, Language::English) == L"0h 0m", "zero uptime");
}

void TestGraphBuffer() {
    processlens::GraphBuffer graph(3);
    graph.Push(1); graph.Push(2); graph.Push(3); graph.Push(4);
    const auto values = graph.Values();
    Check(values == std::vector<float>({2, 3, 4}), "graph ring buffer preserves chronological order");
    Check(graph.Size() == 3 && graph.Capacity() == 3, "graph ring buffer reports bounded size");
}

void TestSorting() {
    std::vector<processlens::ProcessMetrics> values{
        {1, L"beta.exe", 4.0, 100}, {2, L"Alpha.exe", 8.0, 50}, {3, L"gamma.exe", 1.0, 900}};
    processlens::SortProcesses(values, processlens::ProcessSortColumn::Cpu, false);
    Check(values[0].pid == 2 && values[2].pid == 3, "sorts CPU descending");
    processlens::SortProcesses(values, processlens::ProcessSortColumn::Name, true);
    Check(values[0].pid == 2 && values[2].pid == 3, "sorts process names case-insensitively");
}

void TestFiltering() {
    const std::vector<processlens::ProcessMetrics> values{
        {101, L"chrome.exe"}, {202, L"Discord.exe"}, {303, L"explorer.exe"}};
    Check(processlens::FilterProcesses(values, L"DIS").size() == 1, "filters process names case-insensitively");
    const auto byPid = processlens::FilterProcesses(values, L"03");
    Check(byPid.size() == 1 && byPid[0].pid == 303, "filters by partial PID");
}

void TestSettings() {
    processlens::Settings expected;
    expected.x = -200;
    expected.y = 45;
    expected.width = 900;
    expected.height = 650;
    expected.mode = processlens::ViewMode::Expanded;
    expected.alwaysOnTop = true;
    expected.clickThrough = true;
    expected.startWithWindows = false;
    expected.refreshIntervalMs = 500;
    expected.language = processlens::Language::English;
    expected.accent = 2;
    expected.opacityPercent = 85;
    expected.animations = false;
    const auto parsed = processlens::Settings::FromJson(expected.ToJson());
    Check(parsed.x == expected.x && parsed.y == expected.y && parsed.width == expected.width &&
          parsed.height == expected.height && parsed.mode == expected.mode &&
          parsed.alwaysOnTop == expected.alwaysOnTop && parsed.clickThrough == expected.clickThrough &&
          parsed.startWithWindows == expected.startWithWindows &&
          parsed.refreshIntervalMs == expected.refreshIntervalMs && parsed.language == expected.language &&
          parsed.accent == expected.accent && parsed.opacityPercent == expected.opacityPercent &&
          parsed.animations == expected.animations,
          "settings JSON round-trips");
    Check(processlens::Settings::FromJson(R"({"refreshIntervalMs": 333})").refreshIntervalMs == 1000,
          "settings rejects unsupported refresh interval");
    const auto legacy = processlens::Settings::FromJson(R"({"mode":"expanded","refreshIntervalMs":500})");
    Check(legacy.language == processlens::Language::Russian && legacy.opacityPercent == 100 && legacy.animations,
          "v1 settings migrate to Russian with full opacity and animations");
    const auto invalid = processlens::Settings::FromJson(R"({"accent":9,"opacityPercent":4,"language":9})");
    Check(invalid.accent == 2 && invalid.opacityPercent == 70 && invalid.language == processlens::Language::Russian,
          "invalid appearance settings stay in supported ranges");
}

void TestInteractionBoundaries() {
    Check(processlens::ClampScrollOffset(500, 40, 8) == 32, "scroll stops at last full page");
    Check(processlens::ClampScrollOffset(32, 3, 8) == 0, "filtering clamps stale scroll offset");
    Check(processlens::ClampScrollOffset(-3, 40, 8) == 0, "scroll cannot become negative");
    Check(processlens::VisibleRowCount(820) == 8, "table leaves space for footer");
    Check(processlens::VisibleRowCount(200) == 0, "small window never has negative rows");
    CheckNear(processlens::EaseOutCubic(-1), 0, .0001, "animation starts at zero");
    CheckNear(processlens::EaseOutCubic(2), 1, .0001, "animation settles at one");
    const processlens::HitTarget target{processlens::Action::Settings, 0, 10, 20, 40, 50};
    Check(target.Contains(10, 20) && !target.Contains(40, 50) && !target.Contains(9, 20),
          "hit testing uses consistent half-open bounds");
}

void TestNetworkDelta() {
    Check(processlens::CalculateRate(1000, 4000, 2.0) == 1500, "calculates network byte delta per second");
    Check(processlens::CalculateRate(4000, 1000, 1.0) == 0, "network counter rollback is safe");
    Check(processlens::CalculateRate(1000, 4000, 0.0) == 0, "zero network interval is safe");
}

void TestGpuAggregation() {
    using processlens::GpuEngineUtilization;
    const std::vector<GpuEngineUtilization> samples{
        {L"pid_100_luid_0x0_0x1_phys_0_eng_0_engtype_3D", 21.0},
        {L"pid_200_luid_0x0_0x1_phys_0_eng_0_engtype_3D", 24.0},
        {L"pid_100_luid_0x0_0x1_phys_0_eng_1_engtype_Copy", 62.0},
        {L"pid_300_luid_0x0_0x2_phys_0_eng_0_engtype_3D", 55.0},
    };
    const auto overall = processlens::CalculateOverallGpuPercent(samples);
    Check(overall.has_value(), "GPU aggregation returns a value for valid samples");
    CheckNear(overall.value_or(0.0), 62.0, 0.001,
              "GPU aggregation sums processes per engine and selects the busiest engine");

    const auto clamped = processlens::CalculateOverallGpuPercent({
        {L"pid_1_luid_0x0_0x1_phys_0_eng_0_engtype_3D#1", 70.0},
        {L"pid_2_luid_0x0_0x1_phys_0_eng_0_engtype_3D#2", 60.0},
    });
    CheckNear(clamped.value_or(0.0), 100.0, 0.001,
              "GPU aggregation removes duplicate suffixes and clamps engine totals");

    Check(!processlens::CalculateOverallGpuPercent({
               {L"", 50.0}, {L"invalid", -1.0}, {L"nan", std::nan("")}})
               .has_value(),
          "GPU aggregation ignores invalid samples");
}

void TestLiveGpuCollector() {
    processlens::GpuCollector collector;
    (void)collector.Collect();
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    const auto sample = collector.Collect();
    Check(!sample || (*sample >= 0.0 && *sample <= 100.0),
          "live GPU collector returns unavailable or a bounded percentage");
    if (sample) {
        std::cout << "Live GPU sample: " << *sample << "%\n";
    } else {
        std::cout << "Live GPU counters unavailable on this runner.\n";
    }
}

} // namespace

int main() {
    try {
        TestCpuDelta();
        TestProcessCpu();
        TestFormatting();
        TestGraphBuffer();
        TestSorting();
        TestFiltering();
        TestSettings();
        TestInteractionBoundaries();
        TestNetworkDelta();
        TestGpuAggregation();
        TestLiveGpuCollector();
    } catch (const std::exception& error) {
        std::cerr << "Unexpected exception: " << error.what() << '\n';
        return 2;
    }
    if (failures != 0) {
        std::cerr << failures << " test(s) failed.\n";
        return 1;
    }
    std::cout << "All ProcessLens tests passed.\n";
    return 0;
}
