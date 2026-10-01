#include "file_utils.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <set>
#include <map>
#include <unordered_map>
#include <mutex>

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

std::unordered_map<std::filesystem::path, std::uintmax_t> dir_size_cache;
std::mutex cache_mutex;

std::uintmax_t get_directory_size(const std::filesystem::path& dir_path) {
    namespace fs = std::filesystem;
    fs::path norm_path = dir_path.lexically_normal();
    {
        std::lock_guard<std::mutex> lock(cache_mutex);
        auto it = dir_size_cache.find(norm_path);
        if (it != dir_size_cache.end()) {
            return it->second;
        }
    }
    std::uintmax_t size = 0;
    try {
        for (const auto& entry : fs::recursive_directory_iterator(norm_path, fs::directory_options::skip_permission_denied)) {
            // Evitar seguir enlaces simbólicos
            if (entry.is_symlink()) continue;
            if (entry.is_regular_file()) {
                fs::path file_path = entry.path().lexically_normal();
                std::uintmax_t sz = 0;
                {
                    std::lock_guard<std::mutex> lock(cache_mutex);
                    auto it = dir_size_cache.find(file_path);
                    if (it != dir_size_cache.end()) {
                        sz = it->second;
                    } else {
                        sz = entry.file_size();
                        dir_size_cache[file_path] = sz;
                    }
                }
                if (sz > (1ULL << 40)) continue; // Ignora archivos >1TB
                size += sz;
            }
        }
    } catch (const fs::filesystem_error& ex) {
        // Error al recorrer (demasiados enlaces, permisos, etc.)
        return 0;
    }
    {
        std::lock_guard<std::mutex> lock(cache_mutex);
        dir_size_cache[norm_path] = size;
    }
    return size;
}

struct RestoState {
    std::map<std::filesystem::path, int> resto_page; // path -> página actual
};
static RestoState resto_state;

// Recibe el path raíz, el set de rutas expandidas y el nivel de profundidad
void build_tree_entries(const std::filesystem::path& path, 
                        const std::set<std::filesystem::path>& expanded_dirs,
                        std::vector<EntryInfo>& out,
                        int depth,
                        int max_files) {
    namespace fs = std::filesystem;
    std::vector<EntryInfo> all_entries;
    for (const auto& entry : fs::directory_iterator(path)) {
        if (entry.is_directory()) {
            std::uintmax_t dir_size = get_directory_size(entry.path());
            bool is_expanded = expanded_dirs.count(entry.path()) > 0;
            all_entries.push_back({"[DIR] ", entry.path().filename().u8string(), entry.path(), dir_size, depth, is_expanded});
        } else if (entry.is_regular_file()) {
            std::uintmax_t sz = entry.file_size();
            all_entries.push_back({"[FILE]", entry.path().filename().u8string(), entry.path(), sz, depth, false});
        }
    }
    // Ordena todo junto por tamaño descendente y nombre
    std::sort(all_entries.begin(), all_entries.end(), [](const EntryInfo& a, const EntryInfo& b) {
        if (a.size != b.size) return a.size > b.size;
        return a.name < b.name;
    });

    int total_pages = ((int)all_entries.size() + max_files - 1) / max_files;
    if (total_pages == 0) total_pages = 1;

    int page = resto_state.resto_page[path];
    if (page >= total_pages) {
        page = 0;
        resto_state.resto_page[path] = 0;
    }
    if (page < 0) {
        page = 0;
        resto_state.resto_page[path] = 0;
    }

    int start_idx = page * max_files;
    int end_idx = std::min((int)all_entries.size(), start_idx + max_files);

    // Botón retroceder página si no estamos en la primera
    if (total_pages > 1 && page > 0) {
        std::string label = "\u25c2\u25c2 Anterior (" + std::to_string(page + 1) + "/" + std::to_string(total_pages) + ")";
        out.push_back({"[RESTO_PREV]", label, path, 0, depth, false});
    }

    // Añade los elementos de la página actual
    for (int i = start_idx; i < end_idx; ++i) {
        out.push_back(all_entries[i]);
        if (all_entries[i].type == "[DIR] " && all_entries[i].expanded) {
            build_tree_entries(all_entries[i].full_path, expanded_dirs, out, depth + 1, max_files);
        }
    }

    // Botón avanzar página si hay más
    if (total_pages > 1 && page < total_pages - 1) {
        std::string label = "\u25b8\u25b8 Siguiente (" + std::to_string(page + 1) + "/" + std::to_string(total_pages) + ")";
        out.push_back({"[RESTO_NEXT]", label, path, 0, depth, false});
    }
}

void expand_resto(const std::filesystem::path& path) {
    resto_state.resto_page[path]++;
}

void prev_resto(const std::filesystem::path& path) {
    if (resto_state.resto_page[path] > 0)
        resto_state.resto_page[path]--;
}

void reset_resto(const std::filesystem::path& path) {
    resto_state.resto_page[path] = 0;
}

void reset_resto_state() {
    resto_state.resto_page.clear();
}

int get_current_page(const std::filesystem::path& path) {
    auto it = resto_state.resto_page.find(path);
    return (it != resto_state.resto_page.end()) ? it->second : 0;
}

// Devuelve el vector plano para mostrar en la UI
std::vector<EntryInfo> get_directory_entries(const std::filesystem::path& path, int depth) {
    static std::set<std::filesystem::path> expanded_dirs; // Se gestiona desde main
    std::vector<EntryInfo> result;
    build_tree_entries(path, expanded_dirs, result, depth, 30);
    return result;
}

// Para que main pueda modificar el set de expandidos:
std::set<std::filesystem::path>& get_expanded_dirs() {
    static std::set<std::filesystem::path> expanded_dirs;
    return expanded_dirs;
}

void clear_dir_size_cache() {
    std::lock_guard<std::mutex> lock(cache_mutex);
    dir_size_cache.clear();
}
