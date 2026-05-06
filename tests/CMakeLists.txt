include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.8.0
)
FetchContent_MakeAvailable(Catch2)

add_executable(aegis_unit_tests
    unit/core/test_minimal.cpp
)

target_link_libraries(aegis_unit_tests
    PRIVATE
        aegis_core
        Catch2::Catch2WithMain
)

add_test(NAME aegis_unit COMMAND aegis_unit_tests)
set_tests_properties(aegis_unit PROPERTIES LABELS "unit;core")
