#pragma once

#include "processlens/MetricsSnapshot.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace processlens {

struct CpuTimes {
    std::uint64_t idle{};
    std::uint64_t kernel{};
    std::uint64_t user{};
};

double CalculateSystemCpuPercent(const CpuTimes& previous, const CpuTimes& current) noexcept;
double CalculateProcessCpuPercent(std::uint64_t previousProcessTime,
                                  std::uint64_t currentProcessTime,
                                  std::uint64_t elapsed100ns,
                                  std::uint32_t logicalProcessors) noexcept;
std::uint64_t CalculateRate(std::uint64_t previousBytes,
                            std::uint64_t currentBytes,
                            double elapsedSeconds) noexcept;

struct GpuEngineUtilization {
    std::wstring instanceName;
    double percent{};
};

// Windows reports GPU utilization per process and physical engine. ProcessLens
// sums the process instances for each engine, then returns the busiest engine,
// matching the overall GPU value used by Task Manager.
std::optional<double> CalculateOverallGpuPercent(
    const std::vector<GpuEngineUtilization>& samples);

enum class ProcessSortColumn { Name, Pid, Cpu, Memory, Read, Write };

void SortProcesses(std::vector<ProcessMetrics>& processes,
                   ProcessSortColumn column,
                   bool ascending);
std::vector<ProcessMetrics> FilterProcesses(const std::vector<ProcessMetrics>& processes,
                                            std::wstring_view query);

} // namespace processlens
