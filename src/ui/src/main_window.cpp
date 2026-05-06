#include "aegis/ui/main_window.hpp"

namespace aegis::ui {

struct MainWindow::Impl {};

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_impl(std::make_unique<Impl>())
{
}

MainWindow::~MainWindow() = default;

} // namespace aegis::ui
