#include "processlens/Formatting.hpp"

#include <array>
#include <iomanip>
#include <sstream>

namespace processlens {

std::wstring FormatBytes(std::uint64_t bytes) {
    constexpr std::array units{L"B", L"KB", L"MB", L"GB", L"TB"};
    double value = static_cast<double>(bytes);
    std::size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < units.size()) {
        value /= 1024.0;
        ++unit;
    }
    std::wostringstream output;
    if (unit == 0) {
        output << bytes;
    } else {
        output << std::fixed << std::setprecision(value < 10.0 ? 2 : 1) << value;
    }
    output << L' ' << units[unit];
    return output.str();
}

std::wstring FormatRate(std::uint64_t bytesPerSecond) {
    return FormatBytes(bytesPerSecond) + L"/s";
}

std::wstring FormatPercent(double percent, int decimals) {
    std::wostringstream output;
    output << std::fixed << std::setprecision(decimals) << percent << L'%';
    return output.str();
}

} // namespace processlens
