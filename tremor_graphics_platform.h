#pragma once

#include "tremor_platform.h"

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

#include "volk.h"

#define SDL_MAIN_HANDLED
#define NO_SDL_VULKAN_TYPEDEFS

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <SDL2/SDL_syswm.h>

#include <vulkan/vulkan.hpp>
