#include "file_utils.h"
#include "localization.h"
#include "platform_utils.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <set>
#include <map>
#include <unordered_map>
#include <mutex>
#include <stdexcept>

namespace fs = std::filesystem;

std::string human_readable_size(std::uintmax_t bytes) {
    const char* sizes[] = {text(Text::Bytes), "KB", "MB", "GB", "TB"};
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

struct DirectorySize {
    std::uintmax_t bytes = 0;
    SizeStatus status = SizeStatus::complete;
};

std::unordered_map<fs::path, DirectorySize> dir_size_cache;
std::mutex cache_mutex;

struct RestoState {
    std::map<fs::path, int> resto_page;
};
static RestoState resto_state;

static std::error_code injected_error(const ScanOptions& options, const fs::path& path,
                                      const std::string& operation) {
    return options.error_injector ? options.error_injector(path, operation) : std::error_code{};
}

static void record_issue(ScanResult& result, const fs::path& path,
                         const std::string& operation, const std::error_code& error) {
    if (!error) return;
    const auto duplicate = std::find_if(result.diagnostics.begin(), result.diagnostics.end(),
        [&](const ScanIssue& issue) {
            return issue.path == path && issue.operation == operation && issue.error == error;
        });
    if (duplicate == result.diagnostics.end())
        result.diagnostics.push_back({path, operation, error});
    if (result.status == ScanStatus::complete) result.status = ScanStatus::partial;
}

static DirectorySize measure_directory(const fs::path& dir_path, const ScanOptions& options,
                                       ScanResult& result) {
    const fs::path normalized = dir_path.lexically_normal();
    const bool injectable = static_cast<bool>(options.error_injector);
    if (!injectable) {
        std::lock_guard<std::mutex> lock(cache_mutex);
        auto cached = dir_size_cache.find(normalized);
        if (cached != dir_size_cache.end()) return cached->second;
    }

    DirectorySize measured;
    std::error_code error = injected_error(options, normalized, "directory_open");
    fs::recursive_directory_iterator iterator;
    if (!error) iterator = fs::recursive_directory_iterator(normalized, fs::directory_options::none, error);
    if (error) {
        record_issue(result, normalized, "directory_open", error);
        measured.status = SizeStatus::unavailable;
        return measured;
    }

    const fs::recursive_directory_iterator end;
    while (iterator != end) {
        const fs::path child = iterator->path();
        error = injected_error(options, child, "entry_status");
        fs::file_status status;
        if (!error) status = iterator->symlink_status(error);
        if (error) {
            record_issue(result, child, "entry_status", error);
            measured.status = SizeStatus::partial;
        } else if (fs::is_symlink(status) || is_directory_link(child)) {
            iterator.disable_recursion_pending();
        } else if (fs::is_regular_file(status)) {
            error = injected_error(options, child, "file_size");
            std::uintmax_t bytes = 0;
            if (!error) {
                if (options.file_size_reader) {
                    try { bytes = options.file_size_reader(child); }
                    catch (const fs::filesystem_error& ex) { error = ex.code(); }
                    catch (...) { error = std::make_error_code(std::errc::io_error); }
                } else {
                    bytes = fs::file_size(child, error);
                }
            }
            if (error) {
                record_issue(result, child, "file_size", error);
                measured.status = SizeStatus::partial;
            } else {
                measured.bytes += bytes;
            }
        }

        error.clear();
        iterator.increment(error);
        if (error) {
            record_issue(result, child, "iterator_increment", error);
            measured.status = SizeStatus::partial;
            break;
        }
    }

    if (measured.status == SizeStatus::complete && !injectable) {
        std::lock_guard<std::mutex> lock(cache_mutex);
        dir_size_cache[normalized] = measured;
    }
    return measured;
}

static void append_directory(const fs::path& path,
                             const std::set<fs::path>& expanded_dirs,
                             int depth, int max_files, const ScanOptions& options,
                             ScanResult& result) {
    if (max_files <= 0) throw std::invalid_argument("Page size must be positive.");

    std::vector<EntryInfo> all_entries;
    std::error_code error = injected_error(options, path, "directory_open");
    fs::directory_iterator iterator;
    if (!error) iterator = fs::directory_iterator(path, fs::directory_options::none, error);
    if (error) {
        record_issue(result, path, "directory_open", error);
        if (depth == 0) result.status = ScanStatus::failed;
        return;
    }

    const fs::directory_iterator end;
    while (iterator != end) {
        const fs::path child = iterator->path();
        error = injected_error(options, child, "entry_status");
        fs::file_status status;
        if (!error) status = iterator->symlink_status(error);
        if (error) {
            record_issue(result, child, "entry_status", error);
        } else if (fs::is_symlink(status) || is_directory_link(child)) {
            all_entries.push_back({"[LINK]", child.filename().u8string(), child, 0,
                                   depth, false, SizeStatus::unavailable});
        } else if (fs::is_directory(status)) {
            const auto size = measure_directory(child, options, result);
            const bool expanded = expanded_dirs.count(child) > 0;
            all_entries.push_back({"[DIR] ", child.filename().u8string(), child,
                                   size.bytes, depth, expanded, size.status});
        } else if (fs::is_regular_file(status)) {
            error = injected_error(options, child, "file_size");
            std::uintmax_t bytes = 0;
            if (!error) {
                if (options.file_size_reader) {
                    try { bytes = options.file_size_reader(child); }
                    catch (const fs::filesystem_error& ex) { error = ex.code(); }
                    catch (...) { error = std::make_error_code(std::errc::io_error); }
                } else {
                    bytes = fs::file_size(child, error);
                }
            }
            if (error) {
                record_issue(result, child, "file_size", error);
                all_entries.push_back({"[FILE]", child.filename().u8string(), child, 0,
                                       depth, false, SizeStatus::unavailable});
            } else {
                all_entries.push_back({"[FILE]", child.filename().u8string(), child, bytes,
                                       depth, false, SizeStatus::complete});
            }
        }

        error.clear();
        iterator.increment(error);
        if (error) {
            record_issue(result, child, "iterator_increment", error);
            break;
        }
    }

    std::sort(all_entries.begin(), all_entries.end(), [](const EntryInfo& a, const EntryInfo& b) {
        if (a.size != b.size) return a.size > b.size;
        return a.name < b.name;
    });

    const size_t page_capacity = static_cast<size_t>(max_files);
    size_t total_pages = all_entries.size() / page_capacity
                       + (all_entries.size() % page_capacity != 0);
    if (total_pages == 0) total_pages = 1;

    int page = resto_state.resto_page[path];
    if (page < 0 || static_cast<size_t>(page) >= total_pages) {
        page = 0;
        resto_state.resto_page[path] = 0;
    }

    const size_t start_idx = static_cast<size_t>(page) * page_capacity;
    const size_t end_idx = start_idx + std::min(page_capacity, all_entries.size() - start_idx);

    if (total_pages > 1 && page > 0) {
        std::string label = std::string("\u25c2\u25c2 ") + text(Text::Previous) + " (" +
                            std::to_string(page + 1) + "/" + std::to_string(total_pages) + ")";
        result.entries.push_back({"[RESTO_PREV]", label, path, 0, depth, false});
    }

    for (size_t index = start_idx; index < end_idx; ++index) {
        result.entries.push_back(all_entries[index]);
        if (all_entries[index].type == "[DIR] " && all_entries[index].expanded)
            append_directory(all_entries[index].full_path, expanded_dirs, depth + 1,
                             max_files, options, result);
    }

    if (static_cast<size_t>(page) + 1 < total_pages) {
        std::string label = std::string("\u25b8\u25b8 ") + text(Text::Next) + " (" +
                            std::to_string(page + 1) + "/" + std::to_string(total_pages) + ")";
        result.entries.push_back({"[RESTO_NEXT]", label, path, 0, depth, false});
    }
}

ScanResult scan_tree_entries(const fs::path& requested_path,
                             const std::set<fs::path>& expanded_dirs,
                             int max_files, const ScanOptions& options) {
    if (max_files <= 0) throw std::invalid_argument("Page size must be positive.");
    ScanResult result;
    fs::path root = requested_path;
    std::error_code error = injected_error(options, root, "root_status");
    fs::file_status root_status;
    if (!error) root_status = fs::symlink_status(root, error);
    if (error) {
        record_issue(result, root, "root_status", error);
        result.status = ScanStatus::failed;
        return result;
    }
    if (fs::is_symlink(root_status) || is_directory_link(root)) {
        root = fs::canonical(root, error);
        if (error) {
            record_issue(result, requested_path, "resolve_root_link", error);
            result.status = ScanStatus::failed;
            return result;
        }
    }
    const bool root_is_directory = fs::is_directory(root, error);
    if (error || !root_is_directory) {
        record_issue(result, root, "directory_open", error ? error :
                     std::make_error_code(std::errc::not_a_directory));
        result.status = ScanStatus::failed;
        return result;
    }

    try {
        append_directory(root, expanded_dirs, 0, max_files, options, result);
    } catch (const fs::filesystem_error& ex) {
        record_issue(result, ex.path1().empty() ? root : ex.path1(), "scan", ex.code());
        if (result.entries.empty()) result.status = ScanStatus::failed;
    } catch (...) {
        record_issue(result, root, "scan", std::make_error_code(std::errc::io_error));
        if (result.entries.empty()) result.status = ScanStatus::failed;
    }
    return result;
}

void build_tree_entries(const fs::path& path,
                        const std::set<fs::path>& expanded_dirs,
                        std::vector<EntryInfo>& out,
                        int depth,
                        int max_files,
                        const FileSizeReader& file_size_reader) {
    ScanOptions options;
    options.file_size_reader = file_size_reader;
    auto result = scan_tree_entries(path, expanded_dirs, max_files, options);
    for (auto& entry : result.entries) {
        entry.depth += depth;
        out.push_back(std::move(entry));
    }
}

void expand_resto(const fs::path& path) { resto_state.resto_page[path]++; }

void prev_resto(const fs::path& path) {
    if (resto_state.resto_page[path] > 0) resto_state.resto_page[path]--;
}

void reset_resto(const fs::path& path) { resto_state.resto_page[path] = 0; }
void reset_resto_state() { resto_state.resto_page.clear(); }
void clear_tree_entries_cache() { reset_resto_state(); }

static bool keep_directory_state(const fs::path& path) {
    std::error_code error;
    const auto status = fs::symlink_status(path, error);
    if (error) {
        return error != std::errc::no_such_file_or_directory &&
               error != std::errc::not_a_directory;
    }
    return fs::is_directory(status) && !fs::is_symlink(status) && !is_directory_link(path);
}

void prune_tree_state(std::set<fs::path>& expanded_dirs) {
    for (auto it = expanded_dirs.begin(); it != expanded_dirs.end();) {
        if (!keep_directory_state(*it)) it = expanded_dirs.erase(it);
        else ++it;
    }
    for (auto it = resto_state.resto_page.begin(); it != resto_state.resto_page.end();) {
        if (!keep_directory_state(it->first)) it = resto_state.resto_page.erase(it);
        else ++it;
    }
}

int get_current_page(const fs::path& path) {
    auto it = resto_state.resto_page.find(path);
    return it != resto_state.resto_page.end() ? it->second : 0;
}

std::vector<EntryInfo> get_directory_entries(const fs::path& path, int depth) {
    static std::set<fs::path> expanded_dirs;
    std::vector<EntryInfo> result;
    build_tree_entries(path, expanded_dirs, result, depth, 30);
    return result;
}

std::set<fs::path>& get_expanded_dirs() {
    static std::set<fs::path> expanded_dirs;
    return expanded_dirs;
}

void clear_dir_size_cache() {
    std::lock_guard<std::mutex> lock(cache_mutex);
    dir_size_cache.clear();
}
