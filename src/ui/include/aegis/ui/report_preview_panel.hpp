#pragma once

#include "aegis/rules/violation.hpp"
#include "aegis/ui/scene_adapter.hpp"

#include <QWidget>

class QLabel;
class QTextEdit;

namespace aegis::ui {

class ReportPreviewPanel : public QWidget {
    Q_OBJECT
public:
    explicit ReportPreviewPanel(QWidget* parent = nullptr);

    void set_scene(const UiScene& scene);
    void set_violations(aegis::rules::ViolationCollection violations);
    void set_current_violation(const aegis::rules::Violation* violation);
    void set_snapshot(const QPixmap& snapshot);

    [[nodiscard]] QString summary_text() const;
    [[nodiscard]] QString snapshot_status_text() const;

private:
    UiScene m_scene;
    aegis::rules::ViolationCollection m_violations;
    std::optional<aegis::rules::Violation> m_current_violation;
    QLabel* m_snapshot = nullptr;
    QTextEdit* m_summary = nullptr;

    void rebuild_summary();
};

} // namespace aegis::ui
