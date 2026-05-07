set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets)

add_library(aegis_ui STATIC
    include/aegis/ui/main_window.hpp
    src/main_window.cpp
)

target_include_directories(aegis_ui
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(aegis_ui
    PUBLIC
        Qt6::Core
        Qt6::Widgets
    PRIVATE
        aegis_core
        aegis_warnings
)
