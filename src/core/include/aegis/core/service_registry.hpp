#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <type_traits>

namespace aegis::core {

// ---------------------------------------------------------------------------
// Exception
// ---------------------------------------------------------------------------
class CircularDependencyException : public std::runtime_error {
public:
    explicit CircularDependencyException(const std::string& message);
};

// ---------------------------------------------------------------------------
// Base interface
// ---------------------------------------------------------------------------
class IService {
public:
    virtual ~IService() = default;
    virtual std::string name() const = 0;
};

// ---------------------------------------------------------------------------
// Service Registry
// ---------------------------------------------------------------------------
class ServiceRegistry {
public:
    ServiceRegistry();
    ~ServiceRegistry();

    ServiceRegistry(const ServiceRegistry&) = delete;
    ServiceRegistry& operator=(const ServiceRegistry&) = delete;

    ServiceRegistry(ServiceRegistry&&) noexcept;
    ServiceRegistry& operator=(ServiceRegistry&&) noexcept;

    // Type-safe named registration (legacy style also supported)
    template<typename T>
    void register_service(std::unique_ptr<T> service, const std::string& name = {});

    // Legacy helpers (kept for compat)
    bool has_service(const std::string& name) const;
    std::size_t service_count() const;

    // Factory-based lazy registration
    template<typename T, typename Factory>
    void register_factory(Factory&& factory, const std::string& name = {});

    // Type-safe resolution (returns nullptr if not found)
    template<typename T>
    T* resolve(const std::string& name = {}) const;

    // Optional resolution
    template<typename T>
    std::optional<T*> try_resolve(const std::string& name = {}) const;

    // Check availability by type (and optionally name)
    template<typename T>
    bool has(const std::string& name = {}) const;

    // Explicit lifetime management
    void clear();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    // Internal type-erased helpers
    void register_typed_service(std::type_index type,
                                const std::string& name,
                                std::unique_ptr<IService> service);
    void register_typed_factory(std::type_index type,
                                 const std::string& name,
                                 std::function<std::unique_ptr<IService>()> factory);
    IService* resolve_typed(std::type_index type, const std::string& name) const;
    bool has_typed(std::type_index type, const std::string& name) const;
};

} // namespace aegis::core

// ---------------------------------------------------------------------------
// Inline template implementation
// ---------------------------------------------------------------------------
namespace aegis::core {

template<typename T>
void ServiceRegistry::register_service(std::unique_ptr<T> service, const std::string& name) {
    static_assert(std::is_base_of_v<IService, T>, "T must derive from IService");
    register_typed_service(std::type_index(typeid(T)), name, std::move(service));
}

template<typename T, typename Factory>
void ServiceRegistry::register_factory(Factory&& factory, const std::string& name) {
    static_assert(std::is_base_of_v<IService, T>, "T must derive from IService");
    register_typed_factory(std::type_index(typeid(T)), name,
                          std::forward<Factory>(factory));
}

template<typename T>
T* ServiceRegistry::resolve(const std::string& name) const {
    static_assert(std::is_base_of_v<IService, T>, "T must derive from IService");
    auto* base = resolve_typed(std::type_index(typeid(T)), name);
    return base ? static_cast<T*>(base) : nullptr;
}

template<typename T>
std::optional<T*> ServiceRegistry::try_resolve(const std::string& name) const {
    auto* ptr = resolve<T>(name);
    return ptr ? std::optional<T*>(ptr) : std::nullopt;
}

template<typename T>
bool ServiceRegistry::has(const std::string& name) const {
    static_assert(std::is_base_of_v<IService, T>, "T must derive from IService");
    return has_typed(std::type_index(typeid(T)), name);
}

} // namespace aegis::core
