#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif


#define GLM_ENABLE_EXPERIMENTAL

#define NOGDI

// Platform detection
#ifdef _WIN32
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif
    #define NOCOMM
    #define NOSOUND
    #define NODRAWTEXT
    #define NOGDI
    #define NOUSER
    #define NOMCX
    #include <windows.h>
    #include <io.h>
    #include <direct.h>
#else
    #include <unistd.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <dirent.h>
#endif

#ifndef USING_VULKAN
#define USING_VULKAN
#endif

#define VK_NO_PROTOTYPES 1
#if defined(_WIN64) || defined(_M_X64) || defined(_M_ARM64) || defined(__x86_64__) || defined(__aarch64__) || defined(__LP64__)
#define VK_USE_64_BIT_PTR_DEFINES 1
#endif
#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR 1
#endif

//#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1

#include <errno.h>

#include <algorithm>
#include <future>
#include <source_location>
#include <format>
#include <fstream>

#include <iostream>
#include <mutex>
#include <numeric>
#include <string>
#include <vector>
#include <map>

#include <optional>
#include <chrono>

#include <random>

#include "volk.h"

#define SDL_MAIN_HANDLED
#define NO_SDL_VULKAN_TYPEDEFS

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <SDL2/SDL_syswm.h>


#include <vulkan/vulkan.hpp>

#include <cstring>
#include <stdexcept>

#include <glm/glm.hpp>

#include <string_view>
#include <unordered_map>
#include <cctype>
#include <charconv>
#include <functional>
#include <array>

#include <cstdio>
#include <memory>
#include <fstream>
#include <filesystem>
#include <system_error>
#include <unordered_set>

#include "logger.h"

#define GAMENAME "tremor" // directory to look in by default

#ifdef _WIN32
#undef SearchPath
#endif

#ifdef _WIN32
#undef NEAR
#undef FAR
#undef near
#undef far
#endif



