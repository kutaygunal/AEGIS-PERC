#include "aegis/reporting/report_generator.hpp"

namespace aegis::reporting {

struct ReportGenerator::Impl {};

ReportGenerator::ReportGenerator() : m_impl(std::make_unique<Impl>()) {}
ReportGenerator::~ReportGenerator() = default;
ReportGenerator::ReportGenerator(ReportGenerator&&) noexcept = default;
ReportGenerator& ReportGenerator::operator=(ReportGenerator&&) noexcept = default;

bool ReportGenerator::generate_html(const std::string& /*output_path*/) const {
    return true;
}

} // namespace aegis::reporting
