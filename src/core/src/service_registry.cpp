#include "aegis/core/service_registry.hpp"
#include <vector>
#include <algorithm>
#include <functional>
#include <unordered_map>

namespace aegis::core {

// ---------------------------------------------------------------------------
// CircularDependencyException
// ---------------------------------------------------------------------------
CircularDependencyException::CircularDependencyException(const std::string& message)
    : std::runtime_error(message) {}

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------
struct ServiceRegistry::Impl {
    struct Entry {
        std::type_index type;
        std::string      name;
        std::unique_ptr<IService> instance;
        std::function<std::unique_ptr<IService>()> factory;
        bool           resolving = false;

        Entry(std::type_index ti,
              const std::string& n,
              std::unique_ptr<IService> s,
              std::function<std::unique_ptr<IService>()> f,
              bool r)
            : type(std::move(ti)),
              name(n),
              instance(std::move(s)),
              factory(std::move(f)),
              resolving(r) {}
    };

    std::vector<Entry> entries;

    // Helpers
    Entry* find_exact(std::type_index type, const std::string& name) {
        for (auto& e : entries) {
            if (e.type == type && e.name == name) {
                return &e;
            }
        }
        return nullptr;
    }

    const Entry* find_exact(std::type_index type, const std::string& name) const {
        for (const auto& e : entries) {
            if (e.type == type && e.name == name) {
                return &e;
            }
        }
        return nullptr;
    }

    Entry* find_by_type_default(std::type_index type) {
        for (auto& e : entries) {
            if (e.type == type) {
                return &e;
            }
        }
        return nullptr;
    }

    const Entry* find_by_type_default(std::type_index type) const {
        for (const auto& e : entries) {
            if (e.type == type) {
                return &e;
            }
        }
        return nullptr;
    }

    Entry* find_by_name(const std::string& name) {
        for (auto& e : entries) {
            if (e.name == name) {
                return &e;
            }
        }
        return nullptr;
    }

    const Entry* find_by_name(const std::string& name) const {
        for (const auto& e : entries) {
            if (e.name == name) {
                return &e;
            }
        }
        return nullptr;
    }
};

// ---------------------------------------------------------------------------
// ServiceRegistry
// ---------------------------------------------------------------------------
ServiceRegistry::ServiceRegistry()
    : m_impl(std::make_unique<Impl>()) {}

ServiceRegistry::~ServiceRegistry() = default;

ServiceRegistry::ServiceRegistry(ServiceRegistry&&) noexcept = default;
ServiceRegistry& ServiceRegistry::operator=(ServiceRegistry&&) noexcept = default;

// Registration helpers
void ServiceRegistry::register_typed_service(std::type_index type,
                                              const std::string& name,
                                              std::unique_ptr<IService> service) {
    if (!service) return;
    auto resolved_name = name.empty() ? service->name() : name;
    if (m_impl->find_exact(type, resolved_name)) return;

    m_impl->entries.emplace_back(type, resolved_name, std::move(service),
                                   std::function<std::unique_ptr<IService>()>{}, false);
}

bool ServiceRegistry::has_service(const std::string& name) const {
    return m_impl->find_by_name(name) != nullptr;
}

std::size_t ServiceRegistry::service_count() const {
    return m_impl->entries.size();
}

void ServiceRegistry::register_typed_factory(std::type_index type,
                                              const std::string& name,
                                              std::function<std::unique_ptr<IService>()> factory) {
    if (!factory) return;
    if (m_impl->find_exact(type, name)) return; // duplicate

    m_impl->entries.emplace_back(type, name, nullptr, std::move(factory), false);
}

IService* ServiceRegistry::resolve_typed(std::type_index type, const std::string& name) const {
    Impl::Entry* target = nullptr;

    if (name.empty()) {
        target = m_impl->find_by_type_default(type);
    } else {
        target = m_impl->find_exact(type, name);
    }

    if (!target) {
        return nullptr;
    }

    // Lazy resolution if factory present
    if (!target->instance && target->factory) {
        if (target->resolving) {
            throw CircularDependencyException(
                "Circular dependency detected while resolving service " +
                std::string(type.name()) + " [" + name + "]");
        }
        target->resolving = true;
        target->instance   = target->factory();
        target->resolving  = false;
    }

    return target->instance.get();
}

bool ServiceRegistry::has_typed(std::type_index type, const std::string& name) const {
    if (name.empty()) {
        return m_impl->find_by_type_default(type) != nullptr;
    }
    return m_impl->find_exact(type, name) != nullptr;
}

void ServiceRegistry::clear() {
    m_impl->entries.clear();
}

} // namespace aegis::core
