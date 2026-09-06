#pragma once

#include <filesystem>

namespace processlens {

// Creates/updates a Start Menu shortcut whose shell hotkey is Ctrl+Alt+P.
// The shortcut allows Windows Explorer to start ProcessLens even when it is not running.
bool EnsureLauncherShortcut(std::filesystem::path* shortcutPath = nullptr) noexcept;

} // namespace processlens
