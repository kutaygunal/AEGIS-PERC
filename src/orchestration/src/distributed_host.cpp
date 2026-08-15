#include "aegis/orchestration/distributed_host.hpp"

#include "aegis/storage/import_validation.hpp"

#include <atomic>
#include <sstream>
#include <utility>

namespace aegis::orchestration {
namespace {

std::string generate_job_id()
{
    // Process-local uniqueness (epoch-ms + monotonic counter) is sufficient for Phase 2
    // (single host process, localhost only). Multi-host-safe ID generation (e.g. host
    // identity prefix, UUID) is deferred to Phase 4 (multi-node), per
    // docs/design/distributed.md's phased scope -- not needed until there is more than one
    // host process sharing a jobs directory.
    static std::atomic<std::uint64_t> counter{0};
    std::ostringstream out;
    out << "job-" << now_ms() << "-" << counter.fetch_add(1);
    return out.str();
}

} // namespace

DistributedHost::DistributedHost(Config config) : m_config(std::move(config)) {}

const DistributedHost::Config& DistributedHost::config() const noexcept
{
    return m_config;
}

DistributedSubmitResult DistributedHost::submit(DistributedSubmission submission)
{
    DistributedSubmitResult result;

    aegis::storage::ImportPreflightValidator validator;
    const auto diagnostics = validator.validate(submission.package);
    const auto status = validator.derive_status(diagnostics);

    submission.package.diagnostics() = diagnostics;
    submission.package.set_validation_status(status);
    result.diagnostics = diagnostics;

    if (status == aegis::storage::ValidationStatus::Invalid) {
        result.accepted = false;
        result.rejection_reason = "Import preflight validation failed; job was not queued";
        return result;
    }

    submission.package.rebuild_normalized_view();

    DistributedJob job;
    job.job_id = generate_job_id();
    job.package_manifest_json = submission.package.to_manifest_json();
    job.base_path = submission.base_path;
    job.output_dir = submission.output_dir;
    job.options = submission.options;
    job.project_name = submission.package.project().name;

    job.state = JobState::Queued;
    job.stage = JobStage::None;
    job.completed_stages = 0;
    job.total_stages = 5;
    job.cancel_requested = false;
    job.message = "Queued";

    const auto now = now_ms();
    job.created_at = now;
    job.updated_at = now;

    write_job_file_atomic(job_file_path(m_config.jobs_dir, job.job_id), job);

    result.accepted = true;
    result.job_id = job.job_id;
    return result;
}

bool DistributedHost::request_cancel(const std::string& job_id)
{
    const auto path = job_file_path(m_config.jobs_dir, job_id);
    auto record = read_job_file(path);
    if (!record.has_value()) {
        return false;
    }

    if (record->state == JobState::Completed || record->state == JobState::Failed ||
        record->state == JobState::Cancelled) {
        return false;
    }

    record->cancel_requested = true;
    record->updated_at = now_ms();

    if (record->state == JobState::Queued) {
        // No worker has claimed this job yet, so the host can cancel it directly rather than
        // waiting for a worker to observe the flag.
        record->state = JobState::Cancelled;
        record->finished_at = record->updated_at;
        record->message = "Cancelled before a worker claimed the job";
    } else {
        record->state = JobState::Cancelling;
        record->message = "Cancellation requested; waiting for worker to observe it at a stage boundary";
    }

    write_job_file_atomic(path, *record);
    return true;
}

std::optional<DistributedJob> DistributedHost::snapshot(const std::string& job_id) const
{
    return read_job_file(job_file_path(m_config.jobs_dir, job_id));
}

std::vector<DistributedJob> DistributedHost::list_jobs() const
{
    std::vector<DistributedJob> jobs;
    std::error_code ec;
    if (!std::filesystem::exists(m_config.jobs_dir, ec)) {
        return jobs;
    }
    for (const auto& entry : std::filesystem::directory_iterator(m_config.jobs_dir, ec)) {
        if (ec) {
            break;
        }
        if (!entry.is_regular_file() || entry.path().extension() != ".json") {
            continue;
        }
        if (auto record = read_job_file(entry.path()); record.has_value()) {
            jobs.push_back(std::move(*record));
        }
    }
    return jobs;
}

std::size_t DistributedHost::reconcile_stale_leases()
{
    std::size_t reconciled = 0;
    std::error_code ec;
    if (!std::filesystem::exists(m_config.jobs_dir, ec)) {
        return 0;
    }

    for (const auto& entry : std::filesystem::directory_iterator(m_config.jobs_dir, ec)) {
        if (ec) {
            break;
        }
        if (!entry.is_regular_file() || entry.path().extension() != ".json") {
            continue;
        }

        auto record = read_job_file(entry.path());
        if (!record.has_value()) {
            continue;
        }
        if (record->state != JobState::Running && record->state != JobState::Cancelling) {
            continue;
        }

        const TimestampMs last_beat = record->heartbeat_at.value_or(
            record->started_at.value_or(record->updated_at));
        const TimestampMs age = now_ms() - last_beat;
        if (age < static_cast<TimestampMs>(m_config.lease_timeout.count())) {
            continue;
        }

        // Stale lease past the configured timeout: presume the worker crashed
        // (docs/design/distributed.md, Failure modes -> "2. Worker crash during execution").
        ++record->retry_count;
        record->last_failure_reason =
            "stale heartbeat (worker presumed crashed), age_ms=" + std::to_string(age);
        record->worker_id.reset();
        record->started_at.reset();
        record->heartbeat_at.reset();
        record->updated_at = now_ms();

        if (record->cancel_requested) {
            // A cancellation was already in flight when the worker died -- honor it instead
            // of retrying (docs/design/distributed.md, Failure modes -> "4. Cancellation":
            // terminal state becomes cancelled, not failed).
            record->state = JobState::Cancelled;
            record->finished_at = record->updated_at;
            record->message = "Cancelled: worker crashed after cancellation had been requested";
        } else if (record->retry_count > m_config.max_retries) {
            record->state = JobState::Failed;
            record->error_summary = "Exceeded max retries (" + std::to_string(m_config.max_retries) +
                                    ") after repeated stale-worker timeouts";
            record->finished_at = record->updated_at;
        } else {
            // Full-job rerun on retry, not partial-stage resume -- explicit non-goal per
            // docs/design/distributed.md, Failure modes -> "5. Retry after transient failure"
            // and "6. Resumable reporting" MVP recommendation. package_manifest_json/base_path
            // are untouched, so the retry re-executes from the same immutable inputs.
            record->state = JobState::Queued;
            record->stage = JobStage::None;
            record->completed_stages = 0;
            record->message = "Requeued after stale worker heartbeat (retry " +
                              std::to_string(record->retry_count) + ")";
        }

        write_job_file_atomic(entry.path(), *record);
        ++reconciled;
    }

    return reconciled;
}

} // namespace aegis::orchestration
