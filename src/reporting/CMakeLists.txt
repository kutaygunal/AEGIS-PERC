add_library(aegis_reporting STATIC
    include/aegis/reporting/baseline.hpp
    include/aegis/reporting/regression_diff.hpp
    include/aegis/reporting/report_generator.hpp
    include/aegis/reporting/signoff_exports.hpp
    src/baseline.cpp
    src/regression_diff.cpp
    src/report_generator.cpp
    src/signoff_exports.cpp
)

target_include_directories(aegis_reporting
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(aegis_reporting PUBLIC aegis_core aegis_rules aegis_graph PRIVATE aegis_warnings)
