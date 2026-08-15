#include "aegis/orchestration/distributed_worker.hpp"

#include "aegis/orchestration/job_pipeline.hpp"
#include "aegis/storage/project_package.hpp"

#include <random>
#include <sstream>
#include <stdexcept>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace aegis::orchestration {
namespace {

std::string generate_worker_id()
{
    static std::random_device rd;
    static std::mt19937_64 engine(rd());
    std::uniform_int_distribution<std::uint32_t> dist;
    std::ostringstream out;
    out << "worker-" << now_ms() << "-" << dist(engine);
    return out.str();
}

// Exclusive file creation used to implement the job-claim lock (see try_claim_next() below).
// Deliberately implemented via the raw OS API (CreateFileW's CREATE_NEW / open()'s O_EXCL)
// rather than `std::fopen(path, "wx")`: the latter works on both MSVC's UCRT and glibc, but
// this project builds with warnings-as-errors and MSVC's secure-CRT checks flag `fopen` itself
// (C4996) regardless of mode string. Going straight to the platform API sidesteps that without
// disabling the warning project-wide, and documents the exact atomicity guarantee being relied
// on (both APIs fail atomically if the file already exists).
bool try_create_exclusive_file(const std::filesystem::path& path)
{
#if defined(_WIN32)
    HANDLE handle = ::CreateFileW(path.wstring().c_str(), GENERIC_WRITE, 0, nullptr,
                                   CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    ::CloseHandle(handle);
    return true;
#else
    const int fd = ::open(path.string().c_str(), O_CREAT | O_EXCL | O_WRONLY, 0644);
    if (fd < 0) {
        return false;
    }
    ::close(fd);
    return true;
#endif
}

} // namespace

DistributedWorker::DistributedWorker(Config config) : m_config(std::move(config))
{
    if (m_config.worker_id.empty()) {
        m_config.worker_id = generate_worker_id();
    }
}

DistributedWorker::~DistributedWorker()
{
    stop();
}

const std::string& DistributedWorker::worker_id() const noexcept
{
    return m_config.worker_id;
}

bool DistributedWorker::is_running() const noexcept
{
    return m_running.load();
}

std::optional<DistributedWorker::ClaimedJob> DistributedWorker::try_claim_next()
{
    std::error_code ec;
    if (!std::filesystem::exists(m_config.jobs_dir, ec)) {
        return std::nullopt;
    }

    for (const auto& entry : std::filesystem::directory_iterator(m_config.jobs_dir, ec)) {
        if (ec) {
            break;
        }
        if (!entry.is_regular_file() || entry.path().extension() != ".json") {
            continue;
        }

        const auto path = entry.path();
        auto peek = read_job_file(path);
        if (!peek.has_value() || peek->state != JobState::Queued) {
            continue;
        }

        // Atomic claim: exclusive-create a sidecar lock file. Exactly one concurrent caller
        // can succeed for a given path (see try_create_exclusive_file()), which is what gives
        // this its atomicity without introducing an external lock/queue dependency. This is
        // the "temp files + atomic promote semantics" guidance from docs/design/distributed.md
        // generalized to a claim step rather than just a final-artifact promote.
        const auto lock_path = path.string() + ".lock";
        if (!try_create_exclusive_file(lock_path)) {
            // Another worker is claiming (or already claimed) this job -- try the next file.
            continue;
        }

        // Re-read under the lock in case the state changed between the unlocked peek above and
        // acquiring the lock (e.g. a host cancellation raced us).
        auto locked_record = read_job_file(path);
        if (!locked_record.has_value() || locked_record->state != JobState::Queued) {
            std::error_code remove_ec;
            std::filesystem::remove(lock_path, remove_ec);
            continue;
        }

        const auto now = now_ms();
        locked_record->state = JobState::Running;
        locked_record->worker_id = m_config.worker_id;
        if (!locked_record->started_at.has_value()) {
            locked_record->started_at = now;
        }
        locked_record->updated_at = now;
        locked_record->heartbeat_at = now;
        locked_record->message = "Claimed by " + m_config.worker_id;

        write_job_file_atomic(path, *locked_record);

        std::error_code remove_ec;
        std::filesystem::remove(lock_path, remove_ec);

        // NOTE (known limitation, see run log): if this process crashes between the exclusive
        // lock-file creation above and the write_job_file_atomic() promote, the ".lock" file
        // is left orphaned and this job can never be claimed again by any worker. Sweeping
        // orphaned lock files is not implemented in this increment -- reconcile_stale_leases()
        // only covers jobs that reached Running before their worker died, not a crash during
        // the claim step itself. Left for a future increment.

        ClaimedJob claimed;
        claimed.job_id = locked_record->job_id.empty() ? path.stem().string() : locked_record->job_id;
        claimed.record = *locked_record;
        return claimed;
    }

    return std::nullopt;
}

void DistributedWorker::execute(ClaimedJob claimed)
{
    const auto path = job_file_path(m_config.jobs_dir, claimed.job_id);

    aegis::storage::ProjectPackage package;
    try {
        package = aegis::storage::ProjectPackage::from_manifest_json(claimed.record.package_manifest_json);
    } catch (const std::exception& ex) {
        DistributedJob failed = claimed.record;
        failed.state = JobState::Failed;
        failed.error_summary = std::string("Failed to reconstruct project package from job record: ") + ex.what();
        failed.updated_at = now_ms();
        failed.finished_at = failed.updated_at;
        failed.heartbeat_at = failed.updated_at;
        write_job_file_atomic(path, failed);
        return;
    }

    LocalJobPipeline pipeline;
    JobRequest request;
    request.package = std::move(package);
    request.base_path = claimed.record.base_path;
    request.output_dir = claimed.record.output_dir;
    request.options = claimed.record.options;

    // Bridges LocalJobPipeline's in-process progress callbacks to the on-disk job record
    // (heartbeat + stage) and, in the other direction, re-reads the on-disk record on every
    // callback to observe a host-issued cancellation (docs/design/distributed.md: "worker
    // observes it at stage boundaries"). Terminal states (Completed/Failed/Cancelled) are
    // intentionally NOT written from this callback -- see the comment below at pipeline.wait()
    // for why the authoritative terminal write happens once, after wait() returns.
    std::atomic<JobId> local_job_id{0};
    std::atomic<bool> submitted{false};

    request.progress_callback = [&](const JobProgressSnapshot& snap) {
        if (snap.state == JobState::Completed || snap.state == JobState::Failed ||
            snap.state == JobState::Cancelled) {
            return;
        }

        // Read the current on-disk record *before* building our update. A host-issued
        // cancellation is written directly to disk (DistributedHost::request_cancel()) and is
        // not something LocalJobPipeline's own snapshot (`snap`) can know about until we tell
        // it via pipeline.request_cancel() below -- so `snap.cancel_requested` alone always
        // lags one stage behind a fresh host request. If we built `updated` purely from
        // `claimed.record` (our last self-written copy) and `snap`, we would overwrite the
        // host's flag with our own stale "not yet cancelled" state and never notice the
        // request at all. OR-ing in the freshly-read disk value avoids that.
        const auto latest_on_disk = read_job_file(path);
        const bool host_wants_cancel = latest_on_disk.has_value() && latest_on_disk->cancel_requested;

        DistributedJob updated = claimed.record;
        updated.state = snap.state;
        updated.stage = snap.stage;
        updated.completed_stages = snap.completed_stages;
        updated.total_stages = snap.total_stages;
        updated.cancel_requested = snap.cancel_requested || host_wants_cancel;
        updated.message = snap.message;
        updated.updated_at = now_ms();
        updated.heartbeat_at = updated.updated_at;
        write_job_file_atomic(path, updated);
        claimed.record = updated;

        if (submitted.load() && host_wants_cancel) {
            pipeline.request_cancel(local_job_id.load());
        }
    };

    const JobId submitted_id = pipeline.submit(std::move(request));
    local_job_id.store(submitted_id);
    submitted.store(true);

    // Cover the narrow window where a host cancellation could have been requested and written
    // to disk between the claim (try_claim_next) and this point, before any progress callback
    // has had a chance to observe it.
    if (const auto latest = read_job_file(path); latest.has_value() && latest->cancel_requested) {
        pipeline.request_cancel(submitted_id);
    }

    // Deliberately generous: this increment has no separate worker-side stage timeout, only
    // the host-side lease/heartbeat timeout (reconcile_stale_leases()). A genuinely hung stage
    // is expected to be caught by the host, not by the worker waiting on itself.
    pipeline.wait(submitted_id, std::chrono::minutes(30));

    // The authoritative terminal write happens here, once, from LocalJobPipeline::result()
    // rather than from the progress callback above. LocalJobPipeline sets its internal
    // "finished" flag (which wait() waits on) *before* invoking the terminal progress
    // callback, so a callback-based terminal write could race wait() returning. Reading
    // result() after wait() returns true is documented (job_pipeline.hpp) to be available at
    // that point, so this avoids the race entirely.
    const auto result = pipeline.result(submitted_id);

    DistributedJob final_record = claimed.record;
    final_record.updated_at = now_ms();
    final_record.finished_at = final_record.updated_at;
    final_record.heartbeat_at = final_record.updated_at;

    if (result.has_value()) {
        final_record.state = result->state;
        final_record.json_report_path = result->json_report_path;
        final_record.html_report_path = result->html_report_path;
        if (result->state == JobState::Completed) {
            final_record.completed_stages = final_record.total_stages;
            final_record.stage = JobStage::ReportExport;
            final_record.message = "Job completed successfully";
        } else if (result->state == JobState::Failed) {
            final_record.error_summary = result->error_message;
            final_record.message = "Job failed: " + result->error_message;
        } else if (result->state == JobState::Cancelled) {
            final_record.message = "Job cancelled";
        }
    } else {
        final_record.state = JobState::Failed;
        final_record.error_summary = "Local pipeline did not report a result after wait() returned";
    }

    write_job_file_atomic(path, final_record);
}

std::optional<std::string> DistributedWorker::run_once()
{
    auto claimed = try_claim_next();
    if (!claimed.has_value()) {
        return std::nullopt;
    }
    const std::string job_id = claimed->job_id;
    execute(std::move(*claimed));
    return job_id;
}

void DistributedWorker::start()
{
    if (m_running.exchange(true)) {
        return;
    }
    m_stop_requested.store(false);
    m_loop_thread = std::thread([this]() {
        while (!m_stop_requested.load()) {
            const auto claimed = run_once();
            if (!claimed.has_value()) {
                std::this_thread::sleep_for(m_config.poll_interval);
            }
        }
        m_running.store(false);
    });
}

void DistributedWorker::stop()
{
    m_stop_requested.store(true);
    if (m_loop_thread.joinable()) {
        m_loop_thread.join();
    }
}

} // namespace aegis::orchestration
