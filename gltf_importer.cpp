#include "gltf_importer.h"
#include <iostream>
#include <fstream>
#include <cstring>

namespace Taffy {

GLTFImporter::GLTFImporter() {
    // Configure AssImp importer
    m_importer.SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE, aiPrimitiveType_LINE | aiPrimitiveType_POINT);
}

GLTFImporter::~GLTFImporter() = default;

bool GLTFImporter::convertGLTFToTaffy(const std::string& gltfPath, const std::filesystem::path& outputPath) {
    std::cout << "🔄 Loading GLTF file: " << gltfPath << std::endl;

    // Load the GLTF file with AssImp
    const aiScene* scene = m_importer.ReadFile(gltfPath,
        aiProcess_Triangulate |
        aiProcess_FlipUVs |
        aiProcess_CalcTangentSpace |
        aiProcess_GenNormals |
        aiProcess_JoinIdenticalVertices |
        aiProcess_OptimizeMeshes |
        aiProcess_ValidateDataStructure);

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        std::cerr << "❌ AssImp Error: " << m_importer.GetErrorString() << std::endl;
        return false;
    }

    std::cout << "✅ Loaded GLTF: " << scene->mNumMeshes << " meshes, "
              << scene->mNumMaterials << " materials" << std::endl;

    // Create Taffy asset
    Asset taffyAsset;
    taffyAsset.set_creator("GLTFImporter");
    taffyAsset.set_description("Converted from GLTF: " + gltfPath);

    // Set appropriate feature flags
    FeatureFlags flags = FeatureFlags::QuantizedCoords | FeatureFlags::PBRMaterials;
    taffyAsset.set_feature_flags(flags);

    // Process all meshes in the scene
    bool hasGeometry = false;
    for (unsigned int i = 0; i < scene->mNumMeshes; i++) {
        std::vector<uint8_t> geometryData;
        if (processMesh(scene->mMeshes[i], scene, geometryData)) {
            // Add geometry chunk to asset
            taffyAsset.add_chunk(ChunkType::GEOM, geometryData, "mesh_" + std::to_string(i));
            hasGeometry = true;
            std::cout << "✅ Processed mesh " << i << " (" << geometryData.size() << " bytes)" << std::endl;
        }
    }

    if (!hasGeometry) {
        std::cerr << "❌ No valid geometry found in GLTF file" << std::endl;
        return false;
    }

    // Save the Taffy asset
    std::cout << "💾 Saving Taffy asset: " << outputPath << std::endl;
    if (taffyAsset.save_to_file(outputPath)) {
        std::cout << "✅ Successfully converted GLTF to Taffy asset!" << std::endl;
        return true;
    } else {
        std::cerr << "❌ Failed to save Taffy asset" << std::endl;
        return false;
    }
}

bool GLTFImporter::processMesh(const aiMesh* mesh, const aiScene* scene, std::vector<uint8_t>& geometryData) {
    if (!mesh || mesh->mNumVertices == 0) {
        return false;
    }

    std::cout << "📐 Processing mesh: " << mesh->mNumVertices << " vertices, "
              << mesh->mNumFaces << " faces" << std::endl;

    // Create geometry chunk header
    GeometryChunk header;
    header.vertex_count = mesh->mNumVertices;
    header.index_count = mesh->mNumFaces * 3; // Assuming triangulated

    // Determine vertex format
    VertexFormat format = VertexFormat::Position3D;
    if (mesh->HasNormals()) format = format | VertexFormat::Normal;
    if (mesh->HasTextureCoords(0)) format = format | VertexFormat::TexCoord0;
    if (mesh->HasVertexColors(0)) format = format | VertexFormat::Color;
    if (mesh->HasTangentsAndBitangents()) format = format | VertexFormat::Tangent;

    header.vertex_format = format;

    // Calculate vertex stride
    size_t vertex_stride = sizeof(Vec3Q); // Position (quantized)
    if (mesh->HasNormals()) vertex_stride += 3 * sizeof(float);
    if (mesh->HasTextureCoords(0)) vertex_stride += 2 * sizeof(float);
    if (mesh->HasVertexColors(0)) vertex_stride += 4 * sizeof(float);
    if (mesh->HasTangentsAndBitangents()) vertex_stride += 4 * sizeof(float);

    header.vertex_stride = vertex_stride;

    // Set geometry chunk defaults
    header.lod_distance = 0.0f;
    header.lod_level = 0;
    header.render_mode = GeometryChunk::RenderMode::Traditional;
    header.ms_max_vertices = 0;
    header.ms_max_primitives = 0;
    header.ms_workgroup_size[0] = header.ms_workgroup_size[1] = header.ms_workgroup_size[2] = 0;
    header.ms_primitive_type = GeometryChunk::PrimitiveType::Triangles;
    header.ms_flags = 0;
    header.reserved[0] = header.reserved[1] = 0;

    // Calculate bounds for quantization
    aiVector3D min_pos = mesh->mVertices[0];
    aiVector3D max_pos = mesh->mVertices[0];

    for (unsigned int i = 1; i < mesh->mNumVertices; i++) {
        const aiVector3D& pos = mesh->mVertices[i];
        min_pos.x = std::min(min_pos.x, pos.x);
        min_pos.y = std::min(min_pos.y, pos.y);
        min_pos.z = std::min(min_pos.z, pos.z);
        max_pos.x = std::max(max_pos.x, pos.x);
        max_pos.y = std::max(max_pos.y, pos.y);
        max_pos.z = std::max(max_pos.z, pos.z);
    }

    // Store bounds in header (as Vec3Q)
    header.bounds_min = Vec3Q{
        (int64_t)(min_pos.x * 128000),
        (int64_t)(min_pos.y * 128000),
        (int64_t)(min_pos.z * 128000)
    };
    header.bounds_max = Vec3Q{
        (int64_t)(max_pos.x * 128000),
        (int64_t)(max_pos.y * 128000),
        (int64_t)(max_pos.z * 128000)
    };

    // Start building geometry data
    geometryData.clear();

    // Add header
    geometryData.resize(sizeof(GeometryChunk));
    std::memcpy(geometryData.data(), &header, sizeof(GeometryChunk));

    // Add vertex data
    for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
        size_t vertex_start = geometryData.size();
        geometryData.resize(vertex_start + vertex_stride);
        uint8_t* vertex_ptr = geometryData.data() + vertex_start;
        size_t offset = 0;

        // Position (quantized)
        const aiVector3D& pos = mesh->mVertices[i];
        Vec3Q quantized_pos{
            (int64_t)(pos.x * 128000),
            (int64_t)(pos.y * 128000),
            (int64_t)(pos.z * 128000)
        };
        std::memcpy(vertex_ptr + offset, &quantized_pos, sizeof(Vec3Q));
        offset += sizeof(Vec3Q);

        // Normal
        if (mesh->HasNormals()) {
            const aiVector3D& normal = mesh->mNormals[i];
            float normal_data[3] = { normal.x, normal.y, normal.z };
            std::memcpy(vertex_ptr + offset, normal_data, 3 * sizeof(float));
            offset += 3 * sizeof(float);
        }

        // Texture coordinates
        if (mesh->HasTextureCoords(0)) {
            const aiVector3D& uv = mesh->mTextureCoords[0][i];
            float uv_data[2] = { uv.x, uv.y };
            std::memcpy(vertex_ptr + offset, uv_data, 2 * sizeof(float));
            offset += 2 * sizeof(float);
        }

        // Vertex colors
        if (mesh->HasVertexColors(0)) {
            const aiColor4D& color = mesh->mColors[0][i];
            float color_data[4] = { color.r, color.g, color.b, color.a };
            std::memcpy(vertex_ptr + offset, color_data, 4 * sizeof(float));
            offset += 4 * sizeof(float);
        }

        // Tangents
        if (mesh->HasTangentsAndBitangents()) {
            const aiVector3D& tangent = mesh->mTangents[i];
            const aiVector3D& bitangent = mesh->mBitangents[i];
            // Calculate handedness (w component)
            float handedness = 1.0f; // TODO: Calculate proper handedness
            float tangent_data[4] = { tangent.x, tangent.y, tangent.z, handedness };
            std::memcpy(vertex_ptr + offset, tangent_data, 4 * sizeof(float));
            offset += 4 * sizeof(float);
        }
    }

    // Add index data
    for (unsigned int i = 0; i < mesh->mNumFaces; i++) {
        const aiFace& face = mesh->mFaces[i];
        if (face.mNumIndices == 3) {
            // Add triangle indices
            for (unsigned int j = 0; j < 3; j++) {
                uint32_t index = face.mIndices[j];
                geometryData.insert(geometryData.end(),
                    reinterpret_cast<uint8_t*>(&index),
                    reinterpret_cast<uint8_t*>(&index) + sizeof(uint32_t));
            }
        }
    }

    std::cout << "📊 Geometry data size: " << geometryData.size() << " bytes" << std::endl;
    return true;
}

} // namespace Taffy