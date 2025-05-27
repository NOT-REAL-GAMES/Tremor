// tremor/src/renderer/taffy_integration.cpp
#include "taffy_integration.h"
#include "../vk.h"  // Include full VulkanClusteredRenderer definition
#include <iostream>

namespace Tremor {

    std::unique_ptr<TaffyAssetLoader::LoadedAsset> TaffyAssetLoader::load_asset(const std::string& filepath) {
        std::cout << "=== TaffyAssetLoader::load_asset() called ===" << std::endl;
        std::cout << "Filepath: " << filepath << std::endl;
        std::cout << "File exists: " << std::filesystem::exists(filepath) << std::endl;

        auto loaded_asset = std::make_unique<LoadedAsset>();

        // Load the Taffy asset
        Taffy::Asset asset;
        std::cout << "About to call asset.load_from_file()..." << std::endl;
        if (!asset.load_from_file_safe(filepath)) {
            std::cerr << "Failed to load Taffy asset: " << filepath << std::endl;
            return nullptr;
        }

        std::cout << "Successfully loaded Taffy asset: " << filepath << std::endl;
        std::cout << "Creator: " << asset.get_creator() << std::endl;
        std::cout << "Description: " << asset.get_description() << std::endl;

        // Load materials first (if any)
        loaded_asset->material_ids = load_materials_from_asset(asset);
        if (loaded_asset->material_ids.empty()) {
            std::cout << "No materials found, using default material" << std::endl;
            loaded_asset->material_ids.push_back(0); // Use default material
        }

        // Load geometry
        if (asset.has_chunk(Taffy::ChunkType::GEOM)) {
            auto taffy_mesh = std::make_unique<TaffyMesh>();

            if (taffy_mesh->load_from_asset(asset)) {
                // Upload to renderer
                std::string mesh_name = filepath + "_mesh0"; // Simple naming for now
                uint32_t mesh_id = taffy_mesh->upload_to_renderer(renderer_, mesh_name);

                if (mesh_id != UINT32_MAX) {
                    loaded_asset->mesh_ids.push_back(mesh_id);
                    loaded_asset->meshes.push_back(std::move(taffy_mesh));

                    std::cout << "Uploaded mesh with ID: " << mesh_id << std::endl;
                }
                else {
                    std::cerr << "Failed to upload mesh to renderer" << std::endl;
                }
            }
            else {
                std::cerr << "Failed to load geometry from Taffy asset" << std::endl;
            }
        }
        else {
            std::cout << "No geometry chunk found in asset" << std::endl;
        }

        if (loaded_asset->mesh_ids.empty()) {
            std::cerr << "No meshes were successfully loaded" << std::endl;
            return nullptr;
        }

        return loaded_asset;
    }

    std::unique_ptr<TaffyMesh> TaffyAssetLoader::load_mesh_only(const std::string& filepath) {
        Taffy::Asset asset;
        if (!asset.load_from_file_safe(filepath)) {
            return nullptr;
        }

        auto taffy_mesh = std::make_unique<TaffyMesh>();
        if (!taffy_mesh->load_from_asset(asset)) {
            return nullptr;
        }

        return taffy_mesh;
    }

    std::vector<uint32_t> TaffyAssetLoader::load_materials_from_asset(const Taffy::Asset& asset) {
        std::vector<uint32_t> material_ids;

        // Get materials chunk if it exists
        auto materials_opt = asset.get_chunk_data(Taffy::ChunkType::MTRL);
        if (!materials_opt) {
            return material_ids; // Empty vector
        }

        const auto& materials_chunk = *materials_opt;

        // Get the raw material data
        auto chunk_data = asset.get_chunk_data(Taffy::ChunkType::MTRL);
        if (!chunk_data) {
            return material_ids;
        }

        const uint8_t* chunk_ptr = chunk_data->data();
        const Taffy::MaterialChunk::Material* materials =
            reinterpret_cast<const Taffy::MaterialChunk::Material*>(chunk_ptr + sizeof(Taffy::MaterialChunk));

       

        return {};
    }

    tremor::gfx::PBRMaterial TaffyAssetLoader::convert_taffy_material(const Taffy::MaterialChunk::Material& taffy_mat) {
        tremor::gfx::PBRMaterial pbr_material{};

        // Convert basic PBR properties
        pbr_material.baseColor = glm::vec4(
            taffy_mat.albedo[0],
            taffy_mat.albedo[1],
            taffy_mat.albedo[2],
            taffy_mat.albedo[3]
        );

        pbr_material.metallic = taffy_mat.metallic;
        pbr_material.roughness = taffy_mat.roughness;
        pbr_material.normalScale = taffy_mat.normal_intensity;
        pbr_material.occlusionStrength = 1.0f; // Default value

        pbr_material.emissiveColor = glm::vec3(
            taffy_mat.emission[0],
            taffy_mat.emission[1],
            taffy_mat.emission[2]
        );

        pbr_material.emissiveFactor = 1.0f; // Can be adjusted based on material

        // Texture indices - for now, use -1 (no texture) since we don't have texture loading yet
        pbr_material.albedoTexture = -1;
        pbr_material.normalTexture = -1;
        pbr_material.metallicRoughnessTexture = -1;
        pbr_material.occlusionTexture = -1;
        pbr_material.emissiveTexture = -1;

        pbr_material.flags = 0; // No special flags for now

        return pbr_material;
    }

} // namespace Tremor