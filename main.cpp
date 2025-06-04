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
#include "audio/taffy_audio_processor.h"
#include "renderer/taffy_integration.h"


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
    SDL_AudioDeviceID audioDevice;

    std::unique_ptr<tremor::gfx::RenderBackend> rb;
    std::unique_ptr<tremor::audio::TaffyAudioProcessor> audioProcessor;

    Engine() {
        Logger::get().critical("Engine constructor called!");
        Logger::get().critical("  Engine instance: {}", (void*)this);

        if (SDL_Init(SDL_INIT_EVERYTHING) < 0) {
            Logger::get().critical("SDL INITIALIZATION FAILED.");
        }

        window = SDL_CreateWindow("Tremor", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1280, 720, SDL_WINDOW_VULKAN);
        Logger::get().critical("Creating RenderBackend...");
        rb = tremor::gfx::RenderBackend::create(window);
        Logger::get().critical("RenderBackend created: {}", (void*)rb.get());

        // Initialize audio
        initializeAudio();
    }

    ~Engine() {
        if (audioDevice != 0) {
            SDL_CloseAudioDevice(audioDevice);
        }
    }

    void initializeAudio() {
        Logger::get().info("🎵 Initializing audio system...");

        // Create audio processor
        audioProcessor = std::make_unique<tremor::audio::TaffyAudioProcessor>(48000);

        // Set up SDL audio specification
        SDL_AudioSpec desired, obtained;
        SDL_zero(desired);
        desired.freq = 48000;
        desired.format = AUDIO_F32SYS;  // 32-bit float samples
        desired.channels = 2;           // Stereo
        desired.samples = 512;          // Buffer size
        desired.callback = audioCallback;
        desired.userdata = this;

        audioDevice = SDL_OpenAudioDevice(NULL, 0, &desired, &obtained, 0);
        if (audioDevice == 0) {
            Logger::get().error("Failed to open audio device: {}", SDL_GetError());
            return;
        }

        Logger::get().info("✅ Audio device opened:");
        Logger::get().info("   Sample rate: {} Hz", obtained.freq);
        Logger::get().info("   Channels: {}", obtained.channels);
        Logger::get().info("   Buffer size: {} samples", obtained.samples);

        // Load a test audio asset
        loadTestAudioAsset();

        // Start audio playback
        SDL_PauseAudioDevice(audioDevice, 0);
    }

    void loadTestAudioAsset() {
        // Try to load the sine wave audio asset
        std::string audioPath = "assets/audio/sine_440hz.taf";
        
        Taffy::Asset audioAsset;
        if (audioAsset.load_from_file_safe(audioPath)) {
            Logger::get().info("✅ Loaded audio asset: {}", audioPath);
            
            // Get the audio chunk
            auto audioData = audioAsset.get_chunk_data(Taffy::ChunkType::AUDI);
            if (audioData) {
                if (audioProcessor->loadAudioChunk(*audioData)) {
                    Logger::get().info("✅ Audio chunk loaded into processor");
                }
            } else {
                Logger::get().error("No AUDI chunk found in asset");
            }
        } else {
            Logger::get().error("Failed to load audio asset: {}", audioPath);
        }
    }

    static void audioCallback(void* userdata, Uint8* stream, int len) {
        Engine* engine = static_cast<Engine*>(userdata);
        float* outputBuffer = reinterpret_cast<float*>(stream);
        uint32_t frameCount = len / (sizeof(float) * 2);  // 2 channels
        
        if (engine->audioProcessor) {
            engine->audioProcessor->processAudio(outputBuffer, frameCount, 2);
        } else {
            // Silence
            std::memset(stream, 0, len);
        }
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

        SDL_Delay(17); // Simulate a frame delay
        return true;
#endif
    }
};


int main(int argc, char** argv) {

#ifdef _WIN32
	LPWSTR cmdLineW = GetCommandLineW();
#endif

	std::cout << "Initializing..." << std::endl;

    Logger::Config l{};
    l.enableConsole = true;
    l.enableFileOutput = true;
    l.logFilePath = "tremor_engine.log";
    l.minLevel = Logger::Level::Debug;
    l.showSourceLocation = true;


	Logger::create(l);

	Logger::get().info("Welcome. Starting Tremor...");

    Engine engine;

	do {

	} while (engine.Loop());

	return 0;
}