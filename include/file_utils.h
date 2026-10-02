#pragma once
#include <string>
#include <filesystem>
#include <functional>
#include <optional>
#include <cstddef>
#include <system_error>
#include <vector>
#include <set>

using FileSizeReader = std::function<std::uintmax_t(const std::filesystem::path&)>;
using LastWriteTimeReader = std::function<std::filesystem::file_time_type(const std::filesystem::path&)>;

enum class SortKey { size, name, mtime };
enum class SortOrder { asc, desc };

struct SortOptions {
    SortKey key = SortKey::size;
    SortOrder order = SortOrder::desc;
};

struct FilterOptions {
    std::string text;
    std::string extension;
};

const char* sort_key_name(SortKey key);
const char* sort_order_name(SortOrder order);
bool parse_sort_key(const std::string& value, SortKey& key);
bool parse_sort_order(const std::string& value, SortOrder& order);

enum class ScanStatus { complete, partial, failed };
enum class SizeStatus { complete, partial, unavailable };

struct ScanIssue {
    std::filesystem::path path;
    std::string operation;
    std::error_code error;
};

struct EntryInfo {
    std::string type;
    std::string name;
    std::filesystem::path full_path;
    std::uintmax_t size;
    int depth = 0;           // Nivel de indentación
    bool expanded = false;   // Solo para directorios
    SizeStatus size_status = SizeStatus::complete;
    std::optional<std::filesystem::file_time_type> modified = std::nullopt;
};

struct ScanResult {
    ScanStatus status = ScanStatus::complete;
    std::vector<ScanIssue> diagnostics;
    std::vector<EntryInfo> entries;
    std::size_t matching_files = 0;
};

struct ScanOptions {
    FileSizeReader file_size_reader;
    LastWriteTimeReader last_write_time_reader;
    std::function<std::error_code(const std::filesystem::path&, const std::string&)> error_injector;
    bool reset_pagination = false;
    bool paginate = true;
    SortOptions sort;
    FilterOptions filter;
};

bool entry_matches_filter(const EntryInfo& entry, const FilterOptions& filter);
bool filter_is_active(const FilterOptions& filter);
std::vector<EntryInfo> get_directory_entries(const std::filesystem::path& path = ".", int depth = 0);
std::string human_readable_size(std::uintmax_t bytes);
std::set<std::filesystem::path>& get_expanded_dirs();
void build_tree_entries(const std::filesystem::path& path, 
                        const std::set<std::filesystem::path>& expanded_dirs,
                        std::vector<EntryInfo>& out,
                        int depth = 0,
                        int max_files = 30,
                        const FileSizeReader& file_size_reader = {},
                        SortOptions sort = {});
ScanResult scan_tree_entries(const std::filesystem::path& path,
                             const std::set<std::filesystem::path>& expanded_dirs,
                             int max_files = 30,
                             const ScanOptions& options = {});

void clear_dir_size_cache();
void clear_tree_entries_cache();
void prune_tree_state(std::set<std::filesystem::path>& expanded_dirs);
void expand_resto(const std::filesystem::path& path);
void prev_resto(const std::filesystem::path& path);
void reset_resto_state();
int get_current_page(const std::filesystem::path& path);
