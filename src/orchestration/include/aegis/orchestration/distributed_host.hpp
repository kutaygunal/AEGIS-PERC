#pragma once

#include "aegis/orchestration/distributed_job.hpp"
#include "aegis/orchestration/job_pipeline.hpp"
#include "aegis/storage/project_package.hpp"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace aegis::orchestration {

/// A project ready for distributed execution: the same inputs `LocalJobPipeline::submit()`
/// takes, plus the shared-filesystem root the host and worker(s) both resolve artifacts
/// against (docs/design/distributed.md, Artifact handoff patterns -> Option A).
struct DistributedSubmission {
    aegis::storage::ProjectPackage package;
    std::filesystem::path base_path;
    std::filesystem::path output_dir;
    JobPipelineOptions options;
};

struct DistributedSubmitResult {
    bool accepted = false;
    std::optional<std::string> job_id;
    std::vector<aegis::storage::ImportDiagnostic> diagnostics;
    std::string rejection_reason;
};

/// Host (control plane) for Phase 2 of docs/design/distributed.md: "localhost multi-process --
/// split host process from worker process, keep shared filesystem artifact handoff, add
/// lease/heartbeat and durable job metadata".
///
/// This class is intentionally stateless in-process: every operation reads/writes the job
/// record file(s) under `jobs_dir` directly rather than caching state in memory, so job
/// metadata survives a host restart (durable job metadata, per the doc's Phase 2 goal) without
/// needing a database. This is a deliberate simplification appropriate for a first increment;
/// see docs/design/distributed.md Phase 3/4 for where a real store would replace this.
class DistributedHost {
public:
    struct Config {
        std::filesystem::path jobs_dir;
        /// How long a Running job's heartbeat may go stale before reconcile_stale_leases()
        /// treats the worker as crashed and requeues (or fails) the job. See
        /// docs/design/distributed.md, Failure modes -> "2. Worker crash during execution".
        std::chrono::milliseconds lease_timeout{std::chrono::seconds(30)};
        /// Cap on stale-lease retries before a job is marked Failed, per the doc's "Retry
        /// policy guidance" ("cap retries with exponential backoff" -- this increment caps by
        /// count only; backoff scheduling is left to a future increment, see run log).
        int max_retries = 2;
    };

    explicit DistributedHost(Config config);

    /// Runs import preflight validation (reusing `aegis::storage::ImportPreflightValidator`,
    /// not reinventing it -- docs/design/distributed.md, Host responsibilities: "run import
    /// preflight using the same ProjectPackage + validator model") and, if the package
    /// validates, durably enqueues a new job record. If validation fails, no job is created
    /// (doc: "host should fail submission before queueing if required artifacts are absent").
    DistributedSubmitResult submit(DistributedSubmission submission);

    /// Requests cancellation of a job. A still-unclaimed (Queued) job is cancelled immediately
    /// since no worker owns it yet; a Running job is marked Cancelling so the worker can
    /// observe the request at its next stage boundary (docs/design/distributed.md, Failure
    /// modes -> "4. Cancellation"). Returns false if the job is unknown or already terminal.
    bool request_cancel(const std::string& job_id);

    std::optional<DistributedJob> snapshot(const std::string& job_id) const;
    std::vector<DistributedJob> list_jobs() const;

    /// Scans jobs_dir for Running/Cancelling jobs whose heartbeat has exceeded
    /// Config::lease_timeout and treats them as a presumed worker crash: requeues for a
    /// full-job retry (docs/design/distributed.md explicitly recommends full-job rerun over
    /// partial-stage resume for the MVP) up to Config::max_retries, after which the job is
    /// marked Failed. Returns the number of jobs reconciled. Intended to be called
    /// periodically by whatever process embeds the host (this increment does not itself spin
    /// up a background reconciliation thread -- see run log "Known limitations").
    std::size_t reconcile_stale_leases();

    const Config& config() const noexcept;

private:
    Config m_config;
};

} // namespace aegis::orchestration
