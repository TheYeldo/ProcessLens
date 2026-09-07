#pragma once

#include <memory>
#include <optional>

namespace processlens {

class GpuCollector {
public:
    GpuCollector();
    ~GpuCollector();

    GpuCollector(const GpuCollector&) = delete;
    GpuCollector& operator=(const GpuCollector&) = delete;
    GpuCollector(GpuCollector&&) = delete;
    GpuCollector& operator=(GpuCollector&&) = delete;

    [[nodiscard]] std::optional<double> Collect() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace processlens
