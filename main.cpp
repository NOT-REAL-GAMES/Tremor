#include <vulkan/vulkan_core.h>

#define SDL_MAIN_HANDLED
#define NO_SDL_VULKAN_TYPEDEFS

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#include <SDL2/SDL_vulkan.h>

#include <iostream>

class Engine {
public:
	Engine() {
		auto window = new VID();
	}
	class VID {
	public:
		bool fullscreen;
		SDL_Window* draw_context;
		//VID_Init()
		VID() {
			fullscreen = false;
			auto display = new Display();

			_putenv("SDL_VIDEO_CENTERED = center");

			if (SDL_InitSubSystem(SDL_INIT_VIDEO) < 0) { std::cout << ("SDL_Init failed: %s", SDL_GetError()) << std::endl; }

			SetMode(1280, 720, 60, false);
		}
		void Gamma_Init(){
			std::cout << "Gamma_Init to be implemented: haven't implemented cvars yet" << std::endl;
		}
		int GetCurrentWidth() {
			int w, h;
			SDL_GetWindowSize(draw_context, &w, &h);
			return w;
		}
		int GetCurrentHeight() {
			int w, h;
			SDL_GetWindowSize(draw_context, &w, &h);
			return h;
		}
		int GetCurrentRefreshRate() {
			SDL_DisplayMode mode;
			SDL_GetCurrentDisplayMode(0, &mode);
			return mode.refresh_rate;
		}
		int GetCurrentBPP() {
			return SDL_BITSPERPIXEL(SDL_GetWindowPixelFormat(draw_context));
		}
		bool GetFullscreen() {
			return (SDL_GetWindowFlags(draw_context) & SDL_WINDOW_FULLSCREEN) != 0;
		}
		bool GetDesktopFullscreen() {
			return (SDL_GetWindowFlags(draw_context) & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN_DESKTOP;
		}
		SDL_Window* GetWindow() {
			return draw_context;
		}
		bool HasMouseOrInputFocus() {
			return (SDL_GetWindowFlags(draw_context) & (SDL_WINDOW_MOUSE_FOCUS | SDL_WINDOW_INPUT_FOCUS)) != 0;
		}
		bool IsMinimized() {
			return !(SDL_GetWindowFlags(draw_context) & SDL_WINDOW_SHOWN);
		}
		SDL_DisplayMode* SDL2_GetDisplayMode(int width, int height, int refreshrate) {
			SDL_DisplayMode mode;
			int sdlmodes = SDL_GetNumDisplayModes(0);
			int i = 0;

			for (i = 0; i < sdlmodes; i++)
			{
				if (SDL_GetDisplayMode(0, i, &mode) != 0)
					continue;

				if (mode.w == width && mode.h == height && SDL_BITSPERPIXEL(mode.format) >= 24 && mode.refresh_rate == refreshrate)
				{
					return &mode;
				}
			}
			return NULL;
		}

		bool ValidMode(int width, int height, int refreshrate, bool fullscreen) {
			// ignore width / height / bpp if vid_desktopfullscreen is enabled
			if (fullscreen && GetDesktopFullscreen())
				return true;

			if (width < 320)
				return false;

			if (height < 200)
				return false;

			if (fullscreen && SDL2_GetDisplayMode(width, height, refreshrate) == NULL)
				return false;

			return true;
		}

		bool SetMode(int width, int height, int refreshrate, bool fullscreen) {
			//TODO: quake shit
			if (!draw_context) {
				draw_context = SDL_CreateWindow("Tremor", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, width, height, SDL_WINDOW_VULKAN);
				if (!draw_context) {
					std::cout << ("SDL_CreateWindow failed: %s", SDL_GetError()) << std::endl;
					return false;
				}
			}
			//TODO: more quake shit
		}
		class Display {
		public:
			int width, height;
			int refreshRate;
			Display() {
				width = 1920;
				height = 1080;
				refreshRate = 60;
			}
		};
	};
	class CL{};
};

int main(int argc, char *argv[]) {

	SDL_version v;
	SDL_GetVersion(&v);


	std::cout << "SDL version: " << (int)v.major << "." << (int)v.minor << "." << (int)v.patch << std::endl;

	if (SDL_Init(0) < 0) {
		std::cout << ("SDL_Init failed: %s", SDL_GetError()) << std::endl;
		return 1;
	}	
	
	auto bla = new Engine();

	return 0;
}