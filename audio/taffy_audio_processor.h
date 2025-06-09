#pragma once

#include <vector>
#include <unordered_map>
#include <memory>
#include <cmath>
#include "taffy.h"

namespace tremor::audio {

    /**
     * Simple audio processor for Taffy audio chunks
     * Processes audio graphs and generates output samples
     */
    class TaffyAudioProcessor {
    public:
        TaffyAudioProcessor(uint32_t sample_rate = 48000);
        ~TaffyAudioProcessor();

        /**
         * Load an audio chunk from a Taffy asset
         * @param audioData Raw audio chunk data
         * @return true if loaded successfully
         */
        bool loadAudioChunk(const std::vector<uint8_t>& audioData);
        bool loadFromData(const uint8_t* data, size_t size) {
            std::vector<uint8_t> buffer(data, data + size);
            return loadAudioChunk(buffer);
        }

        /**
         * Process audio and fill output buffer
         * @param outputBuffer Buffer to fill with audio samples
         * @param frameCount Number of frames to generate
         * @param channelCount Number of channels (1=mono, 2=stereo)
         */
        void processAudio(float* outputBuffer, uint32_t frameCount, uint32_t channelCount = 2);

        /**
         * Set a parameter value
         * @param parameterHash Hash of the parameter name
         * @param value New value
         */
        void setParameter(uint64_t parameterHash, float value);

        /**
         * Get current time in seconds
         */
        float getCurrentTime() const { return current_time_; }

    private:
        // Waveform types for oscillators
        enum class Waveform : uint32_t {
            Sine = 0,
            Square = 1,
            Saw = 2,
            Triangle = 3,
            Noise = 4
        };

        // Sample data structure
        struct SampleData {
            std::vector<float> data;      // Sample data (interleaved if stereo)
            uint32_t channelCount;        // 1 = mono, 2 = stereo
            uint32_t sampleRate;          // Original sample rate
            float baseFrequency;          // Base frequency for pitch shifting
            uint32_t loopStart;           // Loop start point (samples)
            uint32_t loopEnd;             // Loop end point (samples)
            bool hasLoop;                 // Whether sample has loop points
        };

        // Envelope phases
        enum class EnvelopePhase : uint32_t {
            Off = 0,
            Attack = 1,
            Decay = 2,
            Sustain = 3,
            Release = 4
        };

        // Filter types
        enum class FilterType : uint32_t {
            Lowpass = 0,
            Highpass = 1,
            Bandpass = 2
        };

        // Distortion types
        enum class DistortionType : uint32_t {
            HardClip = 0,
            SoftClip = 1,
            Foldback = 2,
            BitCrush = 3,
            Overdrive = 4,
            Beeper = 5      // 1-bit ZX Spectrum style
        };

        // Node processing state
        struct NodeState {
            Taffy::AudioChunk::Node node;
            std::vector<float> outputBuffer;  // Output values for this node
            float phase = 0.0f;              // For oscillators
            float lastValue = 0.0f;          // For filters, envelopes, etc.
            
            // Envelope specific state
            EnvelopePhase envPhase = EnvelopePhase::Off;
            float envTime = 0.0f;            // Time in current phase
            float envLevel = 0.0f;           // Current envelope level
            bool lastGate = false;           // Previous gate state for edge detection
            
            // Filter specific state  
            float x1 = 0.0f;                 // x[n-1] input delay
            float x2 = 0.0f;                 // x[n-2] input delay
            float y1 = 0.0f;                 // y[n-1] output delay
            float y2 = 0.0f;                 // y[n-2] output delay
            
            // Sampler specific state
            float samplePosition = 0.0f;     // Current playback position
            bool isPlaying = false;          // Is sample currently playing
            uint32_t sampleIndex = 0;        // Which sample to play
            float lastTrigger = 0.0f;        // Last trigger value for edge detection
        };

        // Connection info
        struct ConnectionInfo {
            uint32_t sourceNode;
            uint32_t sourceOutput;
            uint32_t destNode;
            uint32_t destInput;
            float strength;
        };

        // Parameter info
        struct ParameterInfo {
            Taffy::AudioChunk::Parameter param;
            float currentValue;
        };

        uint32_t sample_rate_;
        float current_time_;
        uint64_t sample_count_;

        // Loaded audio chunk data
        Taffy::AudioChunk header_;
        std::unordered_map<uint32_t, NodeState> nodes_;
        std::vector<ConnectionInfo> connections_;
        std::unordered_map<uint64_t, ParameterInfo> parameters_;  // Global parameters by hash
        std::vector<ParameterInfo> parameterList_;               // All parameters in order
        std::vector<SampleData> samples_;                         // Loaded samples

        // Processing helpers
        void processNode(NodeState& node, uint32_t frameCount);
        float getNodeInput(uint32_t nodeId, uint32_t inputIndex);
        float getParameterValue(uint64_t paramHash);
        float getNodeParameterValue(const NodeState& node, uint64_t paramHash);

        // Node processors
        void processOscillator(NodeState& node, uint32_t frameCount);
        void processAmplifier(NodeState& node, uint32_t frameCount);
        void processParameter(NodeState& node, uint32_t frameCount);
        void processMixer(NodeState& node, uint32_t frameCount);
        void processEnvelope(NodeState& node, uint32_t frameCount);
        void processFilter(NodeState& node, uint32_t frameCount);
        void processDistortion(NodeState& node, uint32_t frameCount);
        void processSampler(NodeState& node, uint32_t frameCount);
        void processStreamingSampler(NodeState& node, uint32_t frameCount);
        
    private:
        // Streaming support
        struct StreamingAudioInfo {
            std::string filePath;
            uint64_t dataOffset;
            uint32_t totalSamples;
            uint32_t chunkSize;
            uint32_t sampleRate;
            uint32_t channelCount;
            uint32_t bitDepth;
            uint32_t format;
            
            // Runtime state
            std::ifstream* fileStream = nullptr;
            std::vector<float> chunkBuffer;
            std::vector<float> nextChunkBuffer;  // Pre-loaded next chunk
            uint32_t currentChunk = 0;
            uint32_t bufferPosition = 0;
            bool nextChunkReady = false;
            bool needsPreload = true;
        };
        
        std::vector<StreamingAudioInfo> streamingAudios_;
        
        void preloadStreamingChunk(StreamingAudioInfo& stream, uint32_t chunkIndex);
        
    public:
        // Set the TAF file path for streaming audio (needed for file access)
        void setStreamingAudioFilePath(const std::string& filePath) {
            for (auto& stream : streamingAudios_) {
                stream.filePath = filePath;
                // Pre-load the first chunk to avoid hitch on first play
                if (stream.needsPreload) {
                    preloadStreamingChunk(stream, 0);
                    stream.needsPreload = false;
                }
            }
        }
    };

} // namespace tremor::audio