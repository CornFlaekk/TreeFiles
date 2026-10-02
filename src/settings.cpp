#include "settings.h"
#include "platform_utils.h"
#include <array>
#include <fstream>
#include <random>
#include <stdexcept>
#include <charconv>

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

Preferences load_preferences(const std::filesystem::path& path, std::string& error) {
    error.clear();
    Preferences preferences;
    if (path.empty()) return preferences;
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        if (ec) error = ec.message();
        return preferences;
    }
    std::ifstream file(path);
    if (!file || !std::filesystem::is_regular_file(path, ec)) {
        error = "Cannot open " + path.u8string();
        return preferences;
    }
    std::string line;
    while (std::getline(file, line)) {
        if (line.rfind("\xef\xbb\xbf", 0) == 0) line.erase(0, 3);
        const auto separator = line.find('=');
        if (separator == std::string::npos) continue;
        const auto key = trim(line.substr(0, separator));
        const auto value = trim(line.substr(separator + 1));
        if (key == "foreground" || key == "background") {
            const int color = color_index(value);
            if (color >= 0 && key == "foreground") preferences.foreground = color;
            else if (color >= 0 && key == "background") preferences.background = color;
        } else if (key == "language") {
            if (value == "en" || value == "es") preferences.language = value;
        } else if (key == "page_size") {
            int page_size = 0;
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), page_size);
            if (parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size() && page_size > 0)
                preferences.page_size = page_size;
        }
    }
    if (file.bad()) error = "Cannot read " + path.u8string();
    return preferences;
}

ColorSettings load_color_settings(const std::filesystem::path& path, std::string& error) {
    return load_preferences(path, error);
}

bool save_color_settings(const std::filesystem::path& path, const ColorSettings& colors, std::string& error) {
    std::string read_error;
    auto preferences = load_preferences(path, read_error);
    if (!read_error.empty()) {
        error = read_error;
        return false;
    }
    preferences.foreground = colors.foreground;
    preferences.background = colors.background;
    return save_preferences(path, preferences, error);
}

bool save_preferences(const std::filesystem::path& path, const Preferences& preferences, std::string& error) {
    error.clear();
    std::filesystem::path temporary;
    try {
        if (path.empty()) throw std::runtime_error("No configuration directory available.");
        if (preferences.foreground < 0 || preferences.foreground > 7 ||
            preferences.background < 0 || preferences.background > 7)
            throw std::runtime_error("Invalid color.");
        if (preferences.language != "en" && preferences.language != "es")
            throw std::runtime_error("Invalid language.");
        if (preferences.page_size <= 0)
            throw std::runtime_error("Invalid page size.");
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        temporary = path;
        temporary += ".tmp." + std::to_string(std::random_device{}()) + "." + std::to_string(std::random_device{}());
        std::ofstream file(temporary, std::ios::trunc);
        file << "foreground=" << color_name(preferences.foreground) << "\n"
             << "background=" << color_name(preferences.background) << "\n"
             << "language=" << preferences.language << "\n"
             << "page_size=" << preferences.page_size << "\n";
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
