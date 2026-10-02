#include "file_utils.h"
#include "test_directory.h"
#include <cstdio>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <vector>
#include <set>
#include <cstdlib>
#include <algorithm>
#include <unordered_map>

namespace fs = std::filesystem;

static int tests_run = 0;
static int tests_failed = 0;

#define TEST(name) do { tests_run++; printf("  %s... ", name); } while(0)
#define CHECK(cond) do { if (!(cond)) { printf("FAIL\n"); tests_failed++; return; } } while(0)
#define PASS() printf("OK\n")

static const TestDirectory test_directory("build_tree");
const fs::path BASE = test_directory.path;

void setup() {
    fs::remove_all(BASE);
    fs::create_directories(BASE);
    fs::create_directories(BASE / "dir_a");
    fs::create_directories(BASE / "dir_b");
    fs::create_directories(BASE / "dir_a/sub");
}

bool create_file(const fs::path& path, std::uintmax_t size) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    if (size > 0) {
        f.seekp(size - 1);
        f.write("", 1);
    }
    return true;
}

void cleanup() {
    fs::remove_all(BASE);
}

void test_sorted_by_size_descending() {
    TEST("entries sorted by size descending");
    setup();
    create_file(BASE / "small.txt", 100);
    create_file(BASE / "medium.txt", 1024);
    create_file(BASE / "large.txt", 10240);
    create_file(BASE / "dir_a/sub/deep.txt", 512);

    clear_dir_size_cache();
    reset_resto_state();
    auto& expanded = get_expanded_dirs();
    expanded.clear();

    std::vector<EntryInfo> entries;
    build_tree_entries(BASE, expanded, entries, 0, 30);

    CHECK(entries.size() >= 3);
    CHECK(entries[0].size >= entries[1].size);
    CHECK(entries[1].size >= entries[2].size);
    PASS();
    cleanup();
}

void test_file_types() {
    TEST("file entries have [FILE] type");
    setup();
    create_file(BASE / "file.txt", 100);

    clear_dir_size_cache();
    reset_resto_state();
    auto& expanded = get_expanded_dirs();
    expanded.clear();

    std::vector<EntryInfo> entries;
    build_tree_entries(BASE, expanded, entries, 0, 30);

    bool found_file = false;
    bool found_dir = false;
    for (const auto& e : entries) {
        if (e.type == "[FILE]" && e.name == "file.txt") found_file = true;
        if (e.type == "[DIR] " && e.name == "dir_a") found_dir = true;
    }
    CHECK(found_file);
    CHECK(found_dir);
    PASS();
    cleanup();
}

void test_expanded_directories() {
    TEST("expanded directories include children");
    setup();
    create_file(BASE / "dir_a/child.txt", 500);
    create_file(BASE / "root.txt", 100);

    clear_dir_size_cache();
    reset_resto_state();
    auto& expanded = get_expanded_dirs();
    expanded.clear();
    expanded.insert(fs::path(BASE / "dir_a"));

    std::vector<EntryInfo> entries;
    build_tree_entries(BASE, expanded, entries, 0, 30);

    bool found_child = false;
    for (const auto& e : entries) {
        if (e.name == "child.txt" && e.depth == 1) found_child = true;
    }
    CHECK(found_child);
    PASS();
    cleanup();
}

void test_depth_increases_for_children() {
    TEST("children have depth = parent depth + 1");
    setup();
    create_file(BASE / "dir_a/child.txt", 500);

    clear_dir_size_cache();
    reset_resto_state();
    auto& expanded = get_expanded_dirs();
    expanded.clear();
    expanded.insert(fs::path(BASE / "dir_a"));

    std::vector<EntryInfo> entries;
    build_tree_entries(BASE, expanded, entries, 0, 30);

    for (const auto& e : entries) {
        if (e.type == "[DIR] " && e.name == "dir_a") {
            CHECK(e.depth == 0);
        }
        if (e.name == "child.txt") {
            CHECK(e.depth == 1);
        }
    }
    PASS();
    cleanup();
}

void test_alphabetic_tiebreaker() {
    TEST("same size entries sorted alphabetically");
    setup();
    create_file(BASE / "zzz.txt", 100);
    create_file(BASE / "aaa.txt", 100);
    create_file(BASE / "mmm.txt", 100);

    clear_dir_size_cache();
    reset_resto_state();
    auto& expanded = get_expanded_dirs();
    expanded.clear();

    std::vector<EntryInfo> entries;
    build_tree_entries(BASE, expanded, entries, 0, 30);

    std::vector<std::string> names;
    for (const auto& e : entries) {
        if (e.type == "[FILE]") names.push_back(e.name);
    }
    CHECK(names.size() == 3);
    CHECK(names[0] == "aaa.txt");
    CHECK(names[1] == "mmm.txt");
    CHECK(names[2] == "zzz.txt");
    PASS();
    cleanup();
}

void test_large_files_are_counted_and_propagated() {
    TEST("logical sizes above 1 TiB are counted, cached and sorted through nested directories");
    setup();
    const std::uintmax_t tib = std::uintmax_t{1} << 40;
    const std::vector<std::pair<std::string, std::uintmax_t>> files = {
        {"below.bin", tib - 1},
        {"exact.bin", tib},
        {"above.bin", tib + 1},
        {"large.bin", tib + 17}
    };
    std::unordered_map<fs::path, std::uintmax_t> sizes;
    for (const auto& file : files) {
        const fs::path path = BASE / "dir_a" / "sub" / file.first;
        std::ofstream output(path, std::ios::binary);
        CHECK(static_cast<bool>(output));
        output << 'x';
        sizes[path.lexically_normal()] = file.second;
    }
    const fs::path sibling = BASE / "dir_a" / "sibling.bin";
    { std::ofstream output(sibling, std::ios::binary); CHECK(static_cast<bool>(output)); output << 'x'; }
    sizes[sibling.lexically_normal()] = 32;

    clear_dir_size_cache();
    reset_resto_state();
    auto& expanded = get_expanded_dirs();
    expanded.clear();
    expanded.insert(BASE / "dir_a");
    expanded.insert(BASE / "dir_a" / "sub");
    const FileSizeReader reader = [&sizes](const fs::path& path) {
        auto found = sizes.find(path.lexically_normal());
        return found == sizes.end() ? fs::file_size(path) : found->second;
    };
    std::vector<EntryInfo> entries;
    build_tree_entries(BASE, expanded, entries, 0, 30, reader);

    auto entry_named = [&entries](const std::string& name, int depth) -> const EntryInfo* {
        auto found = std::find_if(entries.begin(), entries.end(), [&](const EntryInfo& entry) {
            return entry.name == name && entry.depth == depth;
        });
        return found == entries.end() ? nullptr : &*found;
    };
    CHECK(entry_named("below.bin", 2) && entry_named("below.bin", 2)->size == tib - 1);
    CHECK(entry_named("exact.bin", 2) && entry_named("exact.bin", 2)->size == tib);
    CHECK(entry_named("above.bin", 2) && entry_named("above.bin", 2)->size == tib + 1);
    const std::uintmax_t nested_total = tib * 4 + 17;
    const std::uintmax_t parent_total = tib * 4 + 49;
    CHECK(entry_named("sub", 1) && entry_named("sub", 1)->size == nested_total);
    CHECK(entry_named("dir_a", 0) && entry_named("dir_a", 0)->size == parent_total);
    CHECK(entry_named("dir_a", 0)->size > entry_named("dir_b", 0)->size);
    CHECK(entry_named("dir_a", 0) == &entries.front());

    std::vector<EntryInfo> cached_entries;
    build_tree_entries(BASE, expanded, cached_entries, 0, 30, reader);
    auto cached_parent = std::find_if(cached_entries.begin(), cached_entries.end(), [](const EntryInfo& entry) {
        return entry.name == "dir_a" && entry.depth == 0;
    });
    CHECK(cached_parent != cached_entries.end() && cached_parent->size == parent_total);

    sizes[sibling.lexically_normal()] = 64;
    clear_dir_size_cache();
    std::vector<EntryInfo> invalidated_entries;
    build_tree_entries(BASE, expanded, invalidated_entries, 0, 30, reader);
    auto recalculated_parent = std::find_if(invalidated_entries.begin(), invalidated_entries.end(), [](const EntryInfo& entry) {
        return entry.name == "dir_a" && entry.depth == 0;
    });
    CHECK(recalculated_parent != invalidated_entries.end() && recalculated_parent->size == parent_total + 32);
    PASS();
    cleanup();
    clear_dir_size_cache();
}

int main() {
    printf("test_build_tree\n");
    cleanup();
    test_sorted_by_size_descending();
    test_file_types();
    test_expanded_directories();
    test_depth_increases_for_children();
    test_alphabetic_tiebreaker();
    test_large_files_are_counted_and_propagated();
    cleanup();
    printf("  %d run, %d failed\n", tests_run, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
