add_library(aegis_rules STATIC
    include/aegis/rules/rule_engine.hpp
    src/rule_engine.cpp
)

target_include_directories(aegis_rules
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(aegis_rules PUBLIC aegis_core aegis_graph PRIVATE aegis_warnings)
