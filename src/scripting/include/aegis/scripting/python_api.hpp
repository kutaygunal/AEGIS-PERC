#pragma once

#include <memory>
#include <string>

namespace aegis::scripting {

class PythonApi {
public:
    PythonApi();
    ~PythonApi();

    PythonApi(const PythonApi&) = delete;
    PythonApi& operator=(const PythonApi&) = delete;
    PythonApi(PythonApi&&) noexcept;
    PythonApi& operator=(PythonApi&&) noexcept;

    bool initialize();
    bool is_initialized() const;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace aegis::scripting
