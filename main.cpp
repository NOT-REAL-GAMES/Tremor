// Copyright 2025 NOT REAL GAMES
//
// Permission is hereby granted, free of charge, 
// to any person obtaining a copy of this software 
// and associated documentation files(the "Software"), 
// to deal in the Software without restriction, 
// including without limitation the rights to use, copy, 
// modify, merge, publish, distribute, sublicense, and/or 
// sell copies of the Software, and to permit persons to 
// whom the Software is furnished to do so, subject to the 
// following conditions:
//
// The above copyright notice and this permission notice shall 
// be included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, 
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES 
// OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-
// INFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
// BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN 
// AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF 
// OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS 
// IN THE SOFTWARE.

// This file is part of Tremor: a game engine built on a 
// rewrite of the Quake Engine as a foundation. The original 
// Quake Engine is licensed under the GPLv2 license. 

// The Tremor project is not affiliated with or endorsed by id Software.
// idTech 2's dependencies on Quake will be gradually phased out of the Tremor project. 



#include "main.h"


template<typename T>
concept StringLike = requires(T t) {
    { std::string_view(t) } -> std::convertible_to<std::string_view>;
};

#include "RenderBackend.h"

#include "mem.h"

#include "vm_bytecode.hpp"
#include "vm_decoder.hpp"

class Engine {
public:

    int argc;
    char** argv;

    SDL_Window* window;

    std::unique_ptr<tremor::gfx::RenderBackend> rb;

    Engine() {


        if (SDL_Init(SDL_INIT_EVERYTHING) < 0) {
            Logger::get().critical("SDL INITIALIZATION FAILED.");
        }

        window = SDL_CreateWindow("Tremor", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1280, 720, SDL_WINDOW_VULKAN);
        rb = tremor::gfx::RenderBackend::create(window);


    }

    bool Loop() {
#if defined(USING_VULKAN)
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {

            if (event.type == SDL_QUIT) {
                SDL_DestroyWindow(window);
                SDL_Quit();
                return false;
            }
        }

        rb.get()->beginFrame();


        rb.get()->endFrame();

        SDL_Delay(4); // Simulate a frame delay
        return true;
#endif
    }
};


int main(int argc, char** argv) {

	LPWSTR cmdLineW = GetCommandLineW();

	std::cout << "Initializing..." << std::endl;

	Logger::create({
	.enableConsole = true,
	.enableFileOutput = true,
	.logFilePath = "tremor_engine.log",
	.minLevel = Logger::Level::Debug,
	.showSourceLocation = true
		});

	Logger::get().info("Welcome. Starting Tremor...");

    Engine engine;

	do {

	} while (engine.Loop());

	return 0;
}