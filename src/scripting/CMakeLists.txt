add_library(aegis_scripting STATIC
    include/aegis/scripting/python_api.hpp
    src/python_api.cpp
)

target_include_directories(aegis_scripting
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(aegis_scripting PUBLIC aegis_core PRIVATE aegis_warnings)
