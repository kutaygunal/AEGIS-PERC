add_library(aegis_core STATIC
    include/aegis/core/service_registry.hpp
    src/service_registry.cpp
)

target_include_directories(aegis_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

set_target_properties(aegis_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
