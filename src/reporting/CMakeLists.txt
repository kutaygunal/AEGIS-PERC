add_library(aegis_reporting STATIC
    include/aegis/reporting/report_generator.hpp
    src/report_generator.cpp
)

target_include_directories(aegis_reporting
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(aegis_reporting PUBLIC aegis_core)
