#include "aegis/orchestration/distributed_job.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace aegis::orchestration {
namespace {

using json = nlohmann::json;

std::string job_state_to_string_local(JobState state)
{
    return to_string(state);
}

JobState job_state_from_string(const std::string& value)
{
    if (value == "queued") return JobState::Queued;
    if (value == "running") return JobState::Running;
    if (value == "cancelling") return JobState::Cancelling;
    if (value == "cancelled") return JobState::Cancelled;
    if (value == "completed") return JobState::Completed;
    if (value == "failed") return JobState::Failed;
    return JobState::Queued;
}

std::string job_stage_to_string_local(JobStage stage)
{
    return to_string(stage);
}

JobStage job_stage_from_string(const std::string& value)
{
    if (value == "parse") return JobStage::Parse;
    if (value == "normalize") return JobStage::Normalize;
    if (value == "graph_build") return JobStage::GraphBuild;
    if (value == "rule_execution") return JobStage::RuleExecution;
    if (value == "report_export") return JobStage::ReportExport;
    return JobStage::None;
}

json options_to_json(const JobPipelineOptions& options)
{
    return json{
        {"export_json", options.export_json},
        {"export_html", options.export_html},
        {"artificial_stage_delay_ms", options.artificial_stage_delay.count()}
    };
}

JobPipelineOptions options_from_json(const json& value)
{
    JobPipelineOptions options;
    options.export_json = value.value("export_json", true);
    options.export_html = value.value("export_html", true);
    options.artificial_stage_delay = std::chrono::milliseconds(value.value("artificial_stage_delay_ms", std::int64_t{0}));
    return options;
}

std::string path_or_empty(const std::optional<std::filesystem::path>& path)
{
    return path.has_value() ? path->generic_string() : std::string();
}

} // namespace

TimestampMs now_ms()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string to_json_text(const DistributedJob& job, int indent)
{
    json root;
    root["job_id"] = job.job_id;
    root["package_manifest_json"] = job.package_manifest_json;
    root["base_path"] = job.base_path.generic_string();
    root["output_dir"] = job.output_dir.generic_string();
    root["options"] = options_to_json(job.options);
    root["project_name"] = job.project_name;

    root["state"] = job_state_to_string_local(job.state);
    root["stage"] = job_stage_to_string_local(job.stage);
    root["completed_stages"] = job.completed_stages;
    root["total_stages"] = job.total_stages;
    root["cancel_requested"] = job.cancel_requested;
    root["message"] = job.message;

    root["worker_id"] = job.worker_id.value_or(std::string());
    root["has_worker_id"] = job.worker_id.has_value();
    root["json_report_path"] = path_or_empty(job.json_report_path);
    root["has_json_report_path"] = job.json_report_path.has_value();
    root["html_report_path"] = path_or_empty(job.html_report_path);
    root["has_html_report_path"] = job.html_report_path.has_value();
    root["error_summary"] = job.error_summary;

    root["retry_count"] = job.retry_count;
    root["last_failure_reason"] = job.last_failure_reason.value_or(std::string());
    root["has_last_failure_reason"] = job.last_failure_reason.has_value();

    root["created_at"] = job.created_at;
    root["started_at"] = job.started_at.value_or(TimestampMs{0});
    root["has_started_at"] = job.started_at.has_value();
    root["updated_at"] = job.updated_at;
    root["finished_at"] = job.finished_at.value_or(TimestampMs{0});
    root["has_finished_at"] = job.finished_at.has_value();
    root["heartbeat_at"] = job.heartbeat_at.value_or(TimestampMs{0});
    root["has_heartbeat_at"] = job.heartbeat_at.has_value();

    return root.dump(indent);
}

DistributedJob distributed_job_from_json(const std::string& text)
{
    const json root = json::parse(text);

    DistributedJob job;
    job.job_id = root.value("job_id", std::string());
    job.package_manifest_json = root.value("package_manifest_json", std::string());
    job.base_path = root.value("base_path", std::string());
    job.output_dir = root.value("output_dir", std::string());
    if (root.contains("options")) {
        job.options = options_from_json(root.at("options"));
    }
    job.project_name = root.value("project_name", std::string());

    job.state = job_state_from_string(root.value("state", std::string("queued")));
    job.stage = job_stage_from_string(root.value("stage", std::string("none")));
    job.completed_stages = root.value("completed_stages", std::size_t{0});
    job.total_stages = root.value("total_stages", std::size_t{5});
    job.cancel_requested = root.value("cancel_requested", false);
    job.message = root.value("message", std::string());

    if (root.value("has_worker_id", false)) {
        job.worker_id = root.value("worker_id", std::string());
    }
    if (root.value("has_json_report_path", false)) {
        job.json_report_path = std::filesystem::path(root.value("json_report_path", std::string()));
    }
    if (root.value("has_html_report_path", false)) {
        job.html_report_path = std::filesystem::path(root.value("html_report_path", std::string()));
    }
    job.error_summary = root.value("error_summary", std::string());

    job.retry_count = root.value("retry_count", 0);
    if (root.value("has_last_failure_reason", false)) {
        job.last_failure_reason = root.value("last_failure_reason", std::string());
    }

    job.created_at = root.value("created_at", TimestampMs{0});
    if (root.value("has_started_at", false)) {
        job.started_at = root.value("started_at", TimestampMs{0});
    }
    job.updated_at = root.value("updated_at", TimestampMs{0});
    if (root.value("has_finished_at", false)) {
        job.finished_at = root.value("finished_at", TimestampMs{0});
    }
    if (root.value("has_heartbeat_at", false)) {
        job.heartbeat_at = root.value("heartbeat_at", TimestampMs{0});
    }

    return job;
}

std::filesystem::path job_file_path(const std::filesystem::path& jobs_dir, const std::string& job_id)
{
    return jobs_dir / (job_id + ".json");
}

void write_job_file_atomic(const std::filesystem::path& path, const DistributedJob& job)
{
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path());
    }

    // Give the temp file a name unlikely to collide with a concurrent writer of the same
    // logical job (should not normally happen -- only the host or the single worker holding
    // the claim mutates a given job file at a time -- but a unique suffix keeps a stray
    // leftover temp file from ever being mistaken for a valid promote target).
    static std::atomic<std::uint64_t> temp_counter{0};
    const auto suffix = std::to_string(now_ms()) + "_" + std::to_string(temp_counter.fetch_add(1));
    const auto temp_path = path.string() + ".tmp." + suffix;

    {
        std::ofstream out(temp_path, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw std::runtime_error("Unable to open temp job file for write: " + temp_path);
        }
        out << to_json_text(job);
    }

    std::error_code ec;
    std::filesystem::rename(temp_path, path, ec);
    if (ec) {
        // Cross-filesystem or transient failure: fall back to copy+remove, still leaving the
        // destination either fully old or fully new (never torn), then clean up the temp file.
        std::filesystem::copy_file(temp_path, path, std::filesystem::copy_options::overwrite_existing, ec);
        std::error_code remove_ec;
        std::filesystem::remove(temp_path, remove_ec);
        if (ec) {
            throw std::runtime_error("Unable to atomically promote job file: " + path.string());
        }
    }
}

std::optional<DistributedJob> read_job_file(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string text = buffer.str();
    if (text.empty()) {
        return std::nullopt;
    }
    try {
        return distributed_job_from_json(text);
    } catch (const std::exception&) {
        // A reader can race a writer's rename (rare, but possible on some filesystems/timing)
        // and see a momentarily-truncated read, or the file may not exist yet. Treat any parse
        // failure the same as "not available right now" rather than propagating.
        return std::nullopt;
    }
}

} // namespace aegis::orchestration
