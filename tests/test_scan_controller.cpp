#include "scan_controller.h"
#include "test_directory.h"
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>

namespace fs = std::filesystem;

#define CHECK(condition) do { \
    if (!(condition)) { \
        std::cerr << "FAIL at line " << __LINE__ << ": " << #condition << '\n'; \
        std::exit(1); \
    } \
} while (false)

static const TestDirectory test_directory("scan_controller");

static void write_file(const fs::path& path, const std::string& value) {
    std::ofstream output(path, std::ios::binary);
    CHECK(output);
    output << value;
}

static void test_cancelled_scan_does_not_commit_size_cache() {
    const fs::path root = test_directory.path / "cancelled-cache";
    const fs::path folder = root / "folder";
    fs::create_directories(folder);
    for (int index = 0; index < 5; ++index)
        write_file(folder / ("file_" + std::to_string(index) + ".txt"), "data");

    std::atomic<bool> cancel{false};
    ScanOptions cancelled_options;
    cancelled_options.defer_cache_updates = true;
    cancelled_options.isolated_pagination = true;
    cancelled_options.is_cancelled = [&]() { return cancel.load(std::memory_order_acquire); };
    cancelled_options.progress_callback = [&](const ScanProgress& progress) {
        if (progress.entries_processed >= 2) cancel.store(true, std::memory_order_release);
    };

    clear_dir_size_cache();
    const auto cancelled = scan_tree_entries(root, {}, 30, cancelled_options);
    CHECK(cancelled.status == ScanStatus::cancelled);
    CHECK(cancelled.pending_cache_updates.empty());
    commit_scan_cache(cancelled);

    int nested_reads = 0;
    ScanOptions verify_cache;
    verify_cache.file_size_reader = [&](const fs::path& path) {
        if (path.parent_path() == folder) ++nested_reads;
        return fs::file_size(path);
    };
    (void)scan_tree_entries(root, {}, 30, verify_cache);
    CHECK(nested_reads == 5);
    clear_dir_size_cache();
}

static void test_isolated_pagination_does_not_touch_ui_state() {
    const fs::path root = test_directory.path / "isolated-pages";
    fs::create_directories(root);
    write_file(root / "a.txt", "a");
    write_file(root / "b.txt", "b");
    write_file(root / "c.txt", "c");

    restore_resto_state({{root, 9}});
    ScanOptions options;
    options.isolated_pagination = true;
    options.pagination_pages = snapshot_resto_state();
    const auto scanned = scan_tree_entries(root, {}, 1, options);
    CHECK(scanned.entries.size() == 2);
    CHECK(scanned.entries.front().name == "a.txt");
    CHECK(scanned.entries.back().type == "[RESTO_NEXT]");
    CHECK(scanned.pagination_pages.at(root) == 0);
    CHECK(get_current_page(root) == 9);
    reset_resto_state();
}

static void test_newest_request_wins_and_worker_errors_are_returned() {
    const fs::path old_root = test_directory.path / "old-root";
    const fs::path latest_root = test_directory.path / "latest-root";
    fs::create_directories(old_root);
    fs::create_directories(latest_root);
    write_file(old_root / "wait.txt", "old");
    write_file(latest_root / "latest.txt", "new");

    std::mutex gate_mutex;
    std::condition_variable gate_changed;
    bool entered_reader = false;
    bool release_reader = false;
    ScanOptions old_options;
    old_options.file_size_reader = [&](const fs::path& path) {
        if (path.parent_path() == old_root) {
            std::unique_lock<std::mutex> lock(gate_mutex);
            entered_reader = true;
            gate_changed.notify_all();
            gate_changed.wait(lock, [&]() { return release_reader; });
        }
        return fs::file_size(path);
    };

    ScanController controller;
    const auto old_generation = controller.start(old_root, {}, 30, old_options);
    {
        std::unique_lock<std::mutex> lock(gate_mutex);
        gate_changed.wait(lock, [&]() { return entered_reader; });
    }
    const auto latest_generation = controller.start(latest_root, {}, 30);
    CHECK(latest_generation > old_generation);
    const auto active = controller.activity();
    CHECK(active.busy && active.cancelling);
    CHECK(active.active_root == old_root);
    CHECK(active.latest_root == latest_root);
    {
        std::lock_guard<std::mutex> lock(gate_mutex);
        release_reader = true;
    }
    gate_changed.notify_all();

    const auto completions = controller.wait_until_idle();
    CHECK(completions.size() == 2);
    CHECK(completions[0].generation == old_generation);
    CHECK(completions[0].result.status == ScanStatus::cancelled);
    CHECK(completions[1].generation == latest_generation);
    CHECK(completions[1].root == latest_root);
    CHECK(completions[1].result.status == ScanStatus::complete);
    CHECK(completions[1].result.entries.size() == 1);
    CHECK(completions[1].result.entries.front().name == "latest.txt");

    ScanOptions failed_options;
    failed_options.error_injector = [&](const fs::path& path, const std::string& operation) {
        if (path == latest_root && operation == "directory_open")
            return std::make_error_code(std::errc::permission_denied);
        return std::error_code{};
    };
    const auto failed_generation = controller.start(latest_root, {}, 30, failed_options);
    const auto failed = controller.wait_until_idle();
    CHECK(failed.size() == 1);
    CHECK(failed.front().generation == failed_generation);
    CHECK(failed.front().result.status == ScanStatus::failed);
    CHECK(!failed.front().result.diagnostics.empty());
}

int main() {
    fs::create_directories(test_directory.path);
    test_cancelled_scan_does_not_commit_size_cache();
    test_isolated_pagination_does_not_touch_ui_state();
    test_newest_request_wins_and_worker_errors_are_returned();
    std::cout << "test_scan_controller: PASS\n";
    return 0;
}
