#include "aegis/core/service_registry.hpp"
#include <vector>
#include <algorithm>

namespace aegis::core {

struct ServiceRegistry::Impl {
    std::vector<std::unique_ptr<IService>> services;
};

ServiceRegistry::ServiceRegistry()
    : m_impl(std::make_unique<Impl>()) {}

ServiceRegistry::~ServiceRegistry() = default;

ServiceRegistry::ServiceRegistry(ServiceRegistry&&) noexcept = default;
ServiceRegistry& ServiceRegistry::operator=(ServiceRegistry&&) noexcept = default;

void ServiceRegistry::register_service(std::unique_ptr<IService> service) {
    if (service) {
        m_impl->services.push_back(std::move(service));
    }
}

bool ServiceRegistry::has_service(const std::string& name) const {
    return std::any_of(m_impl->services.begin(), m_impl->services.end(),
        [&](const auto& s) { return s->name() == name; });
}

std::size_t ServiceRegistry::service_count() const {
    return m_impl->services.size();
}

} // namespace aegis::core
