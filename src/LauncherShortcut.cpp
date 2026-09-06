#include "processlens/LauncherShortcut.hpp"

#include "processlens/Logging.hpp"

#include <Windows.h>
#include <ShlObj.h>
#include <shobjidl.h>

#include <array>
#include <filesystem>

namespace processlens {

bool EnsureLauncherShortcut(std::filesystem::path* shortcutPath) noexcept {
    try {
        std::array<wchar_t, 32768> executableBuffer{};
        const DWORD length = GetModuleFileNameW(nullptr, executableBuffer.data(),
                                                static_cast<DWORD>(executableBuffer.size()));
        if (length == 0 || length >= executableBuffer.size()) return false;
        const std::filesystem::path executable(executableBuffer.data(), executableBuffer.data() + length);

        PWSTR programsPathRaw = nullptr;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_Programs, KF_FLAG_CREATE, nullptr, &programsPathRaw))) {
            return false;
        }
        const std::filesystem::path programsPath(programsPathRaw);
        CoTaskMemFree(programsPathRaw);
        const auto linkPath = programsPath / L"ProcessLens.lnk";

        IShellLinkW* shellLink = nullptr;
        if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&shellLink)))) {
            return false;
        }

        bool success = false;
        if (SUCCEEDED(shellLink->SetPath(executable.c_str())) &&
            SUCCEEDED(shellLink->SetWorkingDirectory(executable.parent_path().c_str())) &&
            SUCCEEDED(shellLink->SetDescription(L"Open ProcessLens system monitor (Ctrl+Alt+P)")) &&
            SUCCEEDED(shellLink->SetIconLocation(executable.c_str(), 0)) &&
            SUCCEEDED(shellLink->SetHotkey(MAKEWORD('P', HOTKEYF_CONTROL | HOTKEYF_ALT)))) {
            IPersistFile* persistFile = nullptr;
            if (SUCCEEDED(shellLink->QueryInterface(IID_PPV_ARGS(&persistFile)))) {
                success = SUCCEEDED(persistFile->Save(linkPath.c_str(), TRUE));
                persistFile->Release();
            }
        }
        shellLink->Release();

        if (success) {
            SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATHW, linkPath.c_str(), nullptr);
            if (shortcutPath) *shortcutPath = linkPath;
            Log(LogLevel::Info, L"Ctrl+Alt+P Start Menu shortcut is ready");
        }
        return success;
    } catch (...) {
        return false;
    }
}

} // namespace processlens
