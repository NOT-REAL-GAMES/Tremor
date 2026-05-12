cmake_minimum_required(VERSION 3.20)
project(Tremor VERSION 0.1.0 LANGUAGES C CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

# Set C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(MSVC)
    string(APPEND CMAKE_CXX_FLAGS " /Zc:enumTypes-")
    string(APPEND CMAKE_C_FLAGS   " /Zc:enumTypes-")
    string(APPEND CMAKE_CXX_FLAGS_DEBUG " /D_DEBUG /UNDEBUG")
    string(APPEND CMAKE_C_FLAGS_DEBUG   " /D_DEBUG /UNDEBUG")
    string(APPEND CMAKE_CXX_FLAGS_RELEASE " /DNDEBUG /U_DEBUG")
    string(APPEND CMAKE_C_FLAGS_RELEASE   " /DNDEBUG /U_DEBUG")
    string(APPEND CMAKE_CXX_FLAGS_RELWITHDEBINFO " /DNDEBUG /U_DEBUG")
    string(APPEND CMAKE_C_FLAGS_RELWITHDEBINFO   " /DNDEBUG /U_DEBUG")
    string(APPEND CMAKE_CXX_FLAGS_MINSIZEREL " /DNDEBUG /U_DEBUG")
    string(APPEND CMAKE_C_FLAGS_MINSIZEREL   " /DNDEBUG /U_DEBUG")
endif()



# Ensure proper C++23 support for C++ files only
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-std=c++23>)
endif()

# Add custom module paths for finding packages
list(APPEND CMAKE_MODULE_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake"
    "${CMAKE_CURRENT_SOURCE_DIR}/../cmake"
)
message(STATUS "CMAKE_MODULE_PATH: ${CMAKE_MODULE_PATH}")

# Check for Vulkan SDK
if(DEFINED ENV{VULKAN_SDK})
    message(STATUS "VULKAN_SDK found at: $ENV{VULKAN_SDK}")
else()
    message(STATUS "VULKAN_SDK environment variable not set")
endif()

# Option to build shared libraries
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)

set(ABSL_ENABLE_INSTALL          OFF CACHE BOOL "" FORCE)
set(RE2_BUILD_TESTING            OFF CACHE BOOL "" FORCE)
set(SHADERC_SKIP_INSTALL         ON  CACHE BOOL "" FORCE)
set(SHADERC_SKIP_TESTS           ON  CACHE BOOL "" FORCE)
set(SHADERC_SKIP_EXAMPLES        ON  CACHE BOOL "" FORCE)
set(SPIRV_SKIP_EXECUTABLES       ON  CACHE BOOL "" FORCE)
set(SPIRV_SKIP_TESTS             ON  CACHE BOOL "" FORCE)
set(ENABLE_OPT                   OFF CACHE BOOL "" FORCE)
set(ALLOW_EXTERNAL_SPIRV_TOOLS   ON  CACHE BOOL "" FORCE)
set(SPIRV_CROSS_SKIP_INSTALL     ON  CACHE BOOL "" FORCE)
set(SPIRV_CROSS_CLI              OFF CACHE BOOL "" FORCE)
set(GLSLANG_TESTS                OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL       OFF CACHE BOOL "" FORCE)


# Disable AssImp tests and samples to avoid C++23 volatile issues
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_GLTF_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_OBJ_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_COLLADA_IMPORTER ON CACHE BOOL "" FORCE)

# Add Taffy as a subdirectory if it exists
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../Taffy/CMakeLists.txt")
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Taffy ${CMAKE_CURRENT_BINARY_DIR}/Taffy)
    set(TAFFY_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../Taffy/include)
else()
    message(FATAL_ERROR "Taffy library not found. Please ensure Taffy is in the parent directory.")
endif()

# Add flecs using FetchContent
include(FetchContent)
include(ExternalProject)
FetchContent_Declare(
    flecs
    GIT_REPOSITORY https://github.com/SanderMertens/flecs.git
    GIT_TAG v4.0.3
)
FetchContent_MakeAvailable(flecs)

FetchContent_Declare(
    SDL
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG SDL2
)
FetchContent_MakeAvailable(SDL)


# Add Jolt Physics using FetchContent
FetchContent_Declare(
    Jolt
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG v5.0.0
    SOURCE_SUBDIR Build
)

FetchContent_Declare(
    glslang
    GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
    GIT_TAG main
)

FetchContent_MakeAvailable(glslang)

FetchContent_Declare(
    SPIRV-Headers
    GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Headers.git
    GIT_TAG main
)

FetchContent_MakeAvailable(SPIRV-Headers)

FetchContent_Declare(
    SPIRV-Tools
    GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Tools.git
    GIT_TAG main
)

FetchContent_MakeAvailable(SPIRV-Tools)

FetchContent_Declare(
    SPIRV-Cross
    GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Cross.git
    GIT_TAG main
)

FetchContent_MakeAvailable(SPIRV-Cross)

FetchContent_Declare(
    shaderc
    GIT_REPOSITORY https://github.com/google/shaderc.git
    GIT_TAG        main
)

FetchContent_MakeAvailable(shaderc)

option(TREMOR_FETCH_PHYSX "Fetch PhysX 5 source for Tremor's PhysX backend work" ON)
set(TREMOR_PHYSX_TAG "107.3-physx-5.6.1" CACHE STRING "Pinned PhysX release tag from NVIDIA-Omniverse/PhysX")


# Configure Jolt options before making it available
set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
set(TARGET_VIEWER OFF CACHE BOOL "" FORCE)
set(ENABLE_OBJECT_STREAM ON CACHE BOOL "" FORCE)
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
set(GENERATE_DEBUG_SYMBOLS ON CACHE BOOL "" FORCE)
set(OVERRIDE_CXX_FLAGS ON CACHE BOOL "" FORCE)
set(DOUBLE_PRECISION ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(Jolt)

target_compile_definitions(Jolt PRIVATE NOGDI)
target_compile_options(Jolt PRIVATE /W0)

if(TREMOR_FETCH_PHYSX)
    FetchContent_Declare(
        PhysX
        GIT_REPOSITORY https://github.com/NVIDIA-Omniverse/PhysX.git
        GIT_TAG ${TREMOR_PHYSX_TAG}
    )

    FetchContent_GetProperties(PhysX)
    if(NOT physx_POPULATED)
        FetchContent_Populate(PhysX)
    endif()

    set(TREMOR_PHYSX_SOURCE_DIR "${physx_SOURCE_DIR}" CACHE PATH "Fetched PhysX source root")
    set(TREMOR_PHYSX_ROOT_DIR "${physx_SOURCE_DIR}/physx" CACHE PATH "Fetched PhysX SDK root")
    set(TREMOR_PHYSX_INCLUDE_DIR "${TREMOR_PHYSX_ROOT_DIR}/include" CACHE PATH "Fetched PhysX public headers")

    if(MSVC)
        if(MSVC_VERSION GREATER_EQUAL 1930)
            set(_tremor_physx_compiler_suffix "vc143")
        elseif(MSVC_VERSION GREATER_EQUAL 1920)
            set(_tremor_physx_compiler_suffix "vc142")
        elseif(MSVC_VERSION GREATER_EQUAL 1910)
            set(_tremor_physx_compiler_suffix "vc141")
        else()
            message(FATAL_ERROR "Unsupported MSVC toolset for PhysX integration")
        endif()

        set(TREMOR_PHYSX_BUILD_DIR "${CMAKE_BINARY_DIR}/physx-build" CACHE PATH "PhysX isolated build directory")
        set(TREMOR_PHYSX_OUTPUT_DIR "${CMAKE_BINARY_DIR}/physx-artifacts" CACHE PATH "PhysX isolated output root")
        set(TREMOR_PHYSX_DUMMY_THIRDPARTY_DIR "${CMAKE_BINARY_DIR}/physx-thirdparty-dummy" CACHE PATH "Dummy third-party root for PhysX configure-time copy assumptions")
        set(TREMOR_PHYSX_PLATFORM_BIN_NAME "win.x86_64.${_tremor_physx_compiler_suffix}.mt")
        set(_tremor_physx_library_root "${TREMOR_PHYSX_OUTPUT_DIR}/bin/${TREMOR_PHYSX_PLATFORM_BIN_NAME}")
        set(_tremor_physx_build_config "$<IF:$<CONFIG:Debug>,debug,$<IF:$<CONFIG:Release>,release,$<IF:$<CONFIG:RelWithDebInfo>,profile,release>>>")

        file(MAKE_DIRECTORY "${TREMOR_PHYSX_DUMMY_THIRDPARTY_DIR}/bin/win64")
        foreach(_tremor_physx_dummy_dll IN ITEMS freeglut.dll freeglutd.dll PhysXDevice64.dll)
            if(NOT EXISTS "${TREMOR_PHYSX_DUMMY_THIRDPARTY_DIR}/bin/win64/${_tremor_physx_dummy_dll}")
                file(WRITE "${TREMOR_PHYSX_DUMMY_THIRDPARTY_DIR}/bin/win64/${_tremor_physx_dummy_dll}" "")
            endif()
        endforeach()

        set(_tremor_physx_cmake_args
            -DPHYSX_ROOT_DIR=${TREMOR_PHYSX_ROOT_DIR}
            -DTARGET_BUILD_PLATFORM=windows
            -DPUBLIC_RELEASE=OFF
            -DPX_GENERATE_STATIC_LIBRARIES=ON
            -DPX_BUILDSNIPPETS=OFF
            -DPX_BUILDPVDRUNTIME=OFF
            -DPX_GENERATE_GPU_PROJECTS=OFF
            -DPX_GENERATE_GPU_PROJECTS_ONLY=OFF
            -DPX_COPY_EXTERNAL_DLL=OFF
            -DPHYSX_SLN_FREEGLUT_PATH=${TREMOR_PHYSX_DUMMY_THIRDPARTY_DIR}/bin
            -DPHYSX_SLN_PHYSXDEVICE_PATH=${TREMOR_PHYSX_DUMMY_THIRDPARTY_DIR}/bin/win64
            -DNV_USE_STATIC_WINCRT=ON
            -DNV_USE_DEBUG_WINCRT=ON
            -DPX_OUTPUT_LIB_DIR=${TREMOR_PHYSX_OUTPUT_DIR}
            -DPX_OUTPUT_BIN_DIR=${TREMOR_PHYSX_OUTPUT_DIR}
            -DCMAKE_INSTALL_PREFIX=${TREMOR_PHYSX_OUTPUT_DIR}/install
            -DCMAKE_MSVC_RUNTIME_LIBRARY=${CMAKE_MSVC_RUNTIME_LIBRARY}
        )

        if(CMAKE_MAKE_PROGRAM)
            list(APPEND _tremor_physx_cmake_args
                -DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}
            )
        endif()
        if(CMAKE_C_COMPILER)
            list(APPEND _tremor_physx_cmake_args
                -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
            )
        endif()
        if(CMAKE_CXX_COMPILER)
            list(APPEND _tremor_physx_cmake_args
                -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
            )
        endif()
        if(CMAKE_RC_COMPILER)
            list(APPEND _tremor_physx_cmake_args
                -DCMAKE_RC_COMPILER=${CMAKE_RC_COMPILER}
            )
        endif()
        if(CMAKE_BUILD_TYPE)
            list(APPEND _tremor_physx_cmake_args
                -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
            )
        endif()

        set(_tremor_physx_external_project_args
            PREFIX ${CMAKE_BINARY_DIR}/physx-external
            SOURCE_DIR ${TREMOR_PHYSX_ROOT_DIR}/compiler/public
            BINARY_DIR ${TREMOR_PHYSX_BUILD_DIR}
            DOWNLOAD_COMMAND ""
            UPDATE_COMMAND ""
            INSTALL_COMMAND ""
            CMAKE_ARGS
                ${_tremor_physx_cmake_args}
            BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config ${_tremor_physx_build_config} --target PhysXFoundation PhysXPvdSDK PhysXCommon PhysXCooking PhysX PhysXExtensions
            BUILD_BYPRODUCTS
                ${TREMOR_PHYSX_ROOT_DIR}/include/PxConfig.h
                ${_tremor_physx_library_root}/debug/PhysXFoundation_static.lib
                ${_tremor_physx_library_root}/debug/PhysXPvdSDK_static.lib
                ${_tremor_physx_library_root}/debug/PhysXCommon_static.lib
                ${_tremor_physx_library_root}/debug/PhysXCooking_static.lib
                ${_tremor_physx_library_root}/debug/PhysX_static.lib
                ${_tremor_physx_library_root}/debug/PhysXExtensions_static.lib
                ${_tremor_physx_library_root}/release/PhysXFoundation_static.lib
                ${_tremor_physx_library_root}/release/PhysXPvdSDK_static.lib
                ${_tremor_physx_library_root}/release/PhysXCommon_static.lib
                ${_tremor_physx_library_root}/release/PhysXCooking_static.lib
                ${_tremor_physx_library_root}/release/PhysX_static.lib
                ${_tremor_physx_library_root}/release/PhysXExtensions_static.lib
                ${_tremor_physx_library_root}/profile/PhysXFoundation_static.lib
                ${_tremor_physx_library_root}/profile/PhysXPvdSDK_static.lib
                ${_tremor_physx_library_root}/profile/PhysXCommon_static.lib
                ${_tremor_physx_library_root}/profile/PhysXCooking_static.lib
                ${_tremor_physx_library_root}/profile/PhysX_static.lib
                ${_tremor_physx_library_root}/profile/PhysXExtensions_static.lib
        )

        if(CMAKE_GENERATOR)
            list(APPEND _tremor_physx_external_project_args
                CMAKE_GENERATOR "${CMAKE_GENERATOR}"
            )
        endif()
        if(CMAKE_GENERATOR_INSTANCE)
            list(APPEND _tremor_physx_external_project_args
                CMAKE_GENERATOR_INSTANCE "${CMAKE_GENERATOR_INSTANCE}"
            )
        endif()
        if(CMAKE_GENERATOR_PLATFORM)
            list(APPEND _tremor_physx_external_project_args
                CMAKE_GENERATOR_PLATFORM "${CMAKE_GENERATOR_PLATFORM}"
            )
        endif()
        if(CMAKE_GENERATOR_TOOLSET)
            list(APPEND _tremor_physx_external_project_args
                CMAKE_GENERATOR_TOOLSET "${CMAKE_GENERATOR_TOOLSET}"
            )
        endif()

        ExternalProject_Add(TremorPhysXBuild ${_tremor_physx_external_project_args})

        function(tremor_import_physx_static target_name physx_lib_name)
            add_library(${target_name} STATIC IMPORTED GLOBAL)
            set_target_properties(${target_name} PROPERTIES
                IMPORTED_CONFIGURATIONS "DEBUG;RELEASE;RELWITHDEBINFO;MINSIZEREL"
                IMPORTED_LOCATION_DEBUG "${_tremor_physx_library_root}/debug/${physx_lib_name}.lib"
                IMPORTED_LOCATION_RELEASE "${_tremor_physx_library_root}/release/${physx_lib_name}.lib"
                IMPORTED_LOCATION_RELWITHDEBINFO "${_tremor_physx_library_root}/profile/${physx_lib_name}.lib"
                IMPORTED_LOCATION_MINSIZEREL "${_tremor_physx_library_root}/release/${physx_lib_name}.lib"
            )
        endfunction()

        tremor_import_physx_static(TremorPhysXFoundation "PhysXFoundation_static")
        tremor_import_physx_static(TremorPhysXPvdSDK "PhysXPvdSDK_static")
        tremor_import_physx_static(TremorPhysXCommon "PhysXCommon_static")
        tremor_import_physx_static(TremorPhysXCooking "PhysXCooking_static")
        tremor_import_physx_static(TremorPhysX "PhysX_static")
        tremor_import_physx_static(TremorPhysXExtensions "PhysXExtensions_static")

        add_library(TremorPhysXSDK INTERFACE)
        target_include_directories(TremorPhysXSDK INTERFACE
            ${TREMOR_PHYSX_INCLUDE_DIR}
        )
        target_compile_definitions(TremorPhysXSDK INTERFACE
            TREMOR_HAS_PHYSX_SDK=1
            PX_PHYSX_STATIC_LIB=1
            $<$<CONFIG:Debug>:_DEBUG>
            $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
        )
        if(MSVC)
            target_compile_options(TremorPhysXSDK INTERFACE
                $<$<CONFIG:Debug>:/UNDEBUG>
                $<$<NOT:$<CONFIG:Debug>>:/U_DEBUG>
            )
        endif()
        target_link_libraries(TremorPhysXSDK INTERFACE
            TremorPhysXExtensions
            TremorPhysX
            TremorPhysXCooking
            TremorPhysXCommon
            TremorPhysXPvdSDK
            TremorPhysXFoundation
        )
    else()
        message(WARNING "PhysX auto-build is currently only wired for MSVC; the PhysX backend will remain stubbed on this platform.")
    endif()

    message(STATUS "PhysX 5 source fetched from NVIDIA-Omniverse/PhysX (${TREMOR_PHYSX_TAG})")
    message(STATUS "  PhysX source root: ${TREMOR_PHYSX_SOURCE_DIR}")
    message(STATUS "  PhysX SDK root: ${TREMOR_PHYSX_ROOT_DIR}")
    message(STATUS "  PhysX include: ${TREMOR_PHYSX_INCLUDE_DIR}")
endif()


# Ensure Jolt uses the correct C++ standard
if(TARGET Jolt)

    set_target_properties(Jolt PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
endif()

# Find required packages
find_package(Vulkan QUIET)
if(NOT Vulkan_FOUND)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/FindVulkanAlt.cmake")
        include(FindVulkanAlt)
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/FindVulkanAlt.cmake")
        include(FindVulkanAlt)
    endif()
endif()
if(NOT Vulkan_FOUND)
    message(FATAL_ERROR "Vulkan not found! Install the Vulkan SDK on Windows, or libvulkan-dev/vulkan-devel on Linux.")
endif()

# Find SDL2
#set(SDL2_FOUND FALSE)
#if(TARGET SDL2::SDL2)
#    set(SDL2_FOUND TRUE)
#    message(STATUS "Using vendored SDL2 target")
#elseif(WIN32 AND DEFINED ENV{VULKAN_SDK})
#    set(VULKAN_SDK_PATH $ENV{VULKAN_SDK})
#    
#    # Look for SDL2 in Vulkan SDK Third-Party folder
#    find_path(SDL2_INCLUDE_DIR
#        NAMES SDL.h
#        PATHS 
#            ${VULKAN_SDK_PATH}/Include
#            ${VULKAN_SDK_PATH}/Include/SDL2
#        NO_DEFAULT_PATH
#    )
#    
#    find_library(SDL2_LIBRARY
#        NAMES SDL2
#        PATHS
#            ${VULKAN_SDK_PATH}/Bin
#            ${VULKAN_SDK_PATH}/Lib
#        NO_DEFAULT_PATH
#    )
#    
#    find_library(SDL2MAIN_LIBRARY
#        NAMES SDL2main
#        PATHS
#            ${VULKAN_SDK_PATH}/Bin
#            ${VULKAN_SDK_PATH}/Lib
#        NO_DEFAULT_PATH
#    )
#    
#    if(SDL2_INCLUDE_DIR AND SDL2_LIBRARY)
#        set(SDL2_FOUND TRUE)
#        message(STATUS "Found SDL2 in Vulkan SDK: ${VULKAN_SDK_PATH}")
#        message(STATUS "  Include: ${SDL2_INCLUDE_DIR}")
#        message(STATUS "  Library: ${SDL2_LIBRARY}")
#
#        add_library(SDL2::SDL2 UNKNOWN IMPORTED)
#        set_target_properties(SDL2::SDL2 PROPERTIES
#            IMPORTED_LOCATION "${SDL2_LIBRARY}"
#            INTERFACE_INCLUDE_DIRECTORIES "${SDL2_INCLUDE_DIR}"
#        )
#
#        if(SDL2MAIN_LIBRARY)
#            add_library(SDL2::SDL2main UNKNOWN IMPORTED)
#            set_target_properties(SDL2::SDL2main PROPERTIES
#                IMPORTED_LOCATION "${SDL2MAIN_LIBRARY}"
#            )
#        endif()
#    endif()
#else()
#    find_package(SDL2 REQUIRED)
#    if(TARGET SDL2::SDL2)
#        set(SDL2_FOUND TRUE)
#    elseif(SDL2_INCLUDE_DIRS AND SDL2_LIBRARIES)
#        set(SDL2_FOUND TRUE)
#        add_library(SDL2::SDL2 INTERFACE IMPORTED)
#        set_target_properties(SDL2::SDL2 PROPERTIES
#            INTERFACE_INCLUDE_DIRECTORIES "${SDL2_INCLUDE_DIRS}"
#            INTERFACE_LINK_LIBRARIES "${SDL2_LIBRARIES}"
#        )
#    endif()
#endif()

#if(NOT SDL2_FOUND)
#    message(FATAL_ERROR "SDL2 not found! Use the vendored SDL checkout, install SDL2, or set VULKAN_SDK so the bundled SDL2 can be located.")
#endif()

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

FetchContent_Declare(
    assimp
    GIT_REPOSITORY https://github.com/assimp/assimp
)
FetchContent_MakeAvailable(assimp)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/TremorModules.cmake)

# Create executable
add_executable(Tremor
    ${TREMOR_ALL_SOURCES}
    ${TREMOR_ALL_HEADERS}
)

tremor_assign_source_groups()

if(MSVC)
    target_compile_options(Tremor PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/utf-8>)
endif()

# Include directories
target_include_directories(Tremor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/renderer
    ${CMAKE_CURRENT_SOURCE_DIR}/taffy
    ${CMAKE_CURRENT_SOURCE_DIR}/audio
    ${CMAKE_CURRENT_SOURCE_DIR}/editor
    ${TAFFY_INCLUDE_DIR}
    ${Vulkan_INCLUDE_DIRS}
    ${asio_SOURCE_DIR}/asio/include
)

target_compile_definitions(Tremor PRIVATE 
    ASIO_STANDALONE 
    ASIO_HAS_STD_COROUTINE
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
)

if(MSVC)
    target_compile_options(Tremor PRIVATE
        $<$<CONFIG:Debug>:/UNDEBUG>
        $<$<NOT:$<CONFIG:Debug>>:/U_DEBUG>
    )
endif()

# Link libraries
target_link_libraries(Tremor PRIVATE
    Taffy
    ${Vulkan_LIBRARIES}
    SDL2::SDL2
    Threads::Threads
    assimp
    shaderc
    flecs::flecs_static
    spirv-cross-core
    spirv-cross-glsl
    spirv-cross-c
    spirv-cross-cpp
    spirv-cross-hlsl
    spirv-cross-msl
    SPIRV-Headers
    SPIRV-Tools
    glslang::glslang
    Jolt
)

if(TARGET TremorPhysXSDK)
    target_link_libraries(Tremor PRIVATE TremorPhysXSDK)
    add_dependencies(Tremor TremorPhysXBuild)
endif()

if(TARGET SDL2::SDL2main)
    target_link_libraries(Tremor PRIVATE SDL2::SDL2main)
endif()



# Platform-specific libraries
if(UNIX AND NOT APPLE)
    target_link_libraries(Tremor PRIVATE dl)
    # C++23 stacktrace support
    target_link_libraries(Tremor PRIVATE stdc++exp)
endif()

if(WIN32)
    target_link_libraries(Tremor PRIVATE Ole32)
    target_link_libraries(Tremor PRIVATE ws2_32)
endif()

# Compile definitions
target_compile_definitions(Tremor PRIVATE
    USING_VULKAN
    VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
    HAS_ASSIMP
    JPH_DOUBLE_PRECISION
)

# Set properties
set_target_properties(Tremor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Copy assets to build directory
add_custom_command(TARGET Tremor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/assets
    $<TARGET_FILE_DIR:Tremor>/assets
)

add_custom_command(TARGET Tremor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders
    $<TARGET_FILE_DIR:Tremor>/shaders
)

# Shader compilation with SPIRV-Tools integration
option(COMPILE_SHADERS "Compile shaders to SPIR-V" OFF)
option(OPTIMIZE_SHADERS "Optimize shaders with spirv-opt" ON)
if(COMPILE_SHADERS)
    find_program(GLSLC glslc)
    find_program(SPIRV_OPT spirv-opt)
    find_program(SPIRV_VAL spirv-val)

    if(GLSLC)
        file(GLOB SHADER_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.vert
                                 ${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.frag
                                 ${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.mesh
                                 ${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.task)
        set(COMPILED_SHADERS "")
        foreach(SHADER ${SHADER_SOURCES})
            get_filename_component(SHADER_NAME ${SHADER} NAME)
            get_filename_component(SHADER_EXT ${SHADER} EXT)
            set(OUTPUT_SPV ${CMAKE_BINARY_DIR}/bin/shaders/${SHADER_NAME}.spv)
            set(TEMP_SPV ${OUTPUT_SPV}.tmp)

            if(OPTIMIZE_SHADERS AND SPIRV_OPT AND SPIRV_VAL)
                if(SHADER_EXT STREQUAL ".mesh" OR SHADER_EXT STREQUAL ".task")
                    add_custom_command(OUTPUT ${OUTPUT_SPV}
                        COMMAND ${GLSLC} --target-env=vulkan1.2 ${SHADER} -o ${TEMP_SPV}
                        COMMAND ${SPIRV_VAL} ${TEMP_SPV}
                        COMMAND ${SPIRV_OPT} -O ${TEMP_SPV} -o ${OUTPUT_SPV}
                        COMMAND ${CMAKE_COMMAND} -E remove ${TEMP_SPV}
                        DEPENDS ${SHADER}
                        COMMENT "Compiling and optimizing shader ${SHADER_NAME}"
                    )
                else()
                    add_custom_command(OUTPUT ${OUTPUT_SPV}
                        COMMAND ${GLSLC} ${SHADER} -o ${TEMP_SPV}
                        COMMAND ${SPIRV_VAL} ${TEMP_SPV}
                        COMMAND ${SPIRV_OPT} -O ${TEMP_SPV} -o ${OUTPUT_SPV}
                        COMMAND ${CMAKE_COMMAND} -E remove ${TEMP_SPV}
                        DEPENDS ${SHADER}
                        COMMENT "Compiling and optimizing shader ${SHADER_NAME}"
                    )
                endif()
            else()
                if(SHADER_EXT STREQUAL ".mesh" OR SHADER_EXT STREQUAL ".task")
                    add_custom_command(OUTPUT ${OUTPUT_SPV}
                        COMMAND ${GLSLC} --target-env=vulkan1.2 ${SHADER} -o ${OUTPUT_SPV}
                        DEPENDS ${SHADER}
                        COMMENT "Compiling shader ${SHADER_NAME}"
                    )
                else()
                    add_custom_command(OUTPUT ${OUTPUT_SPV}
                        COMMAND ${GLSLC} ${SHADER} -o ${OUTPUT_SPV}
                        DEPENDS ${SHADER}
                        COMMENT "Compiling shader ${SHADER_NAME}"
                    )
                endif()
            endif()
            list(APPEND COMPILED_SHADERS ${OUTPUT_SPV})
        endforeach()

        # Create a custom target that depends on all compiled shaders
        add_custom_target(compile_shaders ALL DEPENDS ${COMPILED_SHADERS})
        add_dependencies(Tremor compile_shaders)

        if(SPIRV_OPT AND SPIRV_VAL)
            message(STATUS "SPIRV-Tools found: shader optimization enabled")
        else()
            message(STATUS "SPIRV-Tools not found: using basic compilation only")
        endif()
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
