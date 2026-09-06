#pragma once

#include <filesystem>

namespace processlens {

// Creates/updates the regular per-user Start Menu launcher.
bool EnsureLauncherShortcut(std::filesystem::path* shortcutPath = nullptr) noexcept;
bool SetStartupEnabled(bool enabled) noexcept;
bool IsStartupEnabled() noexcept;

} // namespace processlens
