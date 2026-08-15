#pragma once

#include "aegis/orchestration/distributed_job.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <thread>

namespace aegis::orchestration {

/// Worker (data plane) for Phase 2 of docs/design/distributed.md.
///
/// Process model choice for this first increment: the worker runs as a background
/// `std::thread` inside the same process as its caller (typically a test, or a CLI/host
/// wrapper), rather than as a genuinely separate OS process. It communicates with the host
/// *only* through the filesystem job-queue contract in distributed_job.hpp -- it never touches
/// a `DistributedHost` instance or any other in-process host state directly. This keeps the
/// choice reversible: a future increment can replace this thread with a real spawned worker
/// executable (`std::system`/`CreateProcess`/`posix_spawn`) with zero changes to the on-disk
/// protocol or to `DistributedHost`, because the worker already behaves as if the host were a
/// separate process. A real separate-process worker executable was not built in this increment
/// because it would add platform-specific process-spawning code (Windows vs. POSIX) and a new
/// CLI entry point for a benefit -- exercising a second OS process -- that the filesystem-only
/// contract already lets us validate without it. See the run log for the full rationale.
///
/// Execution reuses `LocalJobPipeline` internally for the actual parse/normalize/graph_build/
/// rule_execution/report_export staged work (docs/design/distributed.md: "Workers should
/// execute the same core libraries used locally. They must not fork independent parsing or
/// rule semantics.") -- this class only bridges `LocalJobPipeline`'s progress callbacks and
/// terminal result to the on-disk `DistributedJob` record, and reads the record between
/// callbacks to detect host-issued cancellation.
class DistributedWorker {
public:
    struct Config {
        std::filesystem::path jobs_dir;
        /// Identifier recorded into a claimed job's `worker_id` field. Auto-generated
        /// (hostname-independent -- Phase 2 is localhost-only) if left empty.
        std::string worker_id;
        /// Sleep between empty polls in the background loop (start()/stop()). Not used by the
        /// synchronous run_once() entry point.
        std::chrono::milliseconds poll_interval{50};
    };

    explicit DistributedWorker(Config config);
    ~DistributedWorker();

    DistributedWorker(const DistributedWorker&) = delete;
    DistributedWorker& operator=(const DistributedWorker&) = delete;

    /// Synchronously attempts to claim exactly one Queued job from `jobs_dir` and run it to a
    /// terminal state (Completed/Failed/Cancelled), writing every intermediate and final
    /// update back to the job's file. Returns the claimed job's id, or std::nullopt if no
    /// Queued job was available to claim. This is the primary, directly-testable entry point
    /// -- tests call it instead of dealing with start()/stop() background-thread timing.
    std::optional<std::string> run_once();

    /// Runs a background loop on its own thread that repeatedly calls run_once(), sleeping
    /// poll_interval between empty polls, until stop() is called. Simulates a standalone
    /// worker process while staying in-process for this increment (see class comment above).
    void start();
    void stop();
    bool is_running() const noexcept;

    const std::string& worker_id() const noexcept;

private:
    struct ClaimedJob {
        std::string job_id;
        DistributedJob record;
    };

    std::optional<ClaimedJob> try_claim_next();
    void execute(ClaimedJob claimed);

    Config m_config;
    std::atomic<bool> m_stop_requested{false};
    std::atomic<bool> m_running{false};
    std::thread m_loop_thread;
};

} // namespace aegis::orchestration
