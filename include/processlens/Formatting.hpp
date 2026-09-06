#pragma once

#include <cstdint>
#include <string>

namespace processlens {

std::wstring FormatBytes(std::uint64_t bytes);
std::wstring FormatRate(std::uint64_t bytesPerSecond);
std::wstring FormatPercent(double percent, int decimals = 0);

} // namespace processlens
