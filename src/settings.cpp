#include "settings.h"
#include "platform_utils.h"
#include <array>
#include <fstream>
#include <random>
#include <stdexcept>

namespace {
constexpr std::array<const char*, 8> color_names = {
    "black", "red", "green", "yellow", "blue", "magenta", "cyan", "white"
};

std::string trim(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r");
    if (first == std::string::npos) return {};
    return value.substr(first, value.find_last_not_of(" \t\r") - first + 1);
}
}

int color_index(const std::string& name) {
    for (size_t i = 0; i < color_names.size(); ++i)
        if (name == color_names[i]) return static_cast<int>(i);
    return -1;
}

const char* color_name(int index) {
    return color_names.at(static_cast<size_t>(index));
}

ColorSettings load_color_settings(const std::filesystem::path& path, std::string& error) {
    error.clear();
    ColorSettings colors;
    if (path.empty()) return colors;
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        if (ec) error = ec.message();
        return colors;
    }
    std::ifstream file(path);
    if (!file || !std::filesystem::is_regular_file(path, ec)) {
        error = "Cannot open " + path.u8string();
        return colors;
    }
    std::string line;
    while (std::getline(file, line)) {
        if (line.rfind("\xef\xbb\xbf", 0) == 0) line.erase(0, 3);
        const auto separator = line.find('=');
        if (separator == std::string::npos) continue;
        const auto key = trim(line.substr(0, separator));
        const auto value = color_index(trim(line.substr(separator + 1)));
        if (value < 0) continue;
        if (key == "foreground") colors.foreground = value;
        else if (key == "background") colors.background = value;
    }
    if (file.bad()) error = "Cannot read " + path.u8string();
    return colors;
}

bool save_color_settings(const std::filesystem::path& path, const ColorSettings& colors, std::string& error) {
    error.clear();
    std::filesystem::path temporary;
    try {
        if (path.empty()) throw std::runtime_error("No configuration directory available.");
        if (colors.foreground < 0 || colors.foreground > 7 || colors.background < 0 || colors.background > 7)
            throw std::runtime_error("Invalid color.");
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        temporary = path;
        temporary += ".tmp." + std::to_string(std::random_device{}()) + "." + std::to_string(std::random_device{}());
        std::ofstream file(temporary, std::ios::trunc);
        file << "foreground=" << color_name(colors.foreground) << "\n"
             << "background=" << color_name(colors.background) << "\n";
        file.close();
        if (!file) throw std::runtime_error("Cannot write " + path.u8string());
        if (!replace_file(temporary, path, error)) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return false;
        }
        return true;
    } catch (const std::exception& ex) {
        error = ex.what();
        std::error_code ignored;
        if (!temporary.empty()) std::filesystem::remove(temporary, ignored);
        return false;
    }
}
