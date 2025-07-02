#include "file_utils.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <set>
#include <map>

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

// Recibe el path raíz, el set de rutas expandidas y el nivel de profundidad
void build_tree_entries(const std::filesystem::path& path, 
                        const std::set<std::filesystem::path>& expanded_dirs,
                        std::vector<EntryInfo>& out,
                        int depth) {
    namespace fs = std::filesystem;
    std::vector<EntryInfo> entries;

    for (const auto& entry : fs::directory_iterator(path)) {
        if (entry.is_directory()) {
            std::uintmax_t dir_size = get_directory_size(entry.path());
            bool is_expanded = expanded_dirs.count(entry.path()) > 0;
            entries.push_back({"[DIR] ", entry.path().filename().string(), entry.path(), dir_size, depth, is_expanded});
        } else if (entry.is_regular_file()) {
            entries.push_back({"[FILE]", entry.path().filename().string(), entry.path(), entry.file_size(), depth, false});
        } else {
            entries.push_back({"[OTRO]", entry.path().filename().string(), entry.path(), 0, depth, false});
        }
    }

    // Ordenar de mayor a menor tamaño
    std::sort(entries.begin(), entries.end(), [](const EntryInfo& a, const EntryInfo& b) {
        return a.size > b.size;
    });

    for (auto& e : entries) {
        out.push_back(e);
        // Si es directorio y está expandido, añadir sus hijos
        if (e.type == "[DIR] " && e.expanded) {
            build_tree_entries(e.full_path, expanded_dirs, out, depth + 1);
        }
    }
}

// Devuelve el vector plano para mostrar en la UI
std::vector<EntryInfo> get_directory_entries(const std::filesystem::path& path, int depth) {
    std::vector<EntryInfo> result;
    static std::set<std::filesystem::path> expanded_dirs; // Se gestiona desde main
    build_tree_entries(path, expanded_dirs, result, depth);
    return result;
}

// Para que main pueda modificar el set de expandidos:
std::set<std::filesystem::path>& get_expanded_dirs() {
    static std::set<std::filesystem::path> expanded_dirs;
    return expanded_dirs;
}