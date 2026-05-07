#pragma once

#include "aegis/ui/scene_adapter.hpp"

#include <QMainWindow>
#include <memory>

namespace aegis::ui {

// ---------------------------------------------------------------------------
// Application shell with dockable workspace
//
// P1-007 acceptance:
//   - Qt6 dockable panels around a central placeholder canvas.
//   - Menu bar with File, View, Tools, Help placeholders.
//   - Window geometry and dock state persist across restarts via QSettings.
//   - No analysis logic inside the shell.
// ---------------------------------------------------------------------------
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    // Test accessors --------------------------------------------------------
    void set_scene(UiScene scene);

    bool has_central_widget() const;
    bool has_layout_canvas() const;
    bool has_menu_bar() const;
    int  dock_widget_count() const;
    int  layer_panel_count() const;
    int  selected_item_count() const;
    QStringList dock_widget_titles() const;
    QStringList layer_panel_names() const;
    bool is_layer_visible(const QString& layer_name) const;
    QString properties_summary_text() const;

    // Persistence hooks (public for testability)
    void restore_window_state();
    void save_window_state();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    void setup_ui();
    void setup_menus();
    void setup_dock_panels();
};

} // namespace aegis::ui
