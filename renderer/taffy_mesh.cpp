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
        // Get geometry chunk
        auto geometry_opt = asset.get_geometry();
        if (!geometry_opt) {
            std::cerr << "Taffy asset contains no geometry chunk" << std::endl;
            return false;
        }

        const auto& geometry = *geometry_opt;

        std::cout << "Loading Taffy geometry: "
            << geometry.vertex_count << " vertices, "
            << geometry.index_count << " indices" << std::endl;

        // Get the raw chunk data to access vertex and index arrays
        auto chunk_data = asset.get_chunk_data(Taffy::ChunkType::GEOM);
        if (!chunk_data) {
            std::cerr << "Failed to get geometry chunk data" << std::endl;
            return false;
        }

        // Calculate offsets within the chunk
        const uint8_t* chunk_ptr = chunk_data->data();
        const uint8_t* vertex_data = chunk_ptr + sizeof(Taffy::GeometryChunk);
        const uint8_t* index_data = vertex_data + (geometry.vertex_count * geometry.vertex_stride);

        // Store mesh info for bounds checking
        vertex_count_ = geometry.vertex_count;
        index_count_ = geometry.index_count;
        bounds_min_ = geometry.bounds_min;
        bounds_max_ = geometry.bounds_max;

        // Convert Taffy vertex data to MeshVertex format expected by clustered renderer
        vertices_.clear();
        vertices_.reserve(geometry.vertex_count);

        // Assuming Taffy vertices are in a compatible format
        // You may need to adjust this based on your actual vertex format
        struct TaffyVertex {
            Vec3Q position;     // Quantized position
            float normal[3];    // Normal vector
            float texCoord[2];  // UV coordinates
            float tangent[4];   // Tangent vector
        };

        const TaffyVertex* taffy_vertices = reinterpret_cast<const TaffyVertex*>(vertex_data);

        for (uint32_t i = 0; i < geometry.vertex_count; ++i) {
            tremor::gfx::MeshVertex vertex;

            // Convert quantized position back to MeshVertex format
            vertex.position = taffy_vertices[i].position;

            // Copy other attributes
            vertex.normal = glm::vec3(
                taffy_vertices[i].normal[0],
                taffy_vertices[i].normal[1],
                taffy_vertices[i].normal[2]
            );

            vertex.texCoord = glm::vec2(
                taffy_vertices[i].texCoord[0],
                taffy_vertices[i].texCoord[1]
            );

            vertex.tangent = glm::vec4(
                taffy_vertices[i].tangent[0],
                taffy_vertices[i].tangent[1],
                taffy_vertices[i].tangent[2],
                taffy_vertices[i].tangent[3]
            );

            vertices_.push_back(vertex);
        }

        // Copy index data
        indices_.clear();
        indices_.reserve(geometry.index_count);

        const uint32_t* taffy_indices = reinterpret_cast<const uint32_t*>(index_data);
        for (uint32_t i = 0; i < geometry.index_count; ++i) {
            indices_.push_back(taffy_indices[i]);
        }

        std::cout << "Successfully loaded Taffy mesh with quantized bounds: ["
            << bounds_min_.x << "," << bounds_min_.y << "," << bounds_min_.z << "] to ["
            << bounds_max_.x << "," << bounds_max_.y << "," << bounds_max_.z << "]" << std::endl;

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