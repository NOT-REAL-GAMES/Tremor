#include "taffy_audio_processor.h"
#include <iostream>
#include <cstring>
#include <random>
#include <cmath>
#include <chrono>
#include <unordered_set>
#include <functional>
#include <algorithm>

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
        parameterList_.clear();
        samples_.clear();
        
        // Reset time when loading new audio chunk
        current_time_ = 0.0f;
        sample_count_ = 0;

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
                      << ", inputs=" << node.input_count << ", outputs=" << node.output_count 
                      << ", param_offset=" << node.param_offset << ", param_count=" << node.param_count << std::endl;
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
            
            // Store in list (preserves order and duplicates)
            parameterList_.push_back(info);
            
            // Also store by hash for global parameters (last one wins)
            parameters_[param.name_hash] = info;
            
            // Debug parameter loading
            if (param.name_hash == Taffy::fnv1a_hash("pitch")) {
                std::cout << "   📎 Loaded pitch parameter: default=" << param.default_value 
                          << ", current=" << info.currentValue << std::endl;
            }

            std::cout << "   Parameter " << i << ": hash=0x" << std::hex << param.name_hash << std::dec
                      << ", default=" << param.default_value 
                      << ", range=[" << param.min_value << ", " << param.max_value << "]" << std::endl;
        }

        // Read wavetable samples
        for (uint32_t i = 0; i < header_.sample_count; ++i) {
            Taffy::AudioChunk::WaveTable wavetable;
            std::memcpy(&wavetable, ptr, sizeof(wavetable));
            ptr += sizeof(wavetable);

            SampleData sample;
            sample.channelCount = wavetable.channel_count;
            sample.sampleRate = header_.sample_rate;  // Use chunk sample rate
            
            // Ensure we have a valid sample rate
            if (sample.sampleRate == 0) {
                std::cout << "⚠️ WARNING: Sample rate is 0, defaulting to 48000 Hz" << std::endl;
                sample.sampleRate = 48000;
            }
            
            sample.baseFrequency = wavetable.base_frequency;
            sample.loopStart = wavetable.loop_start;
            sample.loopEnd = wavetable.loop_end;
            sample.hasLoop = (wavetable.loop_end > wavetable.loop_start);

            // Calculate number of samples from data size and bit depth
            uint32_t bytesPerSample = wavetable.bit_depth / 8;
            uint32_t totalSamples = wavetable.data_size / (bytesPerSample * wavetable.channel_count);
            sample.data.resize(totalSamples * wavetable.channel_count);

            // Read and convert sample data based on bit depth
            // The data_offset is relative to the start of the chunk data
            const uint8_t* chunkStart = audioData.data();
            const uint8_t* samplePtr = chunkStart + wavetable.data_offset;
            
            std::cout << "   Loading sample data: offset=" << wavetable.data_offset 
                      << ", size=" << wavetable.data_size 
                      << ", total chunk size=" << audioData.size() << std::endl;
            
            // Bounds check
            if (wavetable.data_offset + wavetable.data_size > audioData.size()) {
                std::cerr << "❌ Sample data extends beyond chunk bounds!" << std::endl;
                continue;
            }
            
            for (uint32_t j = 0; j < totalSamples * wavetable.channel_count; ++j) {
                switch (wavetable.bit_depth) {
                    case 8: {
                        uint8_t val = *samplePtr++;
                        sample.data[j] = (static_cast<float>(val) - 128.0f) / 128.0f;
                        break;
                    }
                    case 16: {
                        int16_t val;
                        std::memcpy(&val, samplePtr, 2);
                        samplePtr += 2;
                        sample.data[j] = static_cast<float>(val) / 32768.0f;
                        break;
                    }
                    case 24: {
                        int32_t val = 0;
                        std::memcpy(&val, samplePtr, 3);
                        samplePtr += 3;
                        if (val & 0x800000) val |= 0xFF000000;  // Sign extend
                        sample.data[j] = static_cast<float>(val) / 8388608.0f;
                        break;
                    }
                    case 32: {
                        float val;
                        std::memcpy(&val, samplePtr, 4);
                        samplePtr += 4;
                        sample.data[j] = val;
                        break;
                    }
                }
            }

            samples_.push_back(sample);
            
            std::cout << "   Sample " << i << ": hash=0x" << std::hex << wavetable.name_hash << std::dec
                      << ", " << totalSamples << " samples, " << wavetable.channel_count << " channels, "
                      << wavetable.bit_depth << "-bit" << std::endl;
            std::cout << "   Sample rate: " << sample.sampleRate << " Hz" << std::endl;
            std::cout << "   Base frequency: " << sample.baseFrequency << " Hz" << std::endl;
            std::cout << "   Has loop: " << (sample.hasLoop ? "yes" : "no") << std::endl;
            
            // Debug: Show first few samples and raw bytes
            if (sample.data.size() > 0) {
                std::cout << "   First 10 samples: ";
                for (size_t j = 0; j < std::min(size_t(10), sample.data.size()); ++j) {
                    std::cout << sample.data[j] << " ";
                }
                std::cout << std::endl;
                
                // Also show raw bytes for debugging
                std::cout << "   First 20 raw bytes: ";
                const uint8_t* rawPtr = chunkStart + wavetable.data_offset;
                for (size_t j = 0; j < std::min(size_t(20), size_t(wavetable.data_size)); ++j) {
                    std::cout << std::hex << static_cast<int>(rawPtr[j]) << " ";
                }
                std::cout << std::dec << std::endl;
            }
        }

        std::cout << "✅ Audio chunk loaded successfully!" << std::endl;
        return true;
    }

    void TaffyAudioProcessor::processAudio(float* outputBuffer, uint32_t frameCount, uint32_t channelCount) {
        // Clear output buffer
        std::memset(outputBuffer, 0, frameCount * channelCount * sizeof(float));

        // Debug: Check output buffer before processing
        static int preProcessDebug = 0;
        if (preProcessDebug < 3) {
            std::cout << "🔍 processAudio: frameCount=" << frameCount << ", channelCount=" << channelCount << std::endl;
            std::cout << "   Output buffer cleared, processing " << nodes_.size() << " nodes" << std::endl;
            preProcessDebug++;
        }

        // Process all nodes in dependency order using a simple topological sort
        // Build a list of nodes sorted by dependencies
        std::vector<uint32_t> nodeOrder;
        std::unordered_set<uint32_t> processed;
        std::function<void(uint32_t)> processNodeDependencies = [&](uint32_t nodeId) {
            if (processed.count(nodeId)) return;
            
            // First process all dependencies (nodes that feed into this one)
            for (const auto& conn : connections_) {
                if (conn.destNode == nodeId && processed.count(conn.sourceNode) == 0) {
                    processNodeDependencies(conn.sourceNode);
                }
            }
            
            // Then add this node
            nodeOrder.push_back(nodeId);
            processed.insert(nodeId);
        };
        
        // Process all nodes
        for (const auto& [nodeId, nodeState] : nodes_) {
            processNodeDependencies(nodeId);
        }
        
        // Now process nodes in the correct order
        for (uint32_t nodeId : nodeOrder) {
            auto it = nodes_.find(nodeId);
            if (it != nodes_.end()) {
                processNode(it->second, frameCount);
            }
        }

        // Find the final output node (typically the last amplifier)
        // Look for an amplifier with no outgoing connections
        uint32_t outputNodeId = 1; // Default for simple assets
        
        // Debug: List all nodes and their types
        static int debugCount = 0;
        if (debugCount < 3) {
            std::cout << "🔍 Looking for output node among " << nodes_.size() << " nodes:" << std::endl;
            for (const auto& [nodeId, nodeState] : nodes_) {
                std::cout << "   Node " << nodeId << ": type=" << static_cast<int>(nodeState.node.type) 
                          << " (" << (nodeState.node.type == Taffy::AudioChunk::NodeType::Amplifier ? "Amplifier" : 
                                     nodeState.node.type == Taffy::AudioChunk::NodeType::Sampler ? "Sampler" :
                                     nodeState.node.type == Taffy::AudioChunk::NodeType::Parameter ? "Parameter" : "Other")
                          << ")" << std::endl;
            }
            debugCount++;
        }
        
        // Find the output node - it's typically an amplifier with no outgoing connections
        for (const auto& [nodeId, nodeState] : nodes_) {
            if (nodeState.node.type == Taffy::AudioChunk::NodeType::Amplifier) {
                bool hasOutgoingConnections = false;
                for (const auto& conn : connections_) {
                    if (conn.sourceNode == nodeId) {
                        hasOutgoingConnections = true;
                        break;
                    }
                }
                if (!hasOutgoingConnections) {
                    outputNodeId = nodeId;
                    if (debugCount <= 3) {
                        std::cout << "   ✅ Found output amplifier: Node " << nodeId << std::endl;
                    }
                    break;
                }
            }
        }
        
        auto outputIt = nodes_.find(outputNodeId);
        if (outputIt != nodes_.end()) {
            // Debug: Check if amplifier has any output
            static int ampDebugCount = 0;
            if (ampDebugCount < 5) {
                float maxAmp = 0.0f;
                for (uint32_t i = 0; i < frameCount; ++i) {
                    maxAmp = std::max(maxAmp, std::abs(outputIt->second.outputBuffer[i]));
                }
                if (maxAmp > 0.0f) {
                    std::cout << "🔊 Amplifier output: max amplitude = " << maxAmp << std::endl;
                    ampDebugCount++;
                }
            }
            
            // Copy output to the audio buffer
            static int copyDebug = 0;
            float maxCopied = 0.0f;
            
            for (uint32_t i = 0; i < frameCount; ++i) {
                float sample = outputIt->second.outputBuffer[i];
                maxCopied = std::max(maxCopied, std::abs(sample));
                
                // Write to all channels
                for (uint32_t ch = 0; ch < channelCount; ++ch) {
                    outputBuffer[i * channelCount + ch] = sample;
                }
            }
            
            if (copyDebug < 5 && maxCopied > 0.0f) {
                std::cout << "📻 Copied " << frameCount << " frames to output buffer, max amplitude: " << maxCopied << std::endl;
                // Also check what's actually in the output buffer
                float maxInOutput = 0.0f;
                for (uint32_t i = 0; i < frameCount * channelCount; ++i) {
                    maxInOutput = std::max(maxInOutput, std::abs(outputBuffer[i]));
                }
                std::cout << "   Max amplitude in final output buffer: " << maxInOutput << std::endl;
                copyDebug++;
            }
        } else {
            std::cout << "❌ Output node " << outputNodeId << " not found!" << std::endl;
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
            case Taffy::AudioChunk::NodeType::Mixer:
                processMixer(node, frameCount);
                break;
            case Taffy::AudioChunk::NodeType::Envelope:
                processEnvelope(node, frameCount);
                break;
            case Taffy::AudioChunk::NodeType::Filter:
                processFilter(node, frameCount);
                break;
            case Taffy::AudioChunk::NodeType::Distortion:
                processDistortion(node, frameCount);
                break;
            case Taffy::AudioChunk::NodeType::Sampler:
                static bool samplerLogged = false;
                if (!samplerLogged) {
                    std::cout << "🎵 SAMPLER NODE FOUND AND PROCESSING!" << std::endl;
                    samplerLogged = true;
                }
                processSampler(node, frameCount);
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
        // Get parameters for this specific node from the parameter list
        float frequency = 440.0f;  // Default
        float waveformValue = 0.0f;  // Default to sine
        
        // Get parameters based on node's param_offset
        if (node.node.param_count > 0 && node.node.param_offset < parameterList_.size()) {
            for (uint32_t i = 0; i < node.node.param_count; ++i) {
                uint32_t paramIdx = node.node.param_offset + i;
                if (paramIdx < parameterList_.size()) {
                    const auto& param = parameterList_[paramIdx];
                    
                    if (param.param.name_hash == Taffy::fnv1a_hash("frequency")) {
                        frequency = param.currentValue;
                    } else if (param.param.name_hash == Taffy::fnv1a_hash("waveform")) {
                        waveformValue = param.currentValue;
                    }
                }
            }
        }
        
        Waveform waveform = static_cast<Waveform>(static_cast<uint32_t>(waveformValue));

        // Check for frequency modulation input
        float freqMod = getNodeInput(node.node.id, 0);
        frequency += freqMod;

        // Generate waveform based on type
        float phaseIncrement = 2.0f * M_PI * frequency / static_cast<float>(sample_rate_);
        
        // Static random generator for noise
        static std::mt19937 rng(std::chrono::steady_clock::now().time_since_epoch().count());
        static std::uniform_real_distribution<float> noiseDist(-1.0f, 1.0f);
        
        for (uint32_t i = 0; i < frameCount; ++i) {
            switch (waveform) {
                case Waveform::Sine:
                    node.outputBuffer[i] = std::sin(node.phase);
                    break;
                    
                case Waveform::Square:
                    node.outputBuffer[i] = (node.phase < M_PI) ? 1.0f : -1.0f;
                    break;
                    
                case Waveform::Saw:
                    // Sawtooth: ramps from -1 to 1 over the period
                    node.outputBuffer[i] = 2.0f * (node.phase / (2.0f * M_PI)) - 1.0f;
                    break;
                    
                case Waveform::Triangle:
                    // Triangle: ramps up then down
                    if (node.phase < M_PI) {
                        node.outputBuffer[i] = -1.0f + 2.0f * (node.phase / M_PI);
                    } else {
                        node.outputBuffer[i] = 3.0f - 2.0f * (node.phase / M_PI);
                    }
                    break;
                    
                case Waveform::Noise:
                    // White noise
                    node.outputBuffer[i] = noiseDist(rng);
                    break;
                    
                default:
                    // Default to sine
                    node.outputBuffer[i] = std::sin(node.phase);
                    break;
            }
            
            node.phase += phaseIncrement;
            
            // Wrap phase
            if (node.phase > 2.0f * M_PI) {
                node.phase -= 2.0f * M_PI;
            }
        }
    }

    void TaffyAudioProcessor::processAmplifier(NodeState& node, uint32_t frameCount) {
        // Get amplitude from parameter
        float amplitude = 1.0f; // Default
        
        // Get parameter based on node's param_offset
        if (node.node.param_count > 0 && node.node.param_offset < parameterList_.size()) {
            for (uint32_t i = 0; i < node.node.param_count; ++i) {
                uint32_t paramIdx = node.node.param_offset + i;
                if (paramIdx < parameterList_.size()) {
                    const auto& param = parameterList_[paramIdx];
                    if (param.param.name_hash == Taffy::fnv1a_hash("amplitude")) {
                        amplitude = param.currentValue;
                    }
                }
            }
        }
        
        // Debug
        static int ampProcessDebug = 0;
        if (ampProcessDebug < 3) {
            std::cout << "🎚️ Amplifier " << node.node.id << " processing: amplitude=" << amplitude << std::endl;
            std::cout << "   Parameters at offset " << node.node.param_offset << ":" << std::endl;
            for (uint32_t i = 0; i < node.node.param_count; ++i) {
                uint32_t paramIdx = node.node.param_offset + i;
                if (paramIdx < parameterList_.size()) {
                    const auto& param = parameterList_[paramIdx];
                    std::cout << "     Param " << paramIdx << ": value=" << param.currentValue 
                              << " (hash=0x" << std::hex << param.param.name_hash << std::dec << ")" << std::endl;
                }
            }
            ampProcessDebug++;
        }

        // Process each sample
        for (uint32_t i = 0; i < frameCount; ++i) {
            // Get audio input (input 0)
            float audioInput = 0.0f;
            for (const auto& conn : connections_) {
                if (conn.destNode == node.node.id && conn.destInput == 0) {
                    auto srcIt = nodes_.find(conn.sourceNode);
                    if (srcIt != nodes_.end()) {
                        audioInput += srcIt->second.outputBuffer[i] * conn.strength;
                    }
                }
            }
            
            // Get modulation input (input 1) if available
            float modulation = 1.0f;
            for (const auto& conn : connections_) {
                if (conn.destNode == node.node.id && conn.destInput == 1) {
                    auto srcIt = nodes_.find(conn.sourceNode);
                    if (srcIt != nodes_.end()) {
                        modulation = srcIt->second.outputBuffer[i] * conn.strength;
                        break; // Only use first modulation input
                    }
                }
            }
            
            // Apply amplification with modulation
            node.outputBuffer[i] = audioInput * amplitude * modulation;
        }
        
        // Debug: Check if amplifier received any input
        if (ampProcessDebug <= 3) {
            float maxInput = 0.0f;
            for (uint32_t i = 0; i < frameCount; ++i) {
                // Check input again for max
                float audioInput = 0.0f;
                for (const auto& conn : connections_) {
                    if (conn.destNode == node.node.id && conn.destInput == 0) {
                        auto srcIt = nodes_.find(conn.sourceNode);
                        if (srcIt != nodes_.end()) {
                            audioInput += srcIt->second.outputBuffer[i] * conn.strength;
                        }
                    }
                }
                maxInput = std::max(maxInput, std::abs(audioInput));
            }
            std::cout << "   Max input to amplifier: " << maxInput << std::endl;
            std::cout << "   Max output from amplifier: " << std::max(maxInput * amplitude, 0.0f) << std::endl;
        }
    }

    void TaffyAudioProcessor::processParameter(NodeState& node, uint32_t frameCount) {
        // Parameters output their current value
        if (node.node.param_count > 0 && node.node.param_offset < parameterList_.size()) {
            // Get the parameter value
            float value = 0.0f;
            uint64_t paramHash = 0;
            
            if (node.node.param_offset < parameterList_.size()) {
                const auto& param = parameterList_[node.node.param_offset];
                value = param.currentValue;
                paramHash = param.param.name_hash;
            }
            
            // Special handling for gate parameter - single trigger
            if (paramHash == Taffy::fnv1a_hash("gate")) {
                float sampleTime = 1.0f / static_cast<float>(sample_rate_);
                for (uint32_t i = 0; i < frameCount; ++i) {
                    float currentSampleTime = current_time_ + (i * sampleTime);
                    // Gate triggers once at the beginning for 0.1 seconds, then stays off
                    node.outputBuffer[i] = (currentSampleTime < 0.1f) ? 1.0f : 0.0f;
                }
            } else {
                // Normal parameter - constant value
                std::fill(node.outputBuffer.begin(), node.outputBuffer.begin() + frameCount, value);
            }
        }
    }

    void TaffyAudioProcessor::processMixer(NodeState& node, uint32_t frameCount) {
        // Mixer combines multiple inputs with individual gain controls
        // The mixer can have any number of inputs (typically 2-8)
        
        // Clear output buffer first
        std::memset(node.outputBuffer.data(), 0, frameCount * sizeof(float));
        
        // Get gain parameters for this mixer from parameter list
        std::vector<float> inputGains(node.node.input_count, 1.0f); // Default to unity
        float masterGain = 1.0f;
        
        // Get parameters based on node's param_offset
        if (node.node.param_count > 0 && node.node.param_offset < parameterList_.size()) {
            for (uint32_t i = 0; i < node.node.param_count; ++i) {
                uint32_t paramIdx = node.node.param_offset + i;
                if (paramIdx < parameterList_.size()) {
                    const auto& param = parameterList_[paramIdx];
                    
                    // Check for gain_0, gain_1, etc.
                    for (uint32_t input = 0; input < node.node.input_count; ++input) {
                        std::string gainParamName = "gain_" + std::to_string(input);
                        if (param.param.name_hash == Taffy::fnv1a_hash(gainParamName.c_str())) {
                            inputGains[input] = param.currentValue;
                        }
                    }
                    
                    // Check for master gain
                    if (param.param.name_hash == Taffy::fnv1a_hash("master_gain")) {
                        masterGain = param.currentValue;
                    }
                }
            }
        }
        
        // Process each frame
        for (uint32_t frame = 0; frame < frameCount; ++frame) {
            float mixedSample = 0.0f;
            
            // Sum all inputs with their gains
            for (uint32_t input = 0; input < node.node.input_count; ++input) {
                // Find and sum input connections
                for (const auto& conn : connections_) {
                    if (conn.destNode == node.node.id && conn.destInput == input) {
                        auto srcIt = nodes_.find(conn.sourceNode);
                        if (srcIt != nodes_.end()) {
                            // Use pre-calculated input gain
                            mixedSample += srcIt->second.outputBuffer[frame] * conn.strength * inputGains[input];
                        }
                    }
                }
            }
            
            // Apply master gain and store result
            node.outputBuffer[frame] = mixedSample * masterGain;
        }
    }

    void TaffyAudioProcessor::processEnvelope(NodeState& node, uint32_t frameCount) {
        // ADSR Envelope generator
        // Parameters: attack, decay, sustain, release
        // Input 0: Gate signal (0 or 1)
        
        // Get ADSR parameters from parameter list
        float attack = 0.01f;   // Default 10ms
        float decay = 0.1f;     // Default 100ms
        float sustain = 0.7f;   // Default 70%
        float release = 0.2f;   // Default 200ms
        
        // Get parameters based on node's param_offset
        if (node.node.param_count > 0 && node.node.param_offset < parameterList_.size()) {
            for (uint32_t i = 0; i < node.node.param_count; ++i) {
                uint32_t paramIdx = node.node.param_offset + i;
                if (paramIdx < parameterList_.size()) {
                    const auto& param = parameterList_[paramIdx];
                    
                    if (param.param.name_hash == Taffy::fnv1a_hash("attack")) {
                        attack = param.currentValue;
                    } else if (param.param.name_hash == Taffy::fnv1a_hash("decay")) {
                        decay = param.currentValue;
                    } else if (param.param.name_hash == Taffy::fnv1a_hash("sustain")) {
                        sustain = param.currentValue;
                    } else if (param.param.name_hash == Taffy::fnv1a_hash("release")) {
                        release = param.currentValue;
                    }
                }
            }
        }
        
        // Process each sample
        float sampleTime = 1.0f / static_cast<float>(sample_rate_);
        
        for (uint32_t i = 0; i < frameCount; ++i) {
            // Get gate input (input 0)
            float gate = 0.0f;
            for (const auto& conn : connections_) {
                if (conn.destNode == node.node.id && conn.destInput == 0) {
                    auto srcIt = nodes_.find(conn.sourceNode);
                    if (srcIt != nodes_.end()) {
                        gate = srcIt->second.outputBuffer[i] * conn.strength;
                        break;
                    }
                }
            }
            
            // Detect gate edges
            bool gateOn = gate > 0.5f;
            bool triggerAttack = gateOn && !node.lastGate;
            bool triggerRelease = !gateOn && node.lastGate;
            node.lastGate = gateOn;
            
            // State machine for envelope phases
            if (triggerAttack) {
                // Start attack phase
                node.envPhase = EnvelopePhase::Attack;
                node.envTime = 0.0f;
            } else if (triggerRelease && node.envPhase != EnvelopePhase::Off) {
                // Start release phase
                node.envPhase = EnvelopePhase::Release;
                node.envTime = 0.0f;
                node.lastValue = node.envLevel;  // Save current level for release
                std::cout << "🎹 ADSR: Starting release from level " << node.envLevel 
                         << " with release time " << release << "s" << std::endl;
            }
            
            // Calculate envelope level based on current phase
            switch (node.envPhase) {
                case EnvelopePhase::Attack:
                    if (attack > 0.0f) {
                        node.envLevel = node.envTime / attack;
                        if (node.envLevel >= 1.0f) {
                            node.envLevel = 1.0f;
                            node.envPhase = EnvelopePhase::Decay;
                            node.envTime = 0.0f;
                        }
                    } else {
                        // Instant attack
                        node.envLevel = 1.0f;
                        node.envPhase = EnvelopePhase::Decay;
                        node.envTime = 0.0f;
                    }
                    break;
                    
                case EnvelopePhase::Decay:
                    if (decay > 0.0f) {
                        float decayProgress = node.envTime / decay;
                        node.envLevel = 1.0f - (1.0f - sustain) * decayProgress;
                        if (decayProgress >= 1.0f) {
                            node.envLevel = sustain;
                            node.envPhase = EnvelopePhase::Sustain;
                            node.envTime = 0.0f;
                        }
                    } else {
                        // Instant decay
                        node.envLevel = sustain;
                        node.envPhase = EnvelopePhase::Sustain;
                        node.envTime = 0.0f;
                    }
                    break;
                    
                case EnvelopePhase::Sustain:
                    // Hold at sustain level
                    node.envLevel = sustain;
                    break;
                    
                case EnvelopePhase::Release:
                    if (release > 0.0f) {
                        float releaseStart = node.lastValue;
                        float releaseProgress = node.envTime / release;
                        node.envLevel = releaseStart * (1.0f - releaseProgress);
                        
                        // Debug output for first few samples of release
                        static int releaseDebugCount = 0;
                        if (releaseDebugCount < 5) {
                            std::cout << "Release: start=" << releaseStart 
                                     << " progress=" << releaseProgress 
                                     << " level=" << node.envLevel << std::endl;
                            releaseDebugCount++;
                        }
                        
                        if (releaseProgress >= 1.0f) {
                            node.envLevel = 0.0f;
                            node.envPhase = EnvelopePhase::Off;
                            node.envTime = 0.0f;
                            releaseDebugCount = 0; // Reset debug counter
                        }
                    } else {
                        // Instant release
                        node.envLevel = 0.0f;
                        node.envPhase = EnvelopePhase::Off;
                        node.envTime = 0.0f;
                    }
                    break;
                    
                case EnvelopePhase::Off:
                default:
                    node.envLevel = 0.0f;
                    break;
            }
            
            // Update time in current phase
            if (node.envPhase != EnvelopePhase::Off) {
                node.envTime += sampleTime;
            }
            
            // Output the envelope level
            node.outputBuffer[i] = node.envLevel;
            
            // Only update lastValue when NOT in release phase
            // (we need to preserve the release start level)
            if (node.envPhase != EnvelopePhase::Release) {
                node.lastValue = node.envLevel;
            }
        }
    }

    void TaffyAudioProcessor::processFilter(NodeState& node, uint32_t frameCount) {
        // Biquad filter implementation supporting lowpass, highpass, and bandpass
        // Parameters: cutoff, resonance, type
        // Input 0: Audio signal
        // Input 1: Cutoff modulation (optional)
        
        // Get filter parameters from parameter list
        float cutoff = 1000.0f;     // Default 1kHz
        float resonance = 0.707f;   // Default Q (no resonance peak)
        float filterType = 0.0f;    // Default to lowpass
        
        // Get parameters based on node's param_offset
        if (node.node.param_count > 0 && node.node.param_offset < parameterList_.size()) {
            for (uint32_t i = 0; i < node.node.param_count; ++i) {
                uint32_t paramIdx = node.node.param_offset + i;
                if (paramIdx < parameterList_.size()) {
                    const auto& param = parameterList_[paramIdx];
                    
                    if (param.param.name_hash == Taffy::fnv1a_hash("cutoff")) {
                        cutoff = param.currentValue;
                    } else if (param.param.name_hash == Taffy::fnv1a_hash("resonance")) {
                        resonance = param.currentValue;
                    } else if (param.param.name_hash == Taffy::fnv1a_hash("type")) {
                        filterType = param.currentValue;
                    }
                }
            }
        }
        
        FilterType type = static_cast<FilterType>(static_cast<uint32_t>(filterType));
        
        // Calculate filter coefficients (Robert Bristow-Johnson's cookbook formulas)
        float omega = 2.0f * M_PI * cutoff / static_cast<float>(sample_rate_);
        float sin_omega = std::sin(omega);
        float cos_omega = std::cos(omega);
        float alpha = sin_omega / (2.0f * resonance);
        
        float a0, a1, a2, b0, b1, b2;
        
        switch (type) {
            case FilterType::Lowpass:
                b0 = (1.0f - cos_omega) / 2.0f;
                b1 = 1.0f - cos_omega;
                b2 = (1.0f - cos_omega) / 2.0f;
                a0 = 1.0f + alpha;
                a1 = -2.0f * cos_omega;
                a2 = 1.0f - alpha;
                break;
                
            case FilterType::Highpass:
                b0 = (1.0f + cos_omega) / 2.0f;
                b1 = -(1.0f + cos_omega);
                b2 = (1.0f + cos_omega) / 2.0f;
                a0 = 1.0f + alpha;
                a1 = -2.0f * cos_omega;
                a2 = 1.0f - alpha;
                break;
                
            case FilterType::Bandpass:
                b0 = sin_omega / 2.0f;  // Or alpha for constant peak gain
                b1 = 0.0f;
                b2 = -sin_omega / 2.0f; // Or -alpha
                a0 = 1.0f + alpha;
                a1 = -2.0f * cos_omega;
                a2 = 1.0f - alpha;
                break;
                
            default:
                // Passthrough (no filtering)
                b0 = 1.0f;
                b1 = 0.0f;
                b2 = 0.0f;
                a0 = 1.0f;
                a1 = 0.0f;
                a2 = 0.0f;
                break;
        }
        
        // Normalize coefficients
        b0 /= a0;
        b1 /= a0;
        b2 /= a0;
        a1 /= a0;
        a2 /= a0;
        
        // Process each sample using the biquad difference equation
        for (uint32_t i = 0; i < frameCount; ++i) {
            // Get audio input (input 0)
            float input = 0.0f;
            for (const auto& conn : connections_) {
                if (conn.destNode == node.node.id && conn.destInput == 0) {
                    auto srcIt = nodes_.find(conn.sourceNode);
                    if (srcIt != nodes_.end()) {
                        input += srcIt->second.outputBuffer[i] * conn.strength;
                    }
                }
            }
            
            // Get cutoff modulation (input 1) if available
            float cutoffMod = 0.0f;
            for (const auto& conn : connections_) {
                if (conn.destNode == node.node.id && conn.destInput == 1) {
                    auto srcIt = nodes_.find(conn.sourceNode);
                    if (srcIt != nodes_.end()) {
                        cutoffMod = srcIt->second.outputBuffer[i] * conn.strength;
                        break;
                    }
                }
            }
            
            // Apply cutoff modulation if present
            if (cutoffMod != 0.0f) {
                // Recalculate coefficients with modulated cutoff
                float modCutoff = cutoff + cutoffMod;
                modCutoff = std::max(20.0f, std::min(20000.0f, modCutoff)); // Clamp to audio range
                
                float modOmega = 2.0f * M_PI * modCutoff / static_cast<float>(sample_rate_);
                float modSinOmega = std::sin(modOmega);
                float modCosOmega = std::cos(modOmega);
                float modAlpha = modSinOmega / (2.0f * resonance);
                
                // Recalculate coefficients based on filter type
                switch (type) {
                    case FilterType::Lowpass:
                        b0 = (1.0f - modCosOmega) / 2.0f;
                        b1 = 1.0f - modCosOmega;
                        b2 = (1.0f - modCosOmega) / 2.0f;
                        a0 = 1.0f + modAlpha;
                        a1 = -2.0f * modCosOmega;
                        a2 = 1.0f - modAlpha;
                        break;
                        
                    case FilterType::Highpass:
                        b0 = (1.0f + modCosOmega) / 2.0f;
                        b1 = -(1.0f + modCosOmega);
                        b2 = (1.0f + modCosOmega) / 2.0f;
                        a0 = 1.0f + modAlpha;
                        a1 = -2.0f * modCosOmega;
                        a2 = 1.0f - modAlpha;
                        break;
                        
                    case FilterType::Bandpass:
                        b0 = modSinOmega / 2.0f;
                        b1 = 0.0f;
                        b2 = -modSinOmega / 2.0f;
                        a0 = 1.0f + modAlpha;
                        a1 = -2.0f * modCosOmega;
                        a2 = 1.0f - modAlpha;
                        break;
                }
                
                // Re-normalize
                b0 /= a0;
                b1 /= a0;
                b2 /= a0;
                a1 /= a0;
                a2 /= a0;
            }
            
            // Apply biquad filter equation: y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2] - a1*y[n-1] - a2*y[n-2]
            float output = b0 * input + b1 * node.x1 + b2 * node.x2 - a1 * node.y1 - a2 * node.y2;
            
            // Update delay elements
            node.x2 = node.x1;     // x[n-2] = x[n-1]
            node.x1 = input;       // x[n-1] = x[n]
            node.y2 = node.y1;     // y[n-2] = y[n-1]
            node.y1 = output;      // y[n-1] = y[n]
            
            // Store output
            node.outputBuffer[i] = output;
        }
    }

    void TaffyAudioProcessor::processDistortion(NodeState& node, uint32_t frameCount) {
        // Distortion effects processor
        // Parameters: drive, mix, type
        // Input 0: Audio signal
        
        // Get distortion parameters from parameter list
        float drive = 1.0f;      // Default unity gain
        float mix = 1.0f;        // Default 100% wet
        float distType = 0.0f;   // Default to hard clip
        
        // Get parameters based on node's param_offset
        std::cout << "🎸 Checking distortion node params: offset=" << node.node.param_offset 
                  << ", count=" << node.node.param_count << std::endl;
        if (node.node.param_count > 0 && node.node.param_offset < parameterList_.size()) {
            for (uint32_t i = 0; i < node.node.param_count; ++i) {
                uint32_t paramIdx = node.node.param_offset + i;
                if (paramIdx < parameterList_.size()) {
                    const auto& param = parameterList_[paramIdx];
                    
                    if (param.param.name_hash == Taffy::fnv1a_hash("drive")) {
                        drive = param.currentValue;
                        std::cout << "   Found drive param: " << drive << std::endl;
                    } else if (param.param.name_hash == Taffy::fnv1a_hash("mix")) {
                        mix = param.currentValue;
                        std::cout << "   Found mix param: " << mix << std::endl;
                    } else if (param.param.name_hash == Taffy::fnv1a_hash("type")) {
                        distType = param.currentValue;
                        std::cout << "   Found type param: " << distType << std::endl;
                    }
                }
            }
        }
        
        DistortionType type = static_cast<DistortionType>(static_cast<uint32_t>(distType));
        
        // Debug log parameters
        static bool debugPrinted = false;
        if (!debugPrinted) {
            std::cout << "🎸 Distortion parameters:" << std::endl;
            std::cout << "   Drive: " << drive << std::endl;
            std::cout << "   Mix: " << mix << std::endl;
            std::cout << "   Type: " << static_cast<uint32_t>(type) << std::endl;
            debugPrinted = true;
        }
        
        // Process each sample
        for (uint32_t i = 0; i < frameCount; ++i) {
            // Get audio input (input 0)
            float input = 0.0f;
            for (const auto& conn : connections_) {
                if (conn.destNode == node.node.id && conn.destInput == 0) {
                    auto srcIt = nodes_.find(conn.sourceNode);
                    if (srcIt != nodes_.end()) {
                        input += srcIt->second.outputBuffer[i] * conn.strength;
                    }
                }
            }
            
            // Store dry signal for mixing
            float dry = input;
            
            // Apply drive (pre-gain)
            float driven = input * drive;
            
            // Debug first few samples
            static int sampleCount = 0;
            if (sampleCount < 5) {
                std::cout << "Distortion sample " << sampleCount << ": input=" << input 
                         << ", driven=" << driven << ", drive=" << drive << std::endl;
                sampleCount++;
            }
            
            // Apply distortion based on type
            float wet = 0.0f;
            switch (type) {
                case DistortionType::HardClip:
                    // Hard clipping - brutal digital distortion
                    wet = std::max(-1.0f, std::min(1.0f, driven));
                    break;
                    
                case DistortionType::SoftClip:
                    // Soft clipping using tanh - smooth saturation
                    wet = std::tanh(driven);
                    break;
                    
                case DistortionType::Foldback:
                    // Wavefolding - wraps signal back on itself
                    wet = driven;
                    while (wet > 1.0f) wet = 2.0f - wet;
                    while (wet < -1.0f) wet = -2.0f - wet;
                    break;
                    
                case DistortionType::BitCrush:
                    // Bit crushing - reduces bit depth
                    {
                        float bits = 3.0f; // Crush to 4 bits
                        float levels = std::pow(2.0f, bits);
                        wet = std::round(driven * levels) / levels;
                    }
                    break;
                    
                case DistortionType::Overdrive:
                    // Tube-style overdrive with asymmetric clipping
                    if (driven > 0) {
                        wet = 1.0f - std::exp(-driven);
                    } else {
                        wet = -1.0f + std::exp(driven * 0.7f); // Asymmetric
                    }
                    break;
                    
                case DistortionType::Beeper:
                    // 1-bit ZX Spectrum beeper emulation
                    // Pure on/off with hysteresis to prevent buzzing at zero crossings
                    {
                        static float hysteresis = 0.0f;
                        const float threshold = 0.1f; // Hysteresis threshold
                        
                        if (driven > threshold) {
                            wet = 1.0f;
                            hysteresis = 1.0f;
                        } else if (driven < -threshold) {
                            wet = -1.0f;
                            hysteresis = -1.0f;
                        } else {
                            // In the dead zone - maintain previous state
                            wet = hysteresis;
                        }
                        
                        // Add characteristic "beeper buzz" by slightly varying the output
                        // This simulates the mechanical response of the tiny speaker
                        static float buzzPhase = 0.0f;
                        buzzPhase += 0.1f;
                        if (buzzPhase > 2.0f * M_PI) buzzPhase -= 2.0f * M_PI;
                        
                        // Tiny amplitude modulation to simulate speaker characteristics
                        wet *= (0.95f + 0.05f * std::sin(buzzPhase * 237.0f)); // 237Hz buzz
                    }
                    break;
                    
                default:
                    wet = driven;
                    break;
            }
            
            // Mix dry and wet signals
            node.outputBuffer[i] = dry * (1.0f - mix) + wet * mix;
        }
    }

    void TaffyAudioProcessor::processSampler(NodeState& node, uint32_t frameCount) {
        // Get parameters
        uint32_t sampleIndex = 0;      // Which sample to play
        float pitch = 1.0f;            // Pitch/speed multiplier
        float startPos = 0.0f;         // Start position (0-1)
        bool loop = false;             // Whether to loop
        
        static bool debugPrinted = false;
        static int sampleDebugFrame = 0;
        
        // Get parameters based on node's param_offset
        if (node.node.param_count > 0 && node.node.param_offset < parameterList_.size()) {
            if (!debugPrinted) {
                std::cout << "🎛️ Sampler parameters: offset=" << node.node.param_offset 
                          << ", count=" << node.node.param_count << std::endl;
            }
            for (uint32_t i = 0; i < node.node.param_count; ++i) {
                uint32_t paramIdx = node.node.param_offset + i;
                if (paramIdx < parameterList_.size()) {
                    const auto& param = parameterList_[paramIdx];
                    
                    if (!debugPrinted) {
                        std::cout << "   Param[" << i << "]: hash=0x" << std::hex << param.param.name_hash 
                                  << std::dec << ", value=" << param.currentValue << std::endl;
                    }
                    
                    if (param.param.name_hash == Taffy::fnv1a_hash("sample_index")) {
                        sampleIndex = static_cast<uint32_t>(param.currentValue);
                    } else if (param.param.name_hash == Taffy::fnv1a_hash("pitch")) {
                        pitch = param.currentValue;
                    } else if (param.param.name_hash == Taffy::fnv1a_hash("start_position")) {
                        startPos = param.currentValue;
                    } else if (param.param.name_hash == Taffy::fnv1a_hash("loop")) {
                        loop = param.currentValue > 0.5f;
                    }
                }
            }
        }
        
        // Validate sample index
        if (sampleIndex >= samples_.size()) {
            if (!debugPrinted) {
                std::cout << "❌ Sampler: No samples loaded! samples_.size()=" << samples_.size() << std::endl;
                debugPrinted = true;
            }
            std::memset(node.outputBuffer.data(), 0, frameCount * sizeof(float));
            return;
        }
        
        const SampleData& sample = samples_[sampleIndex];
        
        if (!debugPrinted) {
            std::cout << "🎵 Sampler: Processing sample " << sampleIndex << ", " 
                      << sample.data.size() << " samples, " 
                      << sample.channelCount << " channels" << std::endl;
            std::cout << "   Sample rate: " << sample.sampleRate << " Hz" << std::endl;
            std::cout << "   Base frequency: " << sample.baseFrequency << " Hz" << std::endl;
            std::cout << "   Has loop: " << (sample.hasLoop ? "yes" : "no") << std::endl;
            std::cout << "   Expected parameter hashes:" << std::endl;
            std::cout << "     sample_index: 0x" << std::hex << Taffy::fnv1a_hash("sample_index") << std::dec << std::endl;
            std::cout << "     pitch: 0x" << std::hex << Taffy::fnv1a_hash("pitch") << std::dec << std::endl;
            std::cout << "     start_position: 0x" << std::hex << Taffy::fnv1a_hash("start_position") << std::dec << std::endl;
            std::cout << "     loop: 0x" << std::hex << Taffy::fnv1a_hash("loop") << std::dec << std::endl;
            if (sample.data.size() > 0) {
                std::cout << "   First few samples: ";
                for (size_t i = 0; i < std::min(size_t(10), sample.data.size()); ++i) {
                    std::cout << sample.data[i] << " ";
                }
                std::cout << std::endl;
            }
            debugPrinted = true;
        }
        
        // Process each frame
        for (uint32_t i = 0; i < frameCount; ++i) {
            // Keep using the pitch value we already loaded from parameters above
            // (removed the re-reading code that was overwriting with wrong value)
            
            // Get trigger input (input 0) - triggers playback on rising edge
            float trigger = 0.0f;
            for (const auto& conn : connections_) {
                if (conn.destNode == node.node.id && conn.destInput == 0) {
                    auto srcIt = nodes_.find(conn.sourceNode);
                    if (srcIt != nodes_.end()) {
                        trigger = srcIt->second.outputBuffer[i] * conn.strength;
                        break;
                    }
                }
            }
            
            // Detect rising edge for trigger OR keep playing if already playing
            if (trigger > 0.5f && node.lastTrigger <= 0.5f) {
                // Start playback
                node.isPlaying = true;
                node.samplePosition = startPos * sample.data.size() / sample.channelCount;
                node.sampleIndex = sampleIndex;
                
                std::cout << "🎵 Sampler: Triggered! Starting playback at position " << node.samplePosition << std::endl;
                std::cout << "   Total samples available: " << (sample.data.size() / sample.channelCount) << std::endl;
                std::cout << "   Pitch: " << pitch << std::endl;
                std::cout << "   Sample data size: " << sample.data.size() << " floats" << std::endl;
                
                // Debug: Show actual sample values
                if (sample.data.size() > 0) {
                    std::cout << "   First 20 sample values: ";
                    for (size_t j = 0; j < std::min(size_t(20), sample.data.size()); ++j) {
                        std::cout << sample.data[j] << " ";
                    }
                    std::cout << std::endl;
                    
                    // Find max amplitude in sample
                    float maxAmp = 0.0f;
                    for (const auto& s : sample.data) {
                        maxAmp = std::max(maxAmp, std::abs(s));
                    }
                    std::cout << "   Max amplitude in sample: " << maxAmp << std::endl;
                }
            }
            node.lastTrigger = trigger;
            
            // Get pitch modulation input (input 1) if available
            float pitchMod = 0.0f;
            bool hasPitchMod = false;
            for (const auto& conn : connections_) {
                if (conn.destNode == node.node.id && conn.destInput == 1) {
                    auto srcIt = nodes_.find(conn.sourceNode);
                    if (srcIt != nodes_.end()) {
                        pitchMod = srcIt->second.outputBuffer[i] * conn.strength;
                        hasPitchMod = true;
                        break;
                    }
                }
            }
            
            // Apply pitch modulation (additive if connected, otherwise just use base pitch)
            float finalPitch = hasPitchMod ? (pitch + pitchMod) : pitch;
            
            // Debug pitch values
            if (!debugPrinted) {
                std::cout << "   Pitch calculation: pitch=" << pitch << ", pitchMod=" << pitchMod 
                          << ", finalPitch=" << finalPitch << std::endl;
            }
            
            // Generate output
            if (node.isPlaying && !sample.data.empty()) {
                // Calculate sample rate ratio for pitch shifting
                float sampleRateRatio = static_cast<float>(sample.sampleRate) / static_cast<float>(sample_rate_);
                float playbackRate = finalPitch * sampleRateRatio;
                
                // Debug first time
                static bool rateDebug = false;
                if (!rateDebug || playbackRate == 0) {
                    std::cout << "📊 Sample playback calculation:" << std::endl;
                    std::cout << "   sample.sampleRate = " << sample.sampleRate << std::endl;
                    std::cout << "   sample_rate_ = " << sample_rate_ << std::endl;
                    std::cout << "   sampleRateRatio = " << sampleRateRatio << std::endl;
                    std::cout << "   pitch = " << pitch << std::endl;
                    std::cout << "   pitchMod = " << pitchMod << std::endl;
                    std::cout << "   finalPitch = " << finalPitch << std::endl;
                    std::cout << "   playbackRate = " << playbackRate << std::endl;
                    
                    if (playbackRate == 0) {
                        std::cout << "   ❌ ERROR: playbackRate is 0! Sample won't advance!" << std::endl;
                        std::cout << "   header_.sample_rate = " << header_.sample_rate << std::endl;
                    }
                    rateDebug = true;
                }
                
                // Get current sample position
                uint32_t samplePos = static_cast<uint32_t>(node.samplePosition);
                float fract = node.samplePosition - samplePos;
                
                // Debug: First few samples
                static int sampleOutputDebug = 0;
                if (sampleOutputDebug < 5 && node.isPlaying) {
                    std::cout << "🎧 Sample output[" << sampleOutputDebug << "]: samplePos=" << samplePos 
                              << ", fract=" << fract << ", data.size()=" << sample.data.size() 
                              << ", channels=" << sample.channelCount << std::endl;
                    sampleOutputDebug++;
                }
                
                // Linear interpolation for smoother playback
                float output = 0.0f;
                if (sample.channelCount == 1) {
                    // Mono
                    if (samplePos < sample.data.size() - 1) {
                        output = sample.data[samplePos] * (1.0f - fract) + 
                                sample.data[samplePos + 1] * fract;
                    } else if (samplePos < sample.data.size()) {
                        output = sample.data[samplePos];
                    }
                } else {
                    // Stereo - mix to mono for now
                    uint32_t pos1 = samplePos * 2;
                    uint32_t pos2 = (samplePos + 1) * 2;
                    if (pos1 < sample.data.size() - 2) {
                        float left = sample.data[pos1] * (1.0f - fract) + 
                                    sample.data[pos2] * fract;
                        float right = sample.data[pos1 + 1] * (1.0f - fract) + 
                                     sample.data[pos2 + 1] * fract;
                        output = (left + right) * 0.5f;
                    }
                }
                
                node.outputBuffer[i] = output;
                
                // Debug output for first few samples
                if (sampleDebugFrame < 10 && output != 0.0f) {
                    std::cout << "Sample playback[" << i << "]: pos=" << node.samplePosition 
                              << ", output=" << output << ", playbackRate=" << playbackRate << std::endl;
                }
                
                // Debug: Check for pitch drift every 2 seconds
                static float lastDebugTime = 0.0f;
                static float lastPlaybackRate = -1.0f;
                float currentTime = current_time_ + (i * 1.0f / static_cast<float>(sample_rate_));
                if (currentTime - lastDebugTime > 2.0f && node.isPlaying) {
                    if (lastPlaybackRate >= 0.0f && std::abs(playbackRate - lastPlaybackRate) > 0.0001f) {
                        std::cout << "⚠️ PITCH DRIFT DETECTED at " << currentTime << "s: " 
                                  << "playbackRate changed from " << lastPlaybackRate 
                                  << " to " << playbackRate << " (delta=" << (playbackRate - lastPlaybackRate) << ")" << std::endl;
                        std::cout << "   pitch=" << pitch << ", finalPitch=" << finalPitch 
                                  << ", sampleRateRatio=" << sampleRateRatio << std::endl;
                    } else {
                        std::cout << "🎵 Pitch check at " << currentTime << "s: pitch=" << pitch 
                                  << ", finalPitch=" << finalPitch << ", playbackRate=" << playbackRate << std::endl;
                    }
                    lastPlaybackRate = playbackRate;
                    lastDebugTime = currentTime;
                }
                
                // Advance position
                node.samplePosition += playbackRate;
                
                // Handle looping or end of sample
                uint32_t maxSamples = sample.data.size() / sample.channelCount;
                
                // Debug: Log position info for first few frames
                static int posDebug = 0;
                if (posDebug < 10) {
                    std::cout << "📍 Position debug[" << posDebug << "]: samplePos=" << node.samplePosition 
                              << ", maxSamples=" << maxSamples 
                              << ", playbackRate=" << playbackRate
                              << ", data.size()=" << sample.data.size()
                              << ", channels=" << sample.channelCount << std::endl;
                    posDebug++;
                }
                
                if (loop && sample.hasLoop) {
                    // Loop between loop points
                    if (node.samplePosition >= sample.loopEnd) {
                        node.samplePosition = sample.loopStart + 
                            fmod(node.samplePosition - sample.loopEnd, 
                                 sample.loopEnd - sample.loopStart);
                    }
                } else if (node.samplePosition >= maxSamples) {
                    // End of sample
                    node.isPlaying = false;
                    node.outputBuffer[i] = 0.0f;
                    std::cout << "🛑 Sample playback ended at position " << node.samplePosition 
                              << " (max=" << maxSamples << ")" << std::endl;
                }
            } else {
                node.outputBuffer[i] = 0.0f;
            }
        }
        
        // Increment debug frame counter
        if (node.isPlaying && sampleDebugFrame < 10) {
            sampleDebugFrame++;
        }
    }

} // namespace tremor::audio