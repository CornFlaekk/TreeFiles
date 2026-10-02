#pragma once
#include <filesystem>
#include <string>

struct Preferences {
    int foreground = 0; // black
    int background = 3; // yellow
    std::string language = "en";
    int page_size = 30;
};

using ColorSettings = Preferences;

int color_index(const std::string& name);
const char* color_name(int index);
ColorSettings load_color_settings(const std::filesystem::path& path, std::string& error);
bool save_color_settings(const std::filesystem::path& path, const ColorSettings& colors, std::string& error);
Preferences load_preferences(const std::filesystem::path& path, std::string& error);
bool save_preferences(const std::filesystem::path& path, const Preferences& preferences, std::string& error);
