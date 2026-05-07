@echo off
setlocal enabledelayedexpansion

echo ==========================================
echo  AEGIS-PERC — Incremental Build + Deploy
echo ==========================================
echo.

set "PROJECT_DIR=%~dp0"
cd /d "%PROJECT_DIR%" || (
    echo ERROR: Could not switch to project directory.
    pause
    exit /b 1
)

:: ---------------------------------------------------------------------------
:: Step 1 — CMake configure (fast no-op if already configured)
:: ---------------------------------------------------------------------------
echo [1/4] Configuring with CMake...
if not exist "build\CMakeCache.txt" (
    cmake -S . -B build -G "Visual Studio 17 2022" -A x64
) else (
    cmake -S . -B build
)
if errorlevel 1 (
    echo ERROR: CMake configuration failed.
    pause
    exit /b 1
)
echo          Done.

:: ---------------------------------------------------------------------------
:: Step 2 — Incremental build
:: ---------------------------------------------------------------------------
echo.
echo [2/4] Building Release configuration...
cmake --build build --config Release
if errorlevel 1 (
    echo ERROR: Build failed.
    pause
    exit /b 1
)
echo          Done.

:: ---------------------------------------------------------------------------
:: Step 3 — Run tests
:: ---------------------------------------------------------------------------
echo.
echo [3/4] Running tests...
ctest --test-dir build --output-on-failure --build-config Release
if errorlevel 1 (
    echo WARNING: One or more tests failed.
) else (
    echo          All tests passed.
)

:: ---------------------------------------------------------------------------
:: Step 4 — Deploy Qt runtime DLLs
:: ---------------------------------------------------------------------------
echo.
echo [4/4] Deploying Qt runtime dependencies...

set "QT_WDEPLOY=C:\Qt\6.8.2\msvc2022_64\bin\windeployqt.exe"
set "EXE_PATH=build\bin\Release\aegis-perc.exe"

if not exist "%QT_WDEPLOY%" (
    echo ERROR: windeployqt.exe not found at:
    echo        %QT_WDEPLOY%
    echo        Adjust the path inside build.bat if your Qt installation differs.
    pause
    exit /b 1
)

"%QT_WDEPLOY%" "%EXE_PATH%" --release
if errorlevel 1 (
    echo WARNING: windeployqt reported issues ^(often harmless warnings^).
)
echo          Done.

:: ---------------------------------------------------------------------------
:: Summary
:: ---------------------------------------------------------------------------
echo.
echo ==========================================
echo  BUILD COMPLETE — Ready to run
echo ==========================================
echo.
echo Executable:
echo   %PROJECT_DIR%%EXE_PATH%
echo.

:: Launch the binary to prove it works, then pause so the window stays open.
echo Launching aegis-perc.exe...
echo.
"%PROJECT_DIR%%EXE_PATH%"
if errorlevel 1 (
    echo.
    echo WARNING: Executable returned a non-zero exit code.
)

echo.
pause
