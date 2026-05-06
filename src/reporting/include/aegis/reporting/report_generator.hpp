#pragma once

#include <memory>
#include <string>

namespace aegis::reporting {

class ReportGenerator {
public:
    ReportGenerator();
    ~ReportGenerator();

    ReportGenerator(const ReportGenerator&) = delete;
    ReportGenerator& operator=(const ReportGenerator&) = delete;
    ReportGenerator(ReportGenerator&&) noexcept;
    ReportGenerator& operator=(ReportGenerator&&) noexcept;

    bool generate_html(const std::string& output_path) const;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace aegis::reporting
