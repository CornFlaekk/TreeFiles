#pragma once
#include <filesystem>
#include <string>

struct ColorSettings {
    int foreground = 0; // black
    int background = 3; // yellow
};

int color_index(const std::string& name);
const char* color_name(int index);
ColorSettings load_color_settings(const std::filesystem::path& path, std::string& error);
bool save_color_settings(const std::filesystem::path& path, const ColorSettings& colors, std::string& error);
