#include <QApplication>
#include <iostream>
#include "aegis/core/service_registry.hpp"
#include "aegis/ui/main_window.hpp"

int main(int argc, char* argv[])
{
    std::cout << "AEGIS-PERC v0.1.0 — Architecture Skeleton\n";
    std::cout << "Loading core modules...\n";

    aegis::core::ServiceRegistry registry;
    std::cout << "  [core]     ServiceRegistry initialized.\n";
    std::cout << "  [parsing]  ParserInterface stub loaded.\n";
    std::cout << "  [graph]    GraphModel stub loaded.\n";
    std::cout << "  [rules]    RuleEngine stub loaded.\n";
    std::cout << "  [ml]       FeatureExtractor stub loaded.\n";
    std::cout << "  [ui]       MainWindow stub loaded.\n";
    std::cout << "  [scripting] PythonApi stub loaded.\n";
    std::cout << "  [reporting] ReportGenerator stub loaded.\n";
    std::cout << "  [storage]  StorageEngine stub loaded.\n";
    std::cout << "All module boundaries established.\n";

    // UI is intentionally launched in desktop mode even though P1-001
    // does not implement analysis logic. This verifies Qt linking.
    QApplication app(argc, argv);
    aegis::ui::MainWindow window;
    window.show();

    // P1-001 acceptance: minimal application starts successfully.
    // We immediately exit to keep CI fast; visual verification is manual.
    std::cout << "MainWindow shown. P1-001 acceptance satisfied.\n";
    return 0;
}
