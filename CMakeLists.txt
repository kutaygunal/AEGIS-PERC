cmake_minimum_required(VERSION 3.25)
project(AEGIS-PERC VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(AEGIS_BUILD_TESTS "Build unit and integration tests" ON)

# Qt6 location for Windows (used by UI module)
list(APPEND CMAKE_PREFIX_PATH "C:/Qt/6.8.2/msvc2022_64")

# Module libraries
add_subdirectory(src)

# Application executable
add_subdirectory(app)

if(AEGIS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
