#include "aegis/ui/imported_design_realization.hpp"

#include "aegis/storage/imported_design_session.hpp"
#include "aegis/ui/scene_adapter.hpp"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace aegis::ui {


struct ImportedDesignRealization::Impl {
    mutable std::mutex mutex;
    mutable std::condition_variable cv;
    RealizationProgress progress;
    std::optional<RealizationResult> result;
    std::atomic<bool> cancel_requested{false};
    std::thread worker;
};

namespace {

void notify_progress(ImportedDesignRealization::Impl& impl, RealizationStage stage,
                     std::size_t completed, const QString& message)
{
    std::lock_guard<std::mutex> lock(impl.mutex);
    impl.progress.stage = stage;
    impl.progress.completed_stages = completed;
    impl.progress.message = message;
    if (!impl.progress.cancel_requested) {
        impl.progress.cancel_requested = impl.cancel_requested.load();
    }
}

void emit_progress(ImportedDesignRealization* self, ImportedDesignRealization::Impl& impl)
{
    RealizationProgress snapshot;
    {
        std::lock_guard<std::mutex> lock(impl.mutex);
        snapshot = impl.progress;
    }
    emit self->progress_changed(snapshot);
}

void emit_finished(ImportedDesignRealization* self)
{
    emit self->finished();
}

void finish(ImportedDesignRealization::Impl& impl, RealizationResult result)
{
    std::lock_guard<std::mutex> lock(impl.mutex);
    impl.result = std::move(result);
    impl.progress.finished = true;
    impl.cv.notify_all();
}

} // namespace

ImportedDesignRealization::ImportedDesignRealization(QObject* parent)
    : QObject(parent)
    , m_impl(std::make_unique<Impl>())
{}

ImportedDesignRealization::~ImportedDesignRealization()
{
    if (m_impl && m_impl->worker.joinable()) {
        request_cancel();
        m_impl->worker.join();
    }
}

void ImportedDesignRealization::start(const aegis::storage::ProjectPackage& package,
                                       const std::filesystem::path& base_path)
{
    {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        m_impl->progress = RealizationProgress{};
        m_impl->progress.total_stages = 5;
        m_impl->result = std::nullopt;
    }
    m_impl->cancel_requested.store(false);

    m_impl->worker = std::thread([this, package, base_path]() {
        auto& impl = *m_impl;
        RealizationResult result;
        result.reached_stage = RealizationStage::None;

        try {
            notify_progress(impl, RealizationStage::Imported, 1, "Package imported");
            emit_progress(this, impl);
            if (impl.cancel_requested.load()) {
                result.cancelled = true;
                finish(impl, std::move(result));
                emit_finished(this);
                return;
            }

            aegis::storage::ImportedDesignSessionBuilder builder;
            auto session = std::make_unique<aegis::storage::ImportedDesignSession>(
                builder.build(package, base_path));
            result.session = std::move(session);
            result.reached_stage = RealizationStage::SessionBuilt;
            notify_progress(impl, RealizationStage::SessionBuilt, 2, "Session built");
            emit_progress(this, impl);
            if (impl.cancel_requested.load()) {
                result.cancelled = true;
                finish(impl, std::move(result));
                emit_finished(this);
                return;
            }

            result.scene_result = build_imported_design_scene(*result.session);
            result.reached_stage = RealizationStage::SceneBuilt;
            notify_progress(impl, RealizationStage::SceneBuilt, 3, "Scene built");
            emit_progress(this, impl);
            if (impl.cancel_requested.load()) {
                result.cancelled = true;
                finish(impl, std::move(result));
                emit_finished(this);
                return;
            }

            result.reached_stage = RealizationStage::Indexed;
            notify_progress(impl, RealizationStage::Indexed, 4, "Browser indexed");
            emit_progress(this, impl);
            if (impl.cancel_requested.load()) {
                result.cancelled = true;
                finish(impl, std::move(result));
                emit_finished(this);
                return;
            }

            result.reached_stage = RealizationStage::AnalysisReady;
            notify_progress(impl, RealizationStage::AnalysisReady, 5, "Analysis ready");
            emit_progress(this, impl);
            finish(impl, std::move(result));
            emit_finished(this);
        } catch (const std::exception& ex) {
            result.failed = true;
            result.error_message = ex.what();
            notify_progress(impl, RealizationStage::None, impl.progress.completed_stages,
                            QString("Realization failed: %1").arg(QString::fromStdString(ex.what())));
            emit_progress(this, impl);
            finish(impl, std::move(result));
            emit_finished(this);
        }
    });
}

bool ImportedDesignRealization::request_cancel()
{
    m_impl->cancel_requested.store(true);
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->progress.cancel_requested = true;
    return true;
}

RealizationProgress ImportedDesignRealization::progress() const
{
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    return m_impl->progress;
}

const std::optional<RealizationResult>& ImportedDesignRealization::result() const
{
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    return m_impl->result;
}

std::optional<RealizationResult> ImportedDesignRealization::take_result()
{
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    return std::move(m_impl->result);
}

bool ImportedDesignRealization::wait(std::chrono::milliseconds timeout) const
{
    std::unique_lock<std::mutex> lock(m_impl->mutex);
    return m_impl->cv.wait_for(lock, timeout, [this]() {
        return m_impl->progress.finished;
    });
}

} // namespace aegis::ui
