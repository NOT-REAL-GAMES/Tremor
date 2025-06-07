#!/bin/bash
# Build script for Tremor on Linux/Unix

echo "Building Tremor Engine..."
echo

# Check if we're in the Tremor directory
if [ ! -f "CMakeLists.txt" ]; then
    echo "Error: This script must be run from the Tremor directory"
    exit 1
fi

# Parse arguments
BUILD_TYPE="Debug"
CLEAN_BUILD=0
COMPILE_SHADERS=0

while [[ $# -gt 0 ]]; do
    case $1 in
        --release)
            BUILD_TYPE="Release"
            shift
            ;;
        --clean)
            CLEAN_BUILD=1
            shift
            ;;
        --compile-shaders)
            COMPILE_SHADERS=1
            shift
            ;;
        --help)
            echo "Usage: $0 [options]"
            echo "Options:"
            echo "  --release         Build in Release mode (default: Debug)"
            echo "  --clean           Clean rebuild"
            echo "  --compile-shaders Compile GLSL shaders to SPIR-V"
            echo "  --help            Show this help message"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

# Clean if requested
if [ $CLEAN_BUILD -eq 1 ]; then
    echo "Cleaning previous build..."
    rm -rf build
fi

# Create build directory
mkdir -p build
cd build

# Configure with CMake
echo "Configuring project (${BUILD_TYPE} mode)..."
CMAKE_ARGS=""
if [ $COMPILE_SHADERS -eq 1 ]; then
    CMAKE_ARGS="${CMAKE_ARGS} -DCOMPILE_SHADERS=ON"
fi

cmake -DCMAKE_BUILD_TYPE=${BUILD_TYPE} ${CMAKE_ARGS} ..
if [ $? -ne 0 ]; then
    echo
    echo "CMake configuration failed!"
    echo
    echo "Make sure you have installed:"
    echo "- CMake 3.16+"
    echo "- Vulkan SDK"
    echo "- SDL2 development libraries"
    echo "- A C++23 capable compiler"
    cd ..
    exit 1
fi

# Build
echo
echo "Building project..."
make -j$(nproc)
if [ $? -ne 0 ]; then
    echo
    echo "Build failed!"
    cd ..
    exit 1
fi

echo
echo "Build successful!"
echo "Executable location: build/bin/Tremor"
echo
echo "To run: ./build/bin/Tremor"

cd ..