@echo off
REM Build script for Tremor on Windows

echo Building Tremor Engine...
echo.

REM Check if we're in the Tremor directory
if not exist "CMakeLists.txt" (
    echo Error: This script must be run from the Tremor directory
    exit /b 1
)

REM Create build directory
if not exist "build" mkdir build
cd build

REM Configure with CMake
echo Configuring project...
cmake -G "Visual Studio 17 2022" -A x64 ..
if errorlevel 1 (
    echo.
    echo CMake configuration failed!
    echo.
    echo Common issues:
    echo - Make sure Visual Studio 2022 is installed
    echo - Make sure Vulkan SDK is installed
    echo - If SDL2 is not found, try setting SDL2_PATH:
    echo   cmake -G "Visual Studio 17 2022" -A x64 -DSDL2_PATH=C:\path\to\SDL2 ..
    cd ..
    exit /b 1
)

REM Build
echo.
echo Building project...
cmake --build . --config Release
if errorlevel 1 (
    echo.
    echo Build failed!
    cd ..
    exit /b 1
)

echo.
echo Build successful!
echo Executable location: build\bin\Release\Tremor.exe
echo.
echo To run: build\bin\Release\Tremor.exe

cd ..