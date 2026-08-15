add_library(aegis_orchestration STATIC
    include/aegis/orchestration/job_pipeline.hpp
    src/job_pipeline.cpp
    include/aegis/orchestration/distributed_job.hpp
    src/distributed_job.cpp
    include/aegis/orchestration/distributed_host.hpp
    src/distributed_host.cpp
    include/aegis/orchestration/distributed_worker.hpp
    src/distributed_worker.cpp
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
