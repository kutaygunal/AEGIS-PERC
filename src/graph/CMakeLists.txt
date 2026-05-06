add_library(aegis_graph STATIC
    include/aegis/graph/graph_model.hpp
    src/graph_model.cpp
)

target_include_directories(aegis_graph
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(aegis_graph PUBLIC aegis_core)
