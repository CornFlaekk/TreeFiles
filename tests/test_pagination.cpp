#include "file_utils.h"
#include "test_directory.h"
#include <cstdio>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <vector>
#include <set>
#include <cstdlib>
#include <string>
#include <limits>
#include <stdexcept>

namespace fs = std::filesystem;

static int tests_run = 0;
static int tests_failed = 0;

#define TEST(name) do { tests_run++; printf("  %s... ", name); } while(0)
#define CHECK(cond) do { if (!(cond)) { printf("FAIL\n"); tests_failed++; return; } } while(0)
#define PASS() printf("OK\n")

static const TestDirectory test_directory("pagination");
const fs::path BASE = test_directory.path;

void setup_many_files(int count, const char* subdir) {
    fs::remove_all(BASE);
    fs::create_directories(BASE / subdir);
    for (int i = 0; i < count; i++) {
        char name[64];
        snprintf(name, sizeof(name), "file_%04d.txt", i);
        fs::path path = BASE / subdir / name;
        std::ofstream f(path, std::ios::binary);
        if (f) {
            f.seekp(9);
            f.write("", 1);
        }
    }
}

void cleanup() {
    fs::remove_all(BASE);
}

bool has_type(const std::vector<EntryInfo>& entries, const char* type) {
    for (const auto& e : entries) {
        if (e.type == type) return true;
    }
    return false;
}

void test_no_nav_for_small_dirs() {
    TEST("no nav entries when entries <= max_files");
    setup_many_files(20, "small");
    clear_dir_size_cache();
    reset_resto_state();
    auto& expanded = get_expanded_dirs();
    expanded.clear();

    std::vector<EntryInfo> entries;
    build_tree_entries(fs::path(BASE / "small"), expanded, entries, 0, 30);

    CHECK(!has_type(entries, "[RESTO_NEXT]"));
    CHECK(!has_type(entries, "[RESTO_PREV]"));
    CHECK(entries.size() == 20);
    PASS();
    cleanup();
}

void test_next_appears_when_over_max() {
    TEST("RESTO_NEXT appears when entries > max_files, no PREV on page 0");
    setup_many_files(50, "big");
    clear_dir_size_cache();
    reset_resto_state();
    auto& expanded = get_expanded_dirs();
    expanded.clear();

    std::vector<EntryInfo> entries;
    build_tree_entries(fs::path(BASE / "big"), expanded, entries, 0, 30);

    CHECK(has_type(entries, "[RESTO_NEXT]"));
    CHECK(!has_type(entries, "[RESTO_PREV]"));
    // 30 files + NEXT = 31
    CHECK(entries.size() == 31);
    PASS();
    cleanup();
}

void test_expand_resto_shows_page_1() {
    TEST("expand_resto shows page 1 with PREV and remaining content");
    setup_many_files(50, "big");
    clear_dir_size_cache();
    reset_resto_state();
    auto& expanded = get_expanded_dirs();
    expanded.clear();

    fs::path dir = BASE / "big";

    std::vector<EntryInfo> entries;
    build_tree_entries(dir, expanded, entries, 0, 30);
    CHECK(entries.size() == 31);

    expand_resto(dir);

    std::vector<EntryInfo> entries2;
    build_tree_entries(dir, expanded, entries2, 0, 30);
    // Page 1: PREV + 20 content = 21
    CHECK(entries2.size() == 21);
    CHECK(has_type(entries2, "[RESTO_PREV]"));
    CHECK(!has_type(entries2, "[RESTO_NEXT]"));
    PASS();
    cleanup();
}

void test_prev_resto_goes_back() {
    TEST("prev_resto returns to page 0");
    setup_many_files(50, "big");
    clear_dir_size_cache();
    reset_resto_state();
    auto& expanded = get_expanded_dirs();
    expanded.clear();

    fs::path dir = BASE / "big";

    std::vector<EntryInfo> entries;
    build_tree_entries(dir, expanded, entries, 0, 30);
    CHECK(entries.size() == 31);

    expand_resto(dir);

    std::vector<EntryInfo> entries2;
    build_tree_entries(dir, expanded, entries2, 0, 30);
    CHECK(entries2.size() == 21);

    prev_resto(dir);

    std::vector<EntryInfo> entries3;
    build_tree_entries(dir, expanded, entries3, 0, 30);
    CHECK(entries3.size() == 31);
    CHECK(has_type(entries3, "[RESTO_NEXT]"));
    CHECK(!has_type(entries3, "[RESTO_PREV]"));
    PASS();
    cleanup();
}

void test_reset_resto_state() {
    TEST("reset_resto_state clears pagination");
    setup_many_files(50, "big");
    clear_dir_size_cache();
    reset_resto_state();
    auto& expanded = get_expanded_dirs();
    expanded.clear();

    fs::path dir = BASE / "big";

    std::vector<EntryInfo> entries;
    build_tree_entries(dir, expanded, entries, 0, 30);
    CHECK(entries.size() == 31);

    expand_resto(dir);

    std::vector<EntryInfo> entries2;
    build_tree_entries(dir, expanded, entries2, 0, 30);
    CHECK(entries2.size() == 21);

    reset_resto_state();

    std::vector<EntryInfo> entries3;
    build_tree_entries(dir, expanded, entries3, 0, 30);
    CHECK(entries3.size() == 31);
    PASS();
    cleanup();
}

void test_prev_resto_does_not_go_below_zero() {
    TEST("prev_resto on page 0 stays at page 0");
    setup_many_files(50, "big");
    clear_dir_size_cache();
    reset_resto_state();
    auto& expanded = get_expanded_dirs();
    expanded.clear();

    fs::path dir = BASE / "big";

    prev_resto(dir);

    std::vector<EntryInfo> entries;
    build_tree_entries(dir, expanded, entries, 0, 30);
    CHECK(entries.size() == 31);
    CHECK(!has_type(entries, "[RESTO_PREV]"));
    PASS();
    cleanup();
}

void test_get_current_page() {
    TEST("get_current_page tracks pagination state");
    setup_many_files(50, "big");
    clear_dir_size_cache();
    reset_resto_state();
    auto& expanded = get_expanded_dirs();
    expanded.clear();

    fs::path dir = BASE / "big";

    CHECK(get_current_page(dir) == 0);

    expand_resto(dir);
    CHECK(get_current_page(dir) == 1);

    prev_resto(dir);
    CHECK(get_current_page(dir) == 0);

    reset_resto_state();
    CHECK(get_current_page(dir) == 0);

    PASS();
    cleanup();
}

void test_labels_no_old_format() {
    TEST("nav labels use new format without old Pag prefix");
    setup_many_files(50, "big");
    clear_dir_size_cache();
    reset_resto_state();
    auto& expanded = get_expanded_dirs();
    expanded.clear();

    fs::path dir = BASE / "big";

    std::vector<EntryInfo> entries;
    build_tree_entries(dir, expanded, entries, 0, 30);

    bool found_new_next = false;
    bool found_old_label = false;
    for (const auto& e : entries) {
        if (e.type == "[RESTO_NEXT]" && e.name.find("Siguiente") != std::string::npos)
            found_new_next = true;
        if (e.name.find("Pag ") != std::string::npos)
            found_old_label = true;
    }
    CHECK(found_new_next);
    CHECK(!found_old_label);
    PASS();
    cleanup();
}

void test_custom_page_size() {
    TEST("custom size preserves ordered files across all pages and back");
    setup_many_files(8, "custom");
    reset_resto_state();
    const fs::path dir = BASE / "custom";
    const std::set<fs::path> expanded;
    std::vector<EntryInfo> entries;
    build_tree_entries(dir, expanded, entries, 0, 3);
    CHECK(entries.size() == 4);
    CHECK(entries[0].name == "file_0000.txt");
    CHECK(entries[2].name == "file_0002.txt");
    CHECK(entries.back().type == "[RESTO_NEXT]");
    CHECK(entries.back().name.find("(1/3)") != std::string::npos);

    expand_resto(dir);
    entries.clear();
    build_tree_entries(dir, expanded, entries, 0, 3);
    CHECK(entries.size() == 5);
    CHECK(entries.front().type == "[RESTO_PREV]");
    CHECK(entries[1].name == "file_0003.txt");
    CHECK(entries[3].name == "file_0005.txt");
    CHECK(entries.back().type == "[RESTO_NEXT]");

    expand_resto(dir);
    entries.clear();
    build_tree_entries(dir, expanded, entries, 0, 3);
    CHECK(entries.size() == 3);
    CHECK(entries.front().type == "[RESTO_PREV]");
    CHECK(entries[1].name == "file_0006.txt");
    CHECK(entries[2].name == "file_0007.txt");
    CHECK(!has_type(entries, "[RESTO_NEXT]"));

    prev_resto(dir);
    entries.clear();
    build_tree_entries(dir, expanded, entries, 0, 3);
    CHECK(entries[1].name == "file_0003.txt");
    PASS();
    cleanup();
}

void test_custom_size_in_expanded_directory() {
    TEST("expanded directories use the same custom page size independently");
    setup_many_files(8, "child");
    clear_dir_size_cache();
    reset_resto_state();
    const fs::path child = BASE / "child";
    const std::set<fs::path> expanded{child};
    std::vector<EntryInfo> entries;
    build_tree_entries(BASE, expanded, entries, 0, 3);
    CHECK(entries.size() == 5);
    CHECK(entries[0].type == "[DIR] ");
    CHECK(entries[0].depth == 0);
    CHECK(entries[1].name == "file_0000.txt");
    CHECK(entries[3].name == "file_0002.txt");
    CHECK(entries.back().type == "[RESTO_NEXT]");
    CHECK(entries.back().full_path == child);
    CHECK(entries.back().depth == 1);

    expand_resto(child);
    entries.clear();
    build_tree_entries(BASE, expanded, entries, 0, 3);
    CHECK(entries.size() == 6);
    CHECK(entries[1].type == "[RESTO_PREV]");
    CHECK(entries[2].name == "file_0003.txt");
    CHECK(get_current_page(BASE) == 0);
    PASS();
    cleanup();
}

void test_large_page_size() {
    TEST("maximum integer page size does not overflow or lose entries");
    setup_many_files(5, "large");
    reset_resto_state();
    std::vector<EntryInfo> entries;
    build_tree_entries(BASE / "large", {}, entries, 0, std::numeric_limits<int>::max());
    CHECK(entries.size() == 5);
    CHECK(!has_type(entries, "[RESTO_NEXT]"));
    CHECK(!has_type(entries, "[RESTO_PREV]"));
    PASS();
    cleanup();
}

void test_invalid_page_size() {
    TEST("zero and negative page sizes are rejected before scanning");
    for (int value : {0, -1}) {
        bool rejected = false;
        std::vector<EntryInfo> entries;
        try {
            build_tree_entries(BASE / "missing", {}, entries, 0, value);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        CHECK(rejected);
    }
    PASS();
}

int main() {
    printf("test_pagination\n");
    cleanup();
    test_no_nav_for_small_dirs();
    test_next_appears_when_over_max();
    test_expand_resto_shows_page_1();
    test_prev_resto_goes_back();
    test_reset_resto_state();
    test_prev_resto_does_not_go_below_zero();
    test_get_current_page();
    test_labels_no_old_format();
    test_custom_page_size();
    test_custom_size_in_expanded_directory();
    test_large_page_size();
    test_invalid_page_size();
    cleanup();
    printf("  %d run, %d failed\n", tests_run, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
