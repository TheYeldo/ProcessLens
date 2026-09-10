#pragma once

#include "processlens/GpuCollector.hpp"
#include "processlens/GraphBuffer.hpp"
#include "processlens/MetricsSnapshot.hpp"

#include <Windows.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>
#include <unordered_map>

namespace processlens {

class MetricsCollector {
public:
    explicit MetricsCollector(std::function<void()> onUpdated);
    ~MetricsCollector();

    MetricsCollector(const MetricsCollector&) = delete;
    MetricsCollector& operator=(const MetricsCollector&) = delete;

    void Start();
    void Stop();
    void SetInterval(std::chrono::milliseconds interval);
    [[nodiscard]] std::shared_ptr<const MetricsSnapshot> Snapshot() const noexcept;

private:
    struct ProcessPrevious {
        std::uint64_t cpuTime{};
        std::uint64_t readBytes{};
        std::uint64_t writeBytes{};
        std::uint64_t creationTime{};
        std::wstring executablePath;
    };

    struct NetworkTotals {
        std::uint64_t received{};
        std::uint64_t sent{};
        bool valid{};
    };

    void Run(std::stop_token stopToken);
    MetricsSnapshot Collect(double elapsedSeconds, std::uint64_t elapsed100ns);
    std::vector<ProcessMetrics> CollectProcesses(double elapsedSeconds, std::uint64_t elapsed100ns);
    static NetworkTotals ReadNetworkTotals() noexcept;

    std::function<void()> onUpdated_;
    std::atomic<std::shared_ptr<const MetricsSnapshot>> snapshot_;
    std::jthread worker_;
    mutable std::mutex waitMutex_;
    std::condition_variable_any wake_;
    std::atomic<int> intervalMs_{1000};
    std::unordered_map<std::uint32_t, ProcessPrevious> processPrevious_;
    std::uint64_t networkReceivedPrevious_{};
    std::uint64_t networkSentPrevious_{};
    bool hasNetworkPrevious_{};
    std::uint64_t idlePrevious_{};
    std::uint64_t kernelPrevious_{};
    std::uint64_t userPrevious_{};
    bool hasCpuPrevious_{};
    std::uint64_t sequence_{};
    GraphBuffer cpuHistory_;
    GraphBuffer memoryHistory_;
    GraphBuffer gpuHistory_;
    GraphBuffer networkHistory_;
    GpuCollector gpuCollector_;
};

} // namespace processlens
