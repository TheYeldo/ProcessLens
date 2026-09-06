#pragma once

#include <filesystem>
#include <string>

namespace processlens {

enum class ViewMode { Compact, Expanded };

struct Settings {
    int x{80};
    int y{80};
    int width{370};
    int height{570};
    ViewMode mode{ViewMode::Compact};
    bool alwaysOnTop{false};
    bool clickThrough{false};
    int refreshIntervalMs{1000};

    [[nodiscard]] std::string ToJson() const;
    static Settings FromJson(std::string_view json);
    static std::filesystem::path DefaultPath();
    static Settings Load(const std::filesystem::path& path = DefaultPath());
    bool Save(const std::filesystem::path& path = DefaultPath()) const;
};

} // namespace processlens
