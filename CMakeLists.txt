cmake_minimum_required(VERSION 3.16)
project(Tremor VERSION 0.1.0 LANGUAGES C CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

# Set C standard
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Add custom module path for finding packages
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../cmake")
message(STATUS "CMAKE_MODULE_PATH: ${CMAKE_MODULE_PATH}")

# Check for Vulkan SDK
if(DEFINED ENV{VULKAN_SDK})
    message(STATUS "VULKAN_SDK found at: $ENV{VULKAN_SDK}")
else()
    message(STATUS "VULKAN_SDK environment variable not set")
endif()

# Option to build shared libraries
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)

# Add Taffy as a subdirectory if it exists
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../Taffy/CMakeLists.txt")
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Taffy ${CMAKE_CURRENT_BINARY_DIR}/Taffy)
    set(TAFFY_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../Taffy/include)
else()
    message(FATAL_ERROR "Taffy library not found. Please ensure Taffy is in the parent directory.")
endif()

# Find required packages
find_package(Vulkan QUIET)
if(NOT Vulkan_FOUND)
    # Try our alternative finder
    include(FindVulkanAlt)
endif()
if(NOT Vulkan_FOUND)
    message(FATAL_ERROR "Vulkan not found! Please install libvulkan-dev (Ubuntu/Debian) or vulkan-devel (Fedora)")
endif()

# Find SDL2
set(SDL2_FOUND FALSE)
if(WIN32 AND DEFINED ENV{VULKAN_SDK})
    set(VULKAN_SDK_PATH $ENV{VULKAN_SDK})
    
    # Look for SDL2 in Vulkan SDK Third-Party folder
    find_path(SDL2_INCLUDE_DIR
        NAMES SDL.h
        PATHS 
            ${VULKAN_SDK_PATH}/Include
            ${VULKAN_SDK_PATH}/Include/SDL2
        NO_DEFAULT_PATH
    )
    
    find_library(SDL2_LIBRARY
        NAMES SDL2
        PATHS
            ${VULKAN_SDK_PATH}/Bin
            ${VULKAN_SDK_PATH}/Lib
        NO_DEFAULT_PATH
    )
    
    find_library(SDL2MAIN_LIBRARY
        NAMES SDL2main
        PATHS
            ${VULKAN_SDK_PATH}/Bin
            ${VULKAN_SDK_PATH}/Lib
        NO_DEFAULT_PATH
    )
    
    if(SDL2_INCLUDE_DIR AND SDL2_LIBRARY)
        set(SDL2_FOUND TRUE)
        set(SDL2_INCLUDE_DIRS ${SDL2_INCLUDE_DIR})
        set(SDL2_LIBRARIES ${SDL2_LIBRARY})
        if(SDL2MAIN_LIBRARY)
            list(APPEND SDL2_LIBRARIES ${SDL2MAIN_LIBRARY})
        endif()
        message(STATUS "Found SDL2 in Vulkan SDK: ${VULKAN_SDK_PATH}")
        message(STATUS "  Include: ${SDL2_INCLUDE_DIRS}")
        message(STATUS "  Libraries: ${SDL2_LIBRARIES}")
        
        # Create imported target for modern CMake
        add_library(SDL2::SDL2 INTERFACE IMPORTED)
        set_target_properties(SDL2::SDL2 PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${SDL2_INCLUDE_DIRS}"
            INTERFACE_LINK_LIBRARIES "${SDL2_LIBRARIES}"
        )
    endif()
else()
    # On non-Windows platforms, use standard find_package
    find_package(SDL2 REQUIRED)
endif()

if(NOT SDL2_FOUND)
    message(FATAL_ERROR "SDL2 not found! On Windows, ensure VULKAN_SDK environment variable is set. On Linux, install libsdl2-dev.")
endif()

find_package(Threads REQUIRED)

# Find optional packages
find_package(glm QUIET)
if(NOT glm_FOUND)
    message(STATUS "GLM not found via find_package, using system headers")
endif()

# Fetch ASIO
include(FetchContent)
FetchContent_Declare(
    asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG asio-1-29-0
)
FetchContent_MakeAvailable(asio)

# Create executable
add_executable(Tremor
    main.cpp
    vk.cpp
    volk.c
    vm.cpp
    vm_bytecode.cpp
    renderer/taffy_integration.cpp
    renderer/taffy_mesh.cpp
    renderer/sdf_text_renderer.cpp
    audio/taffy_audio_processor.cpp
)

# Include directories
target_include_directories(Tremor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/renderer
    ${CMAKE_CURRENT_SOURCE_DIR}/taffy
    ${CMAKE_CURRENT_SOURCE_DIR}/audio
    ${TAFFY_INCLUDE_DIR}
    ${Vulkan_INCLUDE_DIRS}
    ${SDL2_INCLUDE_DIRS}
    ${asio_SOURCE_DIR}/asio/include
)

target_compile_definitions(Tremor PRIVATE 
    ASIO_STANDALONE 
    ASIO_HAS_STD_COROUTINE)

# Link libraries
target_link_libraries(Tremor PRIVATE
    Taffy
    ${Vulkan_LIBRARIES}
    SDL2::SDL2
    Threads::Threads
)

# Find and link shaderc
find_library(SHADERC_COMBINED_LIBRARY NAMES shaderc_combined)
find_library(SHADERC_LIBRARY NAMES shaderc_shared shaderc)

if(SHADERC_COMBINED_LIBRARY)
    # Use the combined library which includes all dependencies
    target_link_libraries(Tremor PRIVATE ${SHADERC_COMBINED_LIBRARY})
elseif(SHADERC_LIBRARY)
    # If using the regular shaderc library, we need to link dependencies too
    target_link_libraries(Tremor PRIVATE ${SHADERC_LIBRARY})
    # Try to find and link the dependencies
    find_library(SHADERC_UTIL_LIBRARY NAMES shaderc_util)
    find_library(GLSLANG_LIBRARY NAMES glslang)
    find_library(SPIRV_LIBRARY NAMES SPIRV)
    find_library(SPIRV_TOOLS_LIBRARY NAMES SPIRV-Tools)
    
    if(SHADERC_UTIL_LIBRARY)
        target_link_libraries(Tremor PRIVATE ${SHADERC_UTIL_LIBRARY})
    endif()
    if(GLSLANG_LIBRARY)
        target_link_libraries(Tremor PRIVATE ${GLSLANG_LIBRARY})
    endif()
    if(SPIRV_LIBRARY)
        target_link_libraries(Tremor PRIVATE ${SPIRV_LIBRARY})
    endif()
    if(SPIRV_TOOLS_LIBRARY)
        target_link_libraries(Tremor PRIVATE ${SPIRV_TOOLS_LIBRARY})
    endif()
else()
    message(WARNING "shaderc not found. Shader compilation may not work.")
endif()

# Platform-specific libraries
if(UNIX AND NOT APPLE)
    target_link_libraries(Tremor PRIVATE dl)
    # C++23 stacktrace support
    target_link_libraries(Tremor PRIVATE stdc++exp)
endif()

if(WIN32)
    target_link_libraries(Tremor PRIVATE ws2_32)
endif()

# Compile definitions
target_compile_definitions(Tremor PRIVATE
    USING_VULKAN
    VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
)

# Set properties
set_target_properties(Tremor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Copy assets to build directory
add_custom_command(TARGET Tremor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/assets
    ${CMAKE_BINARY_DIR}/bin/assets
)

add_custom_command(TARGET Tremor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders
    ${CMAKE_BINARY_DIR}/bin/shaders
)

# Shader compilation (optional)
option(COMPILE_SHADERS "Compile shaders to SPIR-V" OFF)
if(COMPILE_SHADERS)
    find_program(GLSLC glslc)
    if(GLSLC)
        file(GLOB SHADER_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.vert
                                 ${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.frag
                                 ${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.mesh
                                 ${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.task)
        foreach(SHADER ${SHADER_SOURCES})
            get_filename_component(SHADER_NAME ${SHADER} NAME)
            get_filename_component(SHADER_EXT ${SHADER} EXT)
            if(SHADER_EXT STREQUAL ".mesh" OR SHADER_EXT STREQUAL ".task")
                add_custom_command(TARGET Tremor POST_BUILD
                    COMMAND ${GLSLC} --target-env=vulkan1.2 ${SHADER} -o ${CMAKE_BINARY_DIR}/bin/shaders/${SHADER_NAME}.spv
                )
            else()
                add_custom_command(TARGET Tremor POST_BUILD
                    COMMAND ${GLSLC} ${SHADER} -o ${CMAKE_BINARY_DIR}/bin/shaders/${SHADER_NAME}.spv
                )
            endif()
        endforeach()
    else()
        message(WARNING "glslc not found, shaders will not be compiled")
    endif()
endif()

# Windows-specific: Copy SDL2.dll to output directory
if(WIN32)
    # Try to find SDL2.dll
    get_target_property(SDL2_DLL_DIR SDL2::SDL2 INTERFACE_LINK_DIRECTORIES)
    find_file(SDL2_DLL 
        NAMES SDL2.dll
        HINTS 
            ${SDL2_DLL_DIR}
            $ENV{SDL2_DIR}/lib/x64
            $ENV{SDL2_PATH}/lib/x64
            $ENV{VULKAN_SDK}/Third-Party/Bin
            $ENV{VULKAN_SDK}/Bin
            $ENV{VULKAN_SDK}/Bin32
        PATH_SUFFIXES bin x64 x86
    )
    
    if(SDL2_DLL)
        add_custom_command(TARGET Tremor POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${SDL2_DLL}
            $<TARGET_FILE_DIR:Tremor>
        )
        message(STATUS "SDL2.dll will be copied from: ${SDL2_DLL}")
    else()
        message(WARNING "SDL2.dll not found. You may need to copy it manually to the executable directory.")
    endif()
endif()
