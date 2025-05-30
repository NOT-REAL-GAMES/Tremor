// tremor/src/renderer/taffy_mesh.cpp
#include "taffy_mesh.h"
#include "../vk.h"
#include <iostream>
#include <cstring>

namespace Tremor {

    TaffyMesh::~TaffyMesh() {
        // Resources are managed by the clustered renderer
        // No manual cleanup needed here
    }

    bool TaffyMesh::load_from_asset(const Taffy::Asset& asset) {
        using namespace Taffy;

        std::cout << "🔄 Loading mesh from Taffy asset..." << std::endl;

        // Get geometry chunk data
        auto chunk_data = asset.get_chunk_data(ChunkType::GEOM);
        if (!chunk_data) {
            std::cerr << "❌ Taffy asset contains no geometry chunk" << std::endl;
            return false;
        }

        // Validate chunk size
        if (chunk_data->size() < sizeof(GeometryChunk)) {
            std::cerr << "❌ Geometry chunk too small: " << chunk_data->size()
                << " bytes (need at least " << sizeof(GeometryChunk) << ")" << std::endl;
            return false;
        }

        // Parse geometry chunk header
        const uint8_t* chunk_ptr = chunk_data->data();
        GeometryChunk geometry_header;
        std::memcpy(&geometry_header, chunk_ptr, sizeof(GeometryChunk));

        std::cout << "  📊 Geometry info:" << std::endl;
        std::cout << "    Vertices: " << geometry_header.vertex_count << std::endl;
        std::cout << "    Indices: " << geometry_header.index_count << std::endl;
        std::cout << "    Vertex stride: " << geometry_header.vertex_stride << " bytes" << std::endl;
        std::cout << "    LOD level: " << geometry_header.lod_level << std::endl;

        // Validate geometry data
        size_t expected_vertex_data_size = geometry_header.vertex_count * geometry_header.vertex_stride;
        size_t expected_index_data_size = geometry_header.index_count * sizeof(uint32_t);
        size_t expected_total_size = sizeof(GeometryChunk) + expected_vertex_data_size + expected_index_data_size;

        if (chunk_data->size() < expected_total_size) {
            std::cerr << "❌ Geometry chunk data incomplete:" << std::endl;
            std::cerr << "   Expected: " << expected_total_size << " bytes" << std::endl;
            std::cerr << "   Actual: " << chunk_data->size() << " bytes" << std::endl;
            return false;
        }

        // Calculate data pointers
        const uint8_t* vertex_data = chunk_ptr + sizeof(GeometryChunk);
        const uint8_t* index_data = vertex_data + expected_vertex_data_size;

        // Store mesh metadata
        vertex_count_ = geometry_header.vertex_count;
        index_count_ = geometry_header.index_count;
        bounds_min_ = geometry_header.bounds_min;
        bounds_max_ = geometry_header.bounds_max;

        // Check vertex format and parse accordingly
        VertexFormat format = geometry_header.vertex_format;
        std::cout << "  🔧 Vertex format flags: 0x" << std::hex << static_cast<uint32_t>(format) << std::dec << std::endl;

        // Determine vertex structure based on format flags
        bool has_position = (format & VertexFormat::Position3D) == VertexFormat::Position3D;
        bool has_normal = (format & VertexFormat::Normal) == VertexFormat::Normal;
        bool has_texcoord = (format & VertexFormat::TexCoord0) == VertexFormat::TexCoord0;
        bool has_color = (format & VertexFormat::Color) == VertexFormat::Color;
        bool has_tangent = (format & VertexFormat::Tangent) == VertexFormat::Tangent;

        std::cout << "  📋 Vertex components:" << std::endl;
        if (has_position) std::cout << "    ✅ Position (Vec3Q)" << std::endl;
        if (has_normal) std::cout << "    ✅ Normal (float[3])" << std::endl;
        if (has_texcoord) std::cout << "    ✅ TexCoord (float[2])" << std::endl;
        if (has_color) std::cout << "    ✅ Color (float[4])" << std::endl;
        if (has_tangent) std::cout << "    ✅ Tangent (float[4])" << std::endl;

        if (!has_position) {
            std::cerr << "❌ Geometry chunk missing position data!" << std::endl;
            return false;
        }

        // Define the actual Taffy vertex structure (matches what we created in tools.cpp)
        struct TaffyVertex {
            Vec3Q position;      // Quantized position (always present)
            float normal[3];     // Normal vector (if has_normal)
            float uv[2];         // Texture coordinates (if has_texcoord)
            float color[4];      // Vertex color (if has_color)
            // Note: No tangent in our current implementation
        };

        // Validate stride matches expected structure
        size_t expected_stride = sizeof(Vec3Q);  // Position always present
        if (has_normal) expected_stride += 3 * sizeof(float);
        if (has_texcoord) expected_stride += 2 * sizeof(float);
        if (has_color) expected_stride += 4 * sizeof(float);
        if (has_tangent) expected_stride += 4 * sizeof(float);

        if (geometry_header.vertex_stride != expected_stride) {
            std::cout << "⚠️  Warning: Vertex stride mismatch!" << std::endl;
            std::cout << "   Expected: " << expected_stride << " bytes" << std::endl;
            std::cout << "   Actual: " << geometry_header.vertex_stride << " bytes" << std::endl;
            std::cout << "   Proceeding with dynamic parsing..." << std::endl;
        }

        // Parse vertices with dynamic format handling
        vertices_.clear();
        vertices_.reserve(geometry_header.vertex_count);

        for (uint32_t i = 0; i < geometry_header.vertex_count; ++i) {
            const uint8_t* vertex_ptr = vertex_data + (i * geometry_header.vertex_stride);
            size_t offset = 0;

            tremor::gfx::MeshVertex vertex{};

            // Parse position (always present)
            if (has_position) {
                Vec3Q quantized_pos;
                std::memcpy(&quantized_pos, vertex_ptr + offset, sizeof(Vec3Q));
                offset += sizeof(Vec3Q);

                // Convert quantized position to float
                vertex.position = quantized_pos;
            }

            // Parse normal (if present)
            if (has_normal) {
                float normal[3];
                std::memcpy(normal, vertex_ptr + offset, 3 * sizeof(float));
                offset += 3 * sizeof(float);
                vertex.normal = glm::vec3(normal[0], normal[1], normal[2]);
            }
            else {
                vertex.normal = glm::vec3(0.0f, 0.0f, 1.0f); // Default normal
            }

            // Parse texture coordinates (if present)
            if (has_texcoord) {
                float uv[2];
                std::memcpy(uv, vertex_ptr + offset, 2 * sizeof(float));
                offset += 2 * sizeof(float);
                vertex.texCoord = glm::vec2(uv[0], uv[1]);
            }
            else {
                vertex.texCoord = glm::vec2(0.0f, 0.0f); // Default UV
            }

            // Parse vertex color (if present) - can be used for debugging/effects
            if (has_color) {
                float color[4];
                std::memcpy(color, vertex_ptr + offset, 4 * sizeof(float));
                offset += 4 * sizeof(float);
                // Store color in vertex (you might need to add a color field to MeshVertex)
                // vertex.color = glm::vec4(color[0], color[1], color[2], color[3]);
            }

            // Parse tangent (if present)
            if (has_tangent) {
                float tangent[4];
                std::memcpy(tangent, vertex_ptr + offset, 4 * sizeof(float));
                offset += 4 * sizeof(float);
                //vertex.tangent = glm::vec4(tangent[0], tangent[1], tangent[2], tangent[3]);
            }
            else {
                //vertex.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f); // Default tangent
            }

            vertices_.push_back(vertex);
        }

        // Parse indices
        indices_.clear();
        indices_.reserve(geometry_header.index_count);

        const uint32_t* taffy_indices = reinterpret_cast<const uint32_t*>(index_data);
        for (uint32_t i = 0; i < geometry_header.index_count; ++i) {
            indices_.push_back(taffy_indices[i]);
        }

        // Convert quantized bounds to float for display

        bounds_min_ = geometry_header.bounds_min;
        bounds_max_ = geometry_header.bounds_max;

        std::cout << "✅ Successfully loaded Taffy mesh:" << std::endl;
        std::cout << "   📊 " << vertices_.size() << " vertices, " << indices_.size() << " indices" << std::endl;
        std::cout << "   📏 Bounds: (" << bounds_min_.x << ", " << bounds_min_.y << ", " << bounds_min_.z
            << ") to (" << bounds_max_.x << ", " << bounds_max_.y << ", " << bounds_max_.z << ")" << std::endl;
        std::cout << "   🎯 Triangle count: " << indices_.size() / 3 << std::endl;

        return true;
    }
    uint32_t TaffyMesh::upload_to_renderer(tremor::gfx::VulkanClusteredRenderer& renderer,
        const std::string& name) {
        if (vertices_.empty()) {
            std::cerr << "No vertex data to upload" << std::endl;
            return UINT32_MAX;
        }

        // Use the clustered renderer's existing mesh loading system
        return renderer.loadMesh(vertices_, indices_, name);
    }

    glm::vec3 TaffyMesh::get_bounds_min() const {
        return bounds_min_.toFloat();
    }

    glm::vec3 TaffyMesh::get_bounds_max() const {
        return bounds_max_.toFloat();
    }

} // namespace Tremor