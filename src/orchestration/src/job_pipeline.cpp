#include "aegis/orchestration/job_pipeline.hpp"

#include "aegis/graph/connectivity_graph.hpp"
#include "aegis/graph/current_activity_application.hpp"
#include "aegis/graph/power_intent_application.hpp"
#include "aegis/parsing/current_activity.hpp"
#include "aegis/parsing/power_intent.hpp"
#include "aegis/rules/rule_engine.hpp"
#include "aegis/rules/rule_pack.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>

#include <nlohmann/json.hpp>

namespace aegis::orchestration {
namespace {

using json = nlohmann::json;
using aegis::graph::ConnectivityGraph;
using aegis::graph::EdgeType;
using aegis::graph::NetNode;
using aegis::graph::PinNode;
using aegis::rules::RuleContext;
using aegis::rules::RuleEngine;
using aegis::rules::RulePackLoader;
using aegis::storage::ProjectPackage;
using aegis::storage::SourceArtifact;

struct PreparedData {
    RuleEngine engine;
    ConnectivityGraph graph;
    json runtime_diagnostics = json::array();
};

std::filesystem::path resolve_artifact_path(const std::filesystem::path& base_path,
                                            const SourceArtifact& artifact)
{
    if (artifact.path.is_absolute()) {
        return artifact.path;
    }
    return base_path / artifact.path;
}

void write_text_file(const std::filesystem::path& path, const std::string& content)
{
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("Unable to open output file: " + path.string());
    }
    out << content;
}

json artifact_to_json(const SourceArtifact& artifact)
{
    return json{
        {"id", artifact.id},
        {"path", artifact.path.generic_string()},
        {"role", aegis::storage::to_string(artifact.role)},
        {"category", aegis::storage::to_string(artifact.category)},
        {"optional", artifact.optional},
        {"origin", artifact.origin}
    };
}

json import_diagnostic_to_json(const aegis::storage::ImportDiagnostic& diagnostic)
{
    json item{
        {"severity", aegis::storage::to_string(diagnostic.severity)},
        {"code", diagnostic.code},
        {"message", diagnostic.message}
    };
    if (diagnostic.artifact_id.has_value()) {
        item["artifact_id"] = *diagnostic.artifact_id;
    }
    return item;
}

json current_diagnostic_to_json(const aegis::parsing::CurrentActivityDiagnostic& diagnostic)
{
    json item{
        {"severity", diagnostic.severity == aegis::parsing::CurrentActivityDiagnostic::Severity::Error ? "error" : "warning"},
        {"code", diagnostic.code},
        {"message", diagnostic.message}
    };
    if (diagnostic.source_row.has_value()) {
        item["source_row"] = *diagnostic.source_row;
    }
    if (diagnostic.net_name.has_value()) {
        item["net_name"] = *diagnostic.net_name;
    }
    return item;
}

json power_diagnostic_to_json(const aegis::parsing::PowerIntentDiagnostic& diagnostic)
{
    json item{
        {"severity", diagnostic.severity == aegis::parsing::PowerIntentDiagnostic::Severity::Error ? "error" : "warning"},
        {"code", diagnostic.code},
        {"message", diagnostic.message}
    };
    if (diagnostic.source_row.has_value()) {
        item["source_row"] = *diagnostic.source_row;
    }
    if (diagnostic.instance_name.has_value()) {
        item["instance_name"] = *diagnostic.instance_name;
    }
    return item;
}

json build_json_report(const ProjectPackage& package,
                       const std::vector<aegis::rules::Violation>& violations,
                       const json& runtime_diagnostics)
{
    std::map<std::string, int> severity_counts;
    for (const auto& violation : violations) {
        ++severity_counts[aegis::rules::severity_to_string(violation.severity)];
    }

    json artifacts = json::array();
    for (const auto& artifact : package.artifacts()) {
        artifacts.push_back(artifact_to_json(artifact));
    }

    json import_diagnostics = json::array();
    for (const auto& diagnostic : package.diagnostics()) {
        import_diagnostics.push_back(import_diagnostic_to_json(diagnostic));
    }

    json summary_severities = json::object();
    for (const auto& [severity, count] : severity_counts) {
        summary_severities[severity] = count;
    }

    return json{
        {"project", {
            {"name", package.project().name},
            {"description", package.project().description},
            {"customer", package.project().customer},
            {"design_stage", package.project().design_stage}
        }},
        {"source_package", {
            {"manifest_version", package.manifest_version()},
            {"validation_status", aegis::storage::to_string(package.validation_status())},
            {"artifacts", artifacts}
        }},
        {"summary", {
            {"artifact_count", package.artifacts().size()},
            {"violation_count", violations.size()},
            {"severity_counts", summary_severities}
        }},
        {"import_diagnostics", import_diagnostics},
        {"runtime_diagnostics", runtime_diagnostics},
        {"violations", violations}
    };
}

std::string build_html_report(const ProjectPackage& package,
                              const std::vector<aegis::rules::Violation>& violations)
{
    std::ostringstream html;
    html << "<!doctype html><html><head><meta charset=\"utf-8\"><title>AEGIS-PERC Report</title></head><body>";
    html << "<h1>AEGIS-PERC Summary</h1>";
    html << "<p><strong>Project:</strong> " << package.project().name << "</p>";
    html << "<p><strong>Validation status:</strong> " << aegis::storage::to_string(package.validation_status()) << "</p>";
    html << "<p><strong>Violation count:</strong> " << violations.size() << "</p>";
    html << "<h2>Violations</h2><ul>";
    for (const auto& violation : violations) {
        html << "<li>[" << aegis::rules::severity_to_string(violation.severity) << "] "
             << violation.rule_id << ": " << violation.message << "</li>";
    }
    html << "</ul></body></html>";
    return html.str();
}

} // namespace

struct LocalJobPipeline::Impl {
    struct JobRecord {
        mutable std::mutex mutex;
        mutable std::condition_variable cv;
        JobRequest request;
        JobProgressSnapshot snapshot;
        JobResult result;
        std::thread worker;
        std::atomic<bool> cancel_requested{false};
        bool finished = false;
    };

    mutable std::mutex jobs_mutex;
    std::unordered_map<JobId, std::shared_ptr<JobRecord>> jobs;
    std::atomic<JobId> next_job_id{1};

    static void publish(JobRecord& job)
    {
        std::function<void(const JobProgressSnapshot&)> callback;
        JobProgressSnapshot snapshot_copy;
        {
            std::lock_guard lock(job.mutex);
            callback = job.request.progress_callback;
            snapshot_copy = job.snapshot;
        }
        if (callback) {
            callback(snapshot_copy);
        }
        job.cv.notify_all();
    }

    static void complete(JobRecord& job, JobState state, std::string message = {})
    {
        {
            std::lock_guard lock(job.mutex);
            job.snapshot.state = state;
            job.snapshot.message = std::move(message);
            job.result.state = state;
            if (!job.snapshot.message.empty() && state == JobState::Failed) {
                job.result.error_message = job.snapshot.message;
            }
            job.finished = true;
        }
        publish(job);
    }

    static void maybe_sleep(const JobRecord& job)
    {
        const auto delay = job.request.options.artificial_stage_delay;
        if (delay.count() > 0) {
            std::this_thread::sleep_for(delay);
        }
    }

    static bool should_cancel(JobRecord& job)
    {
        if (!job.cancel_requested.load()) {
            return false;
        }
        {
            std::lock_guard lock(job.mutex);
            job.snapshot.cancel_requested = true;
            job.snapshot.state = JobState::Cancelling;
            job.snapshot.message = "Cancellation requested";
            job.result.state = JobState::Cancelling;
        }
        publish(job);
        return true;
    }

    static void update_stage(JobRecord& job, JobStage stage, std::size_t completed, std::string message)
    {
        {
            std::lock_guard lock(job.mutex);
            job.snapshot.state = JobState::Running;
            job.snapshot.stage = stage;
            job.snapshot.completed_stages = completed;
            job.snapshot.message = std::move(message);
            job.result.state = JobState::Running;
        }
        publish(job);
        maybe_sleep(job);
    }

    static PreparedData prepare_execution(JobRecord& job)
    {
        PreparedData prepared;
        const auto& package = job.request.package;

        const auto* rules_artifact = package.normalized().rule_artifact_ids.empty()
            ? nullptr
            : package.find_artifact_by_id(package.normalized().rule_artifact_ids.front());
        if (!rules_artifact) {
            throw std::runtime_error("No rule pack artifact available for execution");
        }

        RulePackLoader loader;
        const auto pack = loader.load_from_file(resolve_artifact_path(job.request.base_path, *rules_artifact));
        for (auto& rule : loader.instantiate_rules(pack)) {
            prepared.engine.register_rule(std::move(rule));
        }

        for (const auto& artifact_id : package.normalized().current_artifact_ids) {
            const auto* artifact = package.find_artifact_by_id(artifact_id);
            if (!artifact) {
                continue;
            }
            aegis::parsing::CurrentActivityParser parser;
            const auto data = parser.parse_csv_file(resolve_artifact_path(job.request.base_path, *artifact));
            for (const auto& diagnostic : data.diagnostics) {
                prepared.runtime_diagnostics.push_back(current_diagnostic_to_json(diagnostic));
            }
            if (data.has_errors()) {
                throw std::runtime_error("Current/activity import contains blocking errors");
            }
            for (const auto& record : data.records) {
                if (!prepared.graph.find_net(record.net_name).has_value()) {
                    const auto net_id = prepared.graph.add_net(NetNode{record.net_name, {}});
                    const auto pin_id = prepared.graph.add_pin(PinNode{"port_" + record.net_name, "INPUT", std::nullopt, std::nullopt, std::nullopt});
                    prepared.graph.add_edge(pin_id, net_id, {EdgeType::NetToPin, ""});
                }
            }
            const auto applied = aegis::graph::apply_current_activity(prepared.graph, data);
            for (const auto& diagnostic : applied.diagnostics) {
                prepared.runtime_diagnostics.push_back(current_diagnostic_to_json(diagnostic));
            }
        }

        for (const auto& artifact_id : package.normalized().power_artifact_ids) {
            const auto* artifact = package.find_artifact_by_id(artifact_id);
            if (!artifact) {
                continue;
            }
            aegis::parsing::PowerIntentParser parser;
            const auto data = parser.parse_csv_file(resolve_artifact_path(job.request.base_path, *artifact));
            for (const auto& diagnostic : data.diagnostics) {
                prepared.runtime_diagnostics.push_back(power_diagnostic_to_json(diagnostic));
            }
            if (data.has_errors()) {
                throw std::runtime_error("Power-domain import contains blocking errors");
            }
            for (const auto& assignment : data.assignments) {
                if (!prepared.graph.find_device(assignment.instance_name).has_value()) {
                    prepared.graph.add_device({assignment.instance_name, "BLOCK", {}});
                }
            }
            const auto applied = aegis::graph::apply_power_intent(prepared.graph, data);
            for (const auto& diagnostic : applied.diagnostics) {
                prepared.runtime_diagnostics.push_back(power_diagnostic_to_json(diagnostic));
            }
        }

        return prepared;
    }

    static void run_job(JobId job_id, const std::shared_ptr<JobRecord>& job)
    {
        try {
            {
                std::lock_guard lock(job->mutex);
                job->snapshot.job_id = job_id;
                job->snapshot.project_name = job->request.package.project().name;
                job->result.job_id = job_id;
                job->result.state = JobState::Queued;
            }
            publish(*job);

            update_stage(*job, JobStage::Parse, 0, "Parsing imported artifacts");
            PreparedData prepared = prepare_execution(*job);
            if (should_cancel(*job)) {
                complete(*job, JobState::Cancelled, "Job cancelled during parse stage");
                return;
            }

            update_stage(*job, JobStage::Normalize, 1, "Normalizing project package state");
            job->request.package.rebuild_normalized_view();
            if (should_cancel(*job)) {
                complete(*job, JobState::Cancelled, "Job cancelled during normalize stage");
                return;
            }

            update_stage(*job, JobStage::GraphBuild, 2, "Building local analysis graph");
            if (should_cancel(*job)) {
                complete(*job, JobState::Cancelled, "Job cancelled during graph-build stage");
                return;
            }

            update_stage(*job, JobStage::RuleExecution, 3, "Executing configured rules");
            const RuleContext ctx{prepared.graph, aegis::graph::PropertyMap{}, job->request.package.project().name};
            std::vector<aegis::rules::Violation> violations = prepared.engine.run_all(ctx);
            if (should_cancel(*job)) {
                complete(*job, JobState::Cancelled, "Job cancelled during rule-execution stage");
                return;
            }

            update_stage(*job, JobStage::ReportExport, 4, "Exporting reports");
            const auto json_report = build_json_report(job->request.package, violations, prepared.runtime_diagnostics).dump(2);
            const auto html_report = build_html_report(job->request.package, violations);
            std::filesystem::create_directories(job->request.output_dir);

            std::optional<std::filesystem::path> final_json_path;
            std::optional<std::filesystem::path> final_html_path;
            std::vector<std::filesystem::path> temp_paths;

            if (job->request.options.export_json) {
                final_json_path = job->request.output_dir / "report.json";
                const auto temp = *final_json_path;
                write_text_file(temp.string() + ".tmp", json_report);
                temp_paths.push_back(temp.string() + ".tmp");
            }
            if (job->request.options.export_html) {
                final_html_path = job->request.output_dir / "report.html";
                const auto temp = *final_html_path;
                write_text_file(temp.string() + ".tmp", html_report);
                temp_paths.push_back(temp.string() + ".tmp");
            }

            if (should_cancel(*job)) {
                for (const auto& temp : temp_paths) {
                    std::error_code ec;
                    std::filesystem::remove(temp, ec);
                }
                complete(*job, JobState::Cancelled, "Job cancelled during report-export stage");
                return;
            }

            if (final_json_path.has_value()) {
                std::filesystem::rename(final_json_path->string() + ".tmp", *final_json_path);
            }
            if (final_html_path.has_value()) {
                std::filesystem::rename(final_html_path->string() + ".tmp", *final_html_path);
            }

            {
                std::lock_guard lock(job->mutex);
                job->snapshot.completed_stages = job->snapshot.total_stages;
                job->snapshot.json_report_path = final_json_path;
                job->snapshot.html_report_path = final_html_path;
                job->result.violations = std::move(violations);
                job->result.json_report_path = final_json_path;
                job->result.html_report_path = final_html_path;
            }
            complete(*job, JobState::Completed, "Job completed successfully");
        } catch (const std::exception& ex) {
            complete(*job, JobState::Failed, ex.what());
        }
    }
};

LocalJobPipeline::LocalJobPipeline() : m_impl(std::make_unique<Impl>()) {}
LocalJobPipeline::~LocalJobPipeline()
{
    if (!m_impl) {
        return;
    }
    std::vector<std::shared_ptr<Impl::JobRecord>> jobs;
    {
        std::lock_guard lock(m_impl->jobs_mutex);
        for (auto& [id, job] : m_impl->jobs) {
            (void)id;
            jobs.push_back(job);
        }
    }
    for (auto& job : jobs) {
        if (job->worker.joinable()) {
            job->worker.join();
        }
    }
}
LocalJobPipeline::LocalJobPipeline(LocalJobPipeline&&) noexcept = default;
LocalJobPipeline& LocalJobPipeline::operator=(LocalJobPipeline&&) noexcept = default;

JobId LocalJobPipeline::submit(JobRequest request)
{
    const JobId job_id = m_impl->next_job_id.fetch_add(1);
    auto job = std::make_shared<Impl::JobRecord>();
    job->request = std::move(request);
    job->snapshot.job_id = job_id;
    job->snapshot.project_name = job->request.package.project().name;

    {
        std::lock_guard lock(m_impl->jobs_mutex);
        m_impl->jobs[job_id] = job;
    }

    job->worker = std::thread([job_id, job]() {
        Impl::run_job(job_id, job);
    });
    return job_id;
}

bool LocalJobPipeline::request_cancel(JobId job_id)
{
    std::shared_ptr<Impl::JobRecord> job;
    {
        std::lock_guard lock(m_impl->jobs_mutex);
        const auto it = m_impl->jobs.find(job_id);
        if (it == m_impl->jobs.end()) {
            return false;
        }
        job = it->second;
    }
    job->cancel_requested.store(true);
    return true;
}

std::optional<JobProgressSnapshot> LocalJobPipeline::snapshot(JobId job_id) const
{
    std::shared_ptr<Impl::JobRecord> job;
    {
        std::lock_guard lock(m_impl->jobs_mutex);
        const auto it = m_impl->jobs.find(job_id);
        if (it == m_impl->jobs.end()) {
            return std::nullopt;
        }
        job = it->second;
    }
    std::lock_guard lock(job->mutex);
    return job->snapshot;
}

std::optional<JobResult> LocalJobPipeline::result(JobId job_id) const
{
    std::shared_ptr<Impl::JobRecord> job;
    {
        std::lock_guard lock(m_impl->jobs_mutex);
        const auto it = m_impl->jobs.find(job_id);
        if (it == m_impl->jobs.end()) {
            return std::nullopt;
        }
        job = it->second;
    }
    std::lock_guard lock(job->mutex);
    if (!job->finished) {
        return std::nullopt;
    }
    return job->result;
}

bool LocalJobPipeline::wait(JobId job_id, std::chrono::milliseconds timeout) const
{
    std::shared_ptr<Impl::JobRecord> job;
    {
        std::lock_guard lock(m_impl->jobs_mutex);
        const auto it = m_impl->jobs.find(job_id);
        if (it == m_impl->jobs.end()) {
            return false;
        }
        job = it->second;
    }

    std::unique_lock lock(job->mutex);
    return job->cv.wait_for(lock, timeout, [&]() { return job->finished; });
}

std::string to_string(JobState state)
{
    switch (state) {
    case JobState::Queued: return "queued";
    case JobState::Running: return "running";
    case JobState::Cancelling: return "cancelling";
    case JobState::Cancelled: return "cancelled";
    case JobState::Completed: return "completed";
    case JobState::Failed: return "failed";
    }
    return "queued";
}

std::string to_string(JobStage stage)
{
    switch (stage) {
    case JobStage::None: return "none";
    case JobStage::Parse: return "parse";
    case JobStage::Normalize: return "normalize";
    case JobStage::GraphBuild: return "graph_build";
    case JobStage::RuleExecution: return "rule_execution";
    case JobStage::ReportExport: return "report_export";
    }
    return "none";
}

} // namespace aegis::orchestration
