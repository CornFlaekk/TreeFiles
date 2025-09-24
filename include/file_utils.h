#pragma once
#include <string>
#include <filesystem>
#include <vector>
#include <set>

struct EntryInfo {
    std::string type;
    std::string name;
    std::filesystem::path full_path;
    std::uintmax_t size;
    int depth = 0;           // Nivel de indentación
    bool expanded = false;   // Solo para directorios
};

std::vector<EntryInfo> get_directory_entries(const std::filesystem::path& path = ".", int depth = 0);
std::string human_readable_size(std::uintmax_t bytes);
std::set<std::filesystem::path>& get_expanded_dirs();
void build_tree_entries(const std::filesystem::path& path, 
                        const std::set<std::filesystem::path>& expanded_dirs,
                        std::vector<EntryInfo>& out,
                        int depth = 0,
                        int max_files = 100);

void clear_dir_size_cache();
void clear_tree_entries_cache();
void expand_resto(const std::filesystem::path& path);