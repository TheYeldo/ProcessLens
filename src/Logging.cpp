#include "processlens/Logging.hpp"

#include <Windows.h>

#include <mutex>
#include <string>
#include <unordered_set>

namespace processlens {

void Log(LogLevel level, std::wstring_view message) noexcept {
#ifdef _DEBUG
    const wchar_t* prefix = level == LogLevel::Info ? L"[INFO] " : level == LogLevel::Warning ? L"[WARN] " : L"[ERROR] ";
    std::wstring line(prefix);
    line.append(message);
    line.append(L"\n");
    OutputDebugStringW(line.c_str());
#else
    (void)level;
    (void)message;
#endif
}

void LogWarningOnce(std::wstring_view key, std::wstring_view message) noexcept {
#ifdef _DEBUG
    static std::mutex mutex;
    static std::unordered_set<std::wstring> emitted;
    std::scoped_lock lock(mutex);
    if (emitted.emplace(key).second) {
        Log(LogLevel::Warning, message);
    }
#else
    (void)key;
    (void)message;
#endif
}

} // namespace processlens
