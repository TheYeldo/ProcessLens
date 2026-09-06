#include "processlens/Calculations.hpp"
#include "processlens/Formatting.hpp"
#include "processlens/GraphBuffer.hpp"
#include "processlens/Settings.hpp"

#include <cmath>
#include <exception>
#include <iostream>
#include <string>
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
    const auto parsed = processlens::Settings::FromJson(expected.ToJson());
    Check(parsed.x == expected.x && parsed.y == expected.y && parsed.width == expected.width &&
          parsed.height == expected.height && parsed.mode == expected.mode &&
          parsed.alwaysOnTop == expected.alwaysOnTop && parsed.clickThrough == expected.clickThrough &&
          parsed.startWithWindows == expected.startWithWindows &&
          parsed.refreshIntervalMs == expected.refreshIntervalMs,
          "settings JSON round-trips");
    Check(processlens::Settings::FromJson(R"({"refreshIntervalMs": 333})").refreshIntervalMs == 1000,
          "settings rejects unsupported refresh interval");
}

void TestNetworkDelta() {
    Check(processlens::CalculateRate(1000, 4000, 2.0) == 1500, "calculates network byte delta per second");
    Check(processlens::CalculateRate(4000, 1000, 1.0) == 0, "network counter rollback is safe");
    Check(processlens::CalculateRate(1000, 4000, 0.0) == 0, "zero network interval is safe");
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
        TestNetworkDelta();
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
