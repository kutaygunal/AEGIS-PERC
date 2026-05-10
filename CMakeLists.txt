cmake_minimum_required(VERSION 3.25)
project(AEGIS-PERC VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(AEGIS_BUILD_TESTS       "Build unit and integration tests"        ON)
option(AEGIS_WARNINGS_AS_ERRORS "Treat compiler warnings as errors"      OFF)
option(AEGIS_ENABLE_SANITIZERS "Enable AddressSanitizer and UBSan"       OFF)
option(AEGIS_ENABLE_COVERAGE   "Enable test coverage instrumentation"   OFF)
option(AEGIS_ENABLE_CLANG_TIDY "Enable clang-tidy static analysis"      OFF)

# ---------------------------------------------------------------------------
# Compiler warnings interface (applied only to AEGIS targets)
# ---------------------------------------------------------------------------
add_library(aegis_warnings INTERFACE)

if(MSVC)
    target_compile_options(aegis_warnings INTERFACE
        /W4           # elevated warnings
        /permissive-  # strict standards conformance
        /Zc:__cplusplus
        /Zc:preprocessor
        /wd4251       # disable DLL-interface warning for std:: members in headers
    )
    if(AEGIS_WARNINGS_AS_ERRORS)
        target_compile_options(aegis_warnings INTERFACE /WX)
    endif()
else()
    target_compile_options(aegis_warnings INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wnon-virtual-dtor
        -Wold-style-cast
        -Wcast-align
        -Woverloaded-virtual
        -Wconversion
        -Wsign-conversion
    )
    if(AEGIS_WARNINGS_AS_ERRORS)
        target_compile_options(aegis_warnings INTERFACE -Werror)
    endif()
endif()

# ---------------------------------------------------------------------------
# Sanitizers
# ---------------------------------------------------------------------------
if(AEGIS_ENABLE_SANITIZERS)
    if(MSVC)
        add_compile_options(/fsanitize=address)
        add_link_options(/fsanitize=address)
    else()
        add_compile_options(-fsanitize=address,undefined)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()

# ---------------------------------------------------------------------------
# Coverage
# ---------------------------------------------------------------------------
if(AEGIS_ENABLE_COVERAGE AND NOT MSVC)
    add_compile_options(--coverage -O0 -g)
    add_link_options(--coverage)
endif()

# ---------------------------------------------------------------------------
# clang-tidy
# ---------------------------------------------------------------------------
if(AEGIS_ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy)
    if(CLANG_TIDY_EXE)
        set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
        message(STATUS "clang-tidy enabled: ${CLANG_TIDY_EXE}")
    else()
        message(WARNING "clang-tidy not found; static analysis disabled")
    endif()
endif()

# ---------------------------------------------------------------------------
# Fetch dependencies
# ---------------------------------------------------------------------------
include(FetchContent)

FetchContent_Declare(
    zip
    GIT_REPOSITORY https://github.com/kuba--/zip.git
    GIT_TAG        v0.3.4
)
set(ZIP_BUILD_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(zip)

if(AEGIS_ENABLE_SANITIZERS AND NOT WIN32)
    if(TARGET zip)
        target_compile_options(zip PRIVATE -fno-sanitize=shift)
    endif()
endif()

FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.14.1
)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(spdlog)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nlohmann_json)

# Qt6 location for Windows (used by UI module)
list(APPEND CMAKE_PREFIX_PATH "C:/Qt/6.8.2/msvc2022_64")

# ---------------------------------------------------------------------------
# Module libraries + application
# ---------------------------------------------------------------------------
add_subdirectory(src)
add_subdirectory(app)

if(AEGIS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# CPack configuration (desktop application packaging)
# ---------------------------------------------------------------------------
set(CPACK_PACKAGE_NAME "AEGIS-PERC")
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(CPACK_PACKAGE_VENDOR "AEGIS-PERC Team")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "AEGIS-PERC — Physical/Electrical Rule Checker")
if(WIN32)
    set(CPACK_GENERATOR "ZIP;NSIS")
elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop;TGZ")
else()
    set(CPACK_GENERATOR "TGZ;DEB")
endif()
set(CPACK_PACKAGE_CONTACT "contact@aegis-perc.dev")
include(CPack)
