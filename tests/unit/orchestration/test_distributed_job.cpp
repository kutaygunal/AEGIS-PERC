#include <catch2/catch_test_macros.hpp>

#include "aegis/orchestration/distributed_host.hpp"
#include "aegis/orchestration/distributed_job.hpp"
#include "aegis/orchestration/distributed_worker.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <thread>

namespace fs = std::filesystem;
using namespace aegis::orchestration;
using namespace aegis::storage;

namespace {

fs::path make_temp_dir()
{
    return fs::temp_directory_path() /
           ("aegis_distributed_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
}

void write_file(const fs::path& path, const std::string& content)
{
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << content;
}

void remove_tree(const fs::path& root)
{
    std::error_code ec;
    fs::remove_all(root, ec);
}

std::string read_file(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

ProjectPackage make_manifest_package(const fs::path& root)
{
    write_file(root / "layout/tech.lef", "VERSION 5.8 ;\nLAYER M1\n  TYPE ROUTING ;\n  WIDTH 0.10 ;\n  PITCH 0.20 ;\n  DIRECTION HORIZONTAL ;\nEND M1\nEND LIBRARY\n");
    write_file(root / "layout/top.def", "VERSION 5.8 ;\nDESIGN top ;\n");
    write_file(root / "netlist/top.v", "module top(input A, output Y); endmodule\n");
    write_file(root / "rules/rules.yaml",
               "rules:\n"
               "  - id: EM_CURRENT_LIMIT\n"
               "    type: em_current_limit\n"
               "    severity: medium\n"
               "    parameters:\n"
               "      M4_max_mA: 40\n");
    write_file(root / "reports/current.csv",
               "net_name,current_mA,voltage_domain,layer\n"
               "VDD_CPU,52.4,CORE_0V8,M4\n");

    const std::string manifest =
        "{\n"
        "  \"manifest_version\": 1,\n"
        "  \"project\": {\n"
        "    \"name\": \"FalconCPU\"\n"
        "  },\n"
        "  \"artifacts\": {\n"
        "    \"technology\": [{\"id\": \"tech-1\", \"path\": \"layout/tech.lef\", \"role\": \"lef\"}],\n"
        "    \"layout\": [{\"id\": \"layout-1\", \"path\": \"layout/top.def\", \"role\": \"def\"}],\n"
        "    \"netlist\": [{\"id\": \"netlist-1\", \"path\": \"netlist/top.v\", \"role\": \"verilog\"}],\n"
        "    \"rules\": [{\"id\": \"rules-1\", \"path\": \"rules/rules.yaml\", \"role\": \"aegis_rule_pack\"}],\n"
        "    \"power\": [],\n"
        "    \"current\": [{\"id\": \"current-1\", \"path\": \"reports/current.csv\", \"role\": \"current_csv\", \"optional\": true}],\n"
        "    \"waivers\": [],\n"
        "    \"external_reports\": []\n"
        "  },\n"
        "  \"validation\": {\n"
        "    \"status\": \"valid\"\n"
        "  }\n"
        "}\n";

    return ProjectPackage::from_manifest_json(manifest);
}

} // namespace

TEST_CASE("DistributedHost submit queues a durable job record that a DistributedWorker can claim and run to completion",
          "[orchestration][s3-003][Distributed]")
{
    const fs::path root = make_temp_dir();
    const ProjectPackage package = make_manifest_package(root);
    const fs::path jobs_dir = root / "jobs";

    DistributedHost::Config host_config;
    host_config.jobs_dir = jobs_dir;
    DistributedHost host(host_config);

    DistributedSubmission submission;
    submission.package = package;
    submission.base_path = root;
    submission.output_dir = root / "out";
    submission.options.artificial_stage_delay = std::chrono::milliseconds(60);

    const auto submit_result = host.submit(submission);
    REQUIRE(submit_result.accepted);
    REQUIRE(submit_result.job_id.has_value());
    const std::string job_id = *submit_result.job_id;

    // The job record is durable on disk immediately after submit(), before any worker exists.
    REQUIRE(fs::exists(job_file_path(jobs_dir, job_id)));
    {
        const auto queued_snapshot = host.snapshot(job_id);
        REQUIRE(queued_snapshot.has_value());
        REQUIRE(queued_snapshot->state == JobState::Queued);
        REQUIRE_FALSE(queued_snapshot->worker_id.has_value());
    }

    DistributedWorker::Config worker_config;
    worker_config.jobs_dir = jobs_dir;
    worker_config.worker_id = "worker-test-1";
    DistributedWorker worker(worker_config);

    std::optional<std::string> claimed_id;
    std::thread run_thread([&]() { claimed_id = worker.run_once(); });

    // Poll briefly for the claim to become visible (state Running, worker_id assigned) while
    // the artificial stage delay keeps the job in flight.
    bool observed_claim = false;
    for (int i = 0; i < 200 && !observed_claim; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        const auto snap = host.snapshot(job_id);
        if (snap.has_value() && snap->state == JobState::Running && snap->worker_id.has_value()) {
            observed_claim = true;
            REQUIRE(*snap->worker_id == "worker-test-1");
        }
    }
    REQUIRE(observed_claim);

    run_thread.join();
    REQUIRE(claimed_id.has_value());
    REQUIRE(*claimed_id == job_id);

    const auto final_snapshot = host.snapshot(job_id);
    REQUIRE(final_snapshot.has_value());
    REQUIRE(final_snapshot->state == JobState::Completed);
    REQUIRE(final_snapshot->json_report_path.has_value());
    REQUIRE(final_snapshot->html_report_path.has_value());
    REQUIRE(fs::exists(*final_snapshot->json_report_path));
    REQUIRE(fs::exists(*final_snapshot->html_report_path));

    remove_tree(root);
}

TEST_CASE("DistributedWorker::run_once completes a queued job, producing the expected terminal state and output references",
          "[orchestration][s3-003][Distributed]")
{
    const fs::path root = make_temp_dir();
    const ProjectPackage package = make_manifest_package(root);
    const fs::path jobs_dir = root / "jobs";

    DistributedHost::Config host_config;
    host_config.jobs_dir = jobs_dir;
    DistributedHost host(host_config);

    DistributedSubmission submission;
    submission.package = package;
    submission.base_path = root;
    submission.output_dir = root / "out";

    const auto submit_result = host.submit(submission);
    REQUIRE(submit_result.accepted);
    const std::string job_id = *submit_result.job_id;

    DistributedWorker::Config worker_config;
    worker_config.jobs_dir = jobs_dir;
    worker_config.worker_id = "worker-a";
    DistributedWorker worker(worker_config);

    const auto claimed = worker.run_once();
    REQUIRE(claimed.has_value());
    REQUIRE(*claimed == job_id);

    const auto snapshot = host.snapshot(job_id);
    REQUIRE(snapshot.has_value());
    REQUIRE(snapshot->state == JobState::Completed);
    REQUIRE(snapshot->completed_stages == snapshot->total_stages);
    REQUIRE(snapshot->json_report_path.has_value());
    REQUIRE(snapshot->html_report_path.has_value());

    const std::string json_report = read_file(*snapshot->json_report_path);
    REQUIRE(json_report.find("\"violation_count\": 1") != std::string::npos);
    REQUIRE(json_report.find("\"rule_id\": \"EM_CURRENT_LIMIT\"") != std::string::npos);

    // No more queued work is left for a second worker to claim.
    DistributedWorker::Config worker2_config;
    worker2_config.jobs_dir = jobs_dir;
    worker2_config.worker_id = "worker-b";
    DistributedWorker worker2(worker2_config);
    REQUIRE_FALSE(worker2.run_once().has_value());

    remove_tree(root);
}

TEST_CASE("Cancellation requested mid-job reaches Cancelled, not Failed, and avoids partial output artifacts",
          "[orchestration][s3-003][Distributed]")
{
    const fs::path root = make_temp_dir();
    const ProjectPackage package = make_manifest_package(root);
    const fs::path jobs_dir = root / "jobs";

    DistributedHost::Config host_config;
    host_config.jobs_dir = jobs_dir;
    DistributedHost host(host_config);

    DistributedSubmission submission;
    submission.package = package;
    submission.base_path = root;
    submission.output_dir = root / "cancelled_out";
    submission.options.artificial_stage_delay = std::chrono::milliseconds(250);

    const auto submit_result = host.submit(submission);
    REQUIRE(submit_result.accepted);
    const std::string job_id = *submit_result.job_id;

    DistributedWorker::Config worker_config;
    worker_config.jobs_dir = jobs_dir;
    worker_config.worker_id = "worker-cancel";
    DistributedWorker worker(worker_config);

    std::thread run_thread([&]() { worker.run_once(); });

    // Give the worker time to claim the job and enter its first stage before cancelling. The
    // 250ms per-stage artificial delay above leaves a wide margin against the 50ms wait here so
    // the cancel request reliably lands before the worker's next stage-boundary check.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    REQUIRE(host.request_cancel(job_id));

    run_thread.join();

    const auto snapshot = host.snapshot(job_id);
    REQUIRE(snapshot.has_value());
    REQUIRE(snapshot->state == JobState::Cancelled);
    REQUIRE(snapshot->state != JobState::Failed);
    REQUIRE(snapshot->cancel_requested);
    REQUIRE_FALSE(snapshot->json_report_path.has_value());
    REQUIRE_FALSE(snapshot->html_report_path.has_value());

    REQUIRE_FALSE(fs::exists(root / "cancelled_out" / "report.json"));
    REQUIRE_FALSE(fs::exists(root / "cancelled_out" / "report.html"));

    remove_tree(root);
}

TEST_CASE("A stale/missing worker heartbeat is detected by host reconciliation and the job becomes retry-eligible, "
          "failing once retries are exhausted",
          "[orchestration][s3-003][Distributed]")
{
    const fs::path root = make_temp_dir();
    const ProjectPackage package = make_manifest_package(root);
    const fs::path jobs_dir = root / "jobs";

    DistributedHost::Config host_config;
    host_config.jobs_dir = jobs_dir;
    // Simulated stale heartbeats below are backdated by 100 seconds, so any timeout well under
    // that reliably counts as "stale". A generous (not razor-thin) threshold here also avoids
    // flaking the "healthy job is left untouched" check further down, whose heartbeat is set to
    // "now" immediately before reconcile_stale_leases() runs -- a too-tight timeout could be
    // exceeded purely by the disk I/O latency of the write+reconcile-scan themselves.
    host_config.lease_timeout = std::chrono::milliseconds(2000);
    host_config.max_retries = 1;
    DistributedHost host(host_config);

    DistributedSubmission submission;
    submission.package = package;
    submission.base_path = root;
    submission.output_dir = root / "out";

    const auto submit_result = host.submit(submission);
    REQUIRE(submit_result.accepted);
    const std::string job_id = *submit_result.job_id;
    const auto path = job_file_path(jobs_dir, job_id);

    // Simulate a worker that claimed the job and then crashed: state Running, heartbeat far in
    // the past relative to the (short, test-only) lease_timeout above.
    auto claim_and_go_stale = [&]() {
        auto record = read_job_file(path);
        REQUIRE(record.has_value());
        record->state = JobState::Running;
        record->worker_id = "crashed-worker";
        record->started_at = now_ms() - 100000;
        record->heartbeat_at = now_ms() - 100000;
        write_job_file_atomic(path, *record);
    };

    claim_and_go_stale();
    REQUIRE(host.reconcile_stale_leases() == 1);
    {
        const auto record = host.snapshot(job_id);
        REQUIRE(record.has_value());
        REQUIRE(record->state == JobState::Queued);
        REQUIRE(record->retry_count == 1);
        REQUIRE_FALSE(record->worker_id.has_value());
        REQUIRE(record->last_failure_reason.has_value());
    }

    // A second consecutive "crash" exceeds max_retries (1) and the job should be failed rather
    // than requeued forever.
    claim_and_go_stale();
    REQUIRE(host.reconcile_stale_leases() == 1);
    {
        const auto record = host.snapshot(job_id);
        REQUIRE(record.has_value());
        REQUIRE(record->state == JobState::Failed);
        REQUIRE(record->retry_count == 2);
        REQUIRE_FALSE(record->error_summary.empty());
    }

    // A healthy (non-stale) Running job is left untouched.
    DistributedSubmission healthy_submission = submission;
    healthy_submission.output_dir = root / "healthy_out";
    const auto healthy_submit = host.submit(healthy_submission);
    REQUIRE(healthy_submit.accepted);
    const auto healthy_path = job_file_path(jobs_dir, *healthy_submit.job_id);
    auto healthy_record = read_job_file(healthy_path);
    REQUIRE(healthy_record.has_value());
    healthy_record->state = JobState::Running;
    healthy_record->worker_id = "alive-worker";
    healthy_record->started_at = now_ms();
    healthy_record->heartbeat_at = now_ms();
    write_job_file_atomic(healthy_path, *healthy_record);

    REQUIRE(host.reconcile_stale_leases() == 0);
    const auto still_running = host.snapshot(*healthy_submit.job_id);
    REQUIRE(still_running.has_value());
    REQUIRE(still_running->state == JobState::Running);

    remove_tree(root);
}

TEST_CASE("A retried job (after a simulated worker crash) produces output byte-identical to a fresh run of the same inputs",
          "[orchestration][s3-003][Distributed]")
{
    const fs::path root = make_temp_dir();
    const ProjectPackage package = make_manifest_package(root);
    const fs::path jobs_dir = root / "jobs";

    DistributedHost::Config host_config;
    host_config.jobs_dir = jobs_dir;
    host_config.lease_timeout = std::chrono::milliseconds(10);
    host_config.max_retries = 3;
    DistributedHost host(host_config);

    // Reference run: a fresh, never-interrupted submission + execution.
    DistributedSubmission reference_submission;
    reference_submission.package = package;
    reference_submission.base_path = root;
    reference_submission.output_dir = root / "reference_out";
    const auto reference_submit = host.submit(reference_submission);
    REQUIRE(reference_submit.accepted);

    DistributedWorker::Config reference_worker_config;
    reference_worker_config.jobs_dir = jobs_dir;
    reference_worker_config.worker_id = "reference-worker";
    DistributedWorker reference_worker(reference_worker_config);
    REQUIRE(reference_worker.run_once().has_value());

    const auto reference_snapshot = host.snapshot(*reference_submit.job_id);
    REQUIRE(reference_snapshot.has_value());
    REQUIRE(reference_snapshot->state == JobState::Completed);

    // Retried run: submit the identical package/base_path, simulate a crashed first attempt
    // (Running with a stale heartbeat, no output ever written), let host reconciliation requeue
    // it for a full-job retry (per docs/design/distributed.md's "no partial-stage resume yet"
    // MVP guidance), then let a worker actually complete it from the same immutable inputs.
    DistributedSubmission retry_submission;
    retry_submission.package = package;
    retry_submission.base_path = root;
    retry_submission.output_dir = root / "retry_out";
    const auto retry_submit = host.submit(retry_submission);
    REQUIRE(retry_submit.accepted);
    const std::string retry_job_id = *retry_submit.job_id;
    const auto retry_path = job_file_path(jobs_dir, retry_job_id);

    {
        auto record = read_job_file(retry_path);
        REQUIRE(record.has_value());
        record->state = JobState::Running;
        record->worker_id = "crashed-worker";
        record->started_at = now_ms() - 100000;
        record->heartbeat_at = now_ms() - 100000;
        write_job_file_atomic(retry_path, *record);
    }
    REQUIRE(host.reconcile_stale_leases() == 1);
    {
        const auto record = host.snapshot(retry_job_id);
        REQUIRE(record.has_value());
        REQUIRE(record->state == JobState::Queued);
        REQUIRE(record->retry_count == 1);
    }
    REQUIRE_FALSE(fs::exists(root / "retry_out" / "report.json"));

    DistributedWorker::Config retry_worker_config;
    retry_worker_config.jobs_dir = jobs_dir;
    retry_worker_config.worker_id = "retry-worker";
    DistributedWorker retry_worker(retry_worker_config);
    REQUIRE(retry_worker.run_once().has_value());

    const auto retry_snapshot = host.snapshot(retry_job_id);
    REQUIRE(retry_snapshot.has_value());
    REQUIRE(retry_snapshot->state == JobState::Completed);
    REQUIRE(retry_snapshot->json_report_path.has_value());
    REQUIRE(retry_snapshot->html_report_path.has_value());

    const std::string reference_json = read_file(*reference_snapshot->json_report_path);
    const std::string retried_json = read_file(*retry_snapshot->json_report_path);
    REQUIRE_FALSE(reference_json.empty());
    REQUIRE(reference_json == retried_json);

    const std::string reference_html = read_file(*reference_snapshot->html_report_path);
    const std::string retried_html = read_file(*retry_snapshot->html_report_path);
    REQUIRE_FALSE(reference_html.empty());
    REQUIRE(reference_html == retried_html);

    remove_tree(root);
}
