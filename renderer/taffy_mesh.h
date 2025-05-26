#pragma once

#include "../main.h"
#include "taffy.h" 
#include "quan.h"  // Vec3Q, etc.
#include "../gfx.h"   // MeshVertex

// Forward declaration
namespace tremor::gfx {
    class VulkanClusteredRenderer;
}

namespace Tremor {

    /**
     * Bridge between Taffy assets and Tremor's Vulkan rendering pipeline
     */
    class TaffyMesh {
    public:
        TaffyMesh() = default;
        ~TaffyMesh();

        // Load geometry from Taffy asset
        bool load_from_asset(const Taffy::Asset& asset);

        // Upload to clustered renderer (returns mesh ID)
        uint32_t upload_to_renderer(tremor::gfx::VulkanClusteredRenderer& renderer,
            const std::string& name = "");

        // Get bounding info for culling/streaming (converted to float)
        glm::vec3 get_bounds_min() const;
        glm::vec3 get_bounds_max() const;

        // Access to vertex/index data
        const std::vector<tremor::gfx::MeshVertex>& get_vertices() const { return vertices_; }
        const std::vector<uint32_t>& get_indices() const { return indices_; }

        // Mesh info
        uint32_t get_vertex_count() const { return vertex_count_; }
        uint32_t get_index_count() const { return index_count_; }

    private:
        // Converted mesh data ready for clustered renderer
        std::vector<tremor::gfx::MeshVertex> vertices_;
        std::vector<uint32_t> indices_;

        uint32_t vertex_count_ = 0;
        uint32_t index_count_ = 0;

        // Original Taffy quantized bounds
        Vec3Q bounds_min_{};
        Vec3Q bounds_max_{};
    };

} // namespace Tremor