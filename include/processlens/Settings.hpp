#pragma once

#include "processlens/Localization.hpp"

#include <filesystem>
#include <string>

namespace processlens {

enum class ViewMode { Compact, Expanded };

struct Settings {
    int x{80};
    int y{80};
    int width{420};
    int height{700};
    ViewMode mode{ViewMode::Compact};
    bool alwaysOnTop{false};
    bool clickThrough{false};
    bool startWithWindows{true};
    int refreshIntervalMs{1000};
    Language language{Language::Russian};
    int accent{0};
    int opacityPercent{100};
    bool animations{true};

    [[nodiscard]] std::string ToJson() const;
    static Settings FromJson(std::string_view json);
    static std::filesystem::path DefaultPath();
    static Settings Load(const std::filesystem::path& path = DefaultPath());
    bool Save(const std::filesystem::path& path = DefaultPath()) const;
};

} // namespace processlens
