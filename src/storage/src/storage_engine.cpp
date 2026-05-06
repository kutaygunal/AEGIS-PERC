#include "aegis/storage/storage_engine.hpp"

namespace aegis::storage {

struct StorageEngine::Impl {
    bool open = false;
};

StorageEngine::StorageEngine() : m_impl(std::make_unique<Impl>()) {}
StorageEngine::~StorageEngine() = default;
StorageEngine::StorageEngine(StorageEngine&&) noexcept = default;
StorageEngine& StorageEngine::operator=(StorageEngine&&) noexcept = default;

bool StorageEngine::open(const std::string& /*path*/) {
    m_impl->open = true;
    return true;
}

bool StorageEngine::is_open() const {
    return m_impl->open;
}

} // namespace aegis::storage
