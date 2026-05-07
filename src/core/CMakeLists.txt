add_library(aegis_core STATIC
    include/aegis/core/service_registry.hpp
    include/aegis/core/logger.hpp
    include/aegis/core/config.hpp
    include/aegis/core/config_migrator.hpp
    include/aegis/core/diagnostic_bundle.hpp
    src/service_registry.cpp
    src/logger.cpp
    src/config.cpp
    src/config_migrator.cpp
    src/diagnostic_bundle.cpp
)

target_link_libraries(aegis_core
    PUBLIC
        spdlog::spdlog
        nlohmann_json::nlohmann_json
    PRIVATE
        zip
        aegis_warnings
)

target_include_directories(aegis_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

set_target_properties(aegis_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
