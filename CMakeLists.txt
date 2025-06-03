cmake_minimum_required(VERSION 3.16)
project(Tremor VERSION 0.1.0 LANGUAGES C CXX)

# Find required packages
find_package(Vulkan QUIET)
if(NOT Vulkan_FOUND)
    # Try our alternative finder
    include(FindVulkanAlt)
endif()
if(NOT Vulkan_FOUND)
    message(FATAL_ERROR "Vulkan not found! Please install libvulkan-dev (Ubuntu/Debian) or vulkan-devel (Fedora)")
endif()

find_package(SDL2 REQUIRED)
find_package(Threads REQUIRED)

# Find optional packages
find_package(glm QUIET)
if(NOT glm_FOUND)
    message(STATUS "GLM not found via find_package, using system headers")
endif()

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
)

# Include directories
target_include_directories(Tremor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/renderer
    ${CMAKE_CURRENT_SOURCE_DIR}/taffy
    ${TAFFY_INCLUDE_DIR}
    ${Vulkan_INCLUDE_DIRS}
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
