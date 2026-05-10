add_library(aegis_storage STATIC
    include/aegis/storage/storage_engine.hpp
    include/aegis/storage/project_package.hpp
    include/aegis/storage/import_validation.hpp
    include/aegis/storage/imported_design_session.hpp
    src/storage_engine.cpp
    src/project_package.cpp
    src/import_validation.cpp
    src/imported_design_session.cpp
)

target_include_directories(aegis_storage
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(aegis_storage
    PUBLIC
        aegis_core
        aegis_parsing
        aegis_graph
    PRIVATE
        aegis_warnings
)
