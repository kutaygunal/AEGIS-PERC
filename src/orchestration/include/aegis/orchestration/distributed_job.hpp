#pragma once

#include "aegis/orchestration/job_pipeline.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace aegis::orchestration {

/// Milliseconds since the Unix epoch (UTC).
///
/// Simplification vs. docs/design/distributed.md's generic "timestamps: created, started,
/// updated, finished" guidance: we store epoch milliseconds instead of ISO-8601 strings.
/// This keeps lease/heartbeat staleness comparisons (host reconciliation) trivial integer
/// arithmetic and avoids the gmtime_s/gmtime_r portability pitfalls already hit elsewhere in
/// this codebase (see orchestration/runs/run_2026-08-14_s2-001-declarative-condition-rules.md,
/// "Post-PR CI Fixups"). Values remain trivially convertible to a human-readable form later.
using TimestampMs = std::int64_t;

TimestampMs now_ms();

/// Durable, filesystem-serializable job record for the Phase 2 (localhost multi-process)
/// distributed execution model described in docs/design/distributed.md.
///
/// Field set follows the doc's "Job model" -> "Recommended stable fields" list:
///   job_id; project/package reference; state; stage; completed/total stage counts;
///   cancellation-requested flag; worker assignment; output artifact references; error
///   summary; timestamps (created/started/updated/finished). We add a small number of
///   retry-policy fields (`retry_count`, `last_failure_reason`, `heartbeat_at`) needed to
///   implement the doc's "stale-worker timeout recovery" / "Retry policy guidance" sections.
struct DistributedJob {
    std::string job_id;

    /// Project package handoff (docs/design/distributed.md "Project package handoff" +
    /// "Artifact handoff patterns" -> Option A: shared filesystem).
    ///
    /// `package_manifest_json` is the raw, versioned `ProjectPackage::to_manifest_json()`
    /// output -- not a live object reference -- so the job record is self-contained and
    /// durable on disk, matching the doc's "store the raw package manifest/versioned JSON"
    /// recommendation. `base_path` is a filesystem root that host and worker(s) both have
    /// access to (Option A: shared filesystem, the only pattern in scope for Phase 2); source
    /// artifact paths inside the manifest are resolved relative to it, exactly as
    /// `LocalJobPipeline` already does.
    std::string package_manifest_json;
    std::filesystem::path base_path;
    std::filesystem::path output_dir;
    JobPipelineOptions options;

    std::string project_name;

    /// Observable state -- deliberately mirrors `JobProgressSnapshot` so the worker can
    /// forward `LocalJobPipeline` progress callbacks into this record with a near-direct
    /// field copy (see distributed_worker.cpp).
    JobState state = JobState::Queued;
    JobStage stage = JobStage::None;
    std::size_t completed_stages = 0;
    std::size_t total_stages = 5;
    bool cancel_requested = false;
    std::string message;

    /// Distributed-specific fields.
    std::optional<std::string> worker_id;
    std::optional<std::filesystem::path> json_report_path;
    std::optional<std::filesystem::path> html_report_path;
    std::string error_summary;

    /// Retry policy bookkeeping (docs/design/distributed.md "Retry policy guidance" +
    /// "Failure modes" #2 and #5). Per the doc's MVP recommendation we only support full-job
    /// rerun on retry -- partial-stage resume is an explicit non-goal -- so a retry simply
    /// resets stage/completed_stages and returns the job to Queued with the same, immutable
    /// package_manifest_json/base_path inputs.
    int retry_count = 0;
    std::optional<std::string> last_failure_reason;

    /// Timestamps, epoch-ms (see TimestampMs above).
    TimestampMs created_at = 0;
    std::optional<TimestampMs> started_at;
    TimestampMs updated_at = 0;
    std::optional<TimestampMs> finished_at;
    /// Last time a worker actively touched this job (claim or in-progress stage update). Used
    /// by DistributedHost::reconcile_stale_leases() to detect a presumed worker crash.
    std::optional<TimestampMs> heartbeat_at;
};

/// Serialize/deserialize a DistributedJob to/from its on-disk JSON representation.
std::string to_json_text(const DistributedJob& job, int indent = 2);
DistributedJob distributed_job_from_json(const std::string& text);

/// Canonical on-disk path for a job record: `<jobs_dir>/<job_id>.json`.
std::filesystem::path job_file_path(const std::filesystem::path& jobs_dir, const std::string& job_id);

/// Atomic "temp file + rename" write, per docs/design/distributed.md's repeated guidance
/// ("temp files + atomic promote semantics") for failure-safe artifact/metadata export. Used
/// for every job-record write in the host and worker so a crash never leaves a torn/partial
/// job file behind -- readers only ever see the previous complete record or the new one.
void write_job_file_atomic(const std::filesystem::path& path, const DistributedJob& job);

/// Reads and parses a job record. Returns std::nullopt if the file does not exist or fails to
/// parse (e.g. a reader raced a writer's rename and briefly saw nothing -- callers should treat
/// this the same as "job not found yet" and retry on their own schedule).
std::optional<DistributedJob> read_job_file(const std::filesystem::path& path);

} // namespace aegis::orchestration
