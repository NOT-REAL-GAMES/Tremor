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
        // Node processing state
        struct NodeState {
            Taffy::AudioChunk::Node node;
            std::vector<float> outputBuffer;  // Output values for this node
            float phase = 0.0f;              // For oscillators
            float lastValue = 0.0f;          // For filters, envelopes, etc.
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
        std::unordered_map<uint64_t, ParameterInfo> parameters_;

        // Processing helpers
        void processNode(NodeState& node, uint32_t frameCount);
        float getNodeInput(uint32_t nodeId, uint32_t inputIndex);
        float getParameterValue(uint64_t paramHash);

        // Node processors
        void processOscillator(NodeState& node, uint32_t frameCount);
        void processAmplifier(NodeState& node, uint32_t frameCount);
        void processParameter(NodeState& node, uint32_t frameCount);
    };

} // namespace tremor::audio