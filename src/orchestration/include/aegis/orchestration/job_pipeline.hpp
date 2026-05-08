#pragma once

#include "aegis/rules/violation.hpp"
#include "aegis/storage/project_package.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace aegis::orchestration {

using JobId = std::uint64_t;

enum class JobState {
    Queued,
    Running,
    Cancelling,
    Cancelled,
    Completed,
    Failed
};

enum class JobStage {
    None,
    Parse,
    Normalize,
    GraphBuild,
    RuleExecution,
    ReportExport
};

struct JobPipelineOptions {
    bool export_json = true;
    bool export_html = true;
    std::chrono::milliseconds artificial_stage_delay{0};
};

struct JobProgressSnapshot {
    JobId job_id = 0;
    JobState state = JobState::Queued;
    JobStage stage = JobStage::None;
    std::size_t completed_stages = 0;
    std::size_t total_stages = 5;
    bool cancel_requested = false;
    std::string project_name;
    std::string message;
    std::optional<std::filesystem::path> json_report_path;
    std::optional<std::filesystem::path> html_report_path;
};

struct JobResult {
    JobId job_id = 0;
    JobState state = JobState::Queued;
    std::vector<aegis::rules::Violation> violations;
    std::optional<std::filesystem::path> json_report_path;
    std::optional<std::filesystem::path> html_report_path;
    std::string error_message;
};

struct JobRequest {
    aegis::storage::ProjectPackage package;
    std::filesystem::path base_path;
    std::filesystem::path output_dir;
    JobPipelineOptions options;
    std::function<void(const JobProgressSnapshot&)> progress_callback;
};

class LocalJobPipeline {
public:
    LocalJobPipeline();
    ~LocalJobPipeline();

    LocalJobPipeline(const LocalJobPipeline&) = delete;
    LocalJobPipeline& operator=(const LocalJobPipeline&) = delete;
    LocalJobPipeline(LocalJobPipeline&&) noexcept;
    LocalJobPipeline& operator=(LocalJobPipeline&&) noexcept;

    JobId submit(JobRequest request);
    bool request_cancel(JobId job_id);
    std::optional<JobProgressSnapshot> snapshot(JobId job_id) const;
    std::optional<JobResult> result(JobId job_id) const;
    bool wait(JobId job_id, std::chrono::milliseconds timeout) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

std::string to_string(JobState state);
std::string to_string(JobStage stage);

} // namespace aegis::orchestration
