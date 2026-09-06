#pragma once

#include <Windows.h>

namespace processlens {

inline constexpr wchar_t WindowClassName[] = L"ProcessLens.MainWindow";
inline constexpr UINT MessageActivate = WM_APP + 44;

} // namespace processlens
