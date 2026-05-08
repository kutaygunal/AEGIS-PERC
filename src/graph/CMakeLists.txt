add_library(aegis_graph STATIC
    include/aegis/graph/graph_model.hpp
    src/graph_model.cpp
    include/aegis/graph/connectivity_graph.hpp
    src/connectivity_graph.cpp
    include/aegis/graph/data_model.hpp
    src/data_model.cpp
    include/aegis/graph/domain_tagging.hpp
    src/domain_tagging.cpp
    include/aegis/graph/power_intent_application.hpp
    src/power_intent_application.cpp
    include/aegis/graph/current_activity_application.hpp
    src/current_activity_application.cpp
)

target_include_directories(aegis_graph
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(aegis_graph PUBLIC aegis_core aegis_parsing PRIVATE aegis_warnings)
