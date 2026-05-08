add_executable(aegis-perc main.cpp)
add_executable(aegis-perc-cli cli_main.cpp)

target_link_libraries(aegis-perc
    PRIVATE
        aegis_core
        aegis_ui
        aegis_warnings
)

target_link_libraries(aegis-perc-cli
    PRIVATE
        aegis_core
        aegis_scripting
        aegis_warnings
)

set_target_properties(aegis-perc aegis-perc-cli PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
