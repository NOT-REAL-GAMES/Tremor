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

// This file is part of Tremor: a game engine built on a """rewrite/
// reconceptualization""" of the Quake Engine as a foundation. Think 
// "Ship of Theseus", but the ship is made into a cool yacht that's 
// got a wood veneer that kind of reminds you of the original? I'm 
// coining that concept because I think it's funny.

// The original Quake Engine is licensed under the GPLv2 license. 
// Parts of its code have been used as inspiration for Tremor, much
// less copied directly, for the sake of modernization and legality.
// The Tremor project is not affiliated with or endorsed by id Software.
// idTech 2's dependencies on Quake will be gradually phased out of the Tremor project. 



#include "main.h"
#include "audio/taffy_audio_processor.h"
#include "audio/taffy_polyphonic_processor.h"
#include "renderer/taffy_integration.h"
#include "../Taffy/include/taffy_audio_tools.h"
#include "../Taffy/include/taffy_streaming.h"
#include <filesystem>
#include <cstring>
#include <cmath>
#include <fstream>

// Forward declaration for chunked TAF creation
bool createChunkedStreamingTAF(const std::string& inputWavPath,
                               const std::string& outputPath,
                               uint32_t chunkSizeMs,
                               bool verbose = true);


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
    std::unique_ptr<tremor::audio::TaffyPolyphonicProcessor> audioProcessor;
    
    // Simple voice management for drum playback
    int drumGateCounter = 0;  // Counter to track when gate can be retriggered
    static constexpr int MIN_GATE_INTERVAL = 3;  // Minimum audio callbacks between triggers
    
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
        static int waveformChangeCount = 0;
        static int lastWaveform = -1;
        
        Engine* eng = static_cast<Engine*>(userdata);
        if (eng && eng->currentWaveform != lastWaveform) {
            lastWaveform = eng->currentWaveform;
            waveformChangeCount = 0;
        }
        
        if (callbackCount < 5 || waveformChangeCount < 5) {
            Logger::get().info("Audio callback #{}: {} frames, waveform: {}", 
                             callbackCount++, frameCount, eng ? eng->currentWaveform : -1);
            waveformChangeCount++;
        }
        
        if (engine->audioProcessor) {
            static int processorDebugCount = 0;
            static int lastWaveformProcessed = -1;
            if (lastWaveformProcessed != engine->currentWaveform) {
                lastWaveformProcessed = engine->currentWaveform;
                processorDebugCount = 0;
            }
            if (processorDebugCount < 5) {
                Logger::get().info("🎵 Processing audio for waveform {}", engine->currentWaveform);
                processorDebugCount++;
            }
            engine->audioProcessor->processAudio(outputBuffer, frameCount, 2);
            
            // Apply bit crusher effect if enabled
            if (engine->bitCrushEnabled) {
                for (uint32_t i = 0; i < frameCount * 2; ++i) {
                    outputBuffer[i] = engine->applyBitCrush(outputBuffer[i]);
                }
            }
            
            // Update drum gate counter
            if (engine->drumGateCounter > 0) {
                engine->drumGateCounter--;
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
        
        // Set up sequencer callback to trigger drum sample
        if (rb) {
            auto* vulkanBackend = static_cast<tremor::gfx::VulkanBackend*>(rb.get());
            vulkanBackend->setSequencerCallback([this](int step) {
                Logger::get().info("🥁 Sequencer step {} triggered - playing drum sample!", step);
                
                // Make sure we're on the kick drum sample
                if (currentWaveform != 17) {
                    currentWaveform = 17;  // Kick drum
                    loadTestAudioAsset();
                }
                
                // With polyphonic processor, just trigger the gate
                if (audioProcessor) {
                    uint64_t gateHash = Taffy::fnv1a_hash("gate");
                    
                    // The polyphonic processor handles voice allocation automatically
                    // Just send gate transitions
                    audioProcessor->setParameter(gateHash, 0.0f);  // Ensure clean start
                    audioProcessor->setParameter(gateHash, 1.0f);  // Trigger
                    
                    Logger::get().info("🎯 Triggered polyphonic drum for step {}", step);
                    
                    // Schedule gate release (shorter for drums)
                    drumGateCounter = 2;  // Release after 2 audio callbacks
                }
            });
            Logger::get().info("✅ Sequencer callback connected to audio system");
        }
        
        Logger::get().critical("Engine constructor complete!");
    }

    ~Engine() {
        if (audioDevice != 0) {
            SDL_CloseAudioDevice(audioDevice);
        }
    }

    void initializeAudio() {
        Logger::get().info("🎵 Initializing audio system...");

        // Create polyphonic audio processor
        audioProcessor = std::make_unique<tremor::audio::TaffyPolyphonicProcessor>(48000);
        Logger::get().info("   Polyphonic audio processor created: {}", (void*)audioProcessor.get());

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
        if (waveform >= 0 && waveform <= 22) {
            currentWaveform = waveform;
            const char* waveform_names[] = {
                "Sine", "Square", "Saw", "Triangle", "Noise", "Mixer Demo", "ADSR Demo", 
                "Lowpass Filter", "Highpass Filter", "Bandpass Filter",
                "Hard Clip Distortion", "Soft Clip Distortion", "Foldback Distortion",
                "Bit Crush Distortion", "Overdrive Distortion", "ZX Spectrum Beeper",
                "Imported Sample", "Kick Drum", "Hi-Hat", "Pad Loop", "Bit-Crushed Import",
                "Streaming Import", "Streaming Test"
            };
            Logger::get().info("🎵 Switching to {}", waveform_names[waveform]);
            
            // For streaming audio, don't pause - just load new audio
            // Pausing/resuming can cause additional hitches
            if (waveform == 21 || waveform == 22) {
                Logger::get().info("🎵 Loading streaming audio without pausing...");
                // For chunked TAFs, pause audio to ensure clean switch
                if (waveform == 21) {
                    SDL_PauseAudioDevice(audioDevice, 1);
                }
                loadTestAudioAsset();
                if (waveform == 21) {
                    SDL_PauseAudioDevice(audioDevice, 0);
                }
            } else {
                // Pause audio during switch for non-streaming
                SDL_PauseAudioDevice(audioDevice, 1);
                
                // Reload with new waveform
                loadTestAudioAsset();
                
                // Resume audio
                SDL_PauseAudioDevice(audioDevice, 0);
            }
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
            "assets/audio/pad_loop.taf",
            "assets/audio/imported_bitcrushed.taf",
            "assets/audio/imported_sample.taf",
            "streaming_drum.taf"
        };
        
        // Use current waveform selection
        int waveform_index = currentWaveform;
        
        // Also check for old filename for backward compatibility
        std::string audioPath = waveform_files[waveform_index];
        Logger::get().info("Checking for audio file: {}", audioPath);
        
        // Check if file exists, especially important for streaming audio
        if (!std::filesystem::exists(audioPath)) {
            Logger::get().error("❌ File not found: {}", audioPath);
            // For streaming audio, try to create it if it doesn't exist
            if (waveform_index == 22) {
                Logger::get().info("📁 Streaming TAF not found, creating it first...");
                // Try to use an existing WAV file
                std::string inputWav = "assets/audio/imported_sample.wav";
                if (!std::filesystem::exists(inputWav)) {
                    Logger::get().error("❌ No WAV file found to create streaming TAF");
                    return;
                }
                
                if (tremor::taffy::tools::createStreamingAudioAsset(inputWav, audioPath, 10000)) {
                    Logger::get().info("✅ Created streaming TAF from WAV, continuing with load...");
                } else {
                    Logger::get().error("❌ Failed to create streaming TAF from WAV");
                    return;
                }
            } else {
                Logger::get().warning("File not found: {}, falling back to sine_440hz.taf", audioPath);
                audioPath = "assets/audio/sine_440hz.taf"; // Legacy filename
            }
        } else {
            Logger::get().info("✅ File exists: {}", audioPath);
        }

        // For chunked TAFs (waveform 21), use StreamingTaffyLoader to get metadata
        if (waveform_index == 21) {
            Logger::get().info("🎵 Loading chunked TAF with StreamingTaffyLoader...");
            
            // IMPORTANT: Pause audio to prevent race conditions
            SDL_PauseAudioDevice(audioDevice, 1);
            // Audio thread will stop on next callback
            
            auto loader = std::make_shared<Taffy::StreamingTaffyLoader>();
            if (loader->open(audioPath)) {
                Logger::get().info("✅ Opened TAF for streaming, {} chunks", loader->getChunkCount());
                
                // Load the metadata chunk (chunk 0)
                auto metadataChunk = loader->loadChunk(0);
                if (!metadataChunk.empty()) {
                    Logger::get().info("📦 Loaded metadata chunk, size: {} bytes", metadataChunk.size());
                    
                    // Load into audio processor
                    if (audioProcessor->loadAudioChunk(metadataChunk)) {
                        Logger::get().info("✅ Metadata loaded into processor");
                        
                        // Set the streaming loader
                        Logger::get().info("🔧 About to set streaming TAF loader...");
                        audioProcessor->setStreamingTafLoader(loader);
                        Logger::get().info("✅ Set up chunked TAF streaming loader");
                    } else {
                        Logger::get().error("❌ Failed to load metadata into processor");
                    }
                } else {
                    Logger::get().error("❌ Failed to load metadata chunk");
                }
            } else {
                Logger::get().error("❌ Failed to open TAF for streaming");
            }
            
            // Resume audio after loading
            // Background loader will preload chunks
            SDL_PauseAudioDevice(audioDevice, 0);
        } else {
            // Original loading code for non-chunked TAFs
            Taffy::Asset audioAsset;
            if (audioAsset.load_from_file_safe(audioPath)) {
                Logger::get().info("✅ Loaded audio asset: {}", audioPath);
                
                // Get the audio chunk
                auto audioData = audioAsset.get_chunk_data(Taffy::ChunkType::AUDI);
                if (audioData) {
                    Logger::get().info("📦 Found audio chunk, size: {} bytes", audioData->size());
                    
                    // For streaming audio, we need to handle it differently
                    if (waveform_index == 22) {
                    // For streaming, only load the metadata, not the actual audio data
                    // The audio data will be streamed from disk
                    Logger::get().info("🎵 Loading streaming audio metadata only...");
                    
                    // Calculate how much of the chunk is metadata vs audio data
                    // Parse the header to get exact sizes
                    const uint8_t* ptr = audioData->data();
                    Taffy::AudioChunk header;
                    std::memcpy(&header, ptr, sizeof(Taffy::AudioChunk));
                    
                    size_t metadataSize = sizeof(Taffy::AudioChunk);
                    metadataSize += header.node_count * sizeof(Taffy::AudioChunk::Node);
                    metadataSize += header.connection_count * sizeof(Taffy::AudioChunk::Connection);
                    metadataSize += header.pattern_count * sizeof(Taffy::AudioChunk::Pattern);
                    metadataSize += header.sample_count * sizeof(Taffy::AudioChunk::WaveTable);
                    metadataSize += header.parameter_count * sizeof(Taffy::AudioChunk::Parameter);
                    metadataSize += header.streaming_count * sizeof(Taffy::AudioChunk::StreamingAudio);
                    
                    Logger::get().info("📊 Metadata size: {} bytes, Total chunk: {} bytes", 
                                     metadataSize, audioData->size());
                    
                    // Load the entire TAF chunk, including embedded audio data
                    if (audioProcessor->loadAudioChunk(*audioData)) {
                        if (header.streaming_count > 0 && audioData->size() > metadataSize) {
                            Logger::get().info("✅ Streaming audio loaded with {} MB of embedded data", 
                                             (audioData->size() - metadataSize) / (1024.0 * 1024.0));
                            // No need to set external file path - audio is embedded in TAF
                        } else {
                            Logger::get().info("✅ Streaming audio metadata loaded");
                            
                            // Check if this is a chunked TAF
                            if (header.streaming_count > 0) {
                                // Parse streaming info to check chunk count
                                const uint8_t* ptr = audioData->data() + metadataSize - 
                                                   (header.streaming_count * sizeof(Taffy::AudioChunk::StreamingAudio));
                                Taffy::AudioChunk::StreamingAudio streamInfo;
                                std::memcpy(&streamInfo, ptr, sizeof(streamInfo));
                                
                                if (streamInfo.chunk_count > 0 && streamInfo.data_offset == 0) {
                                    Logger::get().info("🔄 Detected chunked TAF with {} chunks", streamInfo.chunk_count);
                                    Logger::get().info("   Total samples: {}", streamInfo.total_samples);
                                    Logger::get().info("   Sample rate: {} Hz", streamInfo.sample_rate);
                                    Logger::get().info("   Chunk size: {} samples", streamInfo.chunk_size);
                                    
                                    // Create streaming TAF loader
                                    auto loader = std::make_shared<Taffy::StreamingTaffyLoader>();
                                    if (loader->open(audioPath)) {
                                        audioProcessor->setStreamingTafLoader(loader);
                                        Logger::get().info("✅ Set up chunked TAF streaming loader");
                                        Logger::get().info("   Loader reports {} chunks", loader->getChunkCount());
                                    } else {
                                        Logger::get().error("❌ Failed to open TAF for streaming");
                                    }
                                }
                            }
                        }
                    }
                } else {
                    // Non-streaming audio, load the entire chunk
                    if (audioProcessor->loadAudioChunk(*audioData)) {
                        Logger::get().info("✅ Audio chunk loaded into processor");
                        Logger::get().info("🎵 Playing waveform type {}", waveform_index);
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
        
        // Disabled gate triggering to debug hanging issue
        // // For sample-based assets (16-22), trigger the gate parameter
        // // This is outside the loading logic so it works for both regular and chunked TAFs
        // if (waveform_index >= 16 && waveform_index <= 22) {
        //     // Set gate parameter to 1 to trigger sample playback
        //     uint64_t gateHash = Taffy::fnv1a_hash("gate");
        //     audioProcessor->setParameter(gateHash, 1.0f);
        //     Logger::get().info("🎯 Triggered sample playback (gate=1) for waveform {}", waveform_index);
        //     
        //     // Also debug what parameters exist
        //     Logger::get().info("   Setting gate parameter with hash: 0x{:x}", gateHash);
        //     
        //     // Reset gate after 100 audio callbacks (about 1 second at typical buffer sizes)
        //     gateResetCounter = 100;
        //     
        //     // Check audio device status
        //     SDL_AudioStatus status = SDL_GetAudioDeviceStatus(audioDevice);
        //     const char* statusStr = (status == SDL_AUDIO_STOPPED) ? "STOPPED" :
        //                            (status == SDL_AUDIO_PLAYING) ? "PLAYING" : 
        //                            (status == SDL_AUDIO_PAUSED) ? "PAUSED" : "UNKNOWN";
        //     Logger::get().info("   Audio device status: {}", statusStr);
        // }
    }

    bool Loop() {
#if defined(USING_VULKAN)
        static int loopCount = 0;
        if (loopCount < 10) {
            Logger::get().info("Loop() called, count: {}", loopCount++);
        }
        
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            // Pass event to render backend for UI handling
            if (rb) {
                static_cast<tremor::gfx::VulkanBackend*>(rb.get())->handleInput(event);
            }

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
                    
                    case SDLK_m:
                        // Create chunked streaming audio TAF
                        Logger::get().info("🎵 Creating chunked streaming audio TAF...");
                        {
                            // First check if we have a large WAV file
                            std::string inputWav = "assets/audio/imported_sample.wav";
                            if (!std::filesystem::exists(inputWav)) {
                                // Use a test file if no imported sample
                                inputWav = "../large_test.wav";
                                if (!std::filesystem::exists(inputWav)) {
                                    Logger::get().error("❌ No WAV file found for chunking");
                                    Logger::get().info("💡 Please import a WAV file first (drag & drop)");
                                    break;
                                }
                            }
                            
                            std::string outputTaf = "assets/audio/imported_sample.taf";
                            uint32_t chunkSizeMs = 500; // 500ms chunks for smoother playback
                            
                            Logger::get().info("📦 Input: {}", inputWav);
                            Logger::get().info("📦 Output: {}", outputTaf);
                            Logger::get().info("📦 Chunk size: {} ms", chunkSizeMs);
                            
                            // Call the chunked TAF creation function
                            if (createChunkedStreamingTAF(inputWav, outputTaf, chunkSizeMs)) {
                                Logger::get().info("✅ Chunked streaming TAF created successfully!");
                                Logger::get().info("🎵 Press 'N' to play the chunked streaming audio");
                                
                                // Get file sizes for comparison
                                auto wavSize = std::filesystem::file_size(inputWav) / (1024.0 * 1024.0);
                                auto tafSize = std::filesystem::file_size(outputTaf) / (1024.0 * 1024.0);
                                Logger::get().info("📊 WAV size: {:.2f} MB, TAF size: {:.2f} MB", wavSize, tafSize);
                            } else {
                                Logger::get().error("❌ Failed to create chunked streaming TAF");
                            }
                        }
                        break;
                    case SDLK_n:
                        // Cycle through all waveforms
                        currentWaveform = 21; // Use our chunked TAF
                        Logger::get().info("🎵 Loading waveform {} (chunked TAF)...", currentWaveform);
                        switchWaveform(currentWaveform);
                        break;
                }
            }
        }

        
        static int renderCallCount = 0;
        if (renderCallCount < 5) {
            Logger::get().critical("About to call beginFrame, count {}", renderCallCount);
        }

        rb.get()->beginFrame();

        if (renderCallCount < 5) {
            Logger::get().critical("beginFrame returned, about to call endFrame");
        }

        rb.get()->endFrame();
        
        if (renderCallCount < 5) {
            Logger::get().critical("endFrame returned, render loop iteration {} complete", renderCallCount);
            renderCallCount++;
        }

        // Reset gate when counter expires
        static bool gateIsHigh = false;
        if (drumGateCounter > 0) {
            gateIsHigh = true;
        } else if (gateIsHigh && audioProcessor) {
            // Counter expired, reset gate
            uint64_t gateHash = Taffy::fnv1a_hash("gate");
            audioProcessor->setParameter(gateHash, 0.0f);
            gateIsHigh = false;
            Logger::get().info("🔄 Auto-reset gate after counter expired");
        }
        
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
    Logger::get().critical("Engine constructed, starting main loop...");

	do {

	} while (engine.Loop());

	return 0;
}

// Implementation of chunked streaming TAF creation
bool createChunkedStreamingTAF(const std::string& inputWavPath,
                               const std::string& outputPath,
                               uint32_t chunkSizeMs,
                               bool verbose) {
    Logger::get().info("🎵 Creating chunked streaming TAF");
    Logger::get().info("  Input: {}", inputWavPath);
    Logger::get().info("  Chunk size: {} ms", chunkSizeMs);
    
    // Load WAV file header
    std::ifstream file(inputWavPath, std::ios::binary);
    if (!file) {
        Logger::get().error("Failed to open WAV file: {}", inputWavPath);
        return false;
    }
    
    // Read WAV header
    char riff[4];
    file.read(riff, 4);
    if (std::string(riff, 4) != "RIFF") {
        Logger::get().error("Not a valid WAV file: missing RIFF header");
        return false;
    }
    
    uint32_t fileSize;
    file.read(reinterpret_cast<char*>(&fileSize), 4);
    
    char wave[4];
    file.read(wave, 4);
    if (std::string(wave, 4) != "WAVE") {
        Logger::get().error("Not a valid WAV file: missing WAVE header");
        return false;
    }
    
    // Find and read fmt chunk
    uint32_t sampleRate = 0;
    uint16_t channelCount = 0;
    uint16_t bitsPerSample = 0;
    uint64_t dataSize = 0;
    uint64_t dataOffset = 0;
    
    while (file.good()) {
        char chunkId[4];
        file.read(chunkId, 4);
        if (!file.good()) break;
        
        uint32_t chunkSize;
        file.read(reinterpret_cast<char*>(&chunkSize), 4);
        
        if (std::string(chunkId, 4) == "fmt ") {
            uint16_t format;
            file.read(reinterpret_cast<char*>(&format), 2);
            file.read(reinterpret_cast<char*>(&channelCount), 2);
            file.read(reinterpret_cast<char*>(&sampleRate), 4);
            file.seekg(6, std::ios::cur);
            file.read(reinterpret_cast<char*>(&bitsPerSample), 2);
            
            if (chunkSize > 16) {
                file.seekg(chunkSize - 16, std::ios::cur);
            }
        } else if (std::string(chunkId, 4) == "data") {
            dataSize = chunkSize;
            dataOffset = file.tellg();
            break;
        } else {
            file.seekg(chunkSize, std::ios::cur);
        }
    }
    
    if (dataOffset == 0) {
        Logger::get().error("No data chunk found in WAV file");
        return false;
    }
    
    uint32_t totalSamples = dataSize / (channelCount * (bitsPerSample / 8));
    uint32_t samplesPerChunk = (chunkSizeMs * sampleRate) / 1000;
    uint32_t bytesPerSample = channelCount * (bitsPerSample / 8);
    uint32_t bytesPerChunk = samplesPerChunk * bytesPerSample;
    uint32_t totalChunks = (totalSamples + samplesPerChunk - 1) / samplesPerChunk;
    
    Logger::get().info("📊 WAV file info:");
    Logger::get().info("  Sample rate: {} Hz", sampleRate);
    Logger::get().info("  Channels: {}", channelCount);
    Logger::get().info("  Bits per sample: {}", bitsPerSample);
    Logger::get().info("  Total samples: {}", totalSamples);
    Logger::get().info("  Duration: {:.1f} seconds", totalSamples / (float)sampleRate);
    Logger::get().info("  Data size: {:.2f} MB", dataSize / (1024.0 * 1024.0));
    Logger::get().info("📦 Chunking info:");
    Logger::get().info("  Samples per chunk: {}", samplesPerChunk);
    Logger::get().info("  Bytes per chunk: {:.1f} KB", bytesPerChunk / 1024.0);
    Logger::get().info("  Total chunks: {}", totalChunks);
    
    // Create metadata chunk
    std::vector<uint8_t> metadataChunk;
    
    // Write audio chunk header
    Taffy::AudioChunk header{};  // Zero-initialize
    header.node_count = 3;        // Parameter -> StreamingSampler -> Amplifier
    header.connection_count = 2;
    header.pattern_count = 0;
    header.sample_count = 0;
    header.parameter_count = 5;
    header.sample_rate = sampleRate;
    header.tick_rate = 0;
    header.streaming_count = 1;   // One streaming audio entry
    
    metadataChunk.resize(sizeof(Taffy::AudioChunk));
    std::memcpy(metadataChunk.data(), &header, sizeof(header));
    
    // Add nodes
    std::vector<Taffy::AudioChunk::Node> nodes;
    nodes.resize(3);
    
    // Zero-initialize all nodes first
    std::memset(nodes.data(), 0, nodes.size() * sizeof(Taffy::AudioChunk::Node));
    
    // Parameter (gate)
    nodes[0].id = 0;
    nodes[0].type = static_cast<Taffy::AudioChunk::NodeType>(41);  // Parameter
    nodes[0].name_hash = Taffy::fnv1a_hash("gate_param");
    nodes[0].input_count = 0;
    nodes[0].output_count = 1;
    nodes[0].param_offset = 0;
    nodes[0].param_count = 1;
    nodes[0].position[0] = 0;
    nodes[0].position[1] = 0;
    
    // StreamingSampler
    nodes[1].id = 1;
    nodes[1].type = static_cast<Taffy::AudioChunk::NodeType>(4);  // StreamingSampler
    nodes[1].name_hash = Taffy::fnv1a_hash("streaming_sampler");
    nodes[1].input_count = 1;
    nodes[1].output_count = 1;
    nodes[1].param_offset = 1;
    nodes[1].param_count = 3;
    nodes[1].position[0] = 0;
    nodes[1].position[1] = 0;
    
    // Amplifier
    nodes[2].id = 2;
    nodes[2].type = static_cast<Taffy::AudioChunk::NodeType>(11);  // Amplifier
    nodes[2].name_hash = Taffy::fnv1a_hash("amplifier");
    nodes[2].input_count = 2;
    nodes[2].output_count = 1;
    nodes[2].param_offset = 4;
    nodes[2].param_count = 1;
    nodes[2].position[0] = 0;
    nodes[2].position[1] = 0;
    
    size_t nodeOffset = metadataChunk.size();
    metadataChunk.resize(nodeOffset + nodes.size() * sizeof(Taffy::AudioChunk::Node));
    std::memcpy(metadataChunk.data() + nodeOffset, nodes.data(), 
                nodes.size() * sizeof(Taffy::AudioChunk::Node));
    
    // Add connections
    std::vector<Taffy::AudioChunk::Connection> connections = {
        {0, 0, 1, 0, 1.0f},  // Parameter -> StreamingSampler
        {1, 0, 2, 0, 1.0f}   // StreamingSampler -> Amplifier
    };
    
    size_t connOffset = metadataChunk.size();
    metadataChunk.resize(connOffset + connections.size() * sizeof(Taffy::AudioChunk::Connection));
    std::memcpy(metadataChunk.data() + connOffset, connections.data(),
                connections.size() * sizeof(Taffy::AudioChunk::Connection));
    
    // Add parameters
    std::vector<Taffy::AudioChunk::Parameter> params = {
        {Taffy::fnv1a_hash("gate"), 0.0f, 0.0f, 1.0f},
        {Taffy::fnv1a_hash("stream_index"), 0.0f, 0.0f, 10.0f},
        {Taffy::fnv1a_hash("pitch"), 1.0f, 0.1f, 4.0f},
        {Taffy::fnv1a_hash("start_position"), 0.0f, 0.0f, 1.0f},
        {Taffy::fnv1a_hash("amplitude"), 1.0f, 0.0f, 2.0f}
    };
    
    size_t paramOffset = metadataChunk.size();
    metadataChunk.resize(paramOffset + params.size() * sizeof(Taffy::AudioChunk::Parameter));
    std::memcpy(metadataChunk.data() + paramOffset, params.data(),
                params.size() * sizeof(Taffy::AudioChunk::Parameter));
    
    // Add streaming audio info
    Taffy::AudioChunk::StreamingAudio streamInfo;
    streamInfo.name_hash = Taffy::fnv1a_hash("chunked_stream");
    streamInfo.total_samples = totalSamples;
    streamInfo.sample_rate = sampleRate;
    streamInfo.channel_count = channelCount;
    streamInfo.bit_depth = bitsPerSample;
    streamInfo.chunk_size = samplesPerChunk;
    streamInfo.format = 0;  // PCM
    streamInfo.data_offset = 0;  // Data is in separate chunks
    streamInfo.chunk_count = totalChunks;
    
    size_t streamOffset = metadataChunk.size();
    metadataChunk.resize(streamOffset + sizeof(streamInfo));
    std::memcpy(metadataChunk.data() + streamOffset, &streamInfo, sizeof(streamInfo));
    
    // Use a temporary asset to write chunks
    Taffy::Asset tempAsset;
    
    // Create a single Asset with all chunks combined
    // First, we need to manually build the asset to work around the map limitation
    
    // Calculate total data size
    size_t totalDataSize = metadataChunk.size();
    for (uint32_t i = 0; i < totalChunks; ++i) {
        size_t chunkBytes = (i == totalChunks - 1) ? 
            ((totalSamples - (i * samplesPerChunk)) * bytesPerSample) : bytesPerChunk;
        totalDataSize += chunkBytes;
    }
    
    Logger::get().info("📝 Creating chunked TAF with {} chunks, total data: {:.2f} MB", 
                      totalChunks + 1, totalDataSize / (1024.0 * 1024.0));
    
    // We'll create a custom save method since Asset class has the map limitation
    std::ofstream outFile(outputPath, std::ios::binary);
    if (!outFile) {
        Logger::get().error("❌ Failed to open output file: {}", outputPath);
        return false;
    }
    
    // Write TAF header
    Taffy::AssetHeader assetHeader{};
    std::memcpy(assetHeader.magic, "TAF!", 4);
    assetHeader.version_major = 1;
    assetHeader.version_minor = 0;
    assetHeader.version_patch = 0;
    std::strncpy(assetHeader.creator, "Tremor", sizeof(assetHeader.creator) - 1);
    std::strncpy(assetHeader.description, "Chunked Streaming Audio", sizeof(assetHeader.description) - 1);
    assetHeader.feature_flags = Taffy::FeatureFlags::None;
    assetHeader.chunk_count = totalChunks + 1; // +1 for metadata
    
    // Calculate header and directory size
    size_t headerSize = sizeof(Taffy::AssetHeader);
    size_t dirSize = assetHeader.chunk_count * sizeof(Taffy::ChunkDirectoryEntry);
    assetHeader.total_size = headerSize + dirSize + totalDataSize;
    
    outFile.write(reinterpret_cast<const char*>(&assetHeader), sizeof(assetHeader));
    
    // Write chunk directory
    uint64_t currentOffset = headerSize + dirSize;
    
    // Metadata chunk entry
    Taffy::ChunkDirectoryEntry metaEntry{};
    metaEntry.type = Taffy::ChunkType::AUDI;
    metaEntry.flags = 0;
    metaEntry.offset = currentOffset;
    metaEntry.size = metadataChunk.size();
    metaEntry.checksum = 0; // We'll calculate this properly
    std::strncpy(metaEntry.name, "audio_metadata", sizeof(metaEntry.name) - 1);
    
    // Calculate CRC32 for metadata
    {
        uint32_t crc = 0xFFFFFFFF;
        for (size_t i = 0; i < metadataChunk.size(); ++i) {
            crc ^= metadataChunk[i];
            for (int j = 0; j < 8; ++j) {
                crc = (crc >> 1) ^ (0xEDB88320 * (crc & 1));
            }
        }
        metaEntry.checksum = ~crc;
    }
    
    outFile.write(reinterpret_cast<const char*>(&metaEntry), sizeof(metaEntry));
    currentOffset += metadataChunk.size();
    
    // Write directory entries for audio chunks
    std::vector<Taffy::ChunkDirectoryEntry> audioEntries;
    audioEntries.reserve(totalChunks);
    
    file.seekg(dataOffset);
    
    for (uint32_t i = 0; i < totalChunks; ++i) {
        Taffy::ChunkDirectoryEntry entry{};
        entry.type = Taffy::ChunkType::AUDI;
        entry.flags = 0;
        entry.offset = currentOffset;
        
        size_t chunkBytes = bytesPerChunk;
        if (i == totalChunks - 1) {
            uint32_t remainingSamples = totalSamples - (i * samplesPerChunk);
            chunkBytes = remainingSamples * bytesPerSample;
        }
        
        entry.size = chunkBytes;
        std::string chunkName = "audio_chunk_" + std::to_string(i);
        std::strncpy(entry.name, chunkName.c_str(), sizeof(entry.name) - 1);
        
        // We'll calculate checksum when we write the data
        entry.checksum = 0;
        
        audioEntries.push_back(entry);
        currentOffset += chunkBytes;
    }
    
    // First pass: read all audio chunks and calculate checksums
    Logger::get().info("📊 Calculating checksums for {} audio chunks...", totalChunks);
    std::vector<uint32_t> audioChecksums;
    audioChecksums.reserve(totalChunks);
    
    for (uint32_t i = 0; i < totalChunks; ++i) {
        size_t bytesToRead = bytesPerChunk;
        if (i == totalChunks - 1) {
            uint32_t remainingSamples = totalSamples - (i * samplesPerChunk);
            bytesToRead = remainingSamples * bytesPerSample;
        }
        
        std::vector<uint8_t> tempBuffer(bytesToRead);
        file.read(reinterpret_cast<char*>(tempBuffer.data()), bytesToRead);
        size_t bytesRead = file.gcount();
        
        // Calculate CRC32
        uint32_t crc = 0xFFFFFFFF;
        for (size_t j = 0; j < bytesRead; ++j) {
            crc ^= tempBuffer[j];
            for (int k = 0; k < 8; ++k) {
                crc = (crc >> 1) ^ (0xEDB88320 * (crc & 1));
            }
        }
        audioChecksums.push_back(~crc);
        
        // Update the checksum in our vector
        audioEntries[i].checksum = ~crc;
        audioEntries[i].size = bytesRead; // Update actual size if different
    }
    
    // Now write all directory entries with correct checksums
    for (const auto& entry : audioEntries) {
        outFile.write(reinterpret_cast<const char*>(&entry), sizeof(entry));
    }
    
    // Now write the actual data
    // First, metadata
    outFile.write(reinterpret_cast<const char*>(metadataChunk.data()), metadataChunk.size());
    
    // Then audio chunks (second pass)
    std::vector<uint8_t> audioBuffer(bytesPerChunk);
    file.seekg(dataOffset);
    
    for (uint32_t i = 0; i < totalChunks; ++i) {
        size_t bytesToRead = bytesPerChunk;
        if (i == totalChunks - 1) {
            uint32_t remainingSamples = totalSamples - (i * samplesPerChunk);
            bytesToRead = remainingSamples * bytesPerSample;
            audioBuffer.resize(bytesToRead);
        }
        
        file.read(reinterpret_cast<char*>(audioBuffer.data()), bytesToRead);
        size_t bytesRead = file.gcount();
        
        outFile.write(reinterpret_cast<const char*>(audioBuffer.data()), bytesRead);
        
        if (verbose && (i % 50 == 0 || i == totalChunks - 1)) {
            Logger::get().info("  Progress: {}/{} chunks", i + 1, totalChunks);
        }
    }
    
    file.close();
    outFile.close();
    
    // Verify the file was created
    auto tafFileSize = std::filesystem::file_size(outputPath);
    Logger::get().info("✅ Chunked streaming TAF created successfully!");
    Logger::get().info("  Output: {}", outputPath);
    Logger::get().info("  TAF size: {:.2f} MB", tafFileSize / (1024.0 * 1024.0));
    Logger::get().info("  Chunks: 1 metadata + {} audio", totalChunks);
    
    return true;
}