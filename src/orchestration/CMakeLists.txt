add_library(aegis_orchestration STATIC
    include/aegis/orchestration/job_pipeline.hpp
    src/job_pipeline.cpp
)

target_include_directories(aegis_orchestration
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(aegis_orchestration
    PUBLIC
        aegis_core
        aegis_graph
        aegis_parsing
        aegis_reporting
        aegis_rules
        aegis_storage
    PRIVATE
        nlohmann_json::nlohmann_json
        aegis_warnings
)
