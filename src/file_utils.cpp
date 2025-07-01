#include "file_utils.h"
#include <sstream>
#include <iomanip>
#include <algorithm>

std::string human_readable_size(std::uintmax_t bytes) {
    const char* sizes[] = {"bytes", "KB", "MB", "GB", "TB"};
    int order = 0;
    double size = static_cast<double>(bytes);
    while (size >= 1024 && order < 4) {
        order++;
        size /= 1024;
    }
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(order == 0 ? 0 : 2) << size << " " << sizes[order];
    return oss.str();
}

std::uintmax_t get_directory_size(const std::filesystem::path& dir_path) {
    namespace fs = std::filesystem;
    std::uintmax_t size = 0;
    for (const auto& entry : fs::recursive_directory_iterator(dir_path, fs::directory_options::skip_permission_denied)) {
        if (entry.is_regular_file()) {
            size += entry.file_size();
        }
    }
    return size;
}

std::vector<EntryInfo> get_directory_entries(const std::filesystem::path& path) {
    namespace fs = std::filesystem;
    std::vector<EntryInfo> entries;

    for (const auto& entry : fs::directory_iterator(path)) {
        if (entry.is_directory()) {
            std::uintmax_t dir_size = get_directory_size(entry.path());
            entries.push_back({"[DIR] ", entry.path().filename().string(), dir_size});
        } else if (entry.is_regular_file()) {
            entries.push_back({"[FILE]", entry.path().filename().string(), entry.file_size()});
        } else {
            entries.push_back({"[OTRO]", entry.path().filename().string(), 0});
        }
    }

    // Ordenar de mayor a menor tamaño
    std::sort(entries.begin(), entries.end(), [](const EntryInfo& a, const EntryInfo& b) {
        return a.size > b.size;
    });

    return entries;
}