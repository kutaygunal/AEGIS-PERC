#pragma once

#include <memory>
#include <string>

namespace aegis::core {

class IService {
public:
    virtual ~IService() = default;
    virtual std::string name() const = 0;
};

class ServiceRegistry {
public:
    ServiceRegistry();
    ~ServiceRegistry();

    ServiceRegistry(const ServiceRegistry&) = delete;
    ServiceRegistry& operator=(const ServiceRegistry&) = delete;

    ServiceRegistry(ServiceRegistry&&) noexcept;
    ServiceRegistry& operator=(ServiceRegistry&&) noexcept;

    void register_service(std::unique_ptr<IService> service);
    bool has_service(const std::string& name) const;
    std::size_t service_count() const;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace aegis::core
