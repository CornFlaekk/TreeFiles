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
#include <optional>
#include <string_view>

namespace fs = std::filesystem;

const char* sort_key_name(SortKey key) {
    switch (key) {
        case SortKey::size: return "size";
        case SortKey::name: return "name";
        case SortKey::mtime: return "mtime";
    }
    return "size";
}

const char* sort_order_name(SortOrder order) {
    return order == SortOrder::asc ? "asc" : "desc";
}

bool parse_sort_key(const std::string& value, SortKey& key) {
    if (value == "size") key = SortKey::size;
    else if (value == "name") key = SortKey::name;
    else if (value == "mtime") key = SortKey::mtime;
    else return false;
    return true;
}

bool parse_sort_order(const std::string& value, SortOrder& order) {
    if (value == "asc") order = SortOrder::asc;
    else if (value == "desc") order = SortOrder::desc;
    else return false;
    return true;
}

static bool bytewise_less(std::string_view left, std::string_view right) {
    return std::lexicographical_compare(left.begin(), left.end(), right.begin(), right.end(),
        [](char a, char b) {
            return static_cast<unsigned char>(a) < static_cast<unsigned char>(b);
        });
}

static bool entry_less(const EntryInfo& left, const EntryInfo& right, SortOptions sort) {
    if (sort.key == SortKey::mtime) {
        if (left.modified.has_value() != right.modified.has_value())
            return left.modified.has_value(); // Missing timestamps stay last in either direction.
        if (left.modified && right.modified && *left.modified != *right.modified)
            return sort.order == SortOrder::asc
                ? *left.modified < *right.modified : *left.modified > *right.modified;
    } else if (sort.key == SortKey::size && left.size != right.size) {
        return sort.order == SortOrder::asc ? left.size < right.size : left.size > right.size;
    } else if (sort.key == SortKey::name) {
        if (left.name != right.name)
            return sort.order == SortOrder::asc
                ? bytewise_less(left.name, right.name) : bytewise_less(right.name, left.name);
    }

    if (left.name != right.name) return bytewise_less(left.name, right.name);
    const auto left_path = left.full_path.u8string();
    const auto right_path = right.full_path.u8string();
    return bytewise_less(left_path, right_path);
}

static unsigned char fold_ascii(unsigned char value) {
    if (value >= 'A' && value <= 'Z') return static_cast<unsigned char>(value + ('a' - 'A'));
    return value;
}

static bool contains_ascii_insensitive(std::string_view value, std::string_view needle) {
    if (needle.empty()) return true;
    if (needle.size() > value.size()) return false;
    for (size_t start = 0; start <= value.size() - needle.size(); ++start) {
        size_t offset = 0;
        while (offset < needle.size() &&
               fold_ascii(static_cast<unsigned char>(value[start + offset])) ==
               fold_ascii(static_cast<unsigned char>(needle[offset]))) ++offset;
        if (offset == needle.size()) return true;
    }
    return false;
}

static std::string normalized_extension(std::string value) {
    if (!value.empty() && value.front() == '.') value.erase(value.begin());
    for (char& character : value)
        character = static_cast<char>(fold_ascii(static_cast<unsigned char>(character)));
    return value;
}

bool entry_matches_filter(const EntryInfo& entry, const FilterOptions& filter) {
    if (entry.type == "[DIR] ") return true;
    if (entry.type != "[FILE]" && entry.type != "[LINK]") return false;

    if (!contains_ascii_insensitive(entry.name, filter.text)) return false;
    const auto wanted_extension = normalized_extension(filter.extension);
    if (wanted_extension.empty()) return true;
    auto actual_extension = entry.full_path.extension().u8string();
    if (!actual_extension.empty() && actual_extension.front() == '.')
        actual_extension.erase(actual_extension.begin());
    return normalized_extension(std::move(actual_extension)) == wanted_extension;
}

bool filter_is_active(const FilterOptions& filter) {
    return !filter.text.empty() || !normalized_extension(filter.extension).empty();
}

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
    const auto read_modified_time = [&](const fs::path& entry_path)
        -> std::optional<fs::file_time_type> {
        std::error_code time_error = injected_error(options, entry_path, "last_write_time");
        fs::file_time_type modified{};
        if (!time_error) {
            try {
                modified = options.last_write_time_reader
                    ? options.last_write_time_reader(entry_path)
                    : fs::last_write_time(entry_path, time_error);
            } catch (const fs::filesystem_error& ex) {
                time_error = ex.code();
            } catch (...) {
                time_error = std::make_error_code(std::errc::io_error);
            }
        }
        if (time_error) {
            record_issue(result, entry_path, "last_write_time", time_error);
            return std::nullopt;
        }
        return modified;
    };
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
            const auto modified = options.sort.key == SortKey::mtime
                ? read_modified_time(child) : std::optional<fs::file_time_type>{};
            all_entries.push_back({"[LINK]", child.filename().u8string(), child, 0,
                                   depth, false, SizeStatus::unavailable, modified});
        } else if (fs::is_directory(status)) {
            const auto size = measure_directory(child, options, result);
            const bool expanded = expanded_dirs.count(child) > 0;
            const auto modified = options.sort.key == SortKey::mtime
                ? read_modified_time(child) : std::optional<fs::file_time_type>{};
            all_entries.push_back({"[DIR] ", child.filename().u8string(), child,
                                   size.bytes, depth, expanded, size.status, modified});
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
                const auto modified = options.sort.key == SortKey::mtime
                    ? read_modified_time(child) : std::optional<fs::file_time_type>{};
                all_entries.push_back({"[FILE]", child.filename().u8string(), child, 0,
                                       depth, false, SizeStatus::unavailable, modified});
            } else {
                const auto modified = options.sort.key == SortKey::mtime
                    ? read_modified_time(child) : std::optional<fs::file_time_type>{};
                all_entries.push_back({"[FILE]", child.filename().u8string(), child, bytes,
                                       depth, false, SizeStatus::complete, modified});
            }
        }

        error.clear();
        iterator.increment(error);
        if (error) {
            record_issue(result, child, "iterator_increment", error);
            break;
        }
    }

    std::sort(all_entries.begin(), all_entries.end(), [&](const EntryInfo& a, const EntryInfo& b) {
        return entry_less(a, b, options.sort);
    });

    if (filter_is_active(options.filter)) {
        std::vector<EntryInfo> filtered;
        filtered.reserve(all_entries.size());
        for (auto& entry : all_entries) {
            if (entry.type == "[DIR] ") {
                filtered.push_back(std::move(entry));
            } else if (entry_matches_filter(entry, options.filter)) {
                ++result.matching_files;
                filtered.push_back(std::move(entry));
            }
        }
        all_entries = std::move(filtered);
    } else {
        result.matching_files += static_cast<size_t>(std::count_if(
            all_entries.begin(), all_entries.end(), [](const EntryInfo& entry) {
                return entry.type == "[FILE]" || entry.type == "[LINK]";
            }));
    }

    const size_t page_capacity = static_cast<size_t>(max_files);
    size_t total_pages = all_entries.size() / page_capacity
                       + (all_entries.size() % page_capacity != 0);
    if (total_pages == 0) total_pages = 1;

    int page = options.reset_pagination ? 0 : resto_state.resto_page[path];
    if (page < 0 || static_cast<size_t>(page) >= total_pages) {
        page = 0;
        if (!options.reset_pagination) resto_state.resto_page[path] = 0;
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
                        const FileSizeReader& file_size_reader,
                        SortOptions sort) {
    ScanOptions options;
    options.file_size_reader = file_size_reader;
    options.sort = sort;
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
