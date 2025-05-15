#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#define USING_VULKAN

#include <windows.h>
#include <mmsystem.h>
#include "wsaerror.h"

#include <sys/types.h>
#include <errno.h>
#include <io.h>
#include <direct.h>

#include <algorithm>
#include <future>
#include <iostream>
#include <mutex>
#include <numeric>
#include <string>
#include <vector>

#include <optional>
#include <chrono>

#include <random>

#include <winsock2.h>
#include <ws2tcpip.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
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

#define GAMENAME "tremor" // directory to look in by default

#undef SearchPath
