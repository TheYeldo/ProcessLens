#pragma once

#include <optional>

namespace processlens {

// Kept behind a dedicated interface because Windows exposes GPU utilization through
// adapter/engine-specific counters whose aggregation is not universally reliable.
class GpuCollector {
public:
    [[nodiscard]] std::optional<double> Collect() noexcept;
};

} // namespace processlens
