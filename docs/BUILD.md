# Build Instructions

## Prerequisites

- **CMake** >= 3.25
- **C++20** compiler
  - Windows: MSVC 2022 (v17+)
  - Linux: GCC 12+ or Clang 16+
  - macOS: Xcode 15+ / Clang 16+
- **Qt6** >= 6.5 (Windows path hint: `C:/Qt/6.8.2/msvc2022_64`)
- **Ninja** (Linux / macOS presets; optional on Windows)
- **Python** 3.10+ (for sample validation scripts)

## Quick Start

### Windows

```powershell
# Release
\ncmake --preset windows-release
\ncmake --build --preset windows-release --config Release
\nctest --preset windows-test

# Debug
\ncmake --preset windows-debug
\ncmake --build --preset windows-debug --config Debug

# CI workflow (configure + build + test)
\ncmake --workflow --preset ci-windows-release
```

### Linux / macOS

```bash
# Release
cmake --preset linux-release
cmake --build --preset linux-release
ctest --preset linux-test

# Debug with sanitizers
cmake --preset linux-asan-debug
cmake --build --preset linux-asan-debug
ctest --preset linux-test

# Coverage baseline
cmake --preset linux-coverage
cmake --build --preset linux-coverage
ctest --preset linux-test
```

## Compiler Warnings Policy

AEGIS-PERC maintains a **zero-tolerance warnings policy** on all first-party code.

- **Local development**: warnings are elevated (`/W4` on MSVC, `-Wall -Wextra -Wpedantic` on GCC/Clang) but do **not** block compilation by default.
- **CI / Preset builds**: `AEGIS_WARNINGS_AS_ERRORS` is enabled (`/WX` or `-Werror`). Any warning introduced by a pull request is treated as a build failure.
- **Third-party dependencies**: fetched via `FetchContent` are consumed as system headers and are not subject to our warning levels.
- **Qt headers**: imported via `find_package(Qt6)` are treated as system includes by CMake and do not trigger project warnings.

To disable warnings-as-errors locally:

```bash
cmake -B build -DAEGIS_WARNINGS_AS_ERRORS=OFF
```

## Static Analysis

`clang-tidy` rules are configured in `.clang-tidy`. To enable during CMake configuration:

```bash
cmake -B build -DAEGIS_ENABLE_CLANG_TIDY=ON
```

This runs `clang-tidy` as part of the build. It is **not** enabled by default to keep local iteration fast.

## Sanitizers

AddressSanitizer (ASan) and UndefinedBehaviorSanitizer (UBSan) presets are provided for debug builds.

| Platform | Preset | Sanitizers |
|----------|--------|------------|
| Windows | `windows-asan-debug` | AddressSanitizer |
| Linux/macOS | `linux-asan-debug` | ASan + UBSan |

UBSan is not available on MSVC, so the Windows preset only enables ASan.

## Coverage

Coverage instrumentation is supported on GCC/Clang via the `linux-coverage` preset:

```bash
cmake --preset linux-coverage
cmake --build --preset linux-coverage
ctest --preset linux-test
# Then run lcov / gcovr to capture baseline
```

## Packaging

CPack is configured in the root `CMakeLists.txt`. After building:

```bash
cpack -C Release
```

This produces platform-native packages (ZIP / NSIS on Windows, TGZ / DEB on Linux, DragNDrop on macOS).
