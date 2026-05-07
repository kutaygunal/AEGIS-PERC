@echo off
setlocal enabledelayedexpansion

echo ==========================================
echo  AEGIS-PERC — Clean Build + Deploy
echo ==========================================
echo.

set "PROJECT_DIR=%~dp0"
cd /d "%PROJECT_DIR%" || (
    echo ERROR: Could not switch to project directory.
    pause
    exit /b 1
)

:: ---------------------------------------------------------------------------
:: Step 0 — Kill lingering build processes that may lock build/_deps
:: ---------------------------------------------------------------------------
echo [0/5] Terminating lingering build processes...
for %%P in (cmake msbuild cl link git git-lfs) do (
    taskkill /F /IM %%P.exe 2>nul >nul
)
echo          Done.
:: ---------------------------------------------------------------------------
echo [1/5] Removing old build directory...
if exist "build" (
    :: Retry loop: locked files ^(CMake logs, git packs, IDE handles^) can block deletion.
    set "TRIES=0"
    :CLEAN_RETRY
    rd /s /q "build" 2>nul
    :: rd doesn't always set errorlevel on partial failure, so check existence.
    if exist "build" (
        set /a TRIES+=1
        if !TRIES! lss 3 (
            echo          Some files locked. Retrying in 2 seconds... ^(!TRIES!/3^)
            timeout /t 2 /nobreak >nul
            goto CLEAN_RETRY
        )
        echo.
        echo ERROR: Could not fully delete the 'build' directory.
        echo        Some files are locked by another process ^(IDE, file explorer, antivirus, etc.^).
        echo.
        echo        Please:
        echo          1. Close any IDE or file explorer windows showing the build folder.
        echo          2. Check Task Manager for CMake, MSBuild, or git processes.
        echo          3. Manually delete 'build\' if needed.
        echo          4. Re-run this script.
        echo.
        pause
        exit /b 1
    )
)
echo          Done.

:: ---------------------------------------------------------------------------
:: Step 2 — CMake configure
:: ---------------------------------------------------------------------------
echo.
echo [2/5] Configuring with CMake...
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
if errorlevel 1 (
    echo ERROR: CMake configuration failed.
    pause
    exit /b 1
)
echo          Done.

:: ---------------------------------------------------------------------------
:: Step 3 — Build Release
:: ---------------------------------------------------------------------------
echo.
echo [3/5] Building Release configuration...
cmake --build build --config Release
if errorlevel 1 (
    echo ERROR: Build failed.
    pause
    exit /b 1
)
echo          Done.

:: ---------------------------------------------------------------------------
:: Step 4 — Run tests
:: ---------------------------------------------------------------------------
echo.
echo [4/5] Running tests...
ctest --test-dir build --output-on-failure --build-config Release
if errorlevel 1 (
    echo WARNING: One or more tests failed.
) else (
    echo          All tests passed.
)

:: ---------------------------------------------------------------------------
:: Step 5 — Deploy Qt runtime DLLs
:: ---------------------------------------------------------------------------
echo.
echo [5/5] Deploying Qt runtime dependencies...

set "QT_WDEPLOY=C:\Qt\6.8.2\msvc2022_64\bin\windeployqt.exe"
set "EXE_PATH=build\bin\Release\aegis-perc.exe"

if not exist "%QT_WDEPLOY%" (
    echo ERROR: windeployqt.exe not found at:
    echo        %QT_WDEPLOY%
    echo        Adjust the path inside built.bat if your Qt installation differs.
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
