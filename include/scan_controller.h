#pragma once

#include "file_utils.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

struct ScanCompletion {
    std::uint64_t generation = 0;
    std::filesystem::path root;
    std::set<std::filesystem::path> expanded_dirs;
    ScanResult result;
    double elapsed_ms = 0.0;
};

struct ScanActivity {
    bool busy = false;
    bool cancelling = false;
    std::uint64_t generation = 0;
    std::filesystem::path active_root;
    std::filesystem::path latest_root;
    ScanProgress progress;
    double elapsed_seconds = 0.0;
};

class ScanController {
public:
    ScanController() = default;
    ~ScanController();
    ScanController(const ScanController&) = delete;
    ScanController& operator=(const ScanController&) = delete;

    std::uint64_t start(const std::filesystem::path& root,
                        const std::set<std::filesystem::path>& expanded_dirs,
                        int page_size, ScanOptions options = {});
    bool cancel();
    bool busy() const;
    std::uint64_t latest_generation() const;
    ScanActivity activity() const;
    std::vector<ScanCompletion> poll();
    std::vector<ScanCompletion> wait_until_idle();

private:
    struct Request {
        std::uint64_t generation = 0;
        std::filesystem::path root;
        std::set<std::filesystem::path> expanded_dirs;
        int page_size = 30;
        ScanOptions options;
    };
    struct Job;

    void start_locked(Request request);

    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::shared_ptr<Job> active_;
    std::optional<Request> pending_;
    std::uint64_t latest_generation_ = 0;
};
