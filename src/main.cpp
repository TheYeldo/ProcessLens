#include "processlens/App.hpp"

#include <Windows.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int commandShow) {
    processlens::App app;
    return app.Run(instance, commandShow);
}
