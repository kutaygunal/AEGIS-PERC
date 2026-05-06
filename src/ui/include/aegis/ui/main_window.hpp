#pragma once

#include <QMainWindow>
#include <memory>

namespace aegis::ui {

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace aegis::ui
