#include "processlens/Formatting.hpp"

#include <array>
#include <algorithm>
#include <iomanip>
#include <sstream>

namespace processlens {

std::wstring FormatBytes(std::uint64_t bytes, Language language) {
    const std::array units{Tr(language, L"Б", L"B"), Tr(language, L"КБ", L"KB"),
        Tr(language, L"МБ", L"MB"), Tr(language, L"ГБ", L"GB"), Tr(language, L"ТБ", L"TB")};
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
    auto result = output.str();
    if (language == Language::Russian) std::replace(result.begin(), result.end(), L'.', L',');
    return result;
}

std::wstring FormatRate(std::uint64_t bytesPerSecond, Language language) {
    return FormatBytes(bytesPerSecond, language) + Tr(language, L"/с", L"/s");
}

std::wstring FormatPercent(double percent, int decimals, Language language) {
    std::wostringstream output;
    output << std::fixed << std::setprecision(decimals) << percent << L'%';
    auto result = output.str();
    if (language == Language::Russian) std::replace(result.begin(), result.end(), L'.', L',');
    return result;
}

std::wstring FormatUptime(std::uint64_t seconds, Language language) {
    const auto days = seconds / 86400;
    const auto hours = seconds / 3600 % 24;
    const auto minutes = seconds / 60 % 60;
    return (days ? std::to_wstring(days) + Tr(language, L" д ", L"d ") : L"") +
        std::to_wstring(hours) + Tr(language, L" ч ", L"h ") +
        std::to_wstring(minutes) + Tr(language, L" мин", L"m");
}

} // namespace processlens
