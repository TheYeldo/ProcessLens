#include "processlens/Settings.hpp"

#include <ShlObj.h>
#include <Windows.h>

#include <algorithm>
#include <fstream>
#include <regex>
#include <sstream>

namespace processlens {
namespace {

int ReadInt(std::string_view json, const char* key, int fallback) {
    const std::regex expression(std::string{"\""} + key + "\"\\s*:\\s*(-?[0-9]+)");
    const std::string input(json);
    std::smatch match;
    if (std::regex_search(input, match, expression)) {
        try { return std::stoi(match[1].str()); } catch (...) {}
    }
    return fallback;
}

bool ReadBool(std::string_view json, const char* key, bool fallback) {
    const std::regex expression(std::string{"\""} + key + "\"\\s*:\\s*(true|false)");
    const std::string input(json);
    std::smatch match;
    return std::regex_search(input, match, expression) ? match[1].str() == "true" : fallback;
}

} // namespace

std::string Settings::ToJson() const {
    std::ostringstream json;
    json << "{\n"
         << "  \"x\": " << x << ",\n"
         << "  \"y\": " << y << ",\n"
         << "  \"width\": " << width << ",\n"
         << "  \"height\": " << height << ",\n"
         << "  \"mode\": \"" << (mode == ViewMode::Compact ? "compact" : "expanded") << "\",\n"
         << "  \"alwaysOnTop\": " << (alwaysOnTop ? "true" : "false") << ",\n"
         << "  \"clickThrough\": " << (clickThrough ? "true" : "false") << ",\n"
         << "  \"startWithWindows\": " << (startWithWindows ? "true" : "false") << ",\n"
         << "  \"refreshIntervalMs\": " << refreshIntervalMs << "\n"
         << "}\n";
    return json.str();
}

Settings Settings::FromJson(std::string_view json) {
    Settings settings;
    settings.x = ReadInt(json, "x", settings.x);
    settings.y = ReadInt(json, "y", settings.y);
    settings.width = std::clamp(ReadInt(json, "width", settings.width), 300, 2400);
    settings.height = std::clamp(ReadInt(json, "height", settings.height), 300, 1600);
    settings.mode = json.find("\"mode\"") != std::string_view::npos && json.find("expanded") != std::string_view::npos
                        ? ViewMode::Expanded : ViewMode::Compact;
    settings.alwaysOnTop = ReadBool(json, "alwaysOnTop", settings.alwaysOnTop);
    settings.clickThrough = ReadBool(json, "clickThrough", settings.clickThrough);
    settings.startWithWindows = ReadBool(json, "startWithWindows", settings.startWithWindows);
    const auto interval = ReadInt(json, "refreshIntervalMs", settings.refreshIntervalMs);
    settings.refreshIntervalMs = (interval == 250 || interval == 500 || interval == 1000 || interval == 2000) ? interval : 1000;
    return settings;
}

std::filesystem::path Settings::DefaultPath() {
    PWSTR localAppData = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &localAppData))) {
        std::filesystem::path path(localAppData);
        CoTaskMemFree(localAppData);
        return path / L"ProcessLens" / L"config.json";
    }
    return std::filesystem::temp_directory_path() / L"ProcessLens" / L"config.json";
}

Settings Settings::Load(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    std::ostringstream contents;
    contents << input.rdbuf();
    return FromJson(contents.str());
}

bool Settings::Save(const std::filesystem::path& path) const {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) return false;
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << ToJson();
    return output.good();
}

} // namespace processlens
