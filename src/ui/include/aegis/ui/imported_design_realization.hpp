#pragma once

#include "aegis/storage/imported_design_session.hpp"
#include "aegis/ui/scene_adapter.hpp"

#include <QObject>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace aegis::ui {

struct RealizationProgress {
    RealizationStage stage = RealizationStage::None;
    std::size_t completed_stages = 0;
    std::size_t total_stages = 5;
    QString message;
    bool cancel_requested = false;
    bool finished = false;
    bool failed = false;
};

struct RealizationResult {
    RealizationStage reached_stage = RealizationStage::None;
    std::unique_ptr<aegis::storage::ImportedDesignSession> session;
    ImportedSceneBuildResult scene_result;
    bool failed = false;
    std::string error_message;
    bool cancelled = false;
};

class ImportedDesignRealization : public QObject {
    Q_OBJECT
public:
    explicit ImportedDesignRealization(QObject* parent = nullptr);
    ~ImportedDesignRealization() override;

    ImportedDesignRealization(const ImportedDesignRealization&) = delete;
    ImportedDesignRealization& operator=(const ImportedDesignRealization&) = delete;

    void start(const aegis::storage::ProjectPackage& package,
               const std::filesystem::path& base_path);

    bool request_cancel();
    [[nodiscard]] RealizationProgress progress() const;
    [[nodiscard]] const std::optional<RealizationResult>& result() const;
    [[nodiscard]] std::optional<RealizationResult> take_result();
    bool wait(std::chrono::milliseconds timeout) const;

signals:
    void progress_changed(const RealizationProgress& progress);
    void finished();

public:
    struct Impl;

private:
    std::unique_ptr<Impl> m_impl;
};

} // namespace aegis::ui
