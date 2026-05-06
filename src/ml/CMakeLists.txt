add_library(aegis_ml STATIC
    include/aegis/ml/feature_extractor.hpp
    src/feature_extractor.cpp
)

target_include_directories(aegis_ml
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(aegis_ml PUBLIC aegis_core aegis_graph)
