#include <QApplication>
#include <iostream>
#include "aegis/core/service_registry.hpp"
#include "aegis/ui/main_window.hpp"

int main(int argc, char* argv[])
{
    std::cout << "AEGIS-PERC v0.1.0\n";
    std::cout << "Initializing desktop application...\n";

    aegis::core::ServiceRegistry registry;
    std::cout << "  [core] ServiceRegistry initialized.\n";
    std::cout << "  [app]  Launching Qt workspace shell.\n";
    QApplication app(argc, argv);
    aegis::ui::MainWindow window;
    window.show();

    std::cout << "MainWindow shown. Entering Qt event loop.\n";
    return app.exec();
}
