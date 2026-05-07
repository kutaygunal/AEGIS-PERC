#pragma once

#include "aegis/rules/violation.hpp"
#include "aegis/ui/scene_adapter.hpp"

#include <QWidget>

class QLabel;
class QPushButton;
class QTextEdit;

namespace aegis::ui {

class ReportPreviewPanel : public QWidget {
    Q_OBJECT
public:
    explicit ReportPreviewPanel(QWidget* parent = nullptr);

    void set_scene(const UiScene& scene);
    void set_violations(aegis::rules::ViolationCollection violations);
    void set_current_violation(const aegis::rules::Violation* violation);
    void set_selected_item_count(int count);
    void set_snapshot(const QPixmap& snapshot);

    [[nodiscard]] QString summary_text() const;
    [[nodiscard]] QString snapshot_status_text() const;
    [[nodiscard]] QString last_action_status_text() const;
    [[nodiscard]] bool refresh_enabled() const;
    [[nodiscard]] bool copy_summary_enabled() const;
    [[nodiscard]] bool copy_snapshot_enabled() const;
    void trigger_refresh();
    void trigger_copy_summary();
    void trigger_copy_snapshot();

signals:
    void refresh_requested();

private:
    UiScene m_scene;
    aegis::rules::ViolationCollection m_violations;
    std::optional<aegis::rules::Violation> m_current_violation;
    int m_selected_item_count = 0;
    QLabel* m_snapshot = nullptr;
    QLabel* m_snapshot_status = nullptr;
    QTextEdit* m_summary = nullptr;
    QPushButton* m_refresh = nullptr;
    QPushButton* m_copy_summary = nullptr;
    QPushButton* m_copy_snapshot = nullptr;
    QString m_last_action_status;
    QPixmap m_snapshot_pixmap;

    void rebuild_summary();
    void update_action_state();
    void set_action_status_text(const QString& text);
};

} // namespace aegis::ui
