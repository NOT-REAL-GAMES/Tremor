@echo off
REM Build script for Tremor on Windows

echo Building Tremor Engine...
echo.

REM Check if we're in the Tremor directory
if not exist "CMakeLists.txt" (
    echo Error: This script must be run from the Tremor directory
    exit /b 1
)

where cmake >nul 2>nul
if errorlevel 1 (
    echo Error: cmake was not found on PATH.
    echo Install CMake 3.20+ or run this script from a shell where CMake is available.
    exit /b 1
)

set "VSWHERE=C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo Error: vswhere.exe was not found at "%VSWHERE%".
    echo Install Visual Studio 2022 or newer with the C++ desktop development workload.
    exit /b 1
)

set "VS_VERSION="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion`) do set "VS_VERSION=%%i"

if not defined VS_VERSION (
    echo Error: no Visual Studio installation with MSVC C++ tools was found.
    echo Install Visual Studio 2022 or newer with the C++ desktop development workload.
    exit /b 1
)

for /f "tokens=1 delims=." %%i in ("%VS_VERSION%") do set "VS_MAJOR=%%i"

if "%VS_MAJOR%"=="18" (
    set "CMAKE_GENERATOR=Visual Studio 18 2026"
    set "BUILD_DIR=build-vs18"
) else if "%VS_MAJOR%"=="17" (
    set "CMAKE_GENERATOR=Visual Studio 17 2022"
    set "BUILD_DIR=build-vs17"
) else (
    echo Error: unsupported Visual Studio major version "%VS_MAJOR%" detected from "%VS_VERSION%".
    echo Update this script to add the matching CMake generator name.
    exit /b 1
)

echo Using CMake generator: %CMAKE_GENERATOR%
echo Build directory: %BUILD_DIR%

REM Configure with CMake
echo.
echo Configuring project...
cmake -G "%CMAKE_GENERATOR%" -A x64 -S . -B "%BUILD_DIR%"
if errorlevel 1 (
    echo.
    echo CMake configuration failed!
    echo.
    echo Common issues:
    echo - Make sure Visual Studio 2022 or newer is installed with MSVC C++ tools
    echo - Make sure Vulkan SDK is installed
    echo - If SDL2 is not found, try setting SDL2_PATH:
    echo   cmake -G "%CMAKE_GENERATOR%" -A x64 -DSDL2_PATH=C:\path\to\SDL2 -S . -B "%BUILD_DIR%"
    exit /b 1
)

REM Build
echo.
echo Building project...
cmake --build "%BUILD_DIR%" --config Release
if errorlevel 1 (
    echo.
    echo Build failed!
    exit /b 1
)

echo.
echo Build successful!
echo Executable location: %BUILD_DIR%\bin\Release\Tremor.exe
echo.
echo To run: %BUILD_DIR%\bin\Release\Tremor.exe
