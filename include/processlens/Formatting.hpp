#pragma once

#include "processlens/Localization.hpp"

#include <cstdint>
#include <string>

namespace processlens {

std::wstring FormatBytes(std::uint64_t bytes, Language language = Language::English);
std::wstring FormatRate(std::uint64_t bytesPerSecond, Language language = Language::English);
std::wstring FormatPercent(double percent, int decimals = 0, Language language = Language::English);
std::wstring FormatUptime(std::uint64_t seconds, Language language);

} // namespace processlens
