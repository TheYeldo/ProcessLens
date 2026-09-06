#pragma once

#include <string_view>

namespace processlens {

enum class LogLevel { Info, Warning, Error };
void Log(LogLevel level, std::wstring_view message) noexcept;
void LogWarningOnce(std::wstring_view key, std::wstring_view message) noexcept;

} // namespace processlens
