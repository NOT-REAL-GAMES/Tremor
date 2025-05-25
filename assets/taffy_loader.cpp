// tremor/src/assets/taffy_loader.cpp
#include "../taffy/taffy.h"
#include <fstream>
#include <cstring>
#include <iostream>
#include <ctime>

namespace Taffy {

    // Implement the operators that were moved from header
    FeatureFlags operator|(FeatureFlags a, FeatureFlags b) {
        return static_cast<FeatureFlags>(static_cast<uint64_t>(a) | static_cast<uint64_t>(b));
    }

    bool has_feature(FeatureFlags flags, FeatureFlags feature) {
        return (static_cast<uint64_t>(flags) & static_cast<uint64_t>(feature)) != 0;
    }

    bool Asset::load_from_file(const std::string& filepath) {
        std::ifstream file(filepath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            std::cerr << "Failed to open Taffy file: " << filepath << std::endl;
            return false;
        }

        // Get file size
        const size_t file_size = file.tellg();
        file.seekg(0, std::ios::beg);

        // Read header
        file.read(reinterpret_cast<char*>(&header_), sizeof(Header));

        // Validate magic number
        if (header_.magic != TAFFY_MAGIC) {
            std::cerr << "Invalid Taffy magic number" << std::endl;
            return false;
        }

        // Validate version (for now, just accept 0.1.x)
        if (header_.version_major != 0 || header_.version_minor != 1) {
            std::cerr << "Unsupported Taffy version: "
                << header_.version_major << "." << header_.version_minor << std::endl;
            return false;
        }

        std::cout << "Loading Taffy asset: " << header_.description
            << " (created by: " << header_.creator << ")" << std::endl;

        // Read chunk directory
        chunk_directory_.resize(header_.chunk_count);
        file.read(reinterpret_cast<char*>(chunk_directory_.data()),
            header_.chunk_count * sizeof(ChunkHeader));

        // Read all chunks
        for (const auto& chunk_header : chunk_directory_) {
            std::vector<uint8_t> chunk_data(chunk_header.size);

            file.seekg(chunk_header.offset);
            file.read(reinterpret_cast<char*>(chunk_data.data()), chunk_header.size);

            // TODO: Validate checksum
            // TODO: Handle decompression if needed

            chunks_[chunk_header.type] = std::move(chunk_data);

            std::cout << "Loaded chunk: " << chunk_type_to_string(chunk_header.type)
                << " (" << chunk_header.size << " bytes)" << std::endl;
        }

        return true;
    }

    bool Asset::save_to_file(const std::string& filepath) const {
        std::ofstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }

        // Update header with current chunk information
        Header updated_header = header_;
        updated_header.chunk_count = static_cast<uint32_t>(chunk_directory_.size());

        // Calculate total size
        size_t total_size = sizeof(Header) + (chunk_directory_.size() * sizeof(ChunkHeader));
        for (const auto& [type, data] : chunks_) {
            total_size += data.size();
        }
        updated_header.total_size = total_size;

        // Write header
        file.write(reinterpret_cast<const char*>(&updated_header), sizeof(Header));

        // Write chunk directory
        file.write(reinterpret_cast<const char*>(chunk_directory_.data()),
            chunk_directory_.size() * sizeof(ChunkHeader));

        // Write chunk data
        for (const auto& chunk_header : chunk_directory_) {
            const auto& chunk_data = chunks_.at(chunk_header.type);
            file.write(reinterpret_cast<const char*>(chunk_data.data()), chunk_data.size());
        }

        return true;
    }

    std::optional<GeometryChunk> Asset::get_geometry() const {
        auto it = chunks_.find(ChunkType::GEOM);
        if (it == chunks_.end() || it->second.size() < sizeof(GeometryChunk)) {
            return std::nullopt;
        }

        // Cast raw data to geometry chunk
        const GeometryChunk* geom = reinterpret_cast<const GeometryChunk*>(it->second.data());
        return *geom;
    }

    std::optional<std::vector<uint8_t>> Asset::get_chunk_data(ChunkType type) const {
        auto it = chunks_.find(type);
        if (it == chunks_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    bool Asset::has_feature(FeatureFlags feature) const {
        return ::Taffy::has_feature(header_.feature_flags, feature);
    }

    const char* chunk_type_to_string(ChunkType type) {
        switch (type) {
        case ChunkType::GEOM: return "GEOM";
        case ChunkType::MTRL: return "MTRL";
        case ChunkType::TXTR: return "TXTR";
        case ChunkType::ANIM: return "ANIM";
        case ChunkType::SCPT: return "SCPT";
        case ChunkType::FRAC: return "FRAC";
        case ChunkType::PART: return "PART";
        default: return "UNKNOWN";
        }
    }

} // namespace Taffy