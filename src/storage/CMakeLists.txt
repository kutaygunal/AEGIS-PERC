add_library(aegis_storage STATIC
    include/aegis/storage/storage_engine.hpp
    src/storage_engine.cpp
)

target_include_directories(aegis_storage
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(aegis_storage PUBLIC aegis_core PRIVATE aegis_warnings)
