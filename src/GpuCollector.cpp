#include "processlens/GpuCollector.hpp"

#include "processlens/Calculations.hpp"

#include <Windows.h>
#include <pdh.h>
#include <pdhmsg.h>

#include <chrono>
#include <cstddef>
#include <vector>

namespace processlens {
namespace {

constexpr wchar_t kGpuCounterPath[] = L"\\GPU Engine(*)\\Utilization Percentage";
constexpr auto kRetryDelay = std::chrono::seconds(10);

bool IsUsableCounterStatus(PDH_STATUS status) noexcept {
    return status == PDH_CSTATUS_VALID_DATA || status == PDH_CSTATUS_NEW_DATA;
}

} // namespace

struct GpuCollector::Impl {
    PDH_HQUERY query{};
    PDH_HCOUNTER counter{};
    std::chrono::steady_clock::time_point retryAfter{};

    ~Impl() {
        Close();
    }

    void Close() noexcept {
        if (query != nullptr) {
            PdhCloseQuery(query);
        }
        query = nullptr;
        counter = nullptr;
    }

    bool Open() noexcept {
        const auto now = std::chrono::steady_clock::now();
        if (query != nullptr) {
            return true;
        }
        if (now < retryAfter) {
            return false;
        }

        if (PdhOpenQueryW(nullptr, 0, &query) != ERROR_SUCCESS) {
            query = nullptr;
            retryAfter = now + kRetryDelay;
            return false;
        }
        if (PdhAddEnglishCounterW(query, kGpuCounterPath, 0, &counter) != ERROR_SUCCESS ||
            PdhCollectQueryData(query) != ERROR_SUCCESS) {
            Close();
            retryAfter = now + kRetryDelay;
            return false;
        }
        return true;
    }

    void RetryLater() noexcept {
        Close();
        retryAfter = std::chrono::steady_clock::now() + kRetryDelay;
    }
};

GpuCollector::GpuCollector() : impl_(std::make_unique<Impl>()) {}

GpuCollector::~GpuCollector() = default;

std::optional<double> GpuCollector::Collect() noexcept {
    try {
        if (!impl_->Open()) {
            return std::nullopt;
        }
        if (PdhCollectQueryData(impl_->query) != ERROR_SUCCESS) {
            impl_->RetryLater();
            return std::nullopt;
        }

        DWORD bufferSize = 0;
        DWORD itemCount = 0;
        const auto sizeStatus = PdhGetFormattedCounterArrayW(
            impl_->counter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, nullptr);
        if (sizeStatus == PDH_NO_DATA || itemCount == 0) {
            return std::nullopt;
        }
        if (sizeStatus != PDH_MORE_DATA || bufferSize == 0) {
            impl_->RetryLater();
            return std::nullopt;
        }

        std::vector<std::byte> buffer(bufferSize);
        auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
        const auto dataStatus = PdhGetFormattedCounterArrayW(
            impl_->counter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, items);
        if (dataStatus != ERROR_SUCCESS) {
            impl_->RetryLater();
            return std::nullopt;
        }

        std::vector<GpuEngineUtilization> samples;
        samples.reserve(itemCount);
        for (DWORD index = 0; index < itemCount; ++index) {
            const auto& item = items[index];
            if (item.szName != nullptr && IsUsableCounterStatus(item.FmtValue.CStatus)) {
                samples.push_back({item.szName, item.FmtValue.doubleValue});
            }
        }
        return CalculateOverallGpuPercent(samples);
    } catch (...) {
        impl_->RetryLater();
        return std::nullopt;
    }
}

} // namespace processlens
