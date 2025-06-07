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
#include <filesystem>
#include <cstring>
#include <cmath>


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
    int currentWaveform = 17;  // Start with imported sample
    int gateResetCounter = 0;  // Counter to reset gate after triggering
    bool bitCrushEnabled = false;  // Enable bit crusher effect

    std::unique_ptr<tremor::gfx::RenderBackend> rb;
    std::unique_ptr<tremor::audio::TaffyAudioProcessor> audioProcessor;
    
    // Simple bit crusher effect
    float applyBitCrush(float sample, float bits = 3.0f) {
        if (!bitCrushEnabled) return sample;
        
        // Reduce bit depth
        float levels = std::pow(2.0f, bits);
        float scaled = sample * 0.5f + 0.5f;  // Convert from [-1,1] to [0,1]
        float quantized = std::round(scaled * levels) / levels;
        return quantized * 2.0f - 1.0f;  // Convert back to [-1,1]
    }
    
    static void audioCallback(void* userdata, Uint8* stream, int len) {
        Engine* engine = static_cast<Engine*>(userdata);
        float* outputBuffer = reinterpret_cast<float*>(stream);
        uint32_t frameCount = len / (sizeof(float) * 2);  // 2 channels
        
        static int callbackCount = 0;
        if (callbackCount < 5) {
            Logger::get().info("Audio callback #{}: {} frames", callbackCount++, frameCount);
        }
        
        if (engine->audioProcessor) {
            engine->audioProcessor->processAudio(outputBuffer, frameCount, 2);
            
            // Apply bit crusher effect if enabled
            if (engine->bitCrushEnabled) {
                for (uint32_t i = 0; i < frameCount * 2; ++i) {
                    outputBuffer[i] = engine->applyBitCrush(outputBuffer[i]);
                }
            }
            
            // Handle gate reset for sample triggering
            if (engine->gateResetCounter > 0) {
                engine->gateResetCounter--;
                if (engine->gateResetCounter == 0) {
                    // Reset gate to 0 so next trigger works
                    uint64_t gateHash = Taffy::fnv1a_hash("gate");
                    engine->audioProcessor->setParameter(gateHash, 0.0f);
                    Logger::get().info("🔄 Reset gate parameter to 0");
                }
            }
            
            // Debug: Check the actual output values
            static int outputDebugCount = 0;
            if (outputDebugCount < 10) {
                float maxOutput = 0.0f;
                float sumOutput = 0.0f;
                for (uint32_t i = 0; i < frameCount * 2; ++i) {
                    maxOutput = std::max(maxOutput, std::abs(outputBuffer[i]));
                    sumOutput += std::abs(outputBuffer[i]);
                }
                if (maxOutput > 0.0f || outputDebugCount == 0) {
                    Logger::get().info("🔊 Audio callback output: max={}, avg={}, first few samples: {} {} {} {}",
                        maxOutput, sumOutput / (frameCount * 2),
                        outputBuffer[0], outputBuffer[1], outputBuffer[2], outputBuffer[3]);
                    outputDebugCount++;
                }
            }
            
            // Check if we're getting any output from the processor
            static int silentFrames = 0;
            bool hasSound = false;
            for (uint32_t i = 0; i < frameCount * 2; i += 32) {  // Sample check
                if (std::abs(outputBuffer[i]) > 0.001f) {
                    hasSound = true;
                    break;
                }
            }
            
            if (!hasSound) {
                silentFrames++;
                if (silentFrames == 100) {  // After ~1 second of silence
                    Logger::get().warning("Audio processor is producing silence!");
                }
            } else {
                if (silentFrames > 0) {
                    Logger::get().info("Audio started after {} silent frames", silentFrames);
                }
                silentFrames = 0;
            }
        } else {
            // Silence
            std::memset(stream, 0, len);
        }
    }

    Engine() : audioDevice(0) {
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
        Logger::get().critical("About to initialize audio...");
        initializeAudio();
        Logger::get().critical("Audio initialization complete. Device ID: {}", audioDevice);
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
        desired.samples = 1024;         // Larger buffer size
        desired.callback = audioCallback;
        desired.userdata = this;

        audioDevice = SDL_OpenAudioDevice(NULL, 0, &desired, &obtained, SDL_AUDIO_ALLOW_FORMAT_CHANGE);
        if (audioDevice == 0) {
            Logger::get().error("Failed to open audio device: {}", SDL_GetError());
            
            // List available audio drivers
            int count = SDL_GetNumAudioDrivers();
            Logger::get().error("Available audio drivers:");
            for (int i = 0; i < count; ++i) {
                Logger::get().error("  {}: {}", i, SDL_GetAudioDriver(i));
            }
            Logger::get().error("Current driver: {}", SDL_GetCurrentAudioDriver());
            return;
        }

        Logger::get().info("✅ Audio device opened (ID: {})", audioDevice);
        Logger::get().info("   Sample rate: {} Hz", obtained.freq);
        Logger::get().info("   Channels: {}", obtained.channels);
        Logger::get().info("   Buffer size: {} samples", obtained.samples);
        Logger::get().info("   Format: 0x{:04X} (AUDIO_F32SYS = 0x{:04X})", obtained.format, AUDIO_F32SYS);
        Logger::get().info("   Size per sample: {} bytes", obtained.size);
        Logger::get().info("   Silence value: {}", obtained.silence);
        
        // Check device status
        SDL_AudioStatus status = SDL_GetAudioDeviceStatus(audioDevice);
        const char* statusStr = (status == SDL_AUDIO_STOPPED) ? "STOPPED" :
                               (status == SDL_AUDIO_PLAYING) ? "PLAYING" : 
                               (status == SDL_AUDIO_PAUSED) ? "PAUSED" : "UNKNOWN";
        Logger::get().info("   Initial status: {}", statusStr);
        
        Logger::get().info("🎹 Keyboard controls:");
        Logger::get().info("   1-5: Waveforms (Sine, Square, Saw, Triangle, Noise)");
        Logger::get().info("   6-7: Mixer/ADSR demos");
        Logger::get().info("   8-0: Filters (Low/High/Band-pass)");
        Logger::get().info("   Q-Y: Distortion effects");
        Logger::get().info("   U: Imported sample (Toggle bit crusher with B)");
        Logger::get().info("   I-P: Drum samples (Kick, Hi-hat, Pad loop)");
        Logger::get().info("   B: Toggle bit crusher effect");

        // Load a test audio asset
        loadTestAudioAsset();

        // Start audio playback
        SDL_PauseAudioDevice(audioDevice, 0);
        
        // Verify audio is playing
        SDL_AudioStatus finalStatus = SDL_GetAudioDeviceStatus(audioDevice);
        const char* finalStatusStr = (finalStatus == SDL_AUDIO_STOPPED) ? "STOPPED" :
                                    (finalStatus == SDL_AUDIO_PLAYING) ? "PLAYING" : 
                                    (finalStatus == SDL_AUDIO_PAUSED) ? "PAUSED" : "UNKNOWN";
        Logger::get().info("   Audio playback status: {}", finalStatusStr);
    }

    void switchWaveform(int waveform) {
        if (waveform >= 0 && waveform <= 19 && waveform != currentWaveform) {
            currentWaveform = waveform;
            const char* waveform_names[] = {
                "Sine", "Square", "Saw", "Triangle", "Noise", "Mixer Demo", "ADSR Demo", 
                "Lowpass Filter", "Highpass Filter", "Bandpass Filter",
                "Hard Clip Distortion", "Soft Clip Distortion", "Foldback Distortion",
                "Bit Crush Distortion", "Overdrive Distortion", "ZX Spectrum Beeper",
                "Imported Sample", "Kick Drum", "Hi-Hat", "Pad Loop"
            };
            Logger::get().info("🎵 Switching to {}", waveform_names[waveform]);
            
            // Pause audio during switch
            SDL_PauseAudioDevice(audioDevice, 1);
            
            // Reload with new waveform
            loadTestAudioAsset();
            
            // Resume audio
            SDL_PauseAudioDevice(audioDevice, 0);
        }
    }

    void loadTestAudioAsset() {
        // Try different waveforms based on command line or default to sine
        const char* waveform_files[] = {
            "assets/audio/sine_wave.taf",
            "assets/audio/square_wave.taf", 
            "assets/audio/saw_wave.taf",
            "assets/audio/triangle_wave.taf",
            "assets/audio/noise_wave.taf",
            "assets/audio/mixer_demo.taf",
            "assets/audio/adsr_demo.taf",
            "assets/audio/filter_lowpass.taf",
            "assets/audio/filter_highpass.taf",
            "assets/audio/filter_bandpass.taf",
            "assets/audio/distortion_hardclip.taf",
            "assets/audio/distortion_softclip.taf",
            "assets/audio/distortion_foldback.taf",
            "assets/audio/distortion_bitcrush.taf",
            "assets/audio/distortion_overdrive.taf",
            "assets/audio/distortion_beeper.taf",
            "assets/audio/imported_sample.taf",
            "assets/audio/kick_drum.taf",
            "assets/audio/hihat.taf",
            "assets/audio/pad_loop.taf"
        };
        
        // Use current waveform selection
        int waveform_index = currentWaveform;
        
        // Also check for old filename for backward compatibility
        std::string audioPath = waveform_files[waveform_index];
        Logger::get().info("Checking for audio file: {}", audioPath);
        if (!std::filesystem::exists(audioPath)) {
            Logger::get().warning("File not found: {}, falling back to sine_440hz.taf", audioPath);
            audioPath = "assets/audio/sine_440hz.taf"; // Legacy filename
        }
        
        Taffy::Asset audioAsset;
        if (audioAsset.load_from_file_safe(audioPath)) {
            Logger::get().info("✅ Loaded audio asset: {}", audioPath);
            
            // Get the audio chunk
            auto audioData = audioAsset.get_chunk_data(Taffy::ChunkType::AUDI);
            if (audioData) {
                if (audioProcessor->loadAudioChunk(*audioData)) {
                    Logger::get().info("✅ Audio chunk loaded into processor");
                    Logger::get().info("🎵 Playing waveform type {}", waveform_index);
                    
                    // For sample-based assets (16-19), trigger the gate parameter
                    if (waveform_index >= 16 && waveform_index <= 19) {
                        // Set gate parameter to 1 to trigger sample playback
                        uint64_t gateHash = Taffy::fnv1a_hash("gate");
                        audioProcessor->setParameter(gateHash, 1.0f);
                        Logger::get().info("🎯 Triggered sample playback (gate=1)");
                        
                        // Reset gate after 10 audio callbacks (about 100ms at typical buffer sizes)
                        gateResetCounter = 10;
                    }
                }
            } else {
                Logger::get().error("No AUDI chunk found in asset");
            }
        } else {
            Logger::get().error("Failed to load audio asset: {}", audioPath);
            Logger::get().info("💡 Tip: Run test_waveforms to generate audio assets");
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
            
            // Handle keyboard input for waveform switching
            if (event.type == SDL_KEYDOWN) {
                switch (event.key.keysym.sym) {
                    case SDLK_1:
                        switchWaveform(0); // Sine
                        break;
                    case SDLK_2:
                        switchWaveform(1); // Square
                        break;
                    case SDLK_3:
                        switchWaveform(2); // Saw
                        break;
                    case SDLK_4:
                        switchWaveform(3); // Triangle
                        break;
                    case SDLK_5:
                        switchWaveform(4); // Noise
                        break;
                    case SDLK_6:
                        switchWaveform(5); // Mixer demo
                        break;
                    case SDLK_7:
                        switchWaveform(6); // ADSR demo
                        break;
                    case SDLK_8:
                        switchWaveform(7); // Lowpass filter
                        break;
                    case SDLK_9:
                        switchWaveform(8); // Highpass filter
                        break;
                    case SDLK_0:
                        switchWaveform(9); // Bandpass filter
                        break;
                    case SDLK_q:
                        switchWaveform(10); // Hard clip distortion
                        break;
                    case SDLK_w:
                        switchWaveform(11); // Soft clip distortion
                        break;
                    case SDLK_e:
                        switchWaveform(12); // Foldback distortion
                        break;
                    case SDLK_r:
                        switchWaveform(13); // Bit crush distortion
                        break;
                    case SDLK_t:
                        switchWaveform(14); // Overdrive distortion
                        break;
                    case SDLK_y:
                        switchWaveform(15); // ZX Spectrum beeper
                        break;
                    case SDLK_u:
                        Logger::get().info("🎵 Playing imported sample{}", bitCrushEnabled ? " (bit-crushed)" : "");
                        switchWaveform(16); // Imported sample
                        break;
                    case SDLK_i:
                        Logger::get().info("🥁 Switching to kick drum sample");
                        switchWaveform(17); // Kick drum
                        break;
                    case SDLK_o:
                        switchWaveform(18); // Hi-hat
                        break;
                    case SDLK_p:
                        switchWaveform(19); // Pad loop
                        break;
                    case SDLK_b:
                        // Toggle bit crusher effect
                        bitCrushEnabled = !bitCrushEnabled;
                        Logger::get().info("💥 Bit crusher effect {}", bitCrushEnabled ? "ENABLED" : "DISABLED");
                        if (bitCrushEnabled) {
                            Logger::get().info("   🎚️ 4-bit quantization for maximum crunch!");
                            Logger::get().info("   🎵 Your audio will sound gloriously lo-fi!");
                        }
                        break;
                }
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