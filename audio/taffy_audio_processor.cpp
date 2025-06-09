#include "taffy_audio_processor.h"
#include <iostream>
#include <cstring>
#include <random>
#include <cmath>
#include <chrono>
#include <unordered_set>
#include <functional>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>

namespace tremor::audio {

#ifndef M_PI
    float M_PI = 3.14159f;
#endif

    TaffyAudioProcessor::TaffyAudioProcessor(uint32_t sample_rate)
        : sample_rate_(sample_rate), current_time_(0.0f), sample_count_(0) {
        // Start background loader thread
        loaderThread_ = std::make_unique<std::thread>(&TaffyAudioProcessor::backgroundLoader, this);
    }

    TaffyAudioProcessor::~TaffyAudioProcessor() {
        // Stop loader thread
        shouldStopLoader_ = true;
        loaderCV_.notify_all();
        if (loaderThread_ && loaderThread_->joinable()) {
            loaderThread_->join();
        }
        
        // Clean up streaming file handles
        {
            std::lock_guard<std::mutex> lock(streamingAudiosMutex_);
            for (auto& stream : streamingAudios_) {
                std::lock_guard<std::mutex> streamLock(stream.bufferMutex);
                if (stream.fileStream) {
                    stream.fileStream.reset();
                }
            }
        }
    }

    bool TaffyAudioProcessor::loadAudioChunk(const std::vector<uint8_t>& audioData) {
        // Lock the graph mutex for the entire loading process
        std::lock_guard<std::mutex> lock(graphMutex_);
        
        if (audioData.size() < sizeof(Taffy::AudioChunk)) {
            std::cerr << "❌ Audio chunk data too small!" << std::endl;
            return false;
        }

        // Stop background loader during load to prevent race conditions
        {
            std::lock_guard<std::mutex> lock(loaderMutex_);
            // Clear the load queue
            while (!loadQueue_.empty()) {
                loadQueue_.pop();
            }
        }
        
        // Close any open file streams and clear streaming data
        {
            std::lock_guard<std::mutex> lock(streamingAudiosMutex_);
            for (auto& stream : streamingAudios_) {
                std::lock_guard<std::mutex> streamLock(stream.bufferMutex);
                if (stream.fileStream && stream.fileStream->is_open()) {
                    stream.fileStream->close();
                }
                stream.fileStream.reset();
            }
            streamingAudios_.clear();
        }

        // Clear existing data
        try {
            nodes_.clear();
            connections_.clear();
            parameters_.clear();
            parameterList_.clear();
            samples_.clear();
        } catch (const std::exception& e) {
            std::cerr << "❌ Exception while clearing data: " << e.what() << std::endl;
            return false;
        }
        
        // Reset time when loading new audio chunk
        current_time_ = 0.0f;
        sample_count_ = 0;
        
        // Clear existing data structures to prevent memory corruption
        nodes_.clear();
        connections_.clear();
        parameters_.clear();
        samples_.clear();
        
        // Clear streaming audios with proper mutex protection
        {
            std::lock_guard<std::mutex> lock(streamingAudiosMutex_);
            streamingAudios_.clear();
        }

        // Validate data size
        if (audioData.size() < sizeof(Taffy::AudioChunk)) {
            std::cerr << "❌ Audio data too small for header: " << audioData.size() << " bytes" << std::endl;
            return false;
        }

        // Read header
        const uint8_t* ptr = audioData.data();
        std::memcpy(&header_, ptr, sizeof(Taffy::AudioChunk));
        ptr += sizeof(Taffy::AudioChunk);
        
        // Validate header values
        if (header_.node_count > 1000 || header_.connection_count > 10000 || 
            header_.parameter_count > 10000 || header_.sample_count > 1000 ||
            header_.streaming_count > 100) {
            std::cerr << "❌ Invalid header values detected:" << std::endl;
            std::cerr << "   Nodes: " << header_.node_count << std::endl;
            std::cerr << "   Connections: " << header_.connection_count << std::endl;
            std::cerr << "   Parameters: " << header_.parameter_count << std::endl;
            std::cerr << "   Samples: " << header_.sample_count << std::endl;
            std::cerr << "   Streaming: " << header_.streaming_count << std::endl;
            return false;
        }

        std::cout << "🎵 Loading audio chunk:" << std::endl;
        std::cout << "   Audio data size: " << audioData.size() << " bytes" << std::endl;
        std::cout << "   Nodes: " << header_.node_count << std::endl;
        std::cout << "   Connections: " << header_.connection_count << std::endl;
        std::cout << "   Parameters: " << header_.parameter_count << std::endl;
        std::cout << "   Samples: " << header_.sample_count << std::endl;
        std::cout << "   Streaming audios: " << header_.streaming_count << std::endl;
        std::cout << "   Sample rate: " << header_.sample_rate << " Hz" << std::endl;
        std::cout << "   Header size: " << sizeof(Taffy::AudioChunk) << " bytes" << std::endl;
        
        // Debug first few bytes of header
        std::cout << "   First 16 bytes of header: ";
        for (int i = 0; i < 16 && i < audioData.size(); ++i) {
            std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)audioData[i] << " ";
        }
        std::cout << std::dec << std::endl;
        
        // Debug: Show raw header bytes
        std::cout << "   Raw header bytes (first 32): ";
        for (size_t i = 0; i < std::min(size_t(32), audioData.size()); ++i) {
            std::cout << std::hex << static_cast<int>(audioData[i]) << " ";
        }
        std::cout << std::dec << std::endl;

        // Read nodes
        for (uint32_t i = 0; i < header_.node_count; ++i) {
            // Bounds check
            if (ptr + sizeof(Taffy::AudioChunk::Node) > audioData.data() + audioData.size()) {
                std::cerr << "❌ Not enough data for node " << i << std::endl;
                return false;
            }
            
            Taffy::AudioChunk::Node node;
            std::memcpy(&node, ptr, sizeof(node));
            ptr += sizeof(node);
            
            // Validate node ID
            if (node.id > 1000) { // Sanity check
                std::cerr << "❌ Invalid node ID: " << node.id << std::endl;
                return false;
            }

            // Create NodeState directly in map to avoid copy
            NodeState& state = nodes_[node.id];
            state.node = node;
            
            try {
                state.outputBuffer.resize(1024, 0.0f);  // Pre-allocate buffer with zeros
            } catch (const std::exception& e) {
                std::cerr << "❌ Failed to allocate output buffer for node " << node.id << ": " << e.what() << std::endl;
                return false;
            }

            const char* nodeTypeName = "Unknown";
            switch (node.type) {
                case Taffy::AudioChunk::NodeType::Oscillator: nodeTypeName = "Oscillator"; break;
                case Taffy::AudioChunk::NodeType::Amplifier: nodeTypeName = "Amplifier"; break;
                case Taffy::AudioChunk::NodeType::Parameter: nodeTypeName = "Parameter"; break;
                case Taffy::AudioChunk::NodeType::Mixer: nodeTypeName = "Mixer"; break;
                case Taffy::AudioChunk::NodeType::Envelope: nodeTypeName = "Envelope"; break;
                case Taffy::AudioChunk::NodeType::Filter: nodeTypeName = "Filter"; break;
                case Taffy::AudioChunk::NodeType::Distortion: nodeTypeName = "Distortion"; break;
                case Taffy::AudioChunk::NodeType::Sampler: nodeTypeName = "Sampler"; break;
                case Taffy::AudioChunk::NodeType::StreamingSampler: nodeTypeName = "StreamingSampler"; break;
            }

            std::cout << "   Node " << node.id << ": type=" << static_cast<uint32_t>(node.type) 
                      << " (" << nodeTypeName << ")"
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
        
        // Read streaming audio info
        std::cout << "🎵 Reading " << header_.streaming_count << " streaming audio entries..." << std::endl;
        std::cout << "   Current ptr offset: " << (ptr - audioData.data()) << " bytes from start" << std::endl;
        std::cout << "   Total chunk size: " << audioData.size() << " bytes" << std::endl;
        std::cout << "   Expected streaming info at offset: " << (ptr - audioData.data()) << std::endl;
        for (uint32_t i = 0; i < header_.streaming_count; ++i) {
            // Check if we have enough data
            if (ptr + sizeof(Taffy::AudioChunk::StreamingAudio) > audioData.data() + audioData.size()) {
                std::cerr << "❌ Not enough data for streaming audio " << i << std::endl;
                break;
            }
            
            Taffy::AudioChunk::StreamingAudio streamInfo;
            std::memcpy(&streamInfo, ptr, sizeof(streamInfo));
            ptr += sizeof(streamInfo);
            
            StreamingAudioInfo stream;
            stream.dataOffset = streamInfo.data_offset;
            stream.totalSamples = streamInfo.total_samples;
            stream.chunkSize = streamInfo.chunk_size;
            stream.sampleRate = streamInfo.sample_rate;
            stream.channelCount = streamInfo.channel_count;
            stream.bitDepth = streamInfo.bit_depth;
            stream.format = streamInfo.format;
            
            // Validate loaded parameters BEFORE using them
            if (stream.channelCount == 0 || stream.channelCount > 8) {
                std::cerr << "⚠️  Invalid channel count: " << stream.channelCount << ", defaulting to 2" << std::endl;
                stream.channelCount = 2;
            }
            if (stream.chunkSize == 0 || stream.chunkSize > 1000000) {
                std::cerr << "⚠️  Invalid chunk size: " << stream.chunkSize << ", defaulting to 48000" << std::endl;
                stream.chunkSize = 48000;
            }
            if (stream.sampleRate == 0) {
                std::cerr << "⚠️  Invalid sample rate: " << stream.sampleRate << ", defaulting to 48000" << std::endl;
                stream.sampleRate = 48000;
            }
            if (stream.bitDepth != 16 && stream.bitDepth != 24 && stream.bitDepth != 32) {
                std::cerr << "⚠️  Invalid bit depth: " << stream.bitDepth << ", defaulting to 16" << std::endl;
                stream.bitDepth = 16;
            }
            
            // Pre-allocate buffers with validated sizes
            size_t bufferSize = stream.chunkSize * stream.channelCount;
            try {
                stream.chunkBuffer.resize(bufferSize, 0.0f);
                stream.nextChunkBuffer.resize(bufferSize, 0.0f);
            } catch (const std::exception& e) {
                std::cerr << "❌ Failed to allocate streaming buffers: " << e.what() << std::endl;
                continue;
            }
            
            // Note: We don't have the file path here, it needs to be set when loading from TAF
            // For now, we'll store the TAF file path which contains the audio data
            stream.filePath = ""; // Will be set by the loader
            stream.needsPreload = true;
            
            {
                std::lock_guard<std::mutex> lock(streamingAudiosMutex_);
                streamingAudios_.push_back(stream);
            }
            
            std::cout << "   Streaming Audio " << i << ": hash=0x" << std::hex << streamInfo.name_hash << std::dec
                      << ", " << streamInfo.total_samples << " samples, " << streamInfo.channel_count << " channels, "
                      << streamInfo.bit_depth << "-bit" << std::endl;
            std::cout << "   Sample rate: " << streamInfo.sample_rate << " Hz" << std::endl;
            std::cout << "   Chunk size: " << streamInfo.chunk_size << " samples" << std::endl;
            std::cout << "   Total chunks: " << streamInfo.chunk_count << std::endl;
            std::cout << "   Data offset in TAF: " << streamInfo.data_offset << std::endl;
        }

        std::cout << "✅ Audio chunk loaded successfully!" << std::endl;
        std::cout << "   Total streaming audios loaded: " << streamingAudios_.size() << std::endl;
        return true;
    }
    
    bool TaffyAudioProcessor::loadAudioMetadata(const std::vector<uint8_t>& audioData, size_t metadataSize) {
        // For streaming, we only load the metadata portion
        if (audioData.size() < metadataSize) {
            // If the chunk is smaller than expected metadata, just load it all
            return loadAudioChunk(audioData);
        }
        
        // Create a truncated vector with just metadata
        std::vector<uint8_t> metadataOnly(audioData.begin(), audioData.begin() + metadataSize);
        
        std::cout << "🎵 Loading streaming metadata only (" << metadataSize << " bytes of " 
                  << audioData.size() << " total)" << std::endl;
        
        return loadAudioChunk(metadataOnly);
    }

    void TaffyAudioProcessor::processAudio(float* outputBuffer, uint32_t frameCount, uint32_t channelCount) {
        // Lock the graph mutex during processing
        std::lock_guard<std::mutex> lock(graphMutex_);
        
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
                const char* nodeTypeName = "Unknown";
                switch (nodeState.node.type) {
                    case Taffy::AudioChunk::NodeType::Oscillator: nodeTypeName = "Oscillator"; break;
                    case Taffy::AudioChunk::NodeType::Amplifier: nodeTypeName = "Amplifier"; break;
                    case Taffy::AudioChunk::NodeType::Parameter: nodeTypeName = "Parameter"; break;
                    case Taffy::AudioChunk::NodeType::Mixer: nodeTypeName = "Mixer"; break;
                    case Taffy::AudioChunk::NodeType::Envelope: nodeTypeName = "Envelope"; break;
                    case Taffy::AudioChunk::NodeType::Filter: nodeTypeName = "Filter"; break;
                    case Taffy::AudioChunk::NodeType::Distortion: nodeTypeName = "Distortion"; break;
                    case Taffy::AudioChunk::NodeType::Sampler: nodeTypeName = "Sampler"; break;
                    case Taffy::AudioChunk::NodeType::StreamingSampler: nodeTypeName = "StreamingSampler"; break;
                }
                std::cout << "   Node " << nodeId << ": type=" << static_cast<int>(nodeState.node.type) 
                          << " (" << nodeTypeName << ")" << std::endl;
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
        // Ensure output buffer is properly sized
        if (node.outputBuffer.size() < frameCount) {
            node.outputBuffer.resize(frameCount);
        }
        
        static bool samplerLogged = false;

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
                if (!samplerLogged) {
                    std::cout << "🎵 SAMPLER NODE FOUND AND PROCESSING!" << std::endl;
                    samplerLogged = true;
                }
                processSampler(node, frameCount);
                break;
            case Taffy::AudioChunk::NodeType::StreamingSampler:
                static bool streamingDebugPrinted = false;
                if (!streamingDebugPrinted) {
                    std::cout << "🎵 STREAMING SAMPLER NODE FOUND AND PROCESSING!" << std::endl;
                    std::cout << "   Node ID: " << node.node.id << std::endl;
                    std::cout << "   Input count: " << node.node.input_count << std::endl;
                    std::cout << "   Output count: " << node.node.output_count << std::endl;
                    streamingDebugPrinted = true;
                }
                processStreamingSampler(node, frameCount);
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
                    if (!srcIt->second.outputBuffer.empty()) {
                        return srcIt->second.outputBuffer[0] * conn.strength;
                    }
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
    
    float TaffyAudioProcessor::getNodeParameterValue(const NodeState& node, uint64_t paramHash) {
        // Look for parameter in node's parameter range
        if (node.node.param_count > 0 && node.node.param_offset < parameterList_.size()) {
            for (uint32_t i = 0; i < node.node.param_count; ++i) {
                uint32_t paramIdx = node.node.param_offset + i;
                if (paramIdx < parameterList_.size()) {
                    if (parameterList_[paramIdx].param.name_hash == paramHash) {
                        return parameterList_[paramIdx].currentValue;
                    }
                }
            }
        }
        // Fall back to global parameter
        return getParameterValue(paramHash);
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
                        // Bounds check before accessing outputBuffer
                        if (i < srcIt->second.outputBuffer.size()) {
                            audioInput += srcIt->second.outputBuffer[i] * conn.strength;
                        }
                    }
                }
            }
            
            // Get modulation input (input 1) if available
            float modulation = 1.0f;
            for (const auto& conn : connections_) {
                if (conn.destNode == node.node.id && conn.destInput == 1) {
                    auto srcIt = nodes_.find(conn.sourceNode);
                    if (srcIt != nodes_.end()) {
                        // Bounds check before accessing outputBuffer
                        if (i < srcIt->second.outputBuffer.size()) {
                            modulation = srcIt->second.outputBuffer[i] * conn.strength;
                            break; // Only use first modulation input
                        }
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
                        if (srcIt != nodes_.end() && i < srcIt->second.outputBuffer.size()) {
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
                        wet = std::round(std::round(driven) * levels) / levels;
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
    
    void TaffyAudioProcessor::processStreamingSampler(NodeState& node, uint32_t frameCount) {
        static bool debugPrinted = false;
        
        // Defensive: ensure output buffer is properly sized
        if (node.outputBuffer.size() < frameCount) {
            node.outputBuffer.resize(frameCount);
        }
        
        // Get parameters
        uint32_t streamIndex = static_cast<uint32_t>(getNodeParameterValue(node, Taffy::fnv1a_hash("stream_index")));
        float pitch = getNodeParameterValue(node, Taffy::fnv1a_hash("pitch"));
        float startPos = getNodeParameterValue(node, Taffy::fnv1a_hash("start_position"));
        
        if (!debugPrinted) {
            std::cout << "🎵 StreamingSampler: streamIndex=" << streamIndex 
                      << ", pitch=" << pitch << ", startPos=" << startPos << std::endl;
            std::cout << "   Available streams: " << streamingAudios_.size() << std::endl;
            debugPrinted = true;
        }
        
        if (pitch == 0.0f) pitch = 1.0f; // Default pitch
        
        // Check if we have this streaming audio loaded
        StreamingAudioInfo* streamPtr = nullptr;
        {
            std::lock_guard<std::mutex> lock(streamingAudiosMutex_);
            if (streamIndex >= streamingAudios_.size()) {
                if (!debugPrinted) {
                    std::cout << "❌ No streaming audio at index " << streamIndex << std::endl;
                }
                // Ensure buffer is sized before memset
                if (node.outputBuffer.size() < frameCount) {
                    node.outputBuffer.resize(frameCount);
                }
                std::memset(node.outputBuffer.data(), 0, frameCount * sizeof(float));
                return;
            }
            streamPtr = &streamingAudios_[streamIndex];
        }
        
        if (!streamPtr) {
            // Ensure buffer is sized before memset
            if (node.outputBuffer.size() < frameCount) {
                node.outputBuffer.resize(frameCount);
            }
            std::memset(node.outputBuffer.data(), 0, frameCount * sizeof(float));
            return;
        }
        
        auto& stream = *streamPtr;
        
        // Quick check if file path is valid
        if (stream.filePath.empty()) {
            std::memset(node.outputBuffer.data(), 0, frameCount * sizeof(float));
            return;
        }
        
        // Ensure file stream is open
        if (!stream.fileStream || !stream.fileStream->is_open()) {
            stream.fileStream.reset();
            std::cout << "🔄 Opening streaming audio file: " << stream.filePath << std::endl;
            stream.fileStream = std::make_unique<std::ifstream>(stream.filePath, std::ios::binary);
            if (!stream.fileStream->is_open()) {
                std::cerr << "❌ Failed to open streaming audio file: " << stream.filePath << std::endl;
                std::cerr << "   Does file exist? " << std::filesystem::exists(stream.filePath) << std::endl;
                std::memset(node.outputBuffer.data(), 0, frameCount * sizeof(float));
                return;
            }
            std::cout << "✅ File opened successfully!" << std::endl;
            
            // If this is a WAV file, we need to find the data chunk offset
            if (stream.filePath.find(".wav") != std::string::npos) {
                // Read WAV header to find data chunk
                char buffer[4];
                stream.fileStream->read(buffer, 4);
                if (std::strncmp(buffer, "RIFF", 4) == 0) {
                    stream.fileStream->seekg(12); // Skip RIFF header
                    
                    // Search for data chunk
                    const int maxChunks = 50; // Prevent infinite loop
                    int chunkCount = 0;
                    while (!stream.fileStream->eof() && chunkCount < maxChunks) {
                        stream.fileStream->read(buffer, 4);
                        if (stream.fileStream->gcount() < 4) break;
                        
                        if (std::strncmp(buffer, "data", 4) == 0) {
                            uint32_t dataSize;
                            stream.fileStream->read(reinterpret_cast<char*>(&dataSize), 4);
                            if (stream.fileStream->gcount() == 4) {
                                stream.dataOffset = stream.fileStream->tellg();
                                std::cout << "📍 Found WAV data chunk at offset: " << stream.dataOffset << std::endl;
                            }
                            break;
                        } else {
                            // Skip this chunk
                            uint32_t chunkSize;
                            stream.fileStream->read(reinterpret_cast<char*>(&chunkSize), 4);
                            if (stream.fileStream->gcount() == 4 && chunkSize < 100000000) { // Sanity check
                                stream.fileStream->seekg(chunkSize, std::ios::cur);
                            } else {
                                break;
                            }
                        }
                        chunkCount++;
                    }
                }
            }
        }
        
        // Process each frame
        for (uint32_t i = 0; i < frameCount; ++i) {
            // Get trigger input
            float trigger = 0.0f;
            for (const auto& conn : connections_) {
                if (conn.destNode == node.node.id && conn.destInput == 0) {
                    auto srcIt = nodes_.find(conn.sourceNode);
                    if (srcIt != nodes_.end()) {
                        // Bounds check before accessing source buffer
                        if (i < srcIt->second.outputBuffer.size()) {
                            trigger = srcIt->second.outputBuffer[i] * conn.strength;
                        } else {
                            std::cerr << "❌ Source buffer too small in StreamingSampler trigger: index=" << i 
                                      << ", size=" << srcIt->second.outputBuffer.size() << std::endl;
                        }
                        
                        // Debug trigger values
                        static int triggerDebugCount = 0;
                        if (trigger != 0.0f && triggerDebugCount < 10) {
                            std::cout << "🎯 Trigger value: " << trigger << " from node " << conn.sourceNode << std::endl;
                            triggerDebugCount++;
                        }
                        break;
                    }
                }
            }
            
            // Start/stop playback on trigger
            if (trigger > 0.5f && node.lastTrigger <= 0.5f) {
                std::cout << "🎵 StreamingSampler TRIGGERED! trigger=" << trigger << std::endl;
                std::cout << "   File path: " << stream.filePath << std::endl;
                std::cout << "   Total samples: " << stream.totalSamples << std::endl;
                std::cout << "   Sample rate: " << stream.sampleRate << " Hz" << std::endl;
                std::cout << "   Chunk size: " << stream.chunkSize << " samples" << std::endl;
                
                node.isPlaying = true;
                node.samplePosition = startPos * stream.totalSamples;
                stream.currentChunk = static_cast<uint32_t>(node.samplePosition / stream.chunkSize);
                stream.bufferPosition = static_cast<uint32_t>(node.samplePosition) % stream.chunkSize;
                
                // Use pre-loaded chunk if available
                bool needsSyncLoad = false;
                {
                    std::lock_guard<std::mutex> lock(stream.bufferMutex);
                    if (stream.nextChunkReady && stream.nextChunkIndex == stream.currentChunk) {
                        // Use pre-loaded chunk
                        stream.chunkBuffer = std::move(stream.nextChunkBuffer);
                        stream.nextChunkBuffer.clear();
                        stream.nextChunkReady = false;
                        std::cout << "✅ Using pre-loaded chunk " << stream.currentChunk << std::endl;
                    } else {
                        needsSyncLoad = true;
                    }
                }
                
                if (needsSyncLoad) {
                    // Need to load synchronously (causes hitch)
                    std::cout << "⚠️  Loading chunk " << stream.currentChunk << " synchronously (may cause hitch)" << std::endl;
                    preloadStreamingChunk(stream, stream.currentChunk);
                    
                    std::lock_guard<std::mutex> lock(stream.bufferMutex);
                    stream.chunkBuffer = std::move(stream.nextChunkBuffer);
                    stream.nextChunkBuffer.clear();
                    stream.nextChunkReady = false;
                }
                
                // Pre-load next chunk in background
                if (stream.currentChunk + 1 < (stream.totalSamples + stream.chunkSize - 1) / stream.chunkSize) {
                    preloadStreamingChunkAsync(stream, stream.currentChunk + 1);
                }
            }
            node.lastTrigger = trigger;
            
            // Generate output
            if (node.isPlaying) {
                // Apply sample rate conversion for pitch
                float sampleRateRatio = (stream.sampleRate > 0) ? 
                    static_cast<float>(sample_rate_) / static_cast<float>(stream.sampleRate) : 1.0f;
                float playbackRate = pitch * sampleRateRatio;
                
                // Get current sample with linear interpolation
                uint32_t pos = stream.bufferPosition;
                float frac = node.samplePosition - std::floor(node.samplePosition);
                
                float sample = 0.0f;
                
                // Lock buffer while reading
                {
                    std::lock_guard<std::mutex> lock(stream.bufferMutex);
                    
                    // Bounds check
                    if (!stream.chunkBuffer.empty() && stream.channelCount > 0) {
                        uint32_t maxPos = stream.chunkBuffer.size() / stream.channelCount;
                        if (pos < maxPos - 1) {
                            // Mix channels to mono and interpolate
                            float s1 = 0.0f, s2 = 0.0f;
                            for (uint32_t ch = 0; ch < stream.channelCount; ++ch) {
                                uint32_t idx1 = pos * stream.channelCount + ch;
                                uint32_t idx2 = (pos + 1) * stream.channelCount + ch;
                                if (idx1 < stream.chunkBuffer.size() && idx2 < stream.chunkBuffer.size()) {
                                    s1 += stream.chunkBuffer[idx1];
                                    s2 += stream.chunkBuffer[idx2];
                                }
                            }
                            s1 /= stream.channelCount;
                            s2 /= stream.channelCount;
                            sample = s1 * (1.0f - frac) + s2 * frac;
                        }
                    }
                }
                
                // Bounds check before writing to output buffer
                if (i >= node.outputBuffer.size()) {
                    std::cerr << "❌ StreamingSampler output buffer overrun! index=" << i 
                              << ", buffer size=" << node.outputBuffer.size() 
                              << ", frameCount=" << frameCount << std::endl;
                    // Resize the buffer to prevent crash
                    node.outputBuffer.resize(frameCount, 0.0f);
                }
                
                if (i < node.outputBuffer.size()) {
                    node.outputBuffer[i] = sample;
                } else {
                    std::cerr << "❌ Still can't write to output buffer after resize!" << std::endl;
                    break;
                }
                
                // Debug output
                static int outputDebugCounter = 0;
                if (outputDebugCounter < 100 && sample != 0.0f) {
                    std::cout << "🔊 StreamingSampler output[" << i << "] = " << sample << std::endl;
                    outputDebugCounter++;
                }
                
                // Advance position
                node.samplePosition += playbackRate;
                stream.bufferPosition = static_cast<uint32_t>(node.samplePosition) % stream.chunkSize;
                
                // Check if we need to load next chunk
                uint32_t newChunk = static_cast<uint32_t>(node.samplePosition / stream.chunkSize);
                if (newChunk != stream.currentChunk && newChunk < (stream.totalSamples + stream.chunkSize - 1) / stream.chunkSize) {
                    stream.currentChunk = newChunk;
                    
                    // Load next chunk
                    bool useAsync = false;
                    {
                        std::lock_guard<std::mutex> lock(stream.bufferMutex);
                        if (stream.nextChunkReady && stream.nextChunkIndex == stream.currentChunk) {
                            // Use pre-loaded chunk (seamless transition)
                            stream.chunkBuffer = std::move(stream.nextChunkBuffer);
                            stream.nextChunkBuffer.clear(); // Ensure it's cleared
                            stream.nextChunkReady = false;
                            std::cout << "✅ Seamless transition to chunk " << stream.currentChunk << std::endl;
                            useAsync = true;
                        }
                    }
                    
                    if (!useAsync) {
                        // Need to load synchronously (causes hitch)
                        std::cout << "⚠️  Loading chunk " << stream.currentChunk << " synchronously (HITCH!)" << std::endl;
                        preloadStreamingChunk(stream, stream.currentChunk);
                        stream.chunkBuffer = std::move(stream.nextChunkBuffer);
                        stream.nextChunkReady = false;
                    }
                    
                    // Pre-load next chunk in background
                    if (stream.currentChunk + 1 < (stream.totalSamples + stream.chunkSize - 1) / stream.chunkSize) {
                        preloadStreamingChunkAsync(stream, stream.currentChunk + 1);
                    }
                }
                
                // Stop if we've reached the end
                if (node.samplePosition >= stream.totalSamples) {
                    node.isPlaying = false;
                    node.samplePosition = 0.0f;
                }
            } else {
                node.outputBuffer[i] = 0.0f;
            }
        }
    }

    void TaffyAudioProcessor::preloadStreamingChunk(StreamingAudioInfo& stream, uint32_t chunkIndex) {
        // Lock the stream's buffer mutex to ensure thread safety
        std::lock_guard<std::mutex> lock(stream.bufferMutex);
        
        // Ensure file stream is open
        if (!stream.fileStream || !stream.fileStream->is_open()) {
            stream.fileStream.reset();
            stream.fileStream = std::make_unique<std::ifstream>(stream.filePath, std::ios::binary);
            if (!stream.fileStream->is_open()) {
                std::cerr << "❌ Failed to open streaming audio file for preload: " << stream.filePath << std::endl;
                return;
            }
            
            // If this is a WAV file, we need to find the data chunk offset
            if (stream.filePath.find(".wav") != std::string::npos) {
                // Read WAV header to find data chunk
                char buffer[4];
                stream.fileStream->read(buffer, 4);
                if (std::strncmp(buffer, "RIFF", 4) == 0) {
                    stream.fileStream->seekg(12); // Skip RIFF header
                    
                    // Search for data chunk
                    const int maxChunks = 50; // Prevent infinite loop
                    int chunkCount = 0;
                    while (!stream.fileStream->eof() && chunkCount < maxChunks) {
                        stream.fileStream->read(buffer, 4);
                        if (stream.fileStream->gcount() < 4) break;
                        
                        if (std::strncmp(buffer, "data", 4) == 0) {
                            uint32_t dataSize;
                            stream.fileStream->read(reinterpret_cast<char*>(&dataSize), 4);
                            if (stream.fileStream->gcount() == 4) {
                                stream.dataOffset = stream.fileStream->tellg();
                                std::cout << "📍 Found WAV data chunk at offset: " << stream.dataOffset << std::endl;
                            }
                            break;
                        } else {
                            // Skip this chunk
                            uint32_t chunkSize;
                            stream.fileStream->read(reinterpret_cast<char*>(&chunkSize), 4);
                            if (stream.fileStream->gcount() == 4 && chunkSize < 100000000) { // Sanity check
                                stream.fileStream->seekg(chunkSize, std::ios::cur);
                            } else {
                                break;
                            }
                        }
                        chunkCount++;
                    }
                }
            }
        }
        
        // Calculate chunk offset
        uint64_t chunkOffset = stream.dataOffset + (chunkIndex * stream.chunkSize * stream.channelCount * (stream.bitDepth / 8));
        stream.fileStream->seekg(chunkOffset);
        
        // Validate parameters before resizing
        if (stream.chunkSize == 0 || stream.channelCount == 0 || stream.chunkSize > 1000000) {
            std::cerr << "❌ Invalid chunk parameters: chunkSize=" << stream.chunkSize 
                      << ", channelCount=" << stream.channelCount << std::endl;
            return;
        }
        
        // Resize buffer if needed
        size_t requiredSize = stream.chunkSize * stream.channelCount;
        
        // Additional safety check
        if (requiredSize == 0 || requiredSize > 10000000) { // 10MB max for a single chunk
            std::cerr << "❌ Invalid buffer size requested: " << requiredSize << std::endl;
            return;
        }
        
        try {
            stream.nextChunkBuffer.resize(requiredSize);
        } catch (const std::exception& e) {
            std::cerr << "❌ Failed to resize buffer to " << requiredSize << ": " << e.what() << std::endl;
            return;
        }
        
        // Double-check the resize worked
        if (stream.nextChunkBuffer.size() != requiredSize) {
            std::cerr << "❌ Buffer resize failed silently! Expected " << requiredSize 
                      << " but got " << stream.nextChunkBuffer.size() << std::endl;
            return;
        }
        
        // Read chunk data
        if (stream.format == 1) { // Float format
            stream.fileStream->read(reinterpret_cast<char*>(stream.nextChunkBuffer.data()), 
                                  stream.chunkSize * stream.channelCount * sizeof(float));
            
            // Check how much we actually read
            size_t bytesRead = stream.fileStream->gcount();
            if (bytesRead == 0) {
                std::cerr << "❌ Failed to read float data from file" << std::endl;
                return;
            }
        } else { // PCM format
            if (stream.bitDepth == 16) {
                std::vector<int16_t> pcmBuffer(stream.chunkSize * stream.channelCount);
                stream.fileStream->read(reinterpret_cast<char*>(pcmBuffer.data()), 
                                      pcmBuffer.size() * sizeof(int16_t));
                
                // Check how much we actually read
                size_t bytesRead = stream.fileStream->gcount();
                size_t samplesRead = bytesRead / sizeof(int16_t);
                
                if (samplesRead == 0) {
                    std::cerr << "❌ Failed to read PCM data from file" << std::endl;
                    return;
                }
                
                // Convert to float - ensure buffer sizes match and we don't exceed what we read
                size_t sampleCount = std::min(samplesRead, pcmBuffer.size());
                sampleCount = std::min(sampleCount, stream.nextChunkBuffer.size());
                
                // Debug output (commented out for release)
                // if (sampleCount > 0) {
                //     std::cout << "📊 Converting " << sampleCount << " samples to float" << std::endl;
                //     std::cout << "   pcmBuffer size: " << pcmBuffer.size() << std::endl;
                //     std::cout << "   nextChunkBuffer size: " << stream.nextChunkBuffer.size() << std::endl;
                //     std::cout << "   samplesRead: " << samplesRead << std::endl;
                // }
                
                // Extra safety: validate both buffers exist and have data
                if (pcmBuffer.empty() || stream.nextChunkBuffer.empty()) {
                    std::cerr << "❌ Empty buffer detected! pcmBuffer.empty()=" << pcmBuffer.empty() 
                              << ", nextChunkBuffer.empty()=" << stream.nextChunkBuffer.empty() << std::endl;
                    return;
                }
                
                // Validate pointers before accessing
                if (pcmBuffer.data() == nullptr || stream.nextChunkBuffer.data() == nullptr) {
                    std::cerr << "❌ Null buffer data pointer detected!" << std::endl;
                    return;
                }
                
                // Extra safety: ensure we never exceed either buffer
                for (size_t j = 0; j < sampleCount; ++j) {
                    if (j >= pcmBuffer.size()) {
                        std::cerr << "❌ PCM buffer overrun at index " << j << " (size=" << pcmBuffer.size() << ")" << std::endl;
                        break;
                    }
                    if (j >= stream.nextChunkBuffer.size()) {
                        std::cerr << "❌ Next chunk buffer overrun at index " << j << " (size=" << stream.nextChunkBuffer.size() << ")" << std::endl;
                        break;
                    }
                    
                    // Use safer array access with at() to get bounds checking
                    try {
                        stream.nextChunkBuffer.at(j) = pcmBuffer.at(j) / 32768.0f;
                    } catch (const std::out_of_range& e) {
                        std::cerr << "❌ Buffer access out of range at index " << j << ": " << e.what() << std::endl;
                        break;
                    }
                }
            } else if (stream.bitDepth == 24) {
                std::vector<uint8_t> pcmBuffer(stream.chunkSize * stream.channelCount * 3);
                stream.fileStream->read(reinterpret_cast<char*>(pcmBuffer.data()), pcmBuffer.size());
                
                // Check how much we actually read
                size_t bytesRead = stream.fileStream->gcount();
                size_t samplesRead = bytesRead / 3;  // 3 bytes per sample
                
                if (samplesRead == 0) {
                    std::cerr << "❌ Failed to read 24-bit PCM data from file" << std::endl;
                    return;
                }
                
                // Convert 24-bit to float - ensure we don't exceed buffer size or what we read
                size_t sampleCount = std::min(samplesRead, static_cast<size_t>(stream.chunkSize * stream.channelCount));
                sampleCount = std::min(sampleCount, stream.nextChunkBuffer.size());
                for (size_t j = 0; j < sampleCount; ++j) {
                    // Bounds check for pcmBuffer access
                    if (j * 3 + 2 >= bytesRead) break;
                    
                    int32_t sample = (pcmBuffer[j*3] << 8) | (pcmBuffer[j*3+1] << 16) | (pcmBuffer[j*3+2] << 24);
                    stream.nextChunkBuffer[j] = sample / 2147483648.0f;
                }
            }
        }
        
        stream.nextChunkReady = true;
        std::cout << "📦 Pre-loaded chunk " << chunkIndex << " for streaming audio" << std::endl;
    }

    void TaffyAudioProcessor::backgroundLoader() {
        std::cout << "🚀 Background loader thread started" << std::endl;
        
        while (!shouldStopLoader_) {
            LoadRequest request;
            
            {
                std::unique_lock<std::mutex> lock(loaderMutex_);
                loaderCV_.wait(lock, [this] { return !loadQueue_.empty() || shouldStopLoader_; });
                
                if (shouldStopLoader_) break;
                if (loadQueue_.empty()) continue;
                
                request = loadQueue_.front();
                loadQueue_.pop();
            }
            
            // Validate stream pointer and state
            if (!request.stream || request.stream->filePath.empty()) {
                continue;
            }
            
            // Double-check the stream is still valid (not deallocated)
            bool streamValid = false;
            {
                std::lock_guard<std::mutex> lock(streamingAudiosMutex_);
                for (auto& s : streamingAudios_) {
                    if (&s == request.stream) {
                        streamValid = true;
                        break;
                    }
                }
            }
            
            if (!streamValid) {
                std::cout << "⚠️ Stream pointer no longer valid, skipping load" << std::endl;
                continue;
            }
            
            // Load the chunk
            if (!request.stream->isLoadingNext.exchange(true)) {
                
                std::cout << "🔄 Background loading chunk " << request.chunkIndex << std::endl;
                
                // Ensure file stream is open
                if (!request.stream->fileStream || !request.stream->fileStream->is_open()) {
                    request.stream->fileStream = std::make_unique<std::ifstream>(request.stream->filePath, std::ios::binary);
                    if (!request.stream->fileStream->is_open()) {
                        std::cerr << "❌ Failed to open file in background loader" << std::endl;
                        request.stream->isLoadingNext = false;
                        continue;
                    }
                    
                    // If this is a WAV file, we need to find the data chunk offset
                    if (request.stream->filePath.find(".wav") != std::string::npos) {
                        // Read WAV header to find data chunk
                        char buffer[4];
                        request.stream->fileStream->read(buffer, 4);
                        if (std::strncmp(buffer, "RIFF", 4) == 0) {
                            request.stream->fileStream->seekg(12); // Skip RIFF header
                            
                            // Search for data chunk
                            while (!request.stream->fileStream->eof()) {
                                request.stream->fileStream->read(buffer, 4);
                                if (std::strncmp(buffer, "data", 4) == 0) {
                                    uint32_t dataSize;
                                    request.stream->fileStream->read(reinterpret_cast<char*>(&dataSize), 4);
                                    request.stream->dataOffset = request.stream->fileStream->tellg();
                                    std::cout << "📍 Background loader: Found WAV data chunk at offset: " << request.stream->dataOffset << std::endl;
                                    break;
                                } else {
                                    // Skip this chunk
                                    uint32_t chunkSize;
                                    request.stream->fileStream->read(reinterpret_cast<char*>(&chunkSize), 4);
                                    request.stream->fileStream->seekg(chunkSize, std::ios::cur);
                                }
                            }
                        }
                    }
                }
                
                // Calculate chunk offset
                uint64_t chunkOffset = request.stream->dataOffset + 
                    (request.chunkIndex * request.stream->chunkSize * request.stream->channelCount * (request.stream->bitDepth / 8));
                request.stream->fileStream->seekg(chunkOffset);
                
                // Validate parameters before allocating
                if (request.stream->chunkSize == 0 || request.stream->channelCount == 0 || 
                    request.stream->chunkSize > 1000000) {
                    std::cerr << "❌ Invalid chunk parameters in background loader: chunkSize=" 
                              << request.stream->chunkSize << ", channelCount=" 
                              << request.stream->channelCount << std::endl;
                    request.stream->isLoadingNext = false;
                    continue;
                }
                
                // Allocate temporary buffer
                std::vector<float> tempBuffer;
                try {
                    tempBuffer.resize(request.stream->chunkSize * request.stream->channelCount);
                } catch (const std::exception& e) {
                    std::cerr << "❌ Failed to allocate buffer in background loader: " << e.what() << std::endl;
                    request.stream->isLoadingNext = false;
                    continue;
                }
                
                // Read chunk data
                if (request.stream->format == 1) { // Float format
                    request.stream->fileStream->read(reinterpret_cast<char*>(tempBuffer.data()), 
                                          request.stream->chunkSize * request.stream->channelCount * sizeof(float));
                } else { // PCM format
                    if (request.stream->bitDepth == 16) {
                        std::vector<int16_t> pcmBuffer(request.stream->chunkSize * request.stream->channelCount);
                        request.stream->fileStream->read(reinterpret_cast<char*>(pcmBuffer.data()), 
                                              pcmBuffer.size() * sizeof(int16_t));
                        // Convert to float - ensure buffer sizes match
                        size_t sampleCount = std::min(pcmBuffer.size(), tempBuffer.size());
                        for (size_t j = 0; j < sampleCount; ++j) {
                            tempBuffer[j] = pcmBuffer[j] / 32768.0f;
                        }
                    }
                }
                
                // Swap buffers under lock
                {
                    std::lock_guard<std::mutex> lock(request.stream->bufferMutex);
                    request.stream->nextChunkBuffer = std::move(tempBuffer);
                    request.stream->nextChunkReady = true;
                    request.stream->nextChunkIndex = request.chunkIndex;
                }
                
                std::cout << "✅ Chunk " << request.chunkIndex << " loaded in background" << std::endl;
                request.stream->isLoadingNext = false;
            }
        }
        
        std::cout << "🛑 Background loader thread stopped" << std::endl;
    }
    
    void TaffyAudioProcessor::preloadStreamingChunkAsync(StreamingAudioInfo& stream, uint32_t chunkIndex) {
        // Use atomic exchange to prevent race condition
        if (stream.isLoadingNext.exchange(true)) {
            return; // Already loading
        }
        
        // Validate chunk index
        if (stream.chunkSize == 0 || stream.totalSamples == 0) {
            stream.isLoadingNext = false;
            return;
        }
        
        uint32_t totalChunks = (stream.totalSamples + stream.chunkSize - 1) / stream.chunkSize;
        if (chunkIndex >= totalChunks) {
            stream.isLoadingNext = false;
            return;
        }
        
        {
            std::lock_guard<std::mutex> lock(loaderMutex_);
            
            // Clear any pending loads for this stream to avoid stale requests
            std::queue<LoadRequest> newQueue;
            while (!loadQueue_.empty()) {
                LoadRequest req = loadQueue_.front();
                loadQueue_.pop();
                if (req.stream != &stream) {
                    newQueue.push(req);
                }
            }
            loadQueue_ = std::move(newQueue);
            
            // Add new request
            loadQueue_.push({&stream, chunkIndex});
        }
        loaderCV_.notify_one();
    }

} // namespace tremor::audio