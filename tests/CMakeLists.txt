include(FetchContent)
include(CTest)

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
    unit/data/test_sample_data.cpp
    unit/graph/test_graph_minimal.cpp
    unit/graph/test_connectivity_graph.cpp
    unit/graph/test_data_model.cpp
    unit/ml/test_ml_minimal.cpp
    unit/parsing/test_parsing_minimal.cpp
    unit/parsing/test_layout_ir.cpp
    unit/parsing/test_def_parser.cpp
    unit/reporting/test_reporting_minimal.cpp
    unit/rules/test_rules_minimal.cpp
    unit/rules/test_rule_engine.cpp
    unit/rules/test_electrical_rules.cpp
    unit/rules/test_violation_model.cpp
    unit/scripting/test_scripting_minimal.cpp
    unit/storage/test_storage_minimal.cpp
)

target_compile_definitions(aegis_unit_tests PRIVATE AEGIS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

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
        Catch2::Catch2WithMain
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
