include(FetchContent)
include(CTest)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(Qt6 6.4 REQUIRED COMPONENTS Test)
else()
    find_package(Qt6 6.5 REQUIRED COMPONENTS Test)
endif()

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.8.0
)
FetchContent_MakeAvailable(Catch2)

# Make Catch2 extras available for catch_discover_tests
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)

add_executable(aegis_unit_tests
    unit/core/test_minimal.cpp
    unit/core/test_logging.cpp
    unit/core/test_config.cpp
    unit/core/test_diagnostic_bundle.cpp
    unit/core/test_config_migration.cpp
    unit/ui/test_app_shell.cpp
    unit/ui/test_scene_adapter.cpp
    unit/ui/test_layout_canvas.cpp
    unit/ui/test_canvas_viewport.cpp
    unit/ui/test_canvas_viewport_p13.cpp
    unit/ui/test_layer_panel.cpp
    unit/ui/test_layer_semantics.cpp
    unit/ui/test_canvas_selection.cpp
    unit/ui/test_canvas_coloring.cpp
    unit/ui/test_violation_overlay.cpp
    unit/ui/test_violation_explorer.cpp
    unit/ui/test_violation_filter.cpp
    unit/ui/test_connectivity_trace.cpp
    unit/ui/test_graph_explorer.cpp
    unit/ui/test_graph_explorer_ui.cpp
    unit/ui/test_hierarchy_browser.cpp
    unit/ui/test_properties_panel.cpp
    unit/ui/test_violation_heatmap.cpp
    unit/ui/test_report_preview.cpp
    unit/ui/test_report_preview_ui.cpp
    unit/ui/test_workspace_actions.cpp
    unit/ui/test_help_actions.cpp
    unit/ui/test_activity_log.cpp
    unit/ui/test_import_wizard.cpp
    unit/ui/test_sample_workflow.cpp
    unit/ui/test_run_checks.cpp
    unit/ui/test_workspace_state.cpp
    unit/ui/test_workspace_persistence.cpp
    unit/ui/test_viewport_presets.cpp
    unit/ui/test_trace_workflow.cpp
    unit/ui/test_ui_states.cpp
    unit/ui/test_ui_notifications.cpp
    unit/ui/test_canvas_performance.cpp
    unit/ui/test_ui_workflow.cpp
    unit/data/test_sample_data.cpp
    unit/graph/test_graph_minimal.cpp
    unit/graph/test_connectivity_graph.cpp
    unit/graph/test_data_model.cpp
    unit/graph/test_domain_tagging.cpp
    unit/ml/test_ml_minimal.cpp
    unit/parsing/test_parsing_minimal.cpp
    unit/parsing/test_layout_ir.cpp
    unit/parsing/test_def_parser.cpp
    unit/parsing/test_spice_parser.cpp
    unit/parsing/test_verilog_parser.cpp
    unit/parsing/test_lef_parser.cpp
    unit/parsing/test_power_intent.cpp
    unit/parsing/test_current_activity.cpp
    unit/reporting/test_reporting_minimal.cpp
    unit/rules/test_rules_minimal.cpp
    unit/rules/test_rule_engine.cpp
    unit/rules/test_electrical_rules.cpp
    unit/rules/test_violation_model.cpp
    unit/rules/test_rule_pack.cpp
    unit/scripting/test_scripting_minimal.cpp
    unit/scripting/test_batch_cli.cpp
    unit/orchestration/test_job_pipeline.cpp
    unit/storage/test_storage_minimal.cpp
    unit/storage/test_project_package.cpp
    unit/storage/test_import_validation.cpp
    unit/storage/test_imported_design_session.cpp
)

target_compile_definitions(aegis_unit_tests PRIVATE
    AEGIS_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    AEGIS_BINARY_DIR="${CMAKE_BINARY_DIR}"
)

target_link_libraries(aegis_unit_tests
    PRIVATE
        aegis_core
        aegis_ui
        aegis_graph
        aegis_ml
        aegis_parsing
        aegis_reporting
        aegis_rules
        aegis_scripting
        aegis_storage
        aegis_orchestration
        Catch2::Catch2WithMain
        Qt6::Test
        zip
        aegis_warnings
)

# Auto-discover Catch2 tests and register each as a CTest test.
# Tests are discovered at pre-test time so Qt DLL PATH is available.
include(Catch)

if(WIN32)
    catch_discover_tests(aegis_unit_tests
        DISCOVERY_MODE PRE_TEST
        ADD_TAGS_AS_LABELS
        EXTRA_ARGS --reporter console --verbosity high
        DL_PATHS C:/Qt/6.8.2/msvc2022_64/bin
    )
else()
    catch_discover_tests(aegis_unit_tests
        DISCOVERY_MODE PRE_TEST
        ADD_TAGS_AS_LABELS
        EXTRA_ARGS --reporter console --verbosity high
    )
endif()
