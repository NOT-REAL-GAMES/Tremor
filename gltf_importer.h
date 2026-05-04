#pragma once

#include <string>
#include <vector>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include "taffy.h"

namespace Taffy {

/**
 * GLTF to Taffy Asset Converter
 */
class GLTFImporter {
public:
    GLTFImporter();
    ~GLTFImporter();

    /**
     * Load a GLTF file and convert it to a Taffy asset
     * @param gltfPath Path to the GLTF file (.gltf or .glb)
     * @param outputPath Path for the output Taffy asset (.taf)
     * @return true if successful, false otherwise
     */
    bool convertGLTFToTaffy(const std::string& gltfPath, const std::string& outputPath);

private:
    /**
     * Process a single mesh from the GLTF scene
     */
    bool processMesh(const aiMesh* mesh, const aiScene* scene, std::vector<uint8_t>& geometryData);

    /**
     * Convert AssImp vertex data to Taffy format
     */
    void convertVertices(const aiMesh* mesh, std::vector<uint8_t>& geometryData);

    /**
     * Convert AssImp indices to Taffy format
     */
    void convertIndices(const aiMesh* mesh, std::vector<uint8_t>& geometryData);

    Assimp::Importer m_importer;
};

} // namespace Taffy