#include "processlens/Calculations.hpp"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <unordered_map>

namespace processlens {
namespace {

std::wstring Lower(std::wstring_view value) {
    std::wstring result(value);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return result;
}

template <typename Less>
void SortWithDirection(std::vector<ProcessMetrics>& processes, bool ascending, Less less) {
    std::stable_sort(processes.begin(), processes.end(), [&](const auto& lhs, const auto& rhs) {
        return ascending ? less(lhs, rhs) : less(rhs, lhs);
    });
}

std::wstring GpuEngineKey(std::wstring_view instanceName) {
    const auto luidPosition = instanceName.find(L"_luid_");
    auto key = luidPosition == std::wstring_view::npos
                   ? instanceName
                   : instanceName.substr(luidPosition);

    // PDH can append #<index> when Windows exposes duplicate instance names.
    // That suffix does not identify a different physical engine.
    const auto hashPosition = key.rfind(L'#');
    if (hashPosition != std::wstring_view::npos && hashPosition + 1 < key.size() &&
        std::all_of(key.begin() + static_cast<std::ptrdiff_t>(hashPosition + 1), key.end(),
                    [](wchar_t ch) { return std::iswdigit(ch) != 0; })) {
        key = key.substr(0, hashPosition);
    }
    return std::wstring(key);
}

} // namespace

double CalculateSystemCpuPercent(const CpuTimes& previous, const CpuTimes& current) noexcept {
    if (current.idle < previous.idle || current.kernel < previous.kernel || current.user < previous.user) {
        return 0.0;
    }
    const auto idleDelta = current.idle - previous.idle;
    const auto kernelDelta = current.kernel - previous.kernel;
    const auto userDelta = current.user - previous.user;
    const auto total = kernelDelta + userDelta;
    if (total == 0 || idleDelta > total) {
        return 0.0;
    }
    return std::clamp(100.0 * static_cast<double>(total - idleDelta) /
                          static_cast<double>(total),
                      0.0, 100.0);
}

double CalculateProcessCpuPercent(std::uint64_t previousProcessTime,
                                  std::uint64_t currentProcessTime,
                                  std::uint64_t elapsed100ns,
                                  std::uint32_t logicalProcessors) noexcept {
    if (currentProcessTime < previousProcessTime || elapsed100ns == 0 || logicalProcessors == 0) {
        return 0.0;
    }
    const auto processDelta = currentProcessTime - previousProcessTime;
    return std::clamp(100.0 * static_cast<double>(processDelta) /
                          static_cast<double>(elapsed100ns) /
                          static_cast<double>(logicalProcessors),
                      0.0, 100.0);
}

std::uint64_t CalculateRate(std::uint64_t previousBytes,
                            std::uint64_t currentBytes,
                            double elapsedSeconds) noexcept {
    if (currentBytes < previousBytes || elapsedSeconds <= 0.0) {
        return 0;
    }
    return static_cast<std::uint64_t>(static_cast<double>(currentBytes - previousBytes) /
                                      elapsedSeconds);
}

std::optional<double> CalculateOverallGpuPercent(
    const std::vector<GpuEngineUtilization>& samples) {
    std::unordered_map<std::wstring, double> engineTotals;
    for (const auto& sample : samples) {
        if (sample.instanceName.empty() || !std::isfinite(sample.percent) || sample.percent < 0.0) {
            continue;
        }
        engineTotals[GpuEngineKey(sample.instanceName)] += sample.percent;
    }

    if (engineTotals.empty()) {
        return std::nullopt;
    }

    double busiestEngine = 0.0;
    for (const auto& [engine, total] : engineTotals) {
        (void)engine;
        busiestEngine = std::max(busiestEngine, std::clamp(total, 0.0, 100.0));
    }
    return busiestEngine;
}

void SortProcesses(std::vector<ProcessMetrics>& processes,
                   ProcessSortColumn column,
                   bool ascending) {
    switch (column) {
    case ProcessSortColumn::Name:
        SortWithDirection(processes, ascending, [](const auto& a, const auto& b) {
            return Lower(a.name) < Lower(b.name);
        });
        break;
    case ProcessSortColumn::Pid:
        SortWithDirection(processes, ascending, [](const auto& a, const auto& b) { return a.pid < b.pid; });
        break;
    case ProcessSortColumn::Cpu:
        SortWithDirection(processes, ascending, [](const auto& a, const auto& b) { return a.cpuPercent < b.cpuPercent; });
        break;
    case ProcessSortColumn::Memory:
        SortWithDirection(processes, ascending, [](const auto& a, const auto& b) { return a.workingSet < b.workingSet; });
        break;
    case ProcessSortColumn::Read:
        SortWithDirection(processes, ascending, [](const auto& a, const auto& b) { return a.readBytesPerSecond < b.readBytesPerSecond; });
        break;
    case ProcessSortColumn::Write:
        SortWithDirection(processes, ascending, [](const auto& a, const auto& b) { return a.writeBytesPerSecond < b.writeBytesPerSecond; });
        break;
    }
}

std::vector<ProcessMetrics> FilterProcesses(const std::vector<ProcessMetrics>& processes,
                                            std::wstring_view query) {
    if (query.empty()) {
        return processes;
    }
    const auto needle = Lower(query);
    std::vector<ProcessMetrics> result;
    for (const auto& process : processes) {
        const auto pid = std::to_wstring(process.pid);
        if (Lower(process.name).find(needle) != std::wstring::npos || pid.find(needle) != std::wstring::npos) {
            result.push_back(process);
        }
    }
    return result;
}

} // namespace processlens
