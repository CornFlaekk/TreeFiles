#include "scan_controller.h"
#include <system_error>
#include <utility>

struct ScanController::Job {
    explicit Job(Request scan_request)
        : request(std::move(scan_request)), cancel_requested(std::make_shared<std::atomic<bool>>(false)),
          started(std::chrono::steady_clock::now()) {}

    Request request;
    std::shared_ptr<std::atomic<bool>> cancel_requested;
    mutable std::mutex progress_mutex;
    ScanProgress progress;
    std::chrono::steady_clock::time_point started;
    std::atomic<bool> done{false};
    std::optional<ScanResult> result;
    double elapsed_ms = 0.0;
    std::thread worker;
};

ScanController::~ScanController() {
    cancel();
    (void)wait_until_idle();
}

std::uint64_t ScanController::start(const std::filesystem::path& root,
                                    const std::set<std::filesystem::path>& expanded_dirs,
                                    int page_size, ScanOptions options) {
    std::lock_guard<std::mutex> lock(mutex_);
    Request request;
    request.generation = ++latest_generation_;
    request.root = root;
    request.expanded_dirs = expanded_dirs;
    request.page_size = page_size;
    request.options = std::move(options);
    request.options.defer_cache_updates = true;
    request.options.isolated_pagination = true;

    if (active_) {
        active_->cancel_requested->store(true, std::memory_order_release);
        pending_ = std::move(request);
    } else {
        start_locked(std::move(request));
    }
    return latest_generation_;
}

void ScanController::start_locked(Request request) {
    auto job = std::make_shared<Job>(std::move(request));
    const auto user_cancelled = std::move(job->request.options.is_cancelled);
    const auto user_progress = std::move(job->request.options.progress_callback);
    const auto cancel_flag = job->cancel_requested;
    std::weak_ptr<Job> weak_job = job;
    job->request.options.is_cancelled = [cancel_flag, user_cancelled]() {
        return cancel_flag->load(std::memory_order_acquire) ||
               (user_cancelled && user_cancelled());
    };
    job->request.options.progress_callback = [weak_job, user_progress](const ScanProgress& progress) {
        if (const auto current = weak_job.lock()) {
            std::lock_guard<std::mutex> lock(current->progress_mutex);
            current->progress = progress;
        }
        if (user_progress) user_progress(progress);
    };

    active_ = job;
    auto run_worker = [this, job]() {
        const auto started = std::chrono::steady_clock::now();
        ScanResult result;
        try {
            result = scan_tree_entries(job->request.root, job->request.expanded_dirs,
                                       job->request.page_size, job->request.options);
        } catch (const std::filesystem::filesystem_error& error) {
            result.status = ScanStatus::failed;
            result.diagnostics.push_back({error.path1().empty() ? job->request.root : error.path1(),
                                          "scan", error.code()});
        } catch (...) {
            result.status = ScanStatus::failed;
            result.diagnostics.push_back({job->request.root, "scan",
                std::make_error_code(std::errc::io_error)});
        }
        job->elapsed_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
        job->result.emplace(std::move(result));
        job->done.store(true, std::memory_order_release);
        changed_.notify_all();
    };
    try {
        job->worker = std::thread(std::move(run_worker));
    } catch (const std::system_error& error) {
        ScanResult result;
        result.status = ScanStatus::failed;
        result.diagnostics.push_back({job->request.root, "start_worker", error.code()});
        job->elapsed_ms = 0.0;
        job->result.emplace(std::move(result));
        job->done.store(true, std::memory_order_release);
        changed_.notify_all();
    }
}

bool ScanController::cancel() {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool had_work = static_cast<bool>(active_) || pending_.has_value();
    if (!had_work) return false;
    ++latest_generation_;
    pending_.reset();
    if (active_) active_->cancel_requested->store(true, std::memory_order_release);
    return true;
}

bool ScanController::busy() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<bool>(active_) || pending_.has_value();
}

std::uint64_t ScanController::latest_generation() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return latest_generation_;
}

ScanActivity ScanController::activity() const {
    std::lock_guard<std::mutex> lock(mutex_);
    ScanActivity activity;
    activity.busy = static_cast<bool>(active_) || pending_.has_value();
    activity.generation = latest_generation_;
    if (active_) {
        activity.active_root = active_->request.root;
        activity.cancelling = active_->cancel_requested->load(std::memory_order_acquire);
        {
            std::lock_guard<std::mutex> progress_lock(active_->progress_mutex);
            activity.progress = active_->progress;
        }
        activity.elapsed_seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - active_->started).count();
    }
    if (pending_) activity.latest_root = pending_->root;
    else if (active_) activity.latest_root = active_->request.root;
    return activity;
}

std::vector<ScanCompletion> ScanController::poll() {
    std::shared_ptr<Job> finished;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!active_ || !active_->done.load(std::memory_order_acquire)) return {};
        finished = std::move(active_);
        if (pending_) {
            Request next = std::move(*pending_);
            pending_.reset();
            start_locked(std::move(next));
        }
    }

    if (finished->worker.joinable()) finished->worker.join();
    ScanCompletion completion;
    completion.generation = finished->request.generation;
    completion.root = std::move(finished->request.root);
    completion.expanded_dirs = std::move(finished->request.expanded_dirs);
    completion.elapsed_ms = finished->elapsed_ms;
    if (finished->result) completion.result = std::move(*finished->result);
    return {std::move(completion)};
}

std::vector<ScanCompletion> ScanController::wait_until_idle() {
    std::vector<ScanCompletion> completions;
    while (true) {
        auto finished = poll();
        for (auto& completion : finished) completions.push_back(std::move(completion));

        std::unique_lock<std::mutex> lock(mutex_);
        if (!active_ && !pending_) break;
        changed_.wait(lock, [&]() {
            return !active_ || active_->done.load(std::memory_order_acquire);
        });
    }
    return completions;
}
