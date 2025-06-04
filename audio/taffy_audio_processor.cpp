#include "taffy_audio_processor.h"
#include <iostream>
#include <cstring>

namespace tremor::audio {

    TaffyAudioProcessor::TaffyAudioProcessor(uint32_t sample_rate)
        : sample_rate_(sample_rate), current_time_(0.0f), sample_count_(0) {
    }

    TaffyAudioProcessor::~TaffyAudioProcessor() {
    }

    bool TaffyAudioProcessor::loadAudioChunk(const std::vector<uint8_t>& audioData) {
        if (audioData.size() < sizeof(Taffy::AudioChunk)) {
            std::cerr << "❌ Audio chunk data too small!" << std::endl;
            return false;
        }

        // Clear existing data
        nodes_.clear();
        connections_.clear();
        parameters_.clear();

        // Read header
        const uint8_t* ptr = audioData.data();
        std::memcpy(&header_, ptr, sizeof(Taffy::AudioChunk));
        ptr += sizeof(Taffy::AudioChunk);

        std::cout << "🎵 Loading audio chunk:" << std::endl;
        std::cout << "   Nodes: " << header_.node_count << std::endl;
        std::cout << "   Connections: " << header_.connection_count << std::endl;
        std::cout << "   Parameters: " << header_.parameter_count << std::endl;
        std::cout << "   Sample rate: " << header_.sample_rate << " Hz" << std::endl;

        // Read nodes
        for (uint32_t i = 0; i < header_.node_count; ++i) {
            Taffy::AudioChunk::Node node;
            std::memcpy(&node, ptr, sizeof(node));
            ptr += sizeof(node);

            NodeState state;
            state.node = node;
            state.outputBuffer.resize(1024);  // Pre-allocate buffer
            nodes_[node.id] = state;

            std::cout << "   Node " << node.id << ": type=" << static_cast<uint32_t>(node.type) 
                      << ", inputs=" << node.input_count << ", outputs=" << node.output_count << std::endl;
        }

        // Read connections
        for (uint32_t i = 0; i < header_.connection_count; ++i) {
            Taffy::AudioChunk::Connection conn;
            std::memcpy(&conn, ptr, sizeof(conn));
            ptr += sizeof(conn);

            ConnectionInfo info;
            info.sourceNode = conn.source_node;
            info.sourceOutput = conn.source_output;
            info.destNode = conn.dest_node;
            info.destInput = conn.dest_input;
            info.strength = conn.strength;
            connections_.push_back(info);

            std::cout << "   Connection: " << conn.source_node << "[" << conn.source_output 
                      << "] -> " << conn.dest_node << "[" << conn.dest_input 
                      << "] (strength=" << conn.strength << ")" << std::endl;
        }

        // Read parameters
        for (uint32_t i = 0; i < header_.parameter_count; ++i) {
            Taffy::AudioChunk::Parameter param;
            std::memcpy(&param, ptr, sizeof(param));
            ptr += sizeof(param);

            ParameterInfo info;
            info.param = param;
            info.currentValue = param.default_value;
            parameters_[param.name_hash] = info;

            std::cout << "   Parameter " << i << ": hash=0x" << std::hex << param.name_hash << std::dec
                      << ", default=" << param.default_value 
                      << ", range=[" << param.min_value << ", " << param.max_value << "]" << std::endl;
        }

        std::cout << "✅ Audio chunk loaded successfully!" << std::endl;
        return true;
    }

    void TaffyAudioProcessor::processAudio(float* outputBuffer, uint32_t frameCount, uint32_t channelCount) {
        // Clear output buffer
        std::memset(outputBuffer, 0, frameCount * channelCount * sizeof(float));

        // Process all nodes in order (simple topological sort would be better)
        for (auto& [nodeId, nodeState] : nodes_) {
            processNode(nodeState, frameCount);
        }

        // Find the output from the amplifier (node 1 in our test case)
        auto ampIt = nodes_.find(1);
        if (ampIt != nodes_.end()) {
            // Copy amplifier output to the audio buffer
            for (uint32_t i = 0; i < frameCount; ++i) {
                float sample = ampIt->second.outputBuffer[i];
                
                // Write to all channels
                for (uint32_t ch = 0; ch < channelCount; ++ch) {
                    outputBuffer[i * channelCount + ch] = sample;
                }
            }
        }

        // Update time
        current_time_ += static_cast<float>(frameCount) / static_cast<float>(sample_rate_);
        sample_count_ += frameCount;

        // Update time parameter if it exists
        auto timeHash = Taffy::fnv1a_hash("time");
        auto timeIt = parameters_.find(timeHash);
        if (timeIt != parameters_.end()) {
            timeIt->second.currentValue = current_time_;
        }
    }

    void TaffyAudioProcessor::setParameter(uint64_t parameterHash, float value) {
        auto it = parameters_.find(parameterHash);
        if (it != parameters_.end()) {
            // Clamp to valid range
            value = std::max(it->second.param.min_value, 
                            std::min(it->second.param.max_value, value));
            it->second.currentValue = value;
        }
    }

    void TaffyAudioProcessor::processNode(NodeState& node, uint32_t frameCount) {
        switch (node.node.type) {
            case Taffy::AudioChunk::NodeType::Oscillator:
                processOscillator(node, frameCount);
                break;
            case Taffy::AudioChunk::NodeType::Amplifier:
                processAmplifier(node, frameCount);
                break;
            case Taffy::AudioChunk::NodeType::Parameter:
                processParameter(node, frameCount);
                break;
            default:
                // Clear output for unsupported nodes
                std::memset(node.outputBuffer.data(), 0, frameCount * sizeof(float));
                break;
        }
    }

    float TaffyAudioProcessor::getNodeInput(uint32_t nodeId, uint32_t inputIndex) {
        // Find connection to this input
        for (const auto& conn : connections_) {
            if (conn.destNode == nodeId && conn.destInput == inputIndex && conn.strength > 0.0f) {
                auto srcIt = nodes_.find(conn.sourceNode);
                if (srcIt != nodes_.end() && conn.sourceOutput == 0) {
                    // Return the last value from the source node's output
                    return srcIt->second.outputBuffer[0] * conn.strength;
                }
            }
        }
        return 0.0f;
    }

    float TaffyAudioProcessor::getParameterValue(uint64_t paramHash) {
        auto it = parameters_.find(paramHash);
        if (it != parameters_.end()) {
            return it->second.currentValue;
        }
        return 0.0f;
    }

    void TaffyAudioProcessor::processOscillator(NodeState& node, uint32_t frameCount) {
        // Get frequency from parameter
        uint64_t freqHash = Taffy::fnv1a_hash("frequency");
        float frequency = getParameterValue(freqHash);

        // Check for frequency modulation input
        float freqMod = getNodeInput(node.node.id, 0);
        frequency += freqMod;

        // Generate sine wave
        float phaseIncrement = 2.0f * M_PI * frequency / static_cast<float>(sample_rate_);
        
        for (uint32_t i = 0; i < frameCount; ++i) {
            node.outputBuffer[i] = std::sin(node.phase);
            node.phase += phaseIncrement;
            
            // Wrap phase
            if (node.phase > 2.0f * M_PI) {
                node.phase -= 2.0f * M_PI;
            }
        }
    }

    void TaffyAudioProcessor::processAmplifier(NodeState& node, uint32_t frameCount) {
        // Get amplitude from parameter
        uint64_t ampHash = Taffy::fnv1a_hash("amplitude");
        float amplitude = getParameterValue(ampHash);

        // Process each sample
        for (uint32_t i = 0; i < frameCount; ++i) {
            // Get input from connected nodes
            float input = 0.0f;
            
            // Sum all inputs (for now, just get from first connection)
            for (const auto& conn : connections_) {
                if (conn.destNode == node.node.id && conn.destInput == 0) {
                    auto srcIt = nodes_.find(conn.sourceNode);
                    if (srcIt != nodes_.end()) {
                        input += srcIt->second.outputBuffer[i] * conn.strength;
                    }
                }
            }
            
            // Apply amplification
            node.outputBuffer[i] = input * amplitude;
        }
    }

    void TaffyAudioProcessor::processParameter(NodeState& node, uint32_t frameCount) {
        // Parameters output their current value
        if (node.node.param_count > 0) {
            // Get the parameter this node represents
            uint64_t paramHash = 0;
            uint32_t paramIndex = node.node.param_offset;
            
            // Find the parameter by index (simplified for now)
            uint32_t idx = 0;
            for (const auto& [hash, param] : parameters_) {
                if (idx == paramIndex) {
                    paramHash = hash;
                    break;
                }
                idx++;
            }
            
            float value = getParameterValue(paramHash);
            std::fill(node.outputBuffer.begin(), node.outputBuffer.begin() + frameCount, value);
        }
    }

} // namespace tremor::audio