#include "processlens/App.hpp"

#include "processlens/LauncherShortcut.hpp"
#include "processlens/Logging.hpp"
#include "processlens/MainWindow.hpp"
#include "processlens/WindowProtocol.hpp"

#include <objbase.h>

namespace processlens {

int App::Run(HINSTANCE instance, int commandShow) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    HANDLE instanceMutex = CreateMutexW(nullptr, TRUE, L"Local\\ProcessLens.SingleInstance");
    if (!instanceMutex) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existingWindow = nullptr;
        for (int attempt = 0; attempt < 20 && !existingWindow; ++attempt) {
            existingWindow = FindWindowW(WindowClassName, L"ProcessLens");
            if (!existingWindow) Sleep(50);
        }
        if (existingWindow) PostMessageW(existingWindow, MessageActivate, 0, 0);
        CloseHandle(instanceMutex);
        return existingWindow ? 0 : 1;
    }

    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (!EnsureLauncherShortcut()) {
        LogWarningOnce(L"launcher-shortcut", L"Unable to create the Ctrl+Alt+P Start Menu shortcut");
    }

    MainWindow window(instance);
    if (!window.Create()) {
        if (SUCCEEDED(comResult)) CoUninitialize();
        CloseHandle(instanceMutex);
        return 1;
    }
    window.Show(commandShow);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    if (SUCCEEDED(comResult)) CoUninitialize();
    CloseHandle(instanceMutex);
    return static_cast<int>(message.wParam);
}

} // namespace processlens
