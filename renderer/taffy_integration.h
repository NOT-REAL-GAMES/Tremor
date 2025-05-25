// tremor/src/renderer/taffy_integration.h
#pragma once

#include "../gfx.h"
#include "../taffy/taffy.h"
#include "taffy_mesh.h"
#include <memory>

namespace Tremor {

    /**
     * High-level integration between Taffy assets and the clustered renderer
     */
    class TaffyAssetLoader {
    public:
        struct LoadedAsset {
            std::vector<uint32_t> mesh_ids;      // IDs returned by renderer
            std::vector<uint32_t> material_ids;  // Material IDs
            std::vector<std::unique_ptr<TaffyMesh>> meshes; // Keep meshes for bounds info

            // Convenience methods
            uint32_t get_primary_mesh_id() const {
                return mesh_ids.empty() ? UINT32_MAX : mesh_ids[0];
            }

            uint32_t get_primary_material_id() const {
                return material_ids.empty() ? 0 : material_ids[0]; // Default to material 0
            }
        };

        explicit TaffyAssetLoader(tremor::gfx::VulkanClusteredRenderer& renderer)
            : renderer_(renderer) {
        }

        // Load a complete Taffy asset (meshes + materials)
        std::unique_ptr<LoadedAsset> load_asset(const std::string& filepath);

        // Load just geometry from a Taffy asset
        std::unique_ptr<TaffyMesh> load_mesh_only(const std::string& filepath);

    private:
        tremor::gfx::VulkanClusteredRenderer& renderer_;

        // Helper methods
        std::vector<uint32_t> load_materials_from_asset(const Taffy::Asset& asset);
        tremor::gfx::PBRMaterial convert_taffy_material(const Taffy::MaterialChunk::Material& taffy_mat);
    };

} // namespace Tremor

