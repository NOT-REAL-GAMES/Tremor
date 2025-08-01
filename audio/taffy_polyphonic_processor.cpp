#include "taffy_polyphonic_processor.h"
#include "tools.h"
#include <cstring>

namespace tremor::audio {

    TaffyPolyphonicProcessor::TaffyPolyphonicProcessor(uint32_t sampleRate) 
        : sampleRate_(sampleRate) {
        
        // Initialize all voices
        for (int i = 0; i < MAX_VOICES; ++i) {
            voices_[i].id = i;
            voices_[i].active = false;
            voices_[i].age = 0;
            voices_[i].priority = 0.0f;
            voices_[i].processor = std::make_unique<TaffyAudioProcessor>(sampleRate);
            voices_[i].triggerParam = 0;
            voices_[i].lastGate = 0.0f;
            voices_[i].releaseAge = 0;
        }
        
        Logger::get().info("🎹 TaffyPolyphonicProcessor initialized with {} voices", MAX_VOICES);
    }
    
    TaffyPolyphonicProcessor::~TaffyPolyphonicProcessor() {
    }
    
    bool TaffyPolyphonicProcessor::loadAudioChunk(const std::vector<uint8_t>& audioData) {
        std::lock_guard<std::mutex> lock(voicesMutex_);
        
        // Cache the audio data
        audioChunkData_ = audioData;
        
        // Load into all voices
        bool success = true;
        for (auto& voice : voices_) {
            if (!voice.processor->loadAudioChunk(audioData)) {
                Logger::get().error("Failed to load audio chunk into voice {}", voice.id);
                success = false;
            }
        }
        
        Logger::get().info("✅ Loaded audio chunk into {} voices", MAX_VOICES);
        return success;
    }
    
    void TaffyPolyphonicProcessor::processAudio(float* outputBuffer, uint32_t frameCount, uint32_t channelCount) {
        std::lock_guard<std::mutex> lock(voicesMutex_);
        
        // Clear output buffer first
        std::memset(outputBuffer, 0, frameCount * channelCount * sizeof(float));
        
        // Temporary buffer for each voice
        std::vector<float> voiceBuffer(frameCount * channelCount);
        
        // Process each active voice
        int activeVoices = 0;
        for (auto& voice : voices_) {
            if (voice.active) {
                // Clear voice buffer
                std::memset(voiceBuffer.data(), 0, voiceBuffer.size() * sizeof(float));
                
                // Process this voice
                voice.processor->processAudio(voiceBuffer.data(), frameCount, channelCount);
                
                // Mix into output buffer
                for (uint32_t i = 0; i < frameCount * channelCount; ++i) {
                    outputBuffer[i] += voiceBuffer[i];
                }
                
                activeVoices++;
            }
        }
        
        // Update voice ages
        updateVoiceAges(frameCount);
        
        // Apply some gain reduction if many voices are active to prevent clipping
        if (activeVoices > 1) {
            float mixGain = 1.0f / std::sqrt(static_cast<float>(activeVoices));
            for (uint32_t i = 0; i < frameCount * channelCount; ++i) {
                outputBuffer[i] *= mixGain;
            }
        }
        
        // Debug logging (only occasionally)
        static int processCount = 0;
        if (++processCount % 100 == 0 && activeVoices > 0) {
            Logger::get().debug("🎵 Polyphonic processor: {} active voices", activeVoices);
        }
    }
    
    void TaffyPolyphonicProcessor::setParameter(uint64_t parameterHash, float value) {
        std::lock_guard<std::mutex> lock(voicesMutex_);
        
        // Special handling for gate parameters - these trigger new voices
        if (parameterHash == Taffy::fnv1a_hash("gate")) {
            // Check if this is a rising edge (0->1)
            auto routeIt = parameterRoutes_.find(parameterHash);
            float lastValue = 0.0f;
            
            if (routeIt != parameterRoutes_.end()) {
                lastValue = routeIt->second.lastValue;
            }
            
            if (lastValue < 0.5f && value >= 0.5f) {
                // Rising edge - allocate a new voice
                int voiceId = allocateVoice(parameterHash);
                if (voiceId >= 0) {
                    Logger::get().info("🎵 Gate rising edge - allocated voice {}", voiceId);
                    voices_[voiceId].processor->setParameter(parameterHash, value);
                    voices_[voiceId].lastGate = value;
                    
                    // Update routing
                    parameterRoutes_[parameterHash] = {parameterHash, voiceId, value};
                }
            } else if (lastValue >= 0.5f && value < 0.5f) {
                // Falling edge - find the voice and set gate off
                if (routeIt != parameterRoutes_.end() && routeIt->second.voiceId >= 0) {
                    int voiceId = routeIt->second.voiceId;
                    if (voiceId < MAX_VOICES && voices_[voiceId].active) {
                        voices_[voiceId].processor->setParameter(parameterHash, value);
                        voices_[voiceId].lastGate = value;
                        voices_[voiceId].releaseAge = 0;  // Start counting release time
                        Logger::get().info("🎵 Gate falling edge - voice {} released", voiceId);
                    }
                }
                
                // Clear the parameter route so next trigger allocates a new voice
                parameterRoutes_.erase(parameterHash);
            }
        } else {
            // Non-gate parameter - send to all active voices
            for (auto& voice : voices_) {
                if (voice.active) {
                    voice.processor->setParameter(parameterHash, value);
                }
            }
        }
    }
    
    void TaffyPolyphonicProcessor::setStreamingTafLoader(std::shared_ptr<Taffy::StreamingTaffyLoader> loader) {
        std::lock_guard<std::mutex> lock(voicesMutex_);
        
        // Set loader for all voices
        for (auto& voice : voices_) {
            voice.processor->setStreamingTafLoader(loader);
        }
        
        Logger::get().info("✅ Set streaming TAF loader for all {} voices", MAX_VOICES);
    }
    
    int TaffyPolyphonicProcessor::allocateVoice(uint64_t triggerParam) {
        // First, try to find an inactive voice
        for (int i = 0; i < MAX_VOICES; ++i) {
            if (!voices_[i].active) {
                voices_[i].active = true;
                voices_[i].age = 0;
                voices_[i].priority = 1.0f;
                voices_[i].triggerParam = triggerParam;
                voices_[i].releaseAge = 0;
                return i;
            }
        }
        
        // All voices active - steal the oldest one
        int oldestVoice = findOldestVoice();
        if (oldestVoice >= 0) {
            Logger::get().info("🔄 Voice stealing: taking voice {} (age={})", 
                             oldestVoice, voices_[oldestVoice].age);
            
            // Reset the voice
            voices_[oldestVoice].active = true;
            voices_[oldestVoice].age = 0;
            voices_[oldestVoice].priority = 1.0f;
            voices_[oldestVoice].triggerParam = triggerParam;
            voices_[oldestVoice].releaseAge = 0;
            
            // Force gate off first
            voices_[oldestVoice].processor->setParameter(Taffy::fnv1a_hash("gate"), 0.0f);
            
            return oldestVoice;
        }
        
        return -1;  // No voice available (shouldn't happen)
    }
    
    void TaffyPolyphonicProcessor::updateVoiceAges(uint32_t frameCount) {
        for (auto& voice : voices_) {
            if (voice.active) {
                voice.age += frameCount;
                
                // Check if voice should be deactivated
                // For drums, deactivate quickly after gate release
                if (voice.lastGate < 0.5f) {
                    voice.releaseAge += frameCount;
                    
                    // Deactivate after 50ms (typical drum tail)
                    if (voice.releaseAge > sampleRate_ / 20) {
                        voice.active = false;
                        Logger::get().debug("🔕 Voice {} auto-deactivated after {} samples", 
                                          voice.id, voice.releaseAge);
                    }
                }
            }
        }
    }
    
    int TaffyPolyphonicProcessor::findOldestVoice() {
        int oldestVoice = -1;
        int maxAge = -1;
        
        for (int i = 0; i < MAX_VOICES; ++i) {
            if (voices_[i].active && voices_[i].age > maxAge) {
                maxAge = voices_[i].age;
                oldestVoice = i;
            }
        }
        
        return oldestVoice;
    }

} // namespace tremor::audio