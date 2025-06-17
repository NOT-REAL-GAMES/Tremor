#pragma once

#include "taffy_audio_processor.h"
#include <array>
#include <queue>

namespace tremor::audio {

    /**
     * Polyphonic wrapper for TaffyAudioProcessor
     * Manages multiple voices for polyphonic playback
     */
    class TaffyPolyphonicProcessor {
    public:
        static constexpr int MAX_VOICES = 16;  // Maximum simultaneous voices
        
        struct Voice {
            int id;
            bool active;
            int age;  // How many samples since voice started
            float priority;  // For voice stealing
            std::unique_ptr<TaffyAudioProcessor> processor;
            
            // Voice-specific parameters
            uint64_t triggerParam;  // Parameter that triggered this voice
            float lastGate;         // Last gate value for edge detection
        };
        
        TaffyPolyphonicProcessor(uint32_t sampleRate = 48000);
        ~TaffyPolyphonicProcessor();
        
        /**
         * Load audio chunk into all voices
         */
        bool loadAudioChunk(const std::vector<uint8_t>& audioData);
        
        /**
         * Process all active voices and mix to output
         */
        void processAudio(float* outputBuffer, uint32_t frameCount, uint32_t channelCount = 2);
        
        /**
         * Set a parameter value (will route to appropriate voice)
         */
        void setParameter(uint64_t parameterHash, float value);
        
        /**
         * Set streaming TAF loader for all voices
         */
        void setStreamingTafLoader(std::shared_ptr<Taffy::StreamingTaffyLoader> loader);
        
    private:
        uint32_t sampleRate_;
        std::array<Voice, MAX_VOICES> voices_;
        std::vector<uint8_t> audioChunkData_;  // Cached audio chunk for new voices
        
        // Voice allocation
        int allocateVoice(uint64_t triggerParam);
        void updateVoiceAges(uint32_t frameCount);
        int findOldestVoice();
        
        // Parameter routing
        struct ParameterRoute {
            uint64_t paramHash;
            int voiceId;
            float lastValue;
        };
        std::unordered_map<uint64_t, ParameterRoute> parameterRoutes_;
        
        // Mutex for thread safety
        mutable std::mutex voicesMutex_;
    };

} // namespace tremor::audio