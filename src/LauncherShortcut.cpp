#include "processlens/LauncherShortcut.hpp"

#include "processlens/Logging.hpp"

#include <Windows.h>
#include <ShlObj.h>
#include <shobjidl.h>

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace processlens {
namespace {

std::optional<std::filesystem::path> ExecutablePath() {
    std::array<wchar_t, 32768> buffer{};
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) return std::nullopt;
    return std::filesystem::path(buffer.data(), buffer.data() + length);
}

std::optional<std::filesystem::path> ShortcutPath(REFKNOWNFOLDERID folder, DWORD flags) {
    PWSTR rawPath = nullptr;
    if (FAILED(SHGetKnownFolderPath(folder, flags, nullptr, &rawPath))) return std::nullopt;
    std::filesystem::path path(rawPath);
    CoTaskMemFree(rawPath);
    return path / L"ProcessLens.lnk";
}

bool CreateShortcut(const std::filesystem::path& linkPath,
                    std::wstring_view arguments,
                    WORD hotkey) {
    const auto executable = ExecutablePath();
    if (!executable) return false;

    IShellLinkW* shellLink = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&shellLink)))) {
        return false;
    }

    const std::wstring argumentsCopy(arguments);
    bool success = false;
    if (SUCCEEDED(shellLink->SetPath(executable->c_str())) &&
        SUCCEEDED(shellLink->SetArguments(argumentsCopy.c_str())) &&
        SUCCEEDED(shellLink->SetWorkingDirectory(executable->parent_path().c_str())) &&
        SUCCEEDED(shellLink->SetDescription(L"Open ProcessLens system monitor")) &&
        SUCCEEDED(shellLink->SetIconLocation(executable->c_str(), 0)) &&
        SUCCEEDED(shellLink->SetHotkey(hotkey))) {
        IPersistFile* persistFile = nullptr;
        if (SUCCEEDED(shellLink->QueryInterface(IID_PPV_ARGS(&persistFile)))) {
            success = SUCCEEDED(persistFile->Save(linkPath.c_str(), TRUE));
            persistFile->Release();
        }
    }
    shellLink->Release();
    if (success) SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATHW, linkPath.c_str(), nullptr);
    return success;
}

} // namespace

bool EnsureLauncherShortcut(std::filesystem::path* shortcutPath) noexcept {
    try {
        const auto path = ShortcutPath(FOLDERID_Programs, KF_FLAG_CREATE);
        if (!path || !CreateShortcut(*path, L"", 0)) {
            return false;
        }
        if (shortcutPath) *shortcutPath = *path;
        Log(LogLevel::Info, L"ProcessLens Start Menu shortcut is ready");
        return true;
    } catch (...) {
        return false;
    }
}

bool SetStartupEnabled(bool enabled) noexcept {
    try {
        const auto path = ShortcutPath(FOLDERID_Startup, enabled ? KF_FLAG_CREATE : KF_FLAG_DEFAULT);
        if (!path) return false;
        if (enabled) return CreateShortcut(*path, L"--background", 0);
        return DeleteFileW(path->c_str()) != FALSE || GetLastError() == ERROR_FILE_NOT_FOUND;
    } catch (...) {
        return false;
    }
}

bool IsStartupEnabled() noexcept {
    try {
        const auto path = ShortcutPath(FOLDERID_Startup, KF_FLAG_DEFAULT);
        return path && std::filesystem::exists(*path);
    } catch (...) {
        return false;
    }
}

} // namespace processlens
