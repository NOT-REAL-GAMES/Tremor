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

class Engine {
public:

	int argc;
	char** argv;

	SDL_Window* window;

	Engine() {
		if (SDL_Init(SDL_INIT_EVERYTHING) != 0) {
			std::cerr << "SDL_Init Error: " << SDL_GetError() << std::endl;
			return;
		}
		window = SDL_CreateWindow("Tremor", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, SDL_WINDOW_VULKAN);
		if (window == nullptr) {
			std::cerr << "SDL_CreateWindow Error: " << SDL_GetError() << std::endl;
			SDL_Quit();
			return;
		}
	}
};

int main(int argc, char** argv) {
	Engine engine;

	while (1) {
		SDL_Event event{};
		while (SDL_PollEvent(&event)) {

			if (event.type == SDL_QUIT) {
				SDL_DestroyWindow(engine.window);
				SDL_Quit();
				return 0;
			}
		}
		SDL_Delay(4); // Simulate a frame delay
	}

	return 0;
}