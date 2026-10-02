#include "file_utils.h"
#include "test_directory.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <chrono>

namespace fs = std::filesystem;

#define CHECK(condition) do { \
    if (!(condition)) { \
        std::cerr << "FAIL at line " << __LINE__ << ": " << #condition << '\n'; \
        std::exit(1); \
    } \
} while (false)

static const TestDirectory test_directory("scan");

static void write_file(const fs::path& path, const std::string& value) {
    std::ofstream output(path, std::ios::binary);
    CHECK(output);
    output << value;
}

static const EntryInfo* find_entry(const ScanResult& result, const std::string& name) {
    const auto found = std::find_if(result.entries.begin(), result.entries.end(),
        [&](const EntryInfo& entry) { return entry.name == name; });
    return found == result.entries.end() ? nullptr : &*found;
}

static void test_child_error_keeps_accessible_results() {
    const fs::path root = test_directory.path / "partial";
    fs::create_directories(root / "sub");
    write_file(root / "sub" / "bad.bin", "unreadable");
    write_file(root / "sub" / "good.bin", "12345678901");
    write_file(root / "root.bin", "12345678");

    ScanOptions options;
    options.error_injector = [](const fs::path& path, const std::string& operation) {
        if (path.filename() == "bad.bin" && operation == "file_size")
            return std::make_error_code(std::errc::permission_denied);
        return std::error_code{};
    };
    const auto result = scan_tree_entries(root, {root / "sub"}, 30, options);
    CHECK(result.status == ScanStatus::partial);
    CHECK(!result.diagnostics.empty());
    const auto* bad = find_entry(result, "bad.bin");
    const auto* good = find_entry(result, "good.bin");
    const auto* sub = find_entry(result, "sub");
    CHECK(bad && bad->size_status == SizeStatus::unavailable);
    CHECK(good && good->size == 11 && good->size_status == SizeStatus::complete);
    CHECK(sub && sub->size == 11 && sub->size_status == SizeStatus::partial);
}

static void test_root_error_is_reported_as_failure() {
    const fs::path root = test_directory.path / "root-error";
    fs::create_directories(root);
    write_file(root / "readable.txt", "ok");
    ScanOptions options;
    options.error_injector = [&root](const fs::path& path, const std::string& operation) {
        if (path == root && operation == "directory_open")
            return std::make_error_code(std::errc::permission_denied);
        return std::error_code{};
    };
    const auto result = scan_tree_entries(root, {}, 30, options);
    CHECK(result.status == ScanStatus::failed);
    CHECK(result.entries.empty());
    CHECK(result.diagnostics.size() == 1);
}

static void test_links_are_listed_without_traversal() {
    const fs::path root = test_directory.path / "links";
    const fs::path target = root / "target";
    fs::create_directories(target);
    write_file(target / "inside.txt", "target");
    std::error_code error;
    const fs::path dir_link = root / "directory-link";
    fs::create_directory_symlink(target, dir_link, error);
    if (error) {
        std::cout << "  SKIP symlink cases: " << error.message() << '\n';
        return;
    }
    const fs::path file_link = root / "file-link";
    fs::create_symlink(target / "inside.txt", file_link, error);
    if (error) {
        fs::remove(dir_link);
        std::cout << "  SKIP file symlink case: " << error.message() << '\n';
        return;
    }
    const fs::path broken_link = root / "broken-link";
    fs::create_symlink(root / "missing.txt", broken_link, error);
    if (error) {
        std::cout << "  SKIP broken symlink case: " << error.message() << '\n';
    }
    const fs::path cycle = target / "cycle";
    fs::create_directory_symlink(root, cycle, error);
    if (error) {
        std::cout << "  SKIP symlink-cycle case: " << error.message() << '\n';
    }

    const auto result = scan_tree_entries(root, {dir_link, target}, 30);
    CHECK(result.status == ScanStatus::complete);
    CHECK(find_entry(result, "directory-link") && find_entry(result, "directory-link")->type == "[LINK]");
    CHECK(find_entry(result, "file-link") && find_entry(result, "file-link")->type == "[LINK]");
    if (!fs::exists(broken_link) && fs::is_symlink(broken_link))
        CHECK(find_entry(result, "broken-link") && find_entry(result, "broken-link")->type == "[LINK]");
    if (!error) {
        const auto* cycle_entry = find_entry(result, "cycle");
        CHECK(cycle_entry && cycle_entry->type == "[LINK]");
    }
    const auto descended_through_link = std::find_if(result.entries.begin(), result.entries.end(),
        [&](const EntryInfo& entry) {
            return entry.full_path.parent_path() == dir_link;
        });
    CHECK(descended_through_link == result.entries.end());
    const auto target_file_count = std::count_if(result.entries.begin(), result.entries.end(),
        [&](const EntryInfo& entry) { return entry.full_path == target / "inside.txt"; });
    CHECK(target_file_count == 1);

    ScanOptions link_filter;
    link_filter.filter.text = "FILE-LINK";
    const auto filtered_links = scan_tree_entries(root, {}, 30, link_filter);
    CHECK(filtered_links.matching_files == 1);
    CHECK(find_entry(filtered_links, "file-link"));
    CHECK(!find_entry(filtered_links, "directory-link"));

    const fs::path root_link = root / "root-link";
    fs::create_directory_symlink(target, root_link, error);
    if (!error) {
        const auto linked_root = scan_tree_entries(root_link, {}, 30);
        CHECK(linked_root.status == ScanStatus::complete);
        CHECK(find_entry(linked_root, "inside.txt"));
    } else {
        std::cout << "  SKIP explicit symlink root: " << error.message() << '\n';
    }

    fs::remove(dir_link, error);
    CHECK(!error && fs::exists(target / "inside.txt"));
}

static std::vector<std::string> names(const ScanResult& result) {
    std::vector<std::string> values;
    for (const auto& entry : result.entries) {
        if (entry.type != "[RESTO_NEXT]" && entry.type != "[RESTO_PREV]")
            values.push_back(entry.name);
    }
    return values;
}

static void test_sorting_and_missing_modification_times() {
    const fs::path root = test_directory.path / "sorting";
    fs::create_directories(root / "folder");
    write_file(root / "a.txt", "a");
    write_file(root / "b.txt", "bbb");
    write_file(root / "c.txt", "cc");
    write_file(root / "d.txt", "d");
    write_file(root / "folder" / "inside.txt", "1234");
    write_file(root / "folder" / "small.txt", "s");
    write_file(root / "folder" / "large.txt", "123");

    const auto base = fs::file_time_type::clock::now();
    std::error_code error;
    fs::last_write_time(root / "b.txt", base + std::chrono::hours(1), error);
    CHECK(!error);
    fs::last_write_time(root / "a.txt", base + std::chrono::hours(2), error);
    CHECK(!error);
    fs::last_write_time(root / "d.txt", base + std::chrono::hours(2), error);
    CHECK(!error);
    fs::last_write_time(root / "c.txt", base + std::chrono::hours(3), error);
    CHECK(!error);

    const auto scan = [&](SortKey key, SortOrder order, int page_size = 30,
                          const ScanOptions& extra = {}) {
        ScanOptions options = extra;
        options.last_write_time_reader = [&](const fs::path& path) {
            // MinGW cannot set directory timestamps; file timestamps still use real metadata.
            return path == root / "folder" ? base + std::chrono::hours(4) : fs::last_write_time(path);
        };
        options.sort = {key, order};
        options.reset_pagination = true;
        return scan_tree_entries(root, {}, page_size, options);
    };
    CHECK((names(scan(SortKey::size, SortOrder::desc)) ==
           std::vector<std::string>{"folder", "b.txt", "c.txt", "a.txt", "d.txt"}));
    CHECK((names(scan(SortKey::size, SortOrder::asc)) ==
           std::vector<std::string>{"a.txt", "d.txt", "c.txt", "b.txt", "folder"}));
    CHECK((names(scan(SortKey::name, SortOrder::asc)) ==
           std::vector<std::string>{"a.txt", "b.txt", "c.txt", "d.txt", "folder"}));
    CHECK((names(scan(SortKey::name, SortOrder::desc)) ==
           std::vector<std::string>{"folder", "d.txt", "c.txt", "b.txt", "a.txt"}));
    CHECK((names(scan(SortKey::mtime, SortOrder::asc)) ==
           std::vector<std::string>{"b.txt", "a.txt", "d.txt", "c.txt", "folder"}));
    CHECK((names(scan(SortKey::mtime, SortOrder::desc)) ==
           std::vector<std::string>{"folder", "c.txt", "a.txt", "d.txt", "b.txt"}));
    ScanOptions expanded_options;
    expanded_options.sort = {SortKey::size, SortOrder::desc};
    expanded_options.reset_pagination = true;
    const auto expanded = scan_tree_entries(root, {root / "folder"}, 2, expanded_options);
    const auto inside = std::find_if(expanded.entries.begin(), expanded.entries.end(),
        [](const EntryInfo& entry) { return entry.name == "inside.txt"; });
    CHECK(inside != expanded.entries.end());
    CHECK(inside + 1 != expanded.entries.end() && (inside + 1)->name == "large.txt");
    CHECK(std::find_if(expanded.entries.begin(), expanded.entries.end(),
        [](const EntryInfo& entry) { return entry.name == "small.txt"; }) == expanded.entries.end());
    expand_resto(root / "folder");
    auto nested_options = expanded_options;
    nested_options.reset_pagination = false;
    const auto nested_page = scan_tree_entries(root, {root / "folder"}, 2, nested_options);
    CHECK(std::find_if(nested_page.entries.begin(), nested_page.entries.end(),
        [](const EntryInfo& entry) { return entry.name == "small.txt"; }) != nested_page.entries.end());
    reset_resto_state();

    ScanOptions missing_time;
    missing_time.error_injector = [](const fs::path& path, const std::string& operation) {
        if (path.filename() == "d.txt" && operation == "last_write_time")
            return std::make_error_code(std::errc::permission_denied);
        return std::error_code{};
    };
    const auto partial = scan(SortKey::mtime, SortOrder::desc, 30, missing_time);
    CHECK(partial.status == ScanStatus::partial);
    CHECK(!partial.diagnostics.empty());
    CHECK(names(partial).back() == "d.txt");
    const auto partial_ascending = scan(SortKey::mtime, SortOrder::asc, 30, missing_time);
    CHECK(names(partial_ascending).back() == "d.txt");

    const auto name_page = scan(SortKey::name, SortOrder::asc, 2);
    CHECK((names(name_page) == std::vector<std::string>{"a.txt", "b.txt"}));
    reset_resto_state();
    expand_resto(root);
    ScanOptions page_options;
    page_options.sort = {SortKey::size, SortOrder::asc};
    const auto size_page = scan_tree_entries(root, {}, 2, page_options);
    CHECK((names(size_page) == std::vector<std::string>{"c.txt", "b.txt"}));
    reset_resto_state();

    clear_dir_size_cache();
    int recursive_size_reads = 0;
    ScanOptions counted;
    counted.file_size_reader = [&](const fs::path& path) {
        if (path.parent_path() == root / "folder") ++recursive_size_reads;
        return fs::file_size(path);
    };
    counted.sort = {SortKey::size, SortOrder::desc};
    (void)scan_tree_entries(root, {}, 30, counted);
    CHECK(recursive_size_reads == 3);
    recursive_size_reads = 0;
    counted.sort = {SortKey::name, SortOrder::asc};
    (void)scan_tree_entries(root, {}, 30, counted);
    CHECK(recursive_size_reads == 0);
    clear_dir_size_cache();
    counted.sort = {SortKey::mtime, SortOrder::asc};
    (void)scan_tree_entries(root, {}, 30, counted);
    CHECK(recursive_size_reads == 3);
    clear_dir_size_cache();

    SortKey key = SortKey::size;
    SortOrder order = SortOrder::desc;
    CHECK(parse_sort_key("mtime", key) && key == SortKey::mtime);
    CHECK(!parse_sort_key("extension", key));
    CHECK(parse_sort_order("asc", order) && order == SortOrder::asc);
    CHECK(!parse_sort_order("up", order));
}

static void test_filename_and_extension_filters() {
    const fs::path root = test_directory.path / "filters";
    fs::create_directories(root / "context");
    fs::create_directories(root / "empty-context");
    write_file(root / "Annual Report.TXT", "0123456789");
    write_file(root / "report.csv", "12");
    write_file(root / "other.txt", "12345");
    write_file(root / fs::u8path("canci\u00f3n.txt"), "lower accent");
    write_file(root / "context" / "deep-report.log", "1234567");
    write_file(root / "context" / "unmatched.bin", "12345678901");

    ScanOptions options;
    options.reset_pagination = true;
    options.filter.text = "report";
    const auto by_name = scan_tree_entries(root, {root / "context"}, 30, options);
    CHECK(by_name.matching_files == 3);
    CHECK(find_entry(by_name, "context") && find_entry(by_name, "context")->expanded);
    CHECK(find_entry(by_name, "empty-context"));
    CHECK(find_entry(by_name, "Annual Report.TXT"));
    CHECK(find_entry(by_name, "report.csv"));
    CHECK(find_entry(by_name, "deep-report.log"));
    CHECK(!find_entry(by_name, "other.txt"));
    CHECK(!find_entry(by_name, "unmatched.bin"));
    const auto context_size = find_entry(by_name, "context")->size;
    CHECK(context_size == 18);

    options.filter.extension = ".TXT";
    const auto combined = scan_tree_entries(root, {root / "context"}, 30, options);
    CHECK(combined.matching_files == 1);
    CHECK(find_entry(combined, "Annual Report.TXT"));
    CHECK(!find_entry(combined, "report.csv"));
    CHECK(!find_entry(combined, "deep-report.log"));
    CHECK(find_entry(combined, "context")->size == context_size);

    options.filter = {};
    options.filter.extension = "tXt";
    const auto by_extension = scan_tree_entries(root, {}, 30, options);
    CHECK(by_extension.matching_files == 3);
    CHECK(find_entry(by_extension, "Annual Report.TXT"));
    CHECK(find_entry(by_extension, "other.txt"));
    CHECK(find_entry(by_extension, fs::u8path("canci\u00f3n.txt").u8string()));
    CHECK(!find_entry(by_extension, "report.csv"));

    options.filter = {"CANCI\u00f3N", "txt"};
    const auto lowercase_accent = scan_tree_entries(root, {}, 30, options);
    CHECK(lowercase_accent.matching_files == 1);
    CHECK(find_entry(lowercase_accent, fs::u8path("canci\u00f3n.txt").u8string()));
    options.filter.text = "canci\u00d3N";
    const auto different_accent_case = scan_tree_entries(root, {}, 30, options);
    CHECK(different_accent_case.matching_files == 0);

    options.filter = {"no-such-name", "log"};
    const auto no_matches = scan_tree_entries(root, {root / "context"}, 30, options);
    CHECK(no_matches.matching_files == 0);
    CHECK(find_entry(no_matches, "context"));
    CHECK(find_entry(no_matches, "empty-context"));
    CHECK(!find_entry(no_matches, "deep-report.log"));
    CHECK(std::none_of(no_matches.entries.begin(), no_matches.entries.end(), [](const EntryInfo& entry) {
        return entry.type == "[RESTO_NEXT]" || entry.type == "[RESTO_PREV]";
    }));

    const fs::path paged_root = test_directory.path / "filter-pagination";
    fs::create_directories(paged_root);
    for (int index = 0; index < 12; ++index)
        write_file(paged_root / ("file_" + std::to_string(index) + ".txt"), "x");
    write_file(paged_root / "needle.txt", "x");
    reset_resto_state();
    expand_resto(paged_root);
    CHECK(get_current_page(paged_root) == 1);
    ScanOptions paged_options;
    paged_options.filter.text = "needle";
    const auto one_match = scan_tree_entries(paged_root, {}, 1, paged_options);
    CHECK(one_match.matching_files == 1);
    CHECK(one_match.entries.size() == 1);
    CHECK(one_match.entries.front().name == "needle.txt");
    CHECK(get_current_page(paged_root) == 0);
    reset_resto_state();

    EntryInfo directory_link{"[LINK]", "linked-folder", root / "linked-folder", 0};
    EntryInfo text_link{"[LINK]", "linked.txt", root / "linked.txt", 0};
    CHECK(!entry_matches_filter(directory_link, {"linked", "txt"}));
    CHECK(entry_matches_filter(text_link, {"LINKED", ".TXT"}));
    CHECK(entry_matches_filter(directory_link, {"", ""}));
    CHECK(filter_is_active({"", "."}) == false);
    CHECK(!filter_is_active({"", ""}));
}

int main() {
    fs::create_directories(test_directory.path);
    test_child_error_keeps_accessible_results();
    test_root_error_is_reported_as_failure();
    test_links_are_listed_without_traversal();
    test_sorting_and_missing_modification_times();
    test_filename_and_extension_filters();
    std::cout << "test_scan: PASS\n";
    return 0;
}
