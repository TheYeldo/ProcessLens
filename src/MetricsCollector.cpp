#include "processlens/MetricsCollector.hpp"

#include "processlens/Calculations.hpp"
#include "processlens/Logging.hpp"

#include <TlHelp32.h>
#include <psapi.h>
#include <ws2def.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <unordered_set>

namespace processlens {
namespace {

std::uint64_t ToUInt64(const FILETIME& time) noexcept {
    ULARGE_INTEGER value{};
    value.LowPart = time.dwLowDateTime;
    value.HighPart = time.dwHighDateTime;
    return value.QuadPart;
}

struct HandleCloser {
    void operator()(HANDLE handle) const noexcept {
        if (handle && handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
    }
};
using UniqueHandle = std::unique_ptr<void, HandleCloser>;

std::optional<std::wstring> FormatStartTime(const FILETIME& utc) {
    FILETIME local{};
    SYSTEMTIME system{};
    if (!FileTimeToLocalFileTime(&utc, &local) || !FileTimeToSystemTime(&local, &system)) return std::nullopt;
    wchar_t buffer[16]{};
    if (swprintf_s(buffer, L"%02u:%02u", system.wHour, system.wMinute) < 0) return std::nullopt;
    return std::wstring(buffer);
}

std::uint32_t LogicalProcessorCount() noexcept {
    const DWORD count = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    return count == 0 ? 1U : count;
}

} // namespace

MetricsCollector::MetricsCollector(std::function<void()> onUpdated)
    : onUpdated_(std::move(onUpdated)),
      snapshot_(std::make_shared<const MetricsSnapshot>()),
      cpuHistory_(120), memoryHistory_(120), gpuHistory_(120), networkHistory_(120) {}

MetricsCollector::~MetricsCollector() { Stop(); }

void MetricsCollector::Start() {
    if (worker_.joinable()) return;
    Log(LogLevel::Info, L"Metrics collector started");
    worker_ = std::jthread([this](std::stop_token token) { Run(token); });
}

void MetricsCollector::Stop() {
    if (!worker_.joinable()) return;
    worker_.request_stop();
    wake_.notify_all();
    worker_.join();
    Log(LogLevel::Info, L"Metrics collector stopped");
}

void MetricsCollector::SetInterval(std::chrono::milliseconds interval) {
    intervalMs_.store(static_cast<int>(std::clamp<std::int64_t>(interval.count(), 250, 2000)));
    wake_.notify_all();
}

std::shared_ptr<const MetricsSnapshot> MetricsCollector::Snapshot() const noexcept {
    return snapshot_.load(std::memory_order_acquire);
}

void MetricsCollector::Run(std::stop_token stopToken) {
    auto previous = std::chrono::steady_clock::now();
    while (!stopToken.stop_requested()) {
        const auto started = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration<double>(started - previous).count();
        previous = started;
        const auto elapsed100ns = static_cast<std::uint64_t>(elapsed * 10'000'000.0);

        auto next = std::make_shared<const MetricsSnapshot>(Collect(elapsed, elapsed100ns));
        snapshot_.store(std::move(next), std::memory_order_release);
        if (onUpdated_) onUpdated_();

        const auto interval = std::chrono::milliseconds(intervalMs_.load());
        std::unique_lock lock(waitMutex_);
        wake_.wait_until(lock, stopToken, started + interval, [] { return false; });
    }
}

MetricsSnapshot MetricsCollector::Collect(double elapsedSeconds, std::uint64_t elapsed100ns) {
    MetricsSnapshot result;

    FILETIME idle{}, kernel{}, user{};
    if (GetSystemTimes(&idle, &kernel, &user)) {
        const CpuTimes current{ToUInt64(idle), ToUInt64(kernel), ToUInt64(user)};
        if (hasCpuPrevious_) {
            result.cpuPercent = CalculateSystemCpuPercent(
                {idlePrevious_, kernelPrevious_, userPrevious_}, current);
        }
        idlePrevious_ = current.idle;
        kernelPrevious_ = current.kernel;
        userPrevious_ = current.user;
        hasCpuPrevious_ = true;
    }

    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory)) {
        result.memoryTotal = memory.ullTotalPhys;
        result.memoryAvailable = memory.ullAvailPhys;
        result.memoryUsed = memory.ullTotalPhys - memory.ullAvailPhys;
        result.memoryPercent = memory.dwMemoryLoad;
    }

    const auto network = ReadNetworkTotals();
    if (network.valid && hasNetworkPrevious_) {
        result.networkDownloadBytesPerSecond = CalculateRate(networkReceivedPrevious_, network.received, elapsedSeconds);
        result.networkUploadBytesPerSecond = CalculateRate(networkSentPrevious_, network.sent, elapsedSeconds);
    }
    if (network.valid) {
        networkReceivedPrevious_ = network.received;
        networkSentPrevious_ = network.sent;
        hasNetworkPrevious_ = true;
    }

    result.gpuPercent = gpuCollector_.Collect();
    result.processes = CollectProcesses(elapsedSeconds, elapsed100ns);

    cpuHistory_.Push(static_cast<float>(result.cpuPercent));
    memoryHistory_.Push(static_cast<float>(result.memoryPercent));
    gpuHistory_.Push(static_cast<float>(result.gpuPercent.value_or(0.0)));
    const double networkMib = static_cast<double>(result.networkDownloadBytesPerSecond + result.networkUploadBytesPerSecond) / (1024.0 * 1024.0);
    networkHistory_.Push(static_cast<float>(networkMib));
    result.cpuHistory = cpuHistory_.Values();
    result.memoryHistory = memoryHistory_.Values();
    result.gpuHistory = gpuHistory_.Values();
    result.networkHistory = networkHistory_.Values();
    return result;
}

std::vector<ProcessMetrics> MetricsCollector::CollectProcesses(double elapsedSeconds, std::uint64_t elapsed100ns) {
    std::vector<ProcessMetrics> result;
    std::unordered_map<std::uint32_t, ProcessPrevious> nextPrevious;
    UniqueHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (snapshot.get() == INVALID_HANDLE_VALUE) return result;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot.get(), &entry)) return result;

    const auto processorCount = LogicalProcessorCount();
    do {
        ProcessMetrics metric;
        metric.pid = entry.th32ProcessID;
        metric.name = entry.szExeFile;
        metric.threadCount = entry.cntThreads;

        UniqueHandle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, metric.pid));
        if (!process) {
            // The process is still listed with the information available from Toolhelp.
            result.push_back(std::move(metric));
            continue;
        }

        FILETIME created{}, exited{}, kernel{}, user{};
        if (GetProcessTimes(process.get(), &created, &exited, &kernel, &user)) {
            const auto creation = ToUInt64(created);
            const auto cpuTime = ToUInt64(kernel) + ToUInt64(user);
            const auto previous = processPrevious_.find(metric.pid);
            if (previous != processPrevious_.end() && previous->second.creationTime == creation) {
                metric.cpuPercent = CalculateProcessCpuPercent(previous->second.cpuTime, cpuTime, elapsed100ns, processorCount);
            }
            metric.startTime = FormatStartTime(created);

            IO_COUNTERS io{};
            std::uint64_t readBytes{};
            std::uint64_t writeBytes{};
            if (GetProcessIoCounters(process.get(), &io)) {
                readBytes = io.ReadTransferCount;
                writeBytes = io.WriteTransferCount;
                if (previous != processPrevious_.end() && previous->second.creationTime == creation) {
                    metric.readBytesPerSecond = CalculateRate(previous->second.readBytes, readBytes, elapsedSeconds);
                    metric.writeBytesPerSecond = CalculateRate(previous->second.writeBytes, writeBytes, elapsedSeconds);
                }
            }
            nextPrevious.emplace(metric.pid, ProcessPrevious{cpuTime, readBytes, writeBytes, creation});
        }

        PROCESS_MEMORY_COUNTERS_EX counters{};
        if (GetProcessMemoryInfo(process.get(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters))) {
            metric.workingSet = counters.WorkingSetSize;
        }
        result.push_back(std::move(metric));
    } while (Process32NextW(snapshot.get(), &entry));

    processPrevious_ = std::move(nextPrevious);
    return result;
}

MetricsCollector::NetworkTotals MetricsCollector::ReadNetworkTotals() noexcept {
    PMIB_IF_TABLE2 table = nullptr;
    if (GetIfTable2(&table) != NO_ERROR || !table) return {};

    NetworkTotals totals{};
    for (ULONG i = 0; i < table->NumEntries; ++i) {
        const auto& row = table->Table[i];
        if (row.OperStatus != IfOperStatusUp || row.MediaConnectState != MediaConnectStateConnected ||
            row.Type == IF_TYPE_SOFTWARE_LOOPBACK || row.Type == IF_TYPE_TUNNEL) {
            continue;
        }
        totals.received += row.InOctets;
        totals.sent += row.OutOctets;
        totals.valid = true;
    }
    FreeMibTable(table);
    return totals;
}

} // namespace processlens
