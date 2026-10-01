#include "file_utils.h"
#include "test_directory.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>

namespace fs = std::filesystem;

#define CHECK(condition) do { \
    if (!(condition)) { \
        std::cerr << "FAIL at line " << __LINE__ << ": " << #condition << '\n'; \
        std::abort(); \
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

int main() {
    fs::create_directories(test_directory.path);
    test_child_error_keeps_accessible_results();
    test_root_error_is_reported_as_failure();
    test_links_are_listed_without_traversal();
    std::cout << "test_scan: PASS\n";
    return 0;
}
