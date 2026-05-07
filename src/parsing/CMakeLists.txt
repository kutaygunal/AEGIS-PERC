add_library(aegis_parsing STATIC
    include/aegis/parsing/parser_interface.hpp
    src/parser_interface.cpp
)

target_include_directories(aegis_parsing
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(aegis_parsing PUBLIC aegis_core PRIVATE aegis_warnings)
