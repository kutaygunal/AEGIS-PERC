add_executable(aegis-perc main.cpp)

target_link_libraries(aegis-perc
    PRIVATE
        aegis_core
        aegis_ui
        aegis_warnings
)

set_target_properties(aegis-perc PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
