#pragma once

#include <string>

namespace aegis::parsing {

class IParser {
public:
    virtual ~IParser() = default;
    virtual bool open(const std::string& path) = 0;
    virtual void close() = 0;
    virtual bool is_open() const = 0;
};

} // namespace aegis::parsing
