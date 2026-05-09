set(CMAKE_AUTOMOC ON)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(Qt6 6.4 REQUIRED COMPONENTS Core Widgets)
else()
    find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets)
endif()

add_library(aegis_ui STATIC
    include/aegis/ui/main_window.hpp
    src/main_window.cpp
    include/aegis/ui/activity_log_panel.hpp
    src/activity_log_panel.cpp
    include/aegis/ui/layer_panel.hpp
    src/layer_panel.cpp
    include/aegis/ui/layout_canvas.hpp
    src/layout_canvas.cpp
    include/aegis/ui/connectivity_trace.hpp
    src/connectivity_trace.cpp
    include/aegis/ui/graph_explorer_panel.hpp
    src/graph_explorer_panel.cpp
    include/aegis/ui/properties_panel.hpp
    src/properties_panel.cpp
    include/aegis/ui/report_preview_panel.hpp
    src/report_preview_panel.cpp
    include/aegis/ui/trace_panel.hpp
    src/trace_panel.cpp
    include/aegis/ui/scene_adapter.hpp
    src/scene_adapter.cpp
    include/aegis/ui/selection_model.hpp
    src/selection_model.cpp
    include/aegis/ui/violation_overlay.hpp
    src/violation_overlay.cpp
    include/aegis/ui/violation_heatmap.hpp
    src/violation_heatmap.cpp
    include/aegis/ui/violation_filter.hpp
    src/violation_filter.cpp
    include/aegis/ui/violation_filter_proxy_model.hpp
    src/violation_filter_proxy_model.cpp
    include/aegis/ui/violation_table_model.hpp
    src/violation_table_model.cpp
    include/aegis/ui/violation_explorer_panel.hpp
    src/violation_explorer_panel.cpp
)

target_include_directories(aegis_ui
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_definitions(aegis_ui PRIVATE AEGIS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

target_link_libraries(aegis_ui
    PUBLIC
        Qt6::Core
        Qt6::Widgets
        aegis_graph
        aegis_parsing
        aegis_rules
    PRIVATE
        aegis_core
        aegis_storage
        aegis_orchestration
        aegis_warnings
)
