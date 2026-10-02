#pragma once
#include <string>
#include <filesystem>
#include <functional>
#include <map>
#include <system_error>
#include <vector>
#include <set>

using FileSizeReader = std::function<std::uintmax_t(const std::filesystem::path&)>;

enum class ScanStatus { complete, partial, cancelled, failed };
enum class SizeStatus { complete, partial, unavailable };

struct ScanIssue {
    std::filesystem::path path;
    std::string operation;
    std::error_code error;
};

struct ScanProgress {
    std::filesystem::path current_path;
    std::size_t entries_processed = 0;
    std::size_t directories_processed = 0;
    std::uintmax_t bytes_processed = 0;
};

struct CachedDirectorySize {
    std::filesystem::path path;
    std::uintmax_t bytes = 0;
    SizeStatus status = SizeStatus::complete;
};

struct EntryInfo {
    std::string type;
    std::string name;
    std::filesystem::path full_path;
    std::uintmax_t size;
    int depth = 0;           // Nivel de indentación
    bool expanded = false;   // Solo para directorios
    SizeStatus size_status = SizeStatus::complete;
};

struct ScanResult {
    ScanStatus status = ScanStatus::complete;
    std::vector<ScanIssue> diagnostics;
    std::vector<EntryInfo> entries;
    ScanProgress progress;
    std::vector<CachedDirectorySize> pending_cache_updates;
    std::map<std::filesystem::path, int> pagination_pages;
};

struct ScanOptions {
    FileSizeReader file_size_reader;
    std::function<std::error_code(const std::filesystem::path&, const std::string&)> error_injector;
    std::function<bool()> is_cancelled;
    std::function<void(const ScanProgress&)> progress_callback;
    bool reset_pagination = false;
    bool defer_cache_updates = false;
    bool isolated_pagination = false;
    std::map<std::filesystem::path, int> pagination_pages;
};

std::vector<EntryInfo> get_directory_entries(const std::filesystem::path& path = ".", int depth = 0);
std::string human_readable_size(std::uintmax_t bytes);
std::set<std::filesystem::path>& get_expanded_dirs();
void build_tree_entries(const std::filesystem::path& path, 
                        const std::set<std::filesystem::path>& expanded_dirs,
                        std::vector<EntryInfo>& out,
                        int depth = 0,
                        int max_files = 30,
                        const FileSizeReader& file_size_reader = {});
ScanResult scan_tree_entries(const std::filesystem::path& path,
                             const std::set<std::filesystem::path>& expanded_dirs,
                             int max_files = 30,
                             const ScanOptions& options = {});

void clear_dir_size_cache();
void commit_scan_cache(const ScanResult& result);
void clear_tree_entries_cache();
void prune_tree_state(std::set<std::filesystem::path>& expanded_dirs);
void expand_resto(const std::filesystem::path& path);
void prev_resto(const std::filesystem::path& path);
void reset_resto_state();
int get_current_page(const std::filesystem::path& path);
std::map<std::filesystem::path, int> snapshot_resto_state();
void restore_resto_state(const std::map<std::filesystem::path, int>& pages);
