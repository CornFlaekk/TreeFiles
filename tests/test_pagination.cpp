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

bool has_resto(const std::vector<EntryInfo>& entries) {
    for (const auto& e : entries) {
        if (e.type == "[RESTO]") return true;
    }
    return false;
}

void test_no_resto_for_small_dirs() {
    TEST("[RESTO] not present when entries <= 100");
    setup_many_files(50, "small");
    clear_dir_size_cache();
    reset_resto_state();
    auto& expanded = get_expanded_dirs();
    expanded.clear();

    std::vector<EntryInfo> entries;
    build_tree_entries(fs::path(std::string(BASE) + "/small"), expanded, entries, 0, 100);

    CHECK(!has_resto(entries));
    CHECK(entries.size() == 50);
    PASS();
    cleanup();
}

void test_resto_appears_when_over_100() {
    TEST("[RESTO] appears when entries > 100");
    setup_many_files(150, "big");
    clear_dir_size_cache();
    reset_resto_state();
    auto& expanded = get_expanded_dirs();
    expanded.clear();

    std::vector<EntryInfo> entries;
    build_tree_entries(fs::path(std::string(BASE) + "/big"), expanded, entries, 0, 100);

    CHECK(has_resto(entries));
    CHECK(entries.size() == 101);
    PASS();
    cleanup();
}

void test_expand_resto_adds_more() {
    TEST("expand_resto increases visible entries");
    setup_many_files(150, "big");
    clear_dir_size_cache();
    reset_resto_state();
    auto& expanded = get_expanded_dirs();
    expanded.clear();

    fs::path dir = std::string(BASE) + "/big";

    std::vector<EntryInfo> entries;
    build_tree_entries(dir, expanded, entries, 0, 100);
    CHECK(entries.size() == 101);

    expand_resto(dir);

    std::vector<EntryInfo> entries2;
    build_tree_entries(dir, expanded, entries2, 0, 100);
    CHECK(entries2.size() == 50);
    PASS();
    cleanup();
}

void test_reset_resto_state() {
    TEST("reset_resto_state clears pagination");
    setup_many_files(150, "big");
    clear_dir_size_cache();
    reset_resto_state();
    auto& expanded = get_expanded_dirs();
    expanded.clear();

    fs::path dir = std::string(BASE) + "/big";

    std::vector<EntryInfo> entries;
    build_tree_entries(dir, expanded, entries, 0, 100);
    CHECK(entries.size() == 101);

    expand_resto(dir);

    std::vector<EntryInfo> entries2;
    build_tree_entries(dir, expanded, entries2, 0, 100);
    CHECK(entries2.size() == 50);

    reset_resto_state();

    std::vector<EntryInfo> entries3;
    build_tree_entries(dir, expanded, entries3, 0, 100);
    CHECK(entries3.size() == 101);
    PASS();
    cleanup();
}

int main() {
    printf("test_pagination\n");
    cleanup();
    test_no_resto_for_small_dirs();
    test_resto_appears_when_over_100();
    test_expand_resto_adds_more();
    test_reset_resto_state();
    cleanup();
    printf("  %d run, %d failed\n", tests_run, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
