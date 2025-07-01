#pragma once
#include <string>
#include <filesystem>
#include <vector>

struct EntryInfo {
    std::string type;
    std::string name;
    std::uintmax_t size;
};

std::vector<EntryInfo> get_directory_entries(const std::filesystem::path& path = ".");
std::string human_readable_size(std::uintmax_t bytes);
std::uintmax_t get_directory_size(const std::filesystem::path& dir_path);