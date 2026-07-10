#include "file_utils.h"
#include <cstdio>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <vector>
#include <set>
#include <cstdlib>
#include <string>

namespace fs = std::filesystem;

static int tests_run = 0;
static int tests_failed = 0;

#define TEST(name) do { tests_run++; printf("  %s... ", name); } while(0)
#define CHECK(cond) do { if (!(cond)) { printf("FAIL\n"); tests_failed++; return; } } while(0)
#define PASS() printf("OK\n")

const char* BASE = "/tmp/treefiles_test_pagination";

void setup_many_files(int count, const char* subdir) {
    fs::remove_all(BASE);
    fs::create_directories(std::string(BASE) + "/" + subdir);
    for (int i = 0; i < count; i++) {
        char name[64];
        snprintf(name, sizeof(name), "file_%04d.txt", i);
        std::string path = std::string(BASE) + "/" + subdir + "/" + name;
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
    build_tree_entries(fs::path(std::string(BASE) + "/small"), expanded, entries, 0, 30);

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
    build_tree_entries(fs::path(std::string(BASE) + "/big"), expanded, entries, 0, 30);

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

    fs::path dir = std::string(BASE) + "/big";

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

    fs::path dir = std::string(BASE) + "/big";

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

    fs::path dir = std::string(BASE) + "/big";

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

    fs::path dir = std::string(BASE) + "/big";

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

    fs::path dir = std::string(BASE) + "/big";

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

    fs::path dir = std::string(BASE) + "/big";

    std::vector<EntryInfo> entries;
    build_tree_entries(dir, expanded, entries, 0, 30);

    bool found_new_next = false;
    bool found_old_label = false;
    for (const auto& e : entries) {
        if (e.type == "[RESTO_NEXT]" && e.name.find("Next") != std::string::npos)
            found_new_next = true;
        if (e.name.find("Pag ") != std::string::npos)
            found_old_label = true;
    }
    CHECK(found_new_next);
    CHECK(!found_old_label);
    PASS();
    cleanup();
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
    cleanup();
    printf("  %d run, %d failed\n", tests_run, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
