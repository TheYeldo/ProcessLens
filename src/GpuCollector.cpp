#include "processlens/GpuCollector.hpp"

namespace processlens {

std::optional<double> GpuCollector::Collect() noexcept {
    // There is no single vendor-neutral Win32 counter that can be aggregated into
    // Task Manager's overall GPU value without mapping engines to physical adapters.
    // Returning unavailable is preferable to publishing a plausible but wrong value.
    return std::nullopt;
}

} // namespace processlens
