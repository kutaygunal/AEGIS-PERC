#pragma once

#include <memory>
#include <string>

namespace aegis::storage {

class StorageEngine {
public:
    StorageEngine();
    ~StorageEngine();

    StorageEngine(const StorageEngine&) = delete;
    StorageEngine& operator=(const StorageEngine&) = delete;
    StorageEngine(StorageEngine&&) noexcept;
    StorageEngine& operator=(StorageEngine&&) noexcept;

    bool open(const std::string& path);
    bool is_open() const;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace aegis::storage
