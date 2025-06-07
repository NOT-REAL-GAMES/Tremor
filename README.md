# Tremor Engine

Tremor is a modern game engine built on a reimagined foundation inspired by the Quake Engine, featuring Vulkan-based clustered rendering and integration with the Taffy asset format.

## Prerequisites

### All Platforms
- CMake 3.16 or higher
- C++23 compatible compiler
- Vulkan SDK
- SDL2

### Platform-Specific Requirements

#### Windows
- Visual Studio 2022 with C++ desktop development
- Vulkan SDK from https://vulkan.lunarg.com/
- SDL2 (often included with Vulkan SDK in Third-Party folder)

#### Linux
```bash
# Ubuntu/Debian
sudo apt install build-essential cmake libvulkan-dev vulkan-tools libsdl2-dev libglm-dev glslc

# Fedora
sudo dnf install gcc-c++ cmake vulkan-devel SDL2-devel glm-devel glslc

# Arch
sudo pacman -S base-devel cmake vulkan-devel sdl2 glm shaderc
```

## Building

### Linux/Unix
```bash
cd Tremor
mkdir build
cd build
cmake ..
make -j$(nproc)
```

### Windows (Command Line)
```cmd
cd Tremor
mkdir build
cd build
cmake -G "Visual Studio 17 2022" -A x64 ..
cmake --build . --config Release
```

### Windows (Visual Studio)
1. Open the Tremor folder in Visual Studio 2022
2. VS will automatically detect CMakeLists.txt
3. Select x64-Release or x64-Debug configuration
4. Build → Build All

## Running

The executable will be in `build/bin/`:
```bash
# Linux
./build/bin/Tremor

# Windows
build\bin\Release\Tremor.exe
```

## Troubleshooting

### SDL2 Not Found (Windows)
If you get `SDL2_DIR-NOTFOUND`:
1. Check if SDL2 is in your Vulkan SDK: `C:\VulkanSDK\<version>\Third-Party\`
2. Set SDL2_PATH when configuring: `cmake -DSDL2_PATH=C:\path\to\SDL2 ..`
3. Or set the SDL2_DIR environment variable

### Missing DLLs (Windows)
The build process tries to copy SDL2.dll automatically. If it fails:
- Copy SDL2.dll from Vulkan SDK or SDL2 installation to the exe directory

### Shader Compilation
To compile shaders to SPIR-V during build:
```bash
cmake -DCOMPILE_SHADERS=ON ..
```

## Project Structure

```
Tremor/
├── CMakeLists.txt      # This build configuration
├── main.cpp            # Entry point
├── vk.cpp/h            # Vulkan implementation
├── vm.cpp/hpp          # Virtual machine
├── audio/              # Audio processing
├── renderer/           # Rendering subsystem
├── shaders/            # GLSL shaders
└── assets/             # Game assets
```

## Dependencies

- **Taffy**: Asset format library (expected in ../Taffy)
- **Vulkan**: Graphics API
- **SDL2**: Window management and input
- **ASIO**: Networking (fetched automatically)
- **GLM**: Mathematics library
- **shaderc**: Shader compilation