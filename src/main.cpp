#include <clocale>
#include <curses.h>
#include <ui_utils.h>
#include <file_utils.h>
#include "platform_utils.h"
#include "localization.h"
#include "settings.h"
#include "version.h"
#include "export_utils.h"
#include <filesystem>
#include <vector>
#include <string>
#include <cstdlib>
#include <set>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>
#include <optional>
#include <charconv>
#include <array>
#include <algorithm>

static short curses_color(int index) {
    // PDCurses and ncurses assign different numeric values to the same colors.
    static constexpr std::array<short, 8> colors{
        COLOR_BLACK, COLOR_RED, COLOR_GREEN, COLOR_YELLOW,
        COLOR_BLUE, COLOR_MAGENTA, COLOR_CYAN, COLOR_WHITE
    };
    return colors.at(static_cast<size_t>(index));
}

static const char* scan_status_name(ScanStatus status) {
    switch (status) {
        case ScanStatus::complete: return "complete";
        case ScanStatus::partial: return "partial";
        case ScanStatus::failed: return "failed";
    }
    return "failed";
}

static const char* size_status_name(SizeStatus status) {
    switch (status) {
        case SizeStatus::complete: return "complete";
        case SizeStatus::partial: return "partial";
        case SizeStatus::unavailable: return "unavailable";
    }
    return "unavailable";
}

static void headless_dump_frame(const std::vector<EntryInfo>& entries, int selected,
                                int scroll_offset, int visible_rows,
                                const std::filesystem::path& current_path,
                                const std::set<std::filesystem::path>& expanded_dirs,
                                double last_scan_ms,
                                int bar_fg, int bar_bg, int frame_num,
                                int total_pages, int current_page, int page_size,
                                const ScanResult& scan_result, SortOptions sort,
                                const FilterOptions& filter) {
    std::cout << "=== FRAME " << frame_num << " ===" << std::endl;
    std::cout << "current_path: " << current_path.u8string() << std::endl;
    std::cout << "selected_index: " << selected << std::endl;
    std::cout << "scroll_offset: " << scroll_offset << std::endl;
    std::cout << "visible_rows: " << visible_rows << std::endl;
    std::cout << "page_size: " << page_size << std::endl;
    std::cout << "sort_key: " << sort_key_name(sort.key) << std::endl;
    std::cout << "sort_order: " << sort_order_name(sort.order) << std::endl;
    std::cout << "filter_text: " << filter.text << std::endl;
    std::cout << "filter_extension: " << filter.extension << std::endl;
    std::cout << "matching_files: " << scan_result.matching_files << std::endl;
    std::cout << "language: " << language_code() << std::endl;
    std::cout << "total_entries: " << entries.size() << std::endl;
    if (total_pages > 1) {
        std::cout << "pagination: page " << (current_page + 1) << " of " << total_pages << std::endl;
    }

    std::cout << "expanded_dirs: {";
    bool first_dir = true;
    for (const auto& d : expanded_dirs) {
        if (!first_dir) std::cout << ", ";
        std::cout << d.u8string();
        first_dir = false;
    }
    std::cout << "}" << std::endl;

    std::cout << "last_scan_ms: " << last_scan_ms << std::endl;
    std::cout << "scan_status: " << scan_status_name(scan_result.status) << std::endl;
    std::cout << "diagnostics_count: " << scan_result.diagnostics.size() << std::endl;
    for (size_t i = 0; i < scan_result.diagnostics.size(); ++i) {
        const auto& issue = scan_result.diagnostics[i];
        std::cout << "diagnostic_" << i << ": path=" << issue.path.u8string()
                  << " operation=" << issue.operation << " error_code=" << issue.error.value()
                  << " message=" << issue.error.message() << std::endl;
    }
    std::cout << "bar_fg: " << bar_fg << std::endl;
    std::cout << "bar_bg: " << bar_bg << std::endl;

    std::vector<std::uintmax_t> parent_sizes(entries.size(), 0);
    for (size_t i = 0; i < entries.size(); ++i) {
        int my_depth = entries[i].depth;
        if (my_depth == 0) {
            for (const auto& e : entries) {
                if (e.depth == 0) parent_sizes[i] += e.size;
            }
        } else {
            for (int j = (int)i - 1; j >= 0; --j) {
                if (entries[j].depth == my_depth - 1) {
                    for (size_t k = j + 1; k < entries.size() && entries[k].depth >= my_depth; ++k) {
                        if (entries[k].depth == my_depth)
                            parent_sizes[i] += entries[k].size;
                        if (entries[k].depth < my_depth) break;
                    }
                    break;
                }
            }
        }
        if (parent_sizes[i] == 0) parent_sizes[i] = 1;
    }

    std::cout << "entries:" << std::endl;
    const int BAR_WIDTH = 40;
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        bool is_nav = (e.type == "[RESTO_NEXT]" || e.type == "[RESTO_PREV]");
        std::string indent(e.depth * 2, ' ');
        std::string marker = ((int)i == selected) ? ">>>" : "   ";

        if (is_nav) {
            std::cout << "  " << i << ": " << marker << " " << indent
                      << "--- " << e.name << " ---" << std::endl;
        } else {
            double percent = std::min(1.0, (double)e.size / parent_sizes[i]);
            int filled = (int)(BAR_WIDTH * percent);
            int unfilled = BAR_WIDTH - filled;

            std::string size_str = human_readable_size(e.size);
            char pct_buf[16];
            snprintf(pct_buf, sizeof(pct_buf), "%3d%%", (int)(percent * 100));

            std::string bar;
            for (int b = 0; b < filled; ++b) bar += "\u2588";
            for (int b = 0; b < unfilled; ++b) bar += "\u2591";

            std::cout << "  " << i << ": " << marker << " " << indent
                      << e.type << " " << e.name
                      << "  " << size_str << "  (" << pct_buf << ") " << bar
                      << "  size_status=" << size_status_name(e.size_status) << std::endl;
        }
    }
    std::cout << "=== END FRAME ===" << std::endl;
}

static int parse_headless_event(const std::string& line) {
    if (line == "UP")          return KEY_UP;
    if (line == "DOWN")        return KEY_DOWN;
    if (line == "LEFT")        return KEY_LEFT;
    if (line == "RIGHT")       return KEY_RIGHT;
    if (line == "SPACE")       return ' ';
    if (line == "DELETE")      return KEY_DC;
    if (line == "CTRL_H")      return 8;
    if (line == "ENTER")       return '\n';
    if (line == "REFRESH")     return 0x10001;
    if (line.rfind("CD ", 0) == 0) return 0x10002;
    if (line.rfind("SORT ", 0) == 0) return 0x10003;
    if (line.rfind("FILTER ", 0) == 0) return 0x10004;
    if (line == "FILTER") return 0x10004;
    if (line.rfind("EXT ", 0) == 0) return 0x10005;
    if (line == "EXT") return 0x10005;
    if (line == "CLEAR_FILTER") return 0x10006;
    if (line == "BACKSPACE")   return KEY_BACKSPACE;
    if (line.rfind("COLOR ", 0) == 0) return 0x10000;
    if (line.size() == 1)      return line[0];
    return -1;
}

static void clamp_and_skip_selection(const std::vector<EntryInfo>& entries, int& selected,
                                      std::filesystem::path& select_first_owner,
                                      std::filesystem::path& select_last_owner) {
    int n = (int)entries.size();
    if (!select_first_owner.empty()) {
        selected = 0;
        for (int i = 0; i < n; ++i) {
            if (entries[i].type == "[RESTO_PREV]" && entries[i].full_path == select_first_owner) {
                selected = i + 1;
                break;
            }
        }
        select_first_owner.clear();
    }
    if (!select_last_owner.empty()) {
        selected = n - 1;
        for (int i = n - 1; i >= 0; --i) {
            if (entries[i].type == "[RESTO_NEXT]" && entries[i].full_path == select_last_owner) {
                selected = i - 1;
                break;
            }
        }
        select_last_owner.clear();
    }
    if (n == 0) selected = 0;
    else if (selected >= n) selected = n - 1;
}

static std::filesystem::path find_pagination_owner(const std::vector<EntryInfo>& entries,
                                                     int selected,
                                                     const std::filesystem::path& root) {
    if (entries.empty() || selected < 0 || selected >= (int)entries.size())
        return root;

    const auto& e = entries[selected];

    if (e.type == "[RESTO_NEXT]" || e.type == "[RESTO_PREV]")
        return e.full_path;
    if (e.type == "[DIR] ")
        return e.full_path;

    int sel_depth = e.depth;
    for (int i = selected - 1; i >= 0; --i) {
        if (entries[i].depth < sel_depth)
            return entries[i].full_path;
    }

    return root;
}

static void update_scroll(int selected, int& scroll_offset, int visible_rows) {
    if (selected < scroll_offset) {
        scroll_offset = selected;
    } else if (selected >= scroll_offset + visible_rows) {
        scroll_offset = selected - visible_rows + 1;
    }
}

static void reset_listing_view(int& selected, int& scroll_offset, bool& need_refresh) {
    reset_resto_state();
    selected = 0;
    scroll_offset = 0;
    need_refresh = true;
}

static bool collapse_selected_directory(const std::vector<EntryInfo>& entries, int& selected,
                                         std::set<std::filesystem::path>& expanded_dirs) {
    if (entries.empty()) return false;
    const auto& entry = entries[selected];
    if (entry.type == "[DIR] " && expanded_dirs.erase(entry.full_path)) return true;
    for (int i = selected - 1; i >= 0; --i) {
        if (entries[i].depth < entry.depth && entries[i].type == "[DIR] ") {
            selected = i;
            return expanded_dirs.erase(entries[i].full_path) > 0;
        }
    }
    return false;
}

static ScanResult rebuild_tree_preserving_selection(
    const std::filesystem::path& current_path,
    std::set<std::filesystem::path>& expanded_dirs,
    int page_size,
    std::vector<EntryInfo>& entries,
    int& selected,
    bool invalidate_sizes,
    SortOptions sort,
    const FilterOptions& filter) {
    std::filesystem::path selected_path;
    if (invalidate_sizes && selected >= 0 && selected < static_cast<int>(entries.size()) &&
        entries[selected].type != "[RESTO_NEXT]" && entries[selected].type != "[RESTO_PREV]")
        selected_path = entries[selected].full_path;

    if (invalidate_sizes) {
        clear_dir_size_cache();
        prune_tree_state(expanded_dirs);
    }
    ScanOptions options;
    options.sort = sort;
    options.filter = filter;
    auto result = scan_tree_entries(current_path, expanded_dirs, page_size, options);
    entries = result.entries;
    if (invalidate_sizes && !selected_path.empty()) {
        auto restored = std::find_if(entries.begin(), entries.end(), [&](const EntryInfo& entry) {
            return entry.full_path == selected_path &&
                   entry.type != "[RESTO_NEXT]" && entry.type != "[RESTO_PREV]";
        });
        if (restored != entries.end()) selected = static_cast<int>(restored - entries.begin());
    }
    if (invalidate_sizes) {
        int nearest = -1;
        int nearest_distance = static_cast<int>(entries.size()) + 1;
        for (int index = 0; index < static_cast<int>(entries.size()); ++index) {
            if (entries[index].type == "[RESTO_NEXT]" || entries[index].type == "[RESTO_PREV]") continue;
            const int distance = index > selected ? index - selected : selected - index;
            if (distance < nearest_distance) {
                nearest = index;
                nearest_distance = distance;
            }
        }
        selected = nearest >= 0 ? nearest : 0;
    }
    return result;
}

static bool navigate_to_root(const std::filesystem::path& requested_path,
                             std::filesystem::path& current_path,
                             std::set<std::filesystem::path>& expanded_dirs,
                             int page_size, std::vector<EntryInfo>& entries,
                             ScanResult& scan_result, int& selected, int& scroll_offset,
                             int visible_rows, double& last_scan_ms,
                             const std::filesystem::path& select_on_return,
                             std::string& error_message,
                             SortOptions sort, const FilterOptions& filter) {
    namespace fs = std::filesystem;
    fs::path candidate = requested_path.is_absolute()
        ? requested_path : current_path / requested_path;
    std::error_code error;
    candidate = fs::canonical(candidate, error);
    if (error) {
        error_message = error.message();
        return false;
    }
    if (!fs::is_directory(candidate, error) || error) {
        error_message = error ? error.message() : std::make_error_code(std::errc::not_a_directory).message();
        return false;
    }

    clear_dir_size_cache();
    ScanOptions options;
    options.reset_pagination = true;
    options.sort = sort;
    options.filter = filter;
    const auto started = std::chrono::steady_clock::now();
    auto next_scan = scan_tree_entries(candidate, {}, page_size, options);
    last_scan_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    if (next_scan.status == ScanStatus::failed) {
        error_message = next_scan.diagnostics.empty()
            ? std::make_error_code(std::errc::io_error).message()
            : next_scan.diagnostics.front().error.message();
        return false;
    }

    current_path = candidate;
    expanded_dirs.clear();
    reset_resto_state();
    entries = next_scan.entries;
    scan_result = std::move(next_scan);
    selected = 0;
    if (!select_on_return.empty()) {
        auto returned_directory = std::find_if(entries.begin(), entries.end(), [&](const EntryInfo& entry) {
            return entry.full_path == select_on_return && entry.type == "[DIR] ";
        });
        if (returned_directory != entries.end())
            selected = static_cast<int>(returned_directory - entries.begin());
    }
    if (entries.empty()) selected = 0;
    scroll_offset = 0;
    update_scroll(selected, scroll_offset, visible_rows);
    return true;
}

static bool navigate_to_parent(std::filesystem::path& current_path,
                               std::set<std::filesystem::path>& expanded_dirs,
                               int page_size, std::vector<EntryInfo>& entries,
                               ScanResult& scan_result, int& selected, int& scroll_offset,
                               int visible_rows, double& last_scan_ms,
                               std::string& error_message, SortOptions sort,
                               const FilterOptions& filter) {
    namespace fs = std::filesystem;
    std::error_code error;
    const fs::path absolute_path = fs::absolute(current_path, error).lexically_normal();
    if (error) { error_message = error.message(); return false; }
    if (absolute_path == absolute_path.root_path()) return false;
    const fs::path parent = absolute_path.parent_path();
    if (parent.empty() || parent == absolute_path) return false;
    return navigate_to_root(parent, current_path, expanded_dirs, page_size, entries,
        scan_result, selected, scroll_offset, visible_rows, last_scan_ms,
        absolute_path, error_message, sort, filter);
}

static void print_export_diagnostics(const std::vector<ScanIssue>& diagnostics) {
    for (const auto& issue : diagnostics) {
        std::cerr << issue.path.u8string() << ": " << issue.operation << ": "
                  << issue.error.message() << " (error " << issue.error.value() << ")\n";
    }
}

static int run_export(const std::filesystem::path& requested_root, const std::string& format,
                      const std::optional<std::filesystem::path>& output_path,
                      int page_size, const SortOptions& sort, const FilterOptions& filter) {
    namespace fs = std::filesystem;
    ExportDocument document;
    document.sort = sort;
    document.filter = filter;
    ScanStatus scan_status = ScanStatus::failed;

    std::error_code error;
    const fs::path root = fs::canonical(requested_root, error);
    if (error) {
        std::error_code absolute_error;
        document.root = fs::absolute(requested_root, absolute_error).lexically_normal();
        if (absolute_error) document.root = requested_root;
        document.complete = false;
        document.diagnostics.push_back({document.root, "root", error});
    } else {
        document.root = root;
        ScanOptions options;
        options.paginate = false;
        options.sort = sort;
        options.filter = filter;
        const auto result = scan_tree_entries(root, {}, page_size, options);
        scan_status = result.status;
        document.complete = result.status == ScanStatus::complete;
        document.entries = result.entries;
        document.diagnostics = result.diagnostics;
    }

    print_export_diagnostics(document.diagnostics);
    if (scan_status == ScanStatus::failed) {
        if (format == "json" && !output_path) std::cout << serialize_export_json(document);
        return 1;
    }

    const std::string contents = format == "json"
        ? serialize_export_json(document) : serialize_export_csv(document);
    if (output_path) {
        std::string write_error;
        if (!write_export_file_atomic(contents, *output_path, write_error)) {
            std::cerr << text(Text::ExportWriteError) << write_error << "\n";
            return 1;
        }
    } else {
        std::cout.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        std::cout.flush();
        if (!std::cout) {
            std::cerr << text(Text::ExportWriteError) << "stdout\n";
            return 1;
        }
    }
    return export_exit_code(scan_status);
}

int main(int argc, char* argv[]) {
    ConsoleEncoding console_encoding;
    bool headless = false;
    bool show_usage = false;
    bool show_version = false;
    bool save_preferences_requested = false;
    const auto config_path = configuration_file();
    std::string config_error;
    Preferences preferences = load_preferences(config_path, config_error);
    set_language(preferences.language);
    int page_size = preferences.page_size;
    SortOptions sort_options;
    FilterOptions active_filter;
    std::filesystem::path start_path = ".";
    bool export_requested = false;
    std::string export_format;
    std::optional<std::string> export_output_argument;
    size_t positional_argument_count = 0;

    const auto arguments = command_line_arguments(argc, argv);
    for (size_t i = 0; i < arguments.size(); ++i) {
        const auto& arg = arguments[i];
        if (arg == "--headless") {
            headless = true;
        } else if (arg == "--help" || arg == "-h") {
            show_usage = true;
        } else if (arg == "--version") {
            show_version = true;
        } else if (arg == "--save-settings") {
            save_preferences_requested = true;
        } else if (arg == "--lang" || arg.rfind("--lang=", 0) == 0) {
            std::string value;
            if (arg == "--lang") {
                if (i + 1 < arguments.size()) value = arguments[++i];
            } else {
                value = arg.substr(7);
            }
            if (!set_language(value)) {
                std::cerr << text(Text::LanguageError) << "\n";
                return 2;
            }
            preferences.language = value;
        } else if (arg == "--page-size" || arg.rfind("--page-size=", 0) == 0) {
            std::string value;
            if (arg == "--page-size") {
                if (i + 1 < arguments.size()) value = arguments[++i];
            } else {
                value = arg.substr(12);
            }
            int parsed = 0;
            const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
            if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || parsed <= 0) {
                std::cerr << text(Text::PageSizeError) << "\n";
                return 2;
            }
            page_size = parsed;
            preferences.page_size = parsed;
        } else if (arg == "--export" || arg.rfind("--export=", 0) == 0) {
            if (export_requested) {
                std::cerr << text(Text::ExportOptionError) << "\n";
                return 2;
            }
            std::string value;
            if (arg == "--export") {
                if (i + 1 >= arguments.size() || arguments[i + 1].rfind("--", 0) == 0) {
                    std::cerr << text(Text::ExportFormatError) << "\n";
                    return 2;
                }
                value = arguments[++i];
            } else {
                value = arg.substr(9);
            }
            if (value != "json" && value != "csv") {
                std::cerr << text(Text::ExportFormatError) << "\n";
                return 2;
            }
            export_requested = true;
            export_format = value;
        } else if (arg == "--output" || arg.rfind("--output=", 0) == 0) {
            if (export_output_argument) {
                std::cerr << text(Text::ExportOptionError) << "\n";
                return 2;
            }
            std::string value;
            if (arg == "--output") {
                if (i + 1 >= arguments.size() || arguments[i + 1].rfind("--", 0) == 0) {
                    std::cerr << text(Text::ExportOptionError) << "\n";
                    return 2;
                }
                value = arguments[++i];
            } else {
                value = arg.substr(9);
            }
            if (value.empty()) {
                std::cerr << text(Text::ExportOptionError) << "\n";
                return 2;
            }
            export_output_argument = value;
        } else if (arg == "--sort" || arg.rfind("--sort=", 0) == 0) {
            std::string value;
            if (arg == "--sort") {
                if (i + 1 < arguments.size()) value = arguments[++i];
            } else {
                value = arg.substr(7);
            }
            if (!parse_sort_key(value, sort_options.key)) {
                std::cerr << text(Text::SortKeyError) << "\n";
                return 2;
            }
        } else if (arg == "--order" || arg.rfind("--order=", 0) == 0) {
            std::string value;
            if (arg == "--order") {
                if (i + 1 < arguments.size()) value = arguments[++i];
            } else {
                value = arg.substr(8);
            }
            if (!parse_sort_order(value, sort_options.order)) {
                std::cerr << text(Text::SortOrderError) << "\n";
                return 2;
            }
        } else if (arg == "--filter" || arg.rfind("--filter=", 0) == 0) {
            if (arg == "--filter") {
                if (i + 1 >= arguments.size() || arguments[i + 1].rfind("--", 0) == 0) {
                    std::cerr << text(Text::FilterTextError) << "\n";
                    return 2;
                }
                active_filter.text = arguments[++i];
            } else {
                active_filter.text = arg.substr(9);
            }
        } else if (arg == "--ext" || arg.rfind("--ext=", 0) == 0) {
            if (arg == "--ext") {
                if (i + 1 >= arguments.size() || arguments[i + 1].rfind("--", 0) == 0) {
                    std::cerr << text(Text::FilterExtensionError) << "\n";
                    return 2;
                }
                active_filter.extension = arguments[++i];
            } else {
                active_filter.extension = arg.substr(6);
            }
        } else if (!arg.empty() && arg[0] != '-') {
            ++positional_argument_count;
            start_path = std::filesystem::u8path(arg);
        } else {
            std::cerr << text(Text::UnknownOption) << arg << ". " << text(Text::UsageHint) << "\n";
            return 2;
        }
    }
    if (export_output_argument && !export_requested) {
        std::cerr << text(Text::ExportOptionError) << "\n";
        return 2;
    }
    if (export_requested) {
        if (headless || save_preferences_requested) {
            std::cerr << text(Text::ExportModeError) << "\n";
            return 2;
        }
        if (positional_argument_count != 1) {
            std::cerr << text(Text::ExportDirectoryError) << "\n";
            return 2;
        }
        if (export_format == "csv" && !export_output_argument) {
            std::cerr << text(Text::ExportOutputRequired) << "\n";
            return 2;
        }
    }
    if (show_usage) {
        std::cout << text(Text::Usage);
        return 0;
    }
    if (show_version) {
        std::cout << "TreeFiles " << treefiles_version << "\n";
        return 0;
    }

    if (export_requested) {
        const std::optional<std::filesystem::path> output_path = export_output_argument
            ? std::optional<std::filesystem::path>(std::filesystem::u8path(*export_output_argument))
            : std::nullopt;
        return run_export(start_path, export_format, output_path, page_size,
                          sort_options, active_filter);
    }

    std::error_code path_error;
    if (!std::filesystem::is_directory(start_path, path_error)) {
        std::cerr << text(Text::UnreadableDirectory) << start_path.u8string() << std::endl;
        return 1;
    }
    if (!path_error && (is_directory_link(start_path) ||
        std::filesystem::is_symlink(std::filesystem::symlink_status(start_path, path_error)))) {
        const auto requested_path = start_path;
        start_path = std::filesystem::canonical(requested_path, path_error);
        if (path_error) {
            std::cerr << text(Text::UnreadableDirectory) << requested_path.u8string() << ": "
                      << path_error.message() << std::endl;
            return 1;
        }
    }

    preferences.language = language_code();
    preferences.page_size = page_size;
    if (!config_error.empty()) std::cerr << text(Text::ConfigLoadWarning) << config_error << "\n";
    if (save_preferences_requested && !save_preferences(config_path, preferences, config_error)) {
        std::cerr << text(Text::ConfigSaveWarning) << config_error << "\n";
        return 1;
    }
    auto colors = preferences;

    // ==================== HEADLESS MODE ====================
    if (headless) {
        bool running = true;
        int selected = 0;
        int scroll_offset = 0;
        int visible_rows = 30;

        int bar_bg = colors.background;
        int bar_fg = colors.foreground;

        std::filesystem::path current_path = start_path;
        auto& expanded_dirs = get_expanded_dirs();
        double last_scan_ms = 0.0;
        ScanResult scan_result;

        std::vector<EntryInfo> entries;
        bool need_refresh = true;
        bool refresh_next_scan = false;
        std::filesystem::path select_first_owner;
        std::filesystem::path select_last_owner;
        int frame_num = 0;

        auto rebuild_tree = [&](bool invalidate_sizes = false) {
            auto t0 = std::chrono::high_resolution_clock::now();
            scan_result = rebuild_tree_preserving_selection(current_path, expanded_dirs,
                page_size, entries, selected, invalidate_sizes, sort_options, active_filter);
            auto t1 = std::chrono::high_resolution_clock::now();
            last_scan_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            need_refresh = false;
        };

        auto get_page_info = [&](int& total_pages, int& current_page) {
            current_page = ::get_current_page(current_path);
            total_pages = 1;
            for (const auto& e : entries) {
                if ((e.type == "[RESTO_NEXT]" || e.type == "[RESTO_PREV]") && e.full_path == current_path) {
                    std::string label = e.name;
                    auto paren = label.rfind('(');
                    auto slash = label.rfind('/');
                    if (paren != std::string::npos && slash != std::string::npos && slash > paren) {
                        total_pages = std::stoi(label.substr(slash + 1));
                    }
                    break;
                }
            }
        };

        rebuild_tree();
        if (scan_result.status == ScanStatus::failed) {
            std::cerr << text(Text::UnreadableDirectory) << current_path.u8string() << std::endl;
            for (const auto& issue : scan_result.diagnostics)
                std::cerr << issue.path.u8string() << ": " << issue.error.message() << std::endl;
            return 1;
        }
        int total_pages = 1, current_page = 0;
        get_page_info(total_pages, current_page);
        headless_dump_frame(entries, selected, scroll_offset, visible_rows,
                            current_path, expanded_dirs, last_scan_ms,
                            bar_fg, bar_bg, frame_num, total_pages, current_page, page_size,
                            scan_result, sort_options, active_filter);
        frame_num++;

        std::string event_line;
        while (running && std::getline(std::cin, event_line)) {
            if (event_line.empty() || event_line[0] == '#')
                continue;

            int input = parse_headless_event(event_line);
            if (input == -1)
                continue;

            if (need_refresh)
                rebuild_tree(refresh_next_scan);
            refresh_next_scan = false;
            if (scan_result.status == ScanStatus::failed) {
                std::cerr << text(Text::UnreadableDirectory) << current_path.u8string() << std::endl;
                for (const auto& issue : scan_result.diagnostics)
                    std::cerr << issue.path.u8string() << ": " << issue.error.message() << std::endl;
                return 1;
            }

            bool popup_handled = false;

            switch (input) {
            case 'q':
            case 'Q':
                running = false;
                break;
            case 'r':
            case 'R':
            case 0x10001:
                need_refresh = true;
                refresh_next_scan = true;
                break;
            case '\n':
                if (!entries.empty() && selected >= 0 && selected < static_cast<int>(entries.size())) {
                    const auto entry = entries[selected];
                    if (entry.type == "[DIR] ") {
                        std::string error;
                        if (!navigate_to_root(entry.full_path, current_path, expanded_dirs, page_size,
                            entries, scan_result, selected, scroll_offset, visible_rows, last_scan_ms,
                            {}, error, sort_options, active_filter)) {
                            std::cout << "=== POPUP navigation_error ===\nmessage: "
                                      << text(Text::NavigationError) << error
                                      << "\n=== END POPUP ===\n";
                            popup_handled = true;
                        }
                    } else if (entry.type == "[RESTO_NEXT]") {
                        expand_resto(entry.full_path);
                        need_refresh = true;
                        select_first_owner = entry.full_path;
                    } else if (entry.type == "[RESTO_PREV]") {
                        prev_resto(entry.full_path);
                        need_refresh = true;
                        select_last_owner = entry.full_path;
                    }
                }
                break;
            case KEY_BACKSPACE:
            case 8:
            case 127: {
                std::string error;
                if (!navigate_to_parent(current_path, expanded_dirs, page_size, entries,
                    scan_result, selected, scroll_offset, visible_rows, last_scan_ms, error, sort_options, active_filter) &&
                    !error.empty()) {
                    std::cout << "=== POPUP navigation_error ===\nmessage: "
                              << text(Text::NavigationError) << error
                              << "\n=== END POPUP ===\n";
                    popup_handled = true;
                }
                break;
            }
            case 0x10002: {
                const std::string value = event_line.size() > 3 ? event_line.substr(3) : std::string();
                std::string error;
                if (value.empty() || !navigate_to_root(std::filesystem::u8path(value), current_path,
                    expanded_dirs, page_size, entries, scan_result, selected, scroll_offset,
                    visible_rows, last_scan_ms, {}, error, sort_options, active_filter)) {
                    if (error.empty()) error = std::make_error_code(std::errc::invalid_argument).message();
                    std::cout << "=== POPUP navigation_error ===\nmessage: "
                              << text(Text::NavigationError) << error
                              << "\n=== END POPUP ===\n";
                    popup_handled = true;
                }
                break;
            }
            case 0x10003: {
                std::istringstream event(event_line);
                std::string command, key_value, order_value, extra;
                event >> command >> key_value >> order_value;
                SortOptions requested = sort_options;
                if (!(event >> extra) && parse_sort_key(key_value, requested.key) &&
                    parse_sort_order(order_value, requested.order)) {
                    sort_options = requested;
                    reset_listing_view(selected, scroll_offset, need_refresh);
                } else {
                    std::cout << "=== POPUP error ===\nmessage: " << text(Text::InvalidSortEvent)
                              << "\n=== END POPUP ===\n";
                    popup_handled = true;
                }
                break;
            }
            case 's':
            case 'S':
                sort_options.key = sort_options.key == SortKey::size ? SortKey::name
                    : sort_options.key == SortKey::name ? SortKey::mtime : SortKey::size;
                reset_listing_view(selected, scroll_offset, need_refresh);
                break;
            case 't':
            case 'T':
                sort_options.order = sort_options.order == SortOrder::asc
                    ? SortOrder::desc : SortOrder::asc;
                reset_listing_view(selected, scroll_offset, need_refresh);
                break;
            case 0x10004:
                if (event_line == "FILTER") {
                    std::cout << "=== POPUP error ===\nmessage: " << text(Text::InvalidFilterEvent)
                              << "\n=== END POPUP ===\n";
                    popup_handled = true;
                } else {
                    active_filter.text = event_line.substr(7);
                    reset_listing_view(selected, scroll_offset, need_refresh);
                }
                break;
            case 0x10005:
                if (event_line == "EXT") {
                    std::cout << "=== POPUP error ===\nmessage: " << text(Text::InvalidFilterEvent)
                              << "\n=== END POPUP ===\n";
                    popup_handled = true;
                } else {
                    active_filter.extension = event_line.substr(4);
                    reset_listing_view(selected, scroll_offset, need_refresh);
                }
                break;
            case 0x10006:
                active_filter = {};
                reset_listing_view(selected, scroll_offset, need_refresh);
                break;
            case 'f':
            case 'F':
                active_filter = {};
                reset_listing_view(selected, scroll_offset, need_refresh);
                break;
            case KEY_UP:
            case 'k':
                if (selected > 0) selected--;
                break;
            case KEY_DOWN:
            case 'j': {
                int n = (int)entries.size();
                if (selected < n - 1) selected++;
                break;
            }
            case ' ':
                if (!entries.empty()) {
                    std::cout << "=== ACTION open ===" << std::endl;
                    std::cout << "file: " << entries[selected].full_path.u8string() << std::endl;
                    std::cout << "=== END ACTION ===" << std::endl;
                }
                break;
            case 'e':
            case 'E':
            case 'l':
            case KEY_RIGHT:
                if (!entries.empty()) {
                    const auto& entry = entries[selected];
                    if (entry.type == "[DIR] ") {
                        auto dir_path = entry.full_path;
                        if (expanded_dirs.count(dir_path) && (input == 'e' || input == 'E')) {
                            expanded_dirs.erase(dir_path);
                        } else {
                            expanded_dirs.insert(dir_path);
                        }
                        need_refresh = true;
                    } else if (entry.type == "[RESTO_NEXT]") {
                        expand_resto(entry.full_path);
                        need_refresh = true;
                        select_first_owner = entry.full_path;
                    } else if (entry.type == "[RESTO_PREV]" && (input == 'e' || input == 'E')) {
                        prev_resto(entry.full_path);
                        need_refresh = true;
                        select_last_owner = entry.full_path;
                    }
                }
                break;
            case 'h':
            case KEY_LEFT:
                need_refresh = collapse_selected_directory(entries, selected, expanded_dirs);
                break;
            case 'g':
                selected = 0;
                break;
            case 'G':
                if (!entries.empty()) selected = static_cast<int>(entries.size()) - 1;
                break;
            case 'n':
            case 'N':
                if (!entries.empty()) {
                    auto owner = find_pagination_owner(entries, selected, current_path);
                    bool has_next = false;
                    for (const auto& e : entries) {
                        if (e.type == "[RESTO_NEXT]" && e.full_path == owner) {
                            has_next = true;
                            break;
                        }
                    }
                    if (has_next) {
                        expand_resto(owner);
                        need_refresh = true;
                        select_first_owner = owner;
                    }
                }
                break;
            case 'p':
            case 'P':
                if (!entries.empty()) {
                    auto owner = find_pagination_owner(entries, selected, current_path);
                    if (get_current_page(owner) > 0) {
                        prev_resto(owner);
                        need_refresh = true;
                        select_last_owner = owner;
                    }
                }
                break;
            case KEY_DC:
                if (!entries.empty()) {
                    const auto& entry = entries[selected];
                    if (entry.type == "[RESTO_PREV]" || entry.type == "[RESTO_NEXT]")
                        break;
                    std::cout << "=== POPUP confirm_delete ===" << std::endl;
                    std::cout << "message: " << text(Text::DeletePrompt) << " \"" << entry.name << "\"?" << std::endl;
                    std::cout << "=== END POPUP ===" << std::endl;

                    std::string response;
                    std::getline(std::cin, response);

                    if (response == "y" || response == "Y" || response == "ENTER") {
                        try {
                            if (entry.type == "[DIR] ") {
                                std::filesystem::remove_all(entry.full_path);
                            } else {
                                std::filesystem::remove(entry.full_path);
                            }
                            std::cout << "=== ACTION deleted ===" << std::endl;
                            std::cout << "path: " << entry.full_path.u8string() << std::endl;
                            std::cout << "=== END ACTION ===" << std::endl;
                        } catch (const std::exception& ex) {
                            std::cout << "=== POPUP error ===" << std::endl;
                            std::cout << "message: " << ex.what() << std::endl;
                            std::cout << "=== END POPUP ===" << std::endl;
                        }
                        need_refresh = true;
                        refresh_next_scan = true;
                    } else {
                        std::cout << "=== ACTION cancel_delete ===" << std::endl;
                        std::cout << "=== END ACTION ===" << std::endl;
                    }
                    popup_handled = true;
                }
                break;
            case 'b':
            case 'B':
                std::cout << "=== POPUP bar_color ===" << std::endl;
                std::cout << "colors: black, red, green, yellow, blue, magenta, cyan, white"
                          << std::endl;
                std::cout << "=== END POPUP ===" << std::endl;
                popup_handled = true;
                break;
            case 'w':
            case 'W':
                std::cout << "=== POPUP scan_diagnostics ===" << std::endl;
                std::cout << "scan_status: " << scan_status_name(scan_result.status) << std::endl;
                for (const auto& issue : scan_result.diagnostics)
                    std::cout << "path: " << issue.path.u8string() << " operation: "
                              << issue.operation << " error_code: " << issue.error.value()
                              << " message: " << issue.error.message() << std::endl;
                std::cout << "=== END POPUP ===" << std::endl;
                popup_handled = true;
                break;
            case 0x10000: {
                std::istringstream event(event_line);
                std::string command, foreground, background, extra;
                event >> command >> foreground >> background;
                const int fg = color_index(foreground), bg = color_index(background);
                if (fg < 0 || bg < 0 || (event >> extra)) {
                    std::cout << "=== POPUP error ===\nmessage: " << text(Text::InvalidColorEvent) << "\n=== END POPUP ===\n";
                    break;
                }
                colors = {fg, bg};
                bar_fg = fg;
                bar_bg = bg;
                if (!save_color_settings(config_path, colors, config_error)) {
                    std::cout << "=== POPUP error ===\nmessage: " << text(Text::ConfigSaveWarning)
                              << config_error << "\n=== END POPUP ===\n";
                } else {
                    std::cout << "=== ACTION colors_saved ===\n=== END ACTION ===\n";
                }
                break;
            }
            }

            if (!popup_handled || need_refresh) {
                if (need_refresh)
                    rebuild_tree(refresh_next_scan);
                refresh_next_scan = false;
                clamp_and_skip_selection(entries, selected, select_first_owner, select_last_owner);
                update_scroll(selected, scroll_offset, visible_rows);
                int tp = 1, cp = 0;
                get_page_info(tp, cp);
                headless_dump_frame(entries, selected, scroll_offset, visible_rows,
                                    current_path, expanded_dirs, last_scan_ms,
                                    bar_fg, bar_bg, frame_num, tp, cp, page_size,
                                    scan_result, sort_options, active_filter);
                frame_num++;
            }
        }
        return 0;
    }

    // ==================== NCURSES MODE ====================
    setlocale(LC_ALL, "");
    initscr();
    noecho();
    cbreak();
    keypad(stdscr, TRUE);
    curs_set(0);

    int bar_bg = colors.background;
    int bar_fg = colors.foreground;

    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(1, COLOR_WHITE, -1);
        init_pair(2, curses_color(bar_fg), curses_color(bar_bg));
    }

    endwin();
    refresh();

    bool running = true;
    int input;
    int selected = 0;
    int scroll_offset = 0;
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    int visible_rows = rows - 1 - footer_height(cols);

    std::filesystem::path current_path = start_path;
    auto& expanded_dirs = get_expanded_dirs();
    double last_scan_ms = 0.0;
    ScanResult scan_result;
    std::atomic<bool> loading(false);
    std::atomic<bool> anim_started(false);

    int total_pages = 1;
    int current_page = 0;

    std::vector<EntryInfo> entries;
    bool need_refresh = true;
    bool refresh_next_scan = false;
    std::filesystem::path select_first_owner;
    std::filesystem::path select_last_owner;
    while (running) {
        clear();
        getmaxyx(stdscr, rows, cols);
        visible_rows = rows - 1 - footer_height(cols);
        if (visible_rows < 1) visible_rows = 1;
        if (need_refresh) {
            loading = true;
            anim_started = false;
            std::thread loader([&]() {
                auto t0 = std::chrono::high_resolution_clock::now();
                try {
                    scan_result = rebuild_tree_preserving_selection(current_path, expanded_dirs,
                        page_size, entries, selected, refresh_next_scan, sort_options, active_filter);
                } catch (const std::filesystem::filesystem_error& ex) {
                    scan_result.status = ScanStatus::failed;
                    scan_result.diagnostics.push_back({ex.path1(), "scan", ex.code()});
                } catch (...) {
                    scan_result.status = ScanStatus::failed;
                    scan_result.diagnostics.push_back({current_path, "scan",
                        std::make_error_code(std::errc::io_error)});
                }
                entries = scan_result.entries;
                auto t1 = std::chrono::high_resolution_clock::now();
                last_scan_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
                loading = false;
            });
            int waited = 0;
            int anim_delay = 120;
            while (loading && waited < 500) {
                std::this_thread::sleep_for(std::chrono::milliseconds(anim_delay));
                waited += anim_delay;
            }
            std::thread anim;
            if (loading) {
                anim = std::thread([&]() {
                    show_loading_animation(loading, anim_started);
                });
            }
            loader.join();
            loading = false;
            if (anim.joinable()) anim.join();
            if (anim_started) {
                std::this_thread::sleep_for(std::chrono::milliseconds(80));
            }
            need_refresh = false;
            refresh_next_scan = false;
            if (scan_result.status == ScanStatus::failed)
                show_scan_diagnostics(scan_result);
        }
        clamp_and_skip_selection(entries, selected, select_first_owner, select_last_owner);
        update_scroll(selected, scroll_offset, visible_rows);

        current_page = get_current_page(current_path);
        total_pages = 1;
        for (const auto& e : entries) {
            if ((e.type == "[RESTO_NEXT]" || e.type == "[RESTO_PREV]") && e.full_path == current_path) {
                std::string label = e.name;
                auto paren = label.rfind('(');
                auto slash = label.rfind('/');
                if (paren != std::string::npos && slash != std::string::npos && slash > paren) {
                    total_pages = std::stoi(label.substr(slash + 1));
                }
                break;
            }
        }

        clear();
        draw_header(cols, current_path, current_page, total_pages, sort_options,
                    active_filter, scan_result.matching_files);
        print_directory_entries(entries, selected, scroll_offset, visible_rows, (int)entries.size(), 1, 2);
        draw_footer(rows, cols, selected, (int)entries.size(), last_scan_ms,
                    !scan_result.diagnostics.empty(), active_filter, scan_result.matching_files);
        refresh();
        input = getch();
        switch (input) {
            case 'q':
            case 'Q':
                running = false;
                break;
            case 'r':
            case 'R':
                need_refresh = true;
                refresh_next_scan = true;
                break;
            case 's':
            case 'S':
                sort_options.key = sort_options.key == SortKey::size ? SortKey::name
                    : sort_options.key == SortKey::name ? SortKey::mtime : SortKey::size;
                reset_listing_view(selected, scroll_offset, need_refresh);
                break;
            case 't':
            case 'T':
                sort_options.order = sort_options.order == SortOrder::asc
                    ? SortOrder::desc : SortOrder::asc;
                reset_listing_view(selected, scroll_offset, need_refresh);
                break;
            case '/':
                if (prompt_for_filter(active_filter))
                    reset_listing_view(selected, scroll_offset, need_refresh);
                break;
            case 'f':
            case 'F':
                active_filter = {};
                reset_listing_view(selected, scroll_offset, need_refresh);
                break;
            case '\n':
            case KEY_ENTER:
                if (!entries.empty() && selected >= 0 && selected < static_cast<int>(entries.size())) {
                    const auto entry = entries[selected];
                    if (entry.type == "[DIR] ") {
                        std::string error;
                        if (!navigate_to_root(entry.full_path, current_path, expanded_dirs, page_size,
                            entries, scan_result, selected, scroll_offset, visible_rows,
                            last_scan_ms, {}, error, sort_options, active_filter))
                            confirm_popup(std::string(text(Text::NavigationError)) + error);
                    } else if (entry.type == "[RESTO_NEXT]") {
                        expand_resto(entry.full_path);
                        need_refresh = true;
                        select_first_owner = entry.full_path;
                    } else if (entry.type == "[RESTO_PREV]") {
                        prev_resto(entry.full_path);
                        need_refresh = true;
                        select_last_owner = entry.full_path;
                    }
                }
                break;
            case KEY_BACKSPACE:
            case 8:
            case 127: {
                std::string error;
                if (!navigate_to_parent(current_path, expanded_dirs, page_size, entries,
                    scan_result, selected, scroll_offset, visible_rows, last_scan_ms, error, sort_options, active_filter) &&
                    !error.empty())
                    confirm_popup(std::string(text(Text::NavigationError)) + error);
                break;
            }
            case 'o':
            case 'O': {
                std::string value;
                if (prompt_for_path(value)) {
                    std::string error;
                    if (!navigate_to_root(std::filesystem::u8path(value), current_path,
                        expanded_dirs, page_size, entries, scan_result, selected, scroll_offset,
                        visible_rows, last_scan_ms, {}, error, sort_options, active_filter))
                        confirm_popup(std::string(text(Text::NavigationError)) + error);
                }
                break;
            }
            case KEY_UP:
            case 'k':
                if (selected > 0) selected--;
                break;
            case KEY_DOWN:
            case 'j':
                if (selected < (int)entries.size() - 1) selected++;
                break;
            case ' ':
                if (!entries.empty()) {
                    std::string error;
                    if (!open_path(entries[selected].full_path, error))
                        confirm_popup(error);
                }
                break;
            case 'e':
            case 'E':
            case 'l':
            case KEY_RIGHT:
                if (!entries.empty()) {
                    const auto& entry = entries[selected];
                    if (entry.type == "[DIR] ") {
                        auto dir_path = entry.full_path;
                        if (expanded_dirs.count(dir_path) && (input == 'e' || input == 'E')) {
                            expanded_dirs.erase(dir_path);
                        } else {
                            expanded_dirs.insert(dir_path);
                        }
                        need_refresh = true;
                    } else if (entry.type == "[RESTO_NEXT]") {
                        expand_resto(entry.full_path);
                        need_refresh = true;
                        select_first_owner = entry.full_path;
                    } else if (entry.type == "[RESTO_PREV]" && (input == 'e' || input == 'E')) {
                        prev_resto(entry.full_path);
                        need_refresh = true;
                        select_last_owner = entry.full_path;
                    }
                }
                break;
            case 'h':
            case KEY_LEFT:
                need_refresh = collapse_selected_directory(entries, selected, expanded_dirs);
                break;
            case 'g':
                selected = 0;
                break;
            case 'G':
                if (!entries.empty()) selected = static_cast<int>(entries.size()) - 1;
                break;
            case 'n':
            case 'N':
                if (!entries.empty()) {
                    auto owner = find_pagination_owner(entries, selected, current_path);
                    bool has_next = false;
                    for (const auto& e : entries) {
                        if (e.type == "[RESTO_NEXT]" && e.full_path == owner) {
                            has_next = true;
                            break;
                        }
                    }
                    if (has_next) {
                        expand_resto(owner);
                        need_refresh = true;
                        select_first_owner = owner;
                    }
                }
                break;
            case 'p':
            case 'P':
                if (!entries.empty()) {
                    auto owner = find_pagination_owner(entries, selected, current_path);
                    if (get_current_page(owner) > 0) {
                        prev_resto(owner);
                        need_refresh = true;
                        select_last_owner = owner;
                    }
                }
                break;
            case KEY_DC:
                if (!entries.empty()) {
                    const auto& entry = entries[selected];
                    if (entry.type == "[RESTO_PREV]" || entry.type == "[RESTO_NEXT]")
                        break;
                    std::string msg = std::string(text(Text::DeletePrompt)) + " \"" + entry.name + "\"?";
                    if (confirm_popup(msg)) {
                        try {
                            if (entry.type == "[DIR] ") {
                                std::filesystem::remove_all(entry.full_path);
                            } else {
                                std::filesystem::remove(entry.full_path);
                            }
                        } catch (const std::exception& ex) {
                            confirm_popup(std::string(text(Text::Error)) + ": " + ex.what());
                        }
                        need_refresh = true;
                        refresh_next_scan = true;
                    }
                }
                break;
            case 'b':
            case 'B':
                if (has_colors()) {
                    auto [fg, bg] = bar_color_selection_popup(bar_fg, bar_bg);
                    if (fg >= 0 && bg >= 0) {
                        bar_fg = fg;
                        bar_bg = bg;
                        init_pair(2, curses_color(bar_fg), curses_color(bar_bg));
                        colors = {fg, bg};
                        if (!save_color_settings(config_path, colors, config_error))
                            confirm_popup(std::string(text(Text::ConfigSaveWarning)) + config_error);
                    }
                } else {
                    confirm_popup(text(Text::ColorsUnsupported));
                }
                break;
            case 'w':
            case 'W':
                show_scan_diagnostics(scan_result);
                break;
        }
        update_scroll(selected, scroll_offset, visible_rows);
    }

    endwin();
    return 0;
}
