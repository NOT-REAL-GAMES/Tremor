#pragma once

#include "RenderBackendBase.h"
#if defined(USING_VULKAN)
    #include "vk.h"
#endif

namespace tremor::gfx {

    inline std::unique_ptr<RenderBackend> RenderBackend::create(SDL_Window* window) {
        std::unique_ptr<RenderBackend> backend;

        #if defined(USING_VULKAN)
            backend = std::make_unique<VulkanBackend>();
        #elif defined(USING_D3D12)
            backend = std::make_unique<D3D12Backend>();
        #elif defined(TREMOR_PLATFORM_CONSOLE)
            backend = std::make_unique<ConsoleBackend>();
        #else
            #error "No graphics backend defined"
        #endif

        if (!backend->initialize(window)) {
            return nullptr; // Initialization failed
        }

        return backend;
    }
}