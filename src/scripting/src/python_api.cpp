#include "aegis/scripting/python_api.hpp"

namespace aegis::scripting {

struct PythonApi::Impl {
    bool initialized = false;
};

PythonApi::PythonApi() : m_impl(std::make_unique<Impl>()) {}
PythonApi::~PythonApi() = default;
PythonApi::PythonApi(PythonApi&&) noexcept = default;
PythonApi& PythonApi::operator=(PythonApi&&) noexcept = default;

bool PythonApi::initialize() {
    m_impl->initialized = true;
    return true;
}

bool PythonApi::is_initialized() const {
    return m_impl->initialized;
}

} // namespace aegis::scripting
