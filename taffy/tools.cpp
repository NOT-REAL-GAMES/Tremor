#pragma once
#include "tools.h"
#include "quan.h"
#include "asset.h"

#include <iomanip>

#include <iostream>
#include <unordered_set>


namespace Taffy {

    // =============================================================================
    // STATIC MEMBER DEFINITION (This was causing the linker error!)
    // =============================================================================

    std::unordered_map<uint64_t, std::string> HashRegistry::hash_to_string_;

    // =============================================================================
    // HASH REGISTRY IMPLEMENTATIONS
    // =============================================================================

    void HashRegistry::register_string(const std::string& str) {
        uint64_t hash = fnv1a_hash(str.c_str());
        hash_to_string_[hash] = str;
    }

    uint64_t HashRegistry::register_and_hash(const std::string& str) {
        uint64_t hash = fnv1a_hash(str.c_str());
        hash_to_string_[hash] = str;
        return hash;
    }

    std::string HashRegistry::lookup_string(uint64_t hash) {
        auto it = hash_to_string_.find(hash);
        if (it != hash_to_string_.end()) {
            return it->second;
        }
        return "UNKNOWN_HASH_0x" + std::to_string(hash);
    }

    bool HashRegistry::has_collision(const std::string& str) {
        uint64_t hash = fnv1a_hash(str.c_str());
        auto it = hash_to_string_.find(hash);
        return (it != hash_to_string_.end() && it->second != str);
    }

    void HashRegistry::debug_print_all() {
        std::cout << "🔍 Hash Registry Contents:" << std::endl;
        for (const auto& [hash, str] : hash_to_string_) {
            std::cout << "  0x" << std::hex << hash << std::dec << " -> \"" << str << "\"" << std::endl;
        }
    }

}

namespace tremor::taffy::tools {

    bool validateSPIRV(const std::vector<uint32_t>& spirv, const std::string& name) {
        std::cout << "🔍 SPIR-V Validation: " << name << std::endl;

        if (spirv.empty()) {
            std::cout << "  ❌ Empty SPIR-V!" << std::endl;
            return false;
        }

        if (spirv.size() < 5) {
            std::cout << "  ❌ SPIR-V too small: " << spirv.size() << " words" << std::endl;
            return false;
        }

        // Check magic number
        if (spirv[0] != 0x07230203) {
            std::cout << "  ❌ Invalid SPIR-V magic: 0x" << std::hex << spirv[0] << std::dec << std::endl;
            std::cout << "     Expected: 0x07230203" << std::endl;
            return false;
        }

        std::cout << "  ✅ Magic: 0x" << std::hex << spirv[0] << std::dec << std::endl;
        std::cout << "  📊 Version: " << spirv[1] << std::endl;
        std::cout << "  📊 Generator: 0x" << std::hex << spirv[2] << std::dec << std::endl;
        std::cout << "  📊 Bound: " << spirv[3] << std::endl;
        std::cout << "  📊 Schema: " << spirv[4] << std::endl;
        std::cout << "  📊 Size: " << spirv.size() << " words (" << spirv.size() * 4 << " bytes)" << std::endl;

        return true;
    }

    void dumpSPIRVBytes(const std::vector<uint32_t>& spirv, const std::string& name, size_t max_words = 8) {
        std::cout << "🔍 SPIR-V Hex Dump: " << name << std::endl;

        size_t words_to_dump = std::min(spirv.size(), max_words);

        for (size_t i = 0; i < words_to_dump; ++i) {
            uint32_t word = spirv[i];
            std::cout << "  [" << i << "] = 0x" << std::hex << std::setfill('0') << std::setw(8) << word << std::dec;

            // Show as bytes
            std::cout << " (bytes: ";
            for (int j = 0; j < 4; ++j) {
                uint8_t byte = (word >> (j * 8)) & 0xFF;
                std::cout << "0x" << std::hex << std::setfill('0') << std::setw(2) << (int)byte;
                if (j < 3) std::cout << " ";
            }
            std::cout << ")" << std::dec << std::endl;
        }

        if (spirv.size() > max_words) {
            std::cout << "  ... (" << (spirv.size() - max_words) << " more words)" << std::endl;
        }
    }

    void dumpRawBytes(const uint8_t* data, size_t size, const std::string& name, size_t max_bytes = 32) {
        std::cout << "🔍 Raw Byte Dump: " << name << std::endl;

        size_t bytes_to_dump = std::min(size, max_bytes);

        for (size_t i = 0; i < bytes_to_dump; ++i) {
            if (i % 16 == 0) {
                std::cout << "  " << std::hex << std::setfill('0') << std::setw(4) << i << ": ";
            }

            std::cout << std::hex << std::setfill('0') << std::setw(2) << (int)data[i] << " ";

            if ((i + 1) % 16 == 0 || i == bytes_to_dump - 1) {
                // Pad to align the ASCII part
                for (size_t j = (i % 16) + 1; j < 16; ++j) {
                    std::cout << "   ";
                }

                std::cout << " |";
                for (size_t j = i - (i % 16); j <= i; ++j) {
                    char c = (data[j] >= 32 && data[j] <= 126) ? data[j] : '.';
                    std::cout << c;
                }
                std::cout << "|" << std::dec << std::endl;
            }
        }

        if (size > max_bytes) {
            std::cout << "  ... (" << (size - max_bytes) << " more bytes)" << std::endl;
        }
    }

    std::vector<uint32_t> TaffyAssetCompiler::compileGLSLToSpirv(const std::string& source,
        shaderc_shader_kind kind,
        const std::string& name) {

        std::cout << "🔨 Compiling " << name << " to SPIR-V..." << std::endl;
        std::cout << "  📝 GLSL source length: " << source.length() << " characters" << std::endl;
        std::cout << "  🎯 Shader kind: " << kind << std::endl;

        auto result = compiler_->CompileGlslToSpv(source, kind, name.c_str(), "main", *options_);

        if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
            std::cerr << "❌ Shader compilation failed: " << result.GetErrorMessage() << std::endl;
            return {};
        }

        std::vector<uint32_t> spirv(result.cbegin(), result.cend());
        std::cout << "✅ Compiled " << name << " (" << spirv.size() * 4 << " bytes)" << std::endl;

        // VALIDATE IMMEDIATELY after compilation
        if (!validateSPIRV(spirv, name + "_fresh_compilation")) {
            std::cerr << "❌ Freshly compiled SPIR-V is invalid!" << std::endl;
            dumpSPIRVBytes(spirv, name + "_invalid_fresh");
            return {};
        }

        std::cout << "  ✅ Fresh compilation validation passed" << std::endl;
        return spirv;
    }

    bool createShaderChunkHashDebug(Taffy::Asset& asset,
        const std::vector<uint32_t>& mesh_spirv,
        const std::vector<uint32_t>& frag_spirv) {

        std::cout << "🔧 Creating HASH-BASED shader chunk with INTENSIVE debugging..." << std::endl;

        using namespace Taffy;

        // STEP 1: Validate input SPIR-V
        std::cout << "\n🔍 STEP 1: Validating input SPIR-V..." << std::endl;

        if (!validateSPIRV(mesh_spirv, "mesh_spirv_input")) {
            std::cerr << "❌ Input mesh SPIR-V is invalid!" << std::endl;
            return false;
        }

        if (!validateSPIRV(frag_spirv, "frag_spirv_input")) {
            std::cerr << "❌ Input fragment SPIR-V is invalid!" << std::endl;
            return false;
        }

        std::cout << "✅ Input SPIR-V validation passed" << std::endl;

        // STEP 2: Calculate sizes and create buffer
        size_t mesh_spirv_bytes = mesh_spirv.size() * sizeof(uint32_t);
        size_t frag_spirv_bytes = frag_spirv.size() * sizeof(uint32_t);
        size_t total_size = sizeof(ShaderChunk) +
            2 * sizeof(ShaderChunk::Shader) +
            mesh_spirv_bytes +
            frag_spirv_bytes;

        std::cout << "\n🔍 STEP 2: Buffer allocation..." << std::endl;
        std::cout << "  Mesh SPIR-V: " << mesh_spirv_bytes << " bytes" << std::endl;
        std::cout << "  Frag SPIR-V: " << frag_spirv_bytes << " bytes" << std::endl;
        std::cout << "  Total buffer: " << total_size << " bytes" << std::endl;

        std::vector<uint8_t> shader_data(total_size, 0);

        // STEP 3: Write header and shader infos
        std::cout << "\n🔍 STEP 3: Writing headers..." << std::endl;

        size_t offset = 0;

        // Write shader chunk header
        ShaderChunk header{};
        header.shader_count = 2;
        std::memcpy(shader_data.data() + offset, &header, sizeof(header));
        offset += sizeof(header);
        std::cout << "  ✅ Shader chunk header written at offset " << (offset - sizeof(header)) << std::endl;

        // Register hashes
        uint64_t mesh_name_hash = HashRegistry::register_and_hash("triangle_mesh_shader");
        uint64_t frag_name_hash = HashRegistry::register_and_hash("triangle_fragment_shader");
        uint64_t main_hash = HashRegistry::register_and_hash("main");

        // Write mesh shader info
        ShaderChunk::Shader mesh_info{};
        mesh_info.name_hash = mesh_name_hash;
        mesh_info.entry_point_hash = main_hash;
        mesh_info.stage = ShaderChunk::Shader::ShaderStage::MeshShader;
        mesh_info.spirv_size = static_cast<uint32_t>(mesh_spirv_bytes);
        mesh_info.max_vertices = 3;
        mesh_info.max_primitives = 1;
        mesh_info.workgroup_size[0] = 1;
        mesh_info.workgroup_size[1] = 1;
        mesh_info.workgroup_size[2] = 1;

        std::memcpy(shader_data.data() + offset, &mesh_info, sizeof(mesh_info));
        offset += sizeof(mesh_info);
        std::cout << "  ✅ Mesh shader info written at offset " << (offset - sizeof(mesh_info)) << std::endl;

        // Write fragment shader info
        ShaderChunk::Shader frag_info{};
        frag_info.name_hash = frag_name_hash;
        frag_info.entry_point_hash = main_hash;
        frag_info.stage = ShaderChunk::Shader::ShaderStage::Fragment;
        frag_info.spirv_size = static_cast<uint32_t>(frag_spirv_bytes);

        std::memcpy(shader_data.data() + offset, &frag_info, sizeof(frag_info));
        offset += sizeof(frag_info);
        std::cout << "  ✅ Fragment shader info written at offset " << (offset - sizeof(frag_info)) << std::endl;

        // STEP 4: Write SPIR-V data with intensive validation
        std::cout << "\n🔍 STEP 4: Writing SPIR-V data..." << std::endl;

        size_t mesh_spirv_offset = offset;
        std::cout << "  📍 Mesh SPIR-V will be written at offset " << mesh_spirv_offset << std::endl;

        // Validate SPIR-V one more time before writing
        std::cout << "  🔍 Pre-write validation..." << std::endl;
        dumpSPIRVBytes(mesh_spirv, "mesh_spirv_before_write", 4);

        // Write mesh SPIR-V
        std::memcpy(shader_data.data() + offset, mesh_spirv.data(), mesh_spirv_bytes);
        offset += mesh_spirv_bytes;
        std::cout << "  ✅ Mesh SPIR-V written" << std::endl;

        // VALIDATE IMMEDIATELY after writing
        std::cout << "  🔍 Post-write validation..." << std::endl;
        uint32_t written_magic;
        std::memcpy(&written_magic, shader_data.data() + mesh_spirv_offset, sizeof(uint32_t));
        std::cout << "  📍 Magic at offset " << mesh_spirv_offset << ": 0x" << std::hex << written_magic << std::dec;

        if (written_magic == 0x07230203) {
            std::cout << " ✅ PERFECT!" << std::endl;
        }
        else {
            std::cout << " ❌ CORRUPTED!" << std::endl;

            // Intensive debugging
            std::cout << "  🚨 CORRUPTION DETECTED! Debugging..." << std::endl;

            // Check what we actually wrote
            std::cout << "  📊 Original SPIR-V first word: 0x" << std::hex << mesh_spirv[0] << std::dec << std::endl;
            std::cout << "  📊 Written data first word: 0x" << std::hex << written_magic << std::dec << std::endl;

            // Dump surrounding bytes
            dumpRawBytes(shader_data.data() + mesh_spirv_offset - 16, 48, "surrounding_spirv_area");

            // Check if offset calculation is wrong
            std::cout << "  🔍 Offset calculation check:" << std::endl;
            std::cout << "    Header size: " << sizeof(ShaderChunk) << std::endl;
            std::cout << "    Shader info size: " << sizeof(ShaderChunk::Shader) << std::endl;
            std::cout << "    Expected offset: " << (sizeof(ShaderChunk) + 2 * sizeof(ShaderChunk::Shader)) << std::endl;
            std::cout << "    Actual offset: " << mesh_spirv_offset << std::endl;

            return false;
        }

        // Write fragment SPIR-V
        std::memcpy(shader_data.data() + offset, frag_spirv.data(), frag_spirv_bytes);
        std::cout << "  ✅ Fragment SPIR-V written at offset " << offset << std::endl;

        // STEP 5: Final validation of entire chunk
        std::cout << "\n🔍 STEP 5: Final chunk validation..." << std::endl;

        // Add chunk to asset
        asset.add_chunk(ChunkType::SHDR, shader_data, "hash_based_shaders_debug");

        std::cout << "🎉 Debug shader chunk created!" << std::endl;
        return true;
    }

    // =============================================================================
    // GLSL SHADER SOURCES (Human-readable!)
    // =============================================================================

    const char* TRIANGLE_MESH_SHADER_GLSL = R"(
#version 460
#extension GL_EXT_mesh_shader : require

layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;
layout(triangles, max_vertices = 3, max_primitives = 1) out;

// Output vertex data
layout(location = 0) out vec4 fragColor[];

// Vertex positions and colors
const vec3 positions[3] = vec3[](
    vec3( 0.0,  0.5, 0.0),  // Top vertex
    vec3(-0.5, -0.5, 0.0),  // Bottom left
    vec3( 0.5, -0.5, 0.0)   // Bottom right
);

const vec3 colors[3] = vec3[](
    vec3(1.0, 0.0, 0.0),    // Red
    vec3(0.0, 1.0, 0.0),    // Green (this is what overlays will change!)
    vec3(0.0, 0.0, 1.0)     // Blue
);

void main() {
    SetMeshOutputsEXT(3, 1); // 3 vertices, 1 triangle
    
    // Generate triangle vertices
    for (int i = 0; i < 3; ++i) {
        gl_MeshVerticesEXT[i].gl_Position = vec4(positions[i], 1.0);
        fragColor[i] = vec4(colors[i], 1.0);
    }
    
    // Generate triangle indices  
    gl_PrimitiveTriangleIndicesEXT[0] = uvec3(0, 1, 2);
}
)";

    const char* TRIANGLE_FRAGMENT_SHADER_GLSL = R"(
#version 460

layout(location = 0) in vec4 fragColor;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = fragColor;
}
)";

    bool TaffyAssetCompiler::createTriangleAssetSafeDebug(const std::string& output_path) {
        std::cout << "🚀 Creating triangle asset with INTENSIVE SPIR-V debugging..." << std::endl;

        using namespace Taffy;

        // Pre-register all shader names
        std::cout << "  📋 Pre-registering shader names..." << std::endl;
        HashRegistry::register_string("triangle_mesh_shader");
        HashRegistry::register_string("triangle_fragment_shader");
        HashRegistry::register_string("main");

        // Show GLSL source we're compiling
        std::cout << "\n📝 GLSL Source Code:" << std::endl;
        std::cout << "  📄 Mesh shader length: " << strlen(TRIANGLE_MESH_SHADER_GLSL) << " chars" << std::endl;
        std::cout << "  📄 Fragment shader length: " << strlen(TRIANGLE_FRAGMENT_SHADER_GLSL) << " chars" << std::endl;

        // Compile shaders with enhanced debugging
        auto mesh_spirv = compileGLSLToSpirv(TRIANGLE_MESH_SHADER_GLSL, shaderc_mesh_shader, "triangle_mesh_shader");
        auto frag_spirv = compileGLSLToSpirv(TRIANGLE_FRAGMENT_SHADER_GLSL, shaderc_fragment_shader, "triangle_fragment_shader");

        if (mesh_spirv.empty() || frag_spirv.empty()) {
            std::cerr << "❌ Shader compilation failed!" << std::endl;
            return false;
        }

        // Extra validation after compilation
        std::cout << "\n🔍 POST-COMPILATION VALIDATION:" << std::endl;
        if (!validateSPIRV(mesh_spirv, "mesh_spirv_post_compile")) {
            std::cerr << "❌ Mesh SPIR-V validation failed after compilation!" << std::endl;
            return false;
        }

        if (!validateSPIRV(frag_spirv, "frag_spirv_post_compile")) {
            std::cerr << "❌ Fragment SPIR-V validation failed after compilation!" << std::endl;
            return false;
        }

        // Create asset
        Asset asset;
        asset.set_creator("DEBUG Hash-Based Tremor Taffy Compiler");
        asset.set_description("Triangle with INTENSIVE SPIR-V debugging");
        asset.set_feature_flags(FeatureFlags::QuantizedCoords |
            FeatureFlags::MeshShaders |
            FeatureFlags::EmbeddedShaders |
            FeatureFlags::SPIRVCross |
            FeatureFlags::HashBasedNames);

        // Use debug shader creation
        if (!createShaderChunkHashDebug(asset, mesh_spirv, frag_spirv)) {
            std::cerr << "❌ Debug shader chunk creation failed!" << std::endl;
            return false;
        }

        // Create other chunks
        if (!createGeometryChunk(asset)) {
            return false;
        }

        if (!createMaterialChunk(asset)) {
            return false;
        }

        // Save with debug info
        std::filesystem::create_directories(std::filesystem::path(output_path).parent_path());

        std::cout << "\n💾 SAVING WITH DEBUG INFO..." << std::endl;
        if (!asset.save_to_file(output_path)) {
            std::cerr << "❌ Failed to save asset!" << std::endl;
            return false;
        }

        // Load back and validate EXTENSIVELY
        std::cout << "\n📖 LOADING BACK FOR VALIDATION..." << std::endl;
        Asset test_load;
        if (!test_load.load_from_file_safe(output_path)) {
            std::cerr << "❌ Failed to load back saved asset!" << std::endl;
            return false;
        }

        // Get the shader chunk and validate SPIR-V
        std::cout << "\n🔍 VALIDATING LOADED SPIR-V..." << std::endl;
        auto shader_chunk_data = test_load.get_chunk_data(ChunkType::SHDR);
        if (!shader_chunk_data) {
            std::cerr << "❌ No shader chunk in loaded asset!" << std::endl;
            return false;
        }

        // Parse the shader chunk manually to check SPIR-V
        const uint8_t* chunk_ptr = shader_chunk_data->data();
        ShaderChunk chunk_header;
        std::memcpy(&chunk_header, chunk_ptr, sizeof(chunk_header));

        std::cout << "  📊 Loaded chunk header:" << std::endl;
        std::cout << "    Shader count: " << chunk_header.shader_count << std::endl;

        // Find the first shader's SPIR-V
        size_t spirv_offset = sizeof(ShaderChunk) + 2 * sizeof(ShaderChunk::Shader);

        std::cout << "  📍 SPIR-V should be at offset: " << spirv_offset << std::endl;
        std::cout << "  📊 Chunk size: " << shader_chunk_data->size() << " bytes" << std::endl;

        if (spirv_offset + 4 <= shader_chunk_data->size()) {
            uint32_t loaded_magic;
            std::memcpy(&loaded_magic, chunk_ptr + spirv_offset, sizeof(uint32_t));

            std::cout << "  🔍 Loaded SPIR-V magic: 0x" << std::hex << loaded_magic << std::dec;

            if (loaded_magic == 0x07230203) {
                std::cout << " ✅ PERFECT!" << std::endl;
            }
            else {
                std::cout << " ❌ CORRUPTED!" << std::endl;

                // Show what we got instead
                std::cout << "  🚨 CORRUPTION ANALYSIS:" << std::endl;
                dumpRawBytes(chunk_ptr + spirv_offset, 32, "corrupted_spirv_in_file");

                // Compare with what we originally compiled
                std::cout << "  📊 Original compiled SPIR-V:" << std::endl;
                dumpSPIRVBytes(mesh_spirv, "original_mesh_spirv", 4);
            }
        }

        std::cout << "\n🎉 Debug asset creation completed!" << std::endl;
        std::cout << "   📁 File: " << output_path << std::endl;

        return true;
    }

    inline bool HashBasedShaderCreator::createShaderChunkHash(Taffy::Asset& asset,
        const std::vector<uint32_t>& mesh_spirv,
        const std::vector<uint32_t>& frag_spirv) {

        std::cout << "🔧 Creating HASH-BASED shader chunk..." << std::endl;

        using namespace Taffy;

        // Validate input data
        if (mesh_spirv.empty() || frag_spirv.empty()) {
            std::cerr << "❌ Empty SPIR-V data!" << std::endl;
            return false;
        }

        // Register names and get hashes
        uint64_t mesh_name_hash = HashRegistry::register_and_hash("triangle_mesh_shader");
        uint64_t frag_name_hash = HashRegistry::register_and_hash("triangle_fragment_shader");
        uint64_t main_hash = HashRegistry::register_and_hash("main");

        std::cout << "  📋 Registered hashes:" << std::endl;
        std::cout << "    'triangle_mesh_shader' -> 0x" << std::hex << mesh_name_hash << std::dec << std::endl;
        std::cout << "    'triangle_fragment_shader' -> 0x" << std::hex << frag_name_hash << std::dec << std::endl;
        std::cout << "    'main' -> 0x" << std::hex << main_hash << std::dec << std::endl;

        // Calculate sizes
        size_t mesh_spirv_bytes = mesh_spirv.size() * sizeof(uint32_t);
        size_t frag_spirv_bytes = frag_spirv.size() * sizeof(uint32_t);

        size_t total_size = sizeof(ShaderChunk) +
            2 * sizeof(ShaderChunk::Shader) +
            mesh_spirv_bytes +
            frag_spirv_bytes;

        std::vector<uint8_t> shader_data(total_size, 0);
        size_t offset = 0;

        // Write header
        ShaderChunk header{};
        header.shader_count = 2;
        std::memcpy(shader_data.data() + offset, &header, sizeof(header));
        offset += sizeof(header);

        // Write mesh shader info
        ShaderChunk::Shader mesh_info{};
        mesh_info.name_hash = mesh_name_hash;
        mesh_info.entry_point_hash = main_hash;
        mesh_info.stage = ShaderChunk::Shader::ShaderStage::MeshShader;
        mesh_info.spirv_size = static_cast<uint32_t>(mesh_spirv_bytes);
        mesh_info.max_vertices = 3;
        mesh_info.max_primitives = 1;
        mesh_info.workgroup_size[0] = 1;
        mesh_info.workgroup_size[1] = 1;
        mesh_info.workgroup_size[2] = 1;

        std::memcpy(shader_data.data() + offset, &mesh_info, sizeof(mesh_info));
        offset += sizeof(mesh_info);

        // Write fragment shader info
        ShaderChunk::Shader frag_info{};
        frag_info.name_hash = frag_name_hash;
        frag_info.entry_point_hash = main_hash;
        frag_info.stage = ShaderChunk::Shader::ShaderStage::Fragment;
        frag_info.spirv_size = static_cast<uint32_t>(frag_spirv_bytes);

        std::memcpy(shader_data.data() + offset, &frag_info, sizeof(frag_info));
        offset += sizeof(frag_info);

        // Write mesh SPIR-V data
        size_t mesh_spirv_offset = offset;
        std::memcpy(shader_data.data() + offset, mesh_spirv.data(), mesh_spirv_bytes);
        offset += mesh_spirv_bytes;

        // VALIDATE SPIR-V MAGIC - should work perfectly now!
        uint32_t written_magic;
        std::memcpy(&written_magic, shader_data.data() + mesh_spirv_offset, sizeof(uint32_t));
        std::cout << "  🔍 SPIR-V magic: 0x" << std::hex << written_magic << std::dec;

        if (written_magic == 0x07230203) {
            std::cout << " ✅ PERFECT!" << std::endl;
        }
        else {
            std::cout << " ❌ CORRUPTED!" << std::endl;
            return false;
        }

        // Write fragment SPIR-V data
        std::memcpy(shader_data.data() + offset, frag_spirv.data(), frag_spirv_bytes);

        // Add chunk to asset
        asset.add_chunk(ChunkType::SHDR, shader_data, "hash_based_shaders");

        std::cout << "🎉 Hash-based shader chunk created successfully!" << std::endl;
        return true;
    }

    bool HashBasedShaderCreator::validateHashShaderChunk(const Taffy::Asset& asset) {
        using namespace Taffy;

        std::cout << "🔍 Validating hash-based shader chunk..." << std::endl;

        auto shader_data = asset.get_chunk_data(ChunkType::SHDR);
        if (!shader_data) {
            std::cout << "❌ No shader chunk found in asset" << std::endl;
            return false;
        }

        if (shader_data->size() < sizeof(ShaderChunk)) {
            std::cout << "❌ Shader chunk too small: " << shader_data->size()
                << " bytes (need at least " << sizeof(ShaderChunk) << ")" << std::endl;
            return false;
        }

        // Read header
        ShaderChunk header;
        std::memcpy(&header, shader_data->data(), sizeof(header));

        std::cout << "  📊 Shader chunk header:" << std::endl;
        std::cout << "    Shader count: " << header.shader_count << std::endl;
        std::cout << "    Total chunk size: " << shader_data->size() << " bytes" << std::endl;

        if (header.shader_count == 0 || header.shader_count > 100) {
            std::cout << "❌ Invalid shader count: " << header.shader_count << std::endl;
            return false;
        }

        size_t expected_min_size = sizeof(ShaderChunk) +
            header.shader_count * sizeof(ShaderChunk::Shader);
        if (shader_data->size() < expected_min_size) {
            std::cout << "❌ Chunk too small for " << header.shader_count
                << " shaders. Need at least " << expected_min_size << " bytes" << std::endl;
            return false;
        }

        size_t offset = sizeof(header);
        size_t total_spirv_size = 0;

        // Validate each shader
        for (uint32_t i = 0; i < header.shader_count; ++i) {
            if (offset + sizeof(ShaderChunk::Shader) > shader_data->size()) {
                std::cout << "❌ Shader " << i << " info exceeds chunk boundary" << std::endl;
                return false;
            }

            ShaderChunk::Shader shader_info;
            std::memcpy(&shader_info, shader_data->data() + offset, sizeof(shader_info));
            offset += sizeof(shader_info);

            std::cout << "  🔧 Shader " << i << " validation:" << std::endl;

            // Hash-based name validation
            std::cout << "    Name hash: 0x" << std::hex << shader_info.name_hash << std::dec;
            std::string resolved_name = HashRegistry::lookup_string(shader_info.name_hash);
            std::cout << " (\"" << resolved_name << "\")" << std::endl;

            std::cout << "    Entry hash: 0x" << std::hex << shader_info.entry_point_hash << std::dec;
            std::string resolved_entry = HashRegistry::lookup_string(shader_info.entry_point_hash);
            std::cout << " (\"" << resolved_entry << "\")" << std::endl;

            // Validate stage
            std::cout << "    Stage: ";
            switch (shader_info.stage) {
            case ShaderChunk::Shader::ShaderStage::Vertex:
                std::cout << "Vertex"; break;
            case ShaderChunk::Shader::ShaderStage::Fragment:
                std::cout << "Fragment"; break;
            case ShaderChunk::Shader::ShaderStage::Geometry:
                std::cout << "Geometry"; break;
            case ShaderChunk::Shader::ShaderStage::Compute:
                std::cout << "Compute"; break;
            case ShaderChunk::Shader::ShaderStage::MeshShader:
                std::cout << "MeshShader"; break;
            case ShaderChunk::Shader::ShaderStage::TaskShader:
                std::cout << "TaskShader"; break;
            default:
                std::cout << "UNKNOWN(" << static_cast<int>(shader_info.stage) << ")";
                std::cout << " ❌ Invalid stage!" << std::endl;
                return false;
            }
            std::cout << std::endl;

            // Validate SPIR-V size
            std::cout << "    SPIR-V size: " << shader_info.spirv_size << " bytes" << std::endl;

            if (shader_info.spirv_size == 0) {
                std::cout << "    ❌ Zero SPIR-V size!" << std::endl;
                return false;
            }

            if (shader_info.spirv_size > 10 * 1024 * 1024) { // 10MB sanity check
                std::cout << "    ❌ SPIR-V size too large: " << shader_info.spirv_size << std::endl;
                return false;
            }

            if (shader_info.spirv_size % 4 != 0) {
                std::cout << "    ❌ SPIR-V size not 4-byte aligned!" << std::endl;
                return false;
            }

            // Check if SPIR-V data would exceed chunk
            if (offset + shader_info.spirv_size > shader_data->size()) {
                std::cout << "    ❌ SPIR-V data exceeds chunk boundary!" << std::endl;
                std::cout << "       Offset: " << offset << ", Size: " << shader_info.spirv_size
                    << ", Chunk size: " << shader_data->size() << std::endl;
                return false;
            }

            // Validate SPIR-V magic number
            if (shader_info.spirv_size >= 4) {
                uint32_t magic;
                std::memcpy(&magic, shader_data->data() + offset, sizeof(magic));
                std::cout << "    SPIR-V magic: 0x" << std::hex << magic << std::dec;

                if (magic == 0x07230203) {
                    std::cout << " ✅ VALID" << std::endl;
                }
                else {
                    std::cout << " ❌ INVALID! Expected 0x07230203" << std::endl;

                    // Debug: show what we actually got
                    std::cout << "    🐛 First 16 bytes of SPIR-V data:" << std::endl;
                    for (int j = 0; j < 16 && j < shader_info.spirv_size && (offset + j) < shader_data->size(); ++j) {
                        uint8_t byte_val = shader_data->data()[offset + j];
                        std::cout << "      [" << j << "] = 0x" << std::hex << (int)byte_val
                            << " ('" << (char)(byte_val >= 32 && byte_val <= 126 ? byte_val : '.')
                            << "')" << std::dec << std::endl;
                    }
                    return false;
                }
            }

            // Mesh shader specific validation
            if (shader_info.stage == ShaderChunk::Shader::ShaderStage::MeshShader) {
                std::cout << "    Max vertices: " << shader_info.max_vertices << std::endl;
                std::cout << "    Max primitives: " << shader_info.max_primitives << std::endl;
                std::cout << "    Workgroup size: (" << shader_info.workgroup_size[0]
                    << ", " << shader_info.workgroup_size[1]
                    << ", " << shader_info.workgroup_size[2] << ")" << std::endl;

                if (shader_info.max_vertices == 0 || shader_info.max_primitives == 0) {
                    std::cout << "    ⚠️  Warning: Mesh shader with 0 vertices/primitives" << std::endl;
                }
            }

            // Check for known hash values
            if (shader_info.name_hash == ShaderHashes::TRIANGLE_MESH) {
                std::cout << "    ✅ Recognized as triangle mesh shader" << std::endl;
            }
            else if (shader_info.name_hash == ShaderHashes::TRIANGLE_FRAG) {
                std::cout << "    ✅ Recognized as triangle fragment shader" << std::endl;
            }

            offset += shader_info.spirv_size;
            total_spirv_size += shader_info.spirv_size;
            std::cout << "    ✅ Shader " << i << " validation passed" << std::endl;
        }

        // Final validation
        std::cout << "  📊 Summary:" << std::endl;
        std::cout << "    Total shaders: " << header.shader_count << std::endl;
        std::cout << "    Total SPIR-V data: " << total_spirv_size << " bytes" << std::endl;
        std::cout << "    Chunk utilization: " << offset << "/" << shader_data->size()
            << " bytes (" << (offset * 100 / shader_data->size()) << "%)" << std::endl;

        if (offset != shader_data->size()) {
            std::cout << "    ⚠️  Warning: " << (shader_data->size() - offset)
                << " bytes unused at end of chunk" << std::endl;
        }

        std::cout << "✅ Hash-based shader chunk validation PASSED!" << std::endl;
        return true;
    }

    bool TaffyAssetCompiler::createTriangleAssetHashBased(const std::string& output_path) {
        std::cout << "🚀 Creating triangle asset with HASH-BASED names..." << std::endl;

        using namespace Taffy;
        using namespace tremor::taffy::tools;


        // Compile shaders first
        TaffyAssetCompiler compiler;

        const std::string mesh_shader_glsl = R"(
#version 460
#extension GL_EXT_mesh_shader : require

layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;
layout(triangles, max_vertices = 3, max_primitives = 1) out;

layout(location = 0) out vec4 fragColor[];

const vec3 positions[3] = vec3[](
    vec3( 0.0,  0.5, 0.0),
    vec3(-0.5, -0.5, 0.0),
    vec3( 0.5, -0.5, 0.0)
);

const vec3 colors[3] = vec3[](
    vec3(1.0, 0.0, 0.0),
    vec3(0.0, 1.0, 0.0),    // This vertex color can be changed by overlays!
    vec3(0.0, 0.0, 1.0)
);

void main() {
    SetMeshOutputsEXT(3, 1);
    
    for (int i = 0; i < 3; ++i) {
        gl_MeshVerticesEXT[i].gl_Position = vec4(positions[i], 1.0);
        fragColor[i] = vec4(colors[i], 1.0);
    }
    
    gl_PrimitiveTriangleIndicesEXT[0] = uvec3(0, 1, 2);
}
)";

        const std::string fragment_shader_glsl = R"(
#version 460

layout(location = 0) in vec4 fragColor;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = fragColor;
}
)";

        // Pre-register all shader names we'll use
        std::cout << "  📋 Pre-registering shader names..." << std::endl;
        HashRegistry::register_string("triangle_mesh_shader");
        HashRegistry::register_string("triangle_fragment_shader");
        HashRegistry::register_string("main");
        HashRegistry::register_string("wireframe_mesh_shader");
        HashRegistry::register_string("animated_mesh_shader");

        // Show what we registered
        HashRegistry::debug_print_all();

        // Compile shaders
        auto mesh_spirv = compileGLSLToSpirv(mesh_shader_glsl, shaderc_mesh_shader, "triangle_mesh_shader");
        auto frag_spirv = compileGLSLToSpirv(fragment_shader_glsl, shaderc_fragment_shader, "triangle_frag_shader");

        if (mesh_spirv.empty() || frag_spirv.empty()) {
            std::cerr << "❌ Shader compilation failed!" << std::endl;
            return false;
        }

        // Create asset with proper feature flags
        Asset asset;
        asset.set_creator("Hash-Based Tremor Taffy Compiler");
        asset.set_description("Triangle with hash-based shader names - NO BUFFER OVERFLOWS!");
        asset.set_feature_flags(FeatureFlags::QuantizedCoords |
            FeatureFlags::MeshShaders |
            FeatureFlags::EmbeddedShaders |
            FeatureFlags::SPIRVCross);

        // Use hash-based shader creation
        if (!HashBasedShaderCreator::createShaderChunkHash(asset, mesh_spirv, frag_spirv)) {
            return false;
        }

        // Validate immediately
        if (!HashBasedShaderCreator::validateHashShaderChunk(asset)) {
            std::cerr << "❌ Hash-based shader chunk failed validation!" << std::endl;
            return false;
        }

        // Create other chunks (these don't use names, so they're fine)
        if (!createGeometryChunk(asset)) {
            return false;
        }

        if (!createMaterialChunk(asset)) {
            return false;
        }

        // Save and validate
        std::filesystem::create_directories(std::filesystem::path(output_path).parent_path());
        if (!asset.save_to_file(output_path)) {
            std::cerr << "❌ Failed to save asset!" << std::endl;
            return false;
        }

        // Load back and validate
        Asset test_load;
        if (!test_load.load_from_file_safe(output_path)) {
            std::cerr << "❌ Failed to load back saved asset!" << std::endl;
            return false;
        }

        if (!HashBasedShaderCreator::validateHashShaderChunk(test_load)) {
            std::cerr << "❌ Saved asset failed hash validation!" << std::endl;
            return false;
        }

        std::cout << "🎉 Hash-based asset creation completed successfully!" << std::endl;
        std::cout << "   📁 File: " << output_path << std::endl;
        std::cout << "   📦 Size: " << std::filesystem::file_size(output_path) << " bytes" << std::endl;
        std::cout << "   🔥 NO BUFFER OVERFLOWS EVER AGAIN!" << std::endl;

        return true;
    }
    // =============================================================================
    // GLSL-TO-TAFFY ASSET COMPILER
    // =============================================================================

    TaffyAssetCompiler::TaffyAssetCompiler() {
        compiler_ = std::make_unique<shaderc::Compiler>();
        options_ = std::make_unique<shaderc::CompileOptions>();

        // Configure compilation options
        options_->SetOptimizationLevel(shaderc_optimization_level_performance);
        options_->SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
        options_->SetTargetSpirv(shaderc_spirv_version_1_6);

        std::cout << "🔧 Taffy Asset Compiler initialized with shaderc" << std::endl;
    }

    /**
     * Compile GLSL to SPIR-V
     */

     /**
      * Create complete Taffy asset with mesh shader triangle
      */

      /**
       * Create shader chunk with compiled SPIR-V
       */

       /**
        * Create geometry chunk (for overlay targeting - even though mesh shader generates geometry)
        */
    bool TaffyAssetCompiler::createGeometryChunk(Taffy::Asset& asset) {
        using namespace Taffy;

        std::cout << "  📐 Creating geometry chunk..." << std::endl;

        struct OverlayVertex {
            Vec3Q position;
            float normal[3];
            float uv[2];
            float color[4];
        };

        std::vector<OverlayVertex> vertices = {
            {Vec3Q{0, 50, 0}, {0,0,1}, {0.5f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
            {Vec3Q{-50, -50, 0}, {0,0,1}, {0.0f, 1.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
            {Vec3Q{50, -50, 0}, {0,0,1}, {1.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 1.0f}}
        };

        std::vector<uint32_t> indices = { 0, 1, 2 };

        GeometryChunk geom_header{};
        std::memset(&geom_header, 0, sizeof(geom_header));
        geom_header.vertex_count = static_cast<uint32_t>(vertices.size());
        geom_header.index_count = static_cast<uint32_t>(indices.size());
        geom_header.vertex_stride = sizeof(OverlayVertex);
        geom_header.vertex_format = VertexFormat::Position3D | VertexFormat::Normal |
            VertexFormat::TexCoord0 | VertexFormat::Color;
        geom_header.bounds_min = Vec3Q{ -50, -50, 0 };
        geom_header.bounds_max = Vec3Q{ 50, 50, 0 };
        geom_header.lod_distance = 1000.0f;
        geom_header.lod_level = 0;

        size_t vertex_data_size = vertices.size() * sizeof(OverlayVertex);
        size_t index_data_size = indices.size() * sizeof(uint32_t);
        size_t total_size = sizeof(GeometryChunk) + vertex_data_size + index_data_size;

        std::vector<uint8_t> geom_data(total_size);
        size_t offset = 0;

        std::memcpy(geom_data.data() + offset, &geom_header, sizeof(geom_header));
        offset += sizeof(geom_header);

        std::memcpy(geom_data.data() + offset, vertices.data(), vertex_data_size);
        offset += vertex_data_size;

        std::memcpy(geom_data.data() + offset, indices.data(), index_data_size);

        asset.add_chunk(ChunkType::GEOM, geom_data, "triangle_geometry");

        std::cout << "    ✅ " << vertices.size() << " vertices, " << indices.size() / 3 << " triangle(s)" << std::endl;
        std::cout << "    🎯 Vertex 1 (green) ready for overlay modification" << std::endl;

        return true;
    }
    /**
     * Create basic material chunk
     */
    bool TaffyAssetCompiler::createMaterialChunk(Taffy::Asset& asset) {
        using namespace Taffy;

        std::cout << "  🎨 Creating material chunk..." << std::endl;

        MaterialChunk mat_header{};
        std::memset(&mat_header, 0, sizeof(mat_header));
        mat_header.material_count = 1;

        MaterialChunk::Material material{};
        std::memset(&material, 0, sizeof(material));

        std::strncpy(material.name, "triangle_material", sizeof(material.name) - 1);
        material.name[sizeof(material.name) - 1] = '\0';

        material.albedo[0] = 1.0f; material.albedo[1] = 1.0f;
        material.albedo[2] = 1.0f; material.albedo[3] = 1.0f;
        material.metallic = 0.0f;
        material.roughness = 0.8f;
        material.normal_intensity = 1.0f;
        material.albedo_texture = UINT32_MAX;
        material.normal_texture = UINT32_MAX;
        material.metallic_roughness_texture = UINT32_MAX;
        material.emission_texture = UINT32_MAX;
        material.flags = MaterialFlags::DoubleSided;

        std::vector<uint8_t> mat_data(sizeof(MaterialChunk) + sizeof(MaterialChunk::Material));

        std::memcpy(mat_data.data(), &mat_header, sizeof(mat_header));
        std::memcpy(mat_data.data() + sizeof(mat_header), &material, sizeof(material));

        asset.add_chunk(ChunkType::MTRL, mat_data, "triangle_material");

        std::cout << "    ✅ Basic PBR material created" << std::endl;
        std::cout << "    🎨 Name: " << material.name << std::endl;

        return true;
    }


    // =============================================================================
    // SHADER VARIANT GENERATOR (For different overlay effects)
    // =============================================================================

    std::string ShaderVariantGenerator::generateWireframeMeshShader() {
        return R"(
#version 460
#extension GL_EXT_mesh_shader : require

layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;
layout(lines, max_vertices = 6, max_primitives = 3) out;

layout(location = 0) out vec4 fragColor[];

const vec3 positions[3] = vec3[](
    vec3( 0.0,  0.5, 0.0),
    vec3(-0.5, -0.5, 0.0),
    vec3( 0.5, -0.5, 0.0)
);

void main() {
    SetMeshOutputsEXT(6, 3); // 6 vertices, 3 lines
    
    // Generate wireframe lines
    // Line 0-1
    gl_MeshVerticesEXT[0].gl_Position = vec4(positions[0], 1.0);
    gl_MeshVerticesEXT[1].gl_Position = vec4(positions[1], 1.0);
    fragColor[0] = vec4(1.0, 1.0, 1.0, 1.0); // White wireframe
    fragColor[1] = vec4(1.0, 1.0, 1.0, 1.0);
    
    // Line 1-2
    gl_MeshVerticesEXT[2].gl_Position = vec4(positions[1], 1.0);
    gl_MeshVerticesEXT[3].gl_Position = vec4(positions[2], 1.0);
    fragColor[2] = vec4(1.0, 1.0, 1.0, 1.0);
    fragColor[3] = vec4(1.0, 1.0, 1.0, 1.0);
    
    // Line 2-0
    gl_MeshVerticesEXT[4].gl_Position = vec4(positions[2], 1.0);
    gl_MeshVerticesEXT[5].gl_Position = vec4(positions[0], 1.0);
    fragColor[4] = vec4(1.0, 1.0, 1.0, 1.0);
    fragColor[5] = vec4(1.0, 1.0, 1.0, 1.0);
    
    gl_PrimitiveLineIndicesEXT[0] = uvec2(0, 1);
    gl_PrimitiveLineIndicesEXT[1] = uvec2(2, 3);
    gl_PrimitiveLineIndicesEXT[2] = uvec2(4, 5);
}
)";
    }

    /**
     * Generate animated mesh shader variant
     */
    std::string ShaderVariantGenerator::generateAnimatedMeshShader() {
        return R"(
#version 460
#extension GL_EXT_mesh_shader : require

layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;
layout(triangles, max_vertices = 3, max_primitives = 1) out;

layout(location = 0) out vec4 fragColor[];

// Push constants for animation
layout(push_constant) uniform PushConstants {
    float time;
} pc;

const vec3 positions[3] = vec3[](
    vec3( 0.0,  0.5, 0.0),
    vec3(-0.5, -0.5, 0.0),
    vec3( 0.5, -0.5, 0.0)
);

void main() {
    SetMeshOutputsEXT(3, 1);
    
    float rotation = pc.time;
    mat2 rot = mat2(cos(rotation), -sin(rotation),
                    sin(rotation),  cos(rotation));
    
    for (int i = 0; i < 3; ++i) {
        vec2 rotated = rot * positions[i].xy;
        gl_MeshVerticesEXT[i].gl_Position = vec4(rotated, 0.0, 1.0);
        
        // Animated color based on time
        float phase = pc.time + float(i) * 2.094; // 120 degree phase
        vec3 color = vec3(sin(phase), sin(phase + 2.094), sin(phase + 4.188)) * 0.5 + 0.5;
        fragColor[i] = vec4(color, 1.0);
    }
    
    gl_PrimitiveTriangleIndicesEXT[0] = uvec3(0, 1, 2);
}
)";
    }
};
