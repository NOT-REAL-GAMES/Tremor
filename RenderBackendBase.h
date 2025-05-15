#pragma once
#include "main.h"

namespace tremor::gfx {
    class RenderBackend {
    public:
        virtual ~RenderBackend() = default;
        virtual bool initialize(SDL_Window* window) = 0;
        virtual void shutdown() = 0;
        virtual void beginFrame() = 0;
        virtual void endFrame() = 0;

        // Factory method declaration only
        static inline std::unique_ptr<RenderBackend> create(SDL_Window* window);
    };
}