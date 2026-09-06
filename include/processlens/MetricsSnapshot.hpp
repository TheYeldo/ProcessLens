#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace processlens {

struct ProcessMetrics {
    std::uint32_t pid{};
    std::wstring name;
    double cpuPercent{};
    std::uint64_t workingSet{};
    std::uint64_t readBytesPerSecond{};
    std::uint64_t writeBytesPerSecond{};
    std::uint32_t threadCount{};
    std::optional<std::wstring> startTime;
};

struct MetricsSnapshot {
    double cpuPercent{};
    std::uint64_t memoryUsed{};
    std::uint64_t memoryAvailable{};
    std::uint64_t memoryTotal{};
    double memoryPercent{};
    std::uint64_t networkDownloadBytesPerSecond{};
    std::uint64_t networkUploadBytesPerSecond{};
    std::optional<double> gpuPercent;
    std::vector<float> cpuHistory;
    std::vector<float> memoryHistory;
    std::vector<float> gpuHistory;
    std::vector<float> networkHistory;
    std::vector<ProcessMetrics> processes;
};

} // namespace processlens
