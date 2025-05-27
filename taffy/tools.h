/**
 * Taffy Overlay Creation Tools and Example Overlays
 * Helper utilities for creating overlays programmatically
 */

#pragma once

#include "taffy.h"
#include "overlay.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <filesystem>

#include <shaderc/shaderc.hpp>

namespace tremor::taffy::tools {

    bool createShaderChunkHashDebug(Taffy::Asset& asset,
        const std::vector<uint32_t>& mesh_spirv,
        const std::vector<uint32_t>& frag_spirv);

    class HashBasedShaderCreator {
    public:

        inline static bool validateHashShaderChunk(const Taffy::Asset& asset);
        inline static bool createShaderChunkHash(Taffy::Asset& asset,
            const std::vector<uint32_t>& mesh_spirv,
            const std::vector<uint32_t>& frag_spirv);
    };

    // =============================================================================
    // FORWARD DECLARATIONS (Header file)
    // =============================================================================

    class TaffyAssetCompiler {
    private:
        std::unique_ptr<shaderc::Compiler> compiler_;
        std::unique_ptr<shaderc::CompileOptions> options_;

    public:
        TaffyAssetCompiler();

        bool createTriangleAssetSafeDebug(const std::string& output_path);

        std::vector<uint32_t> compileGLSLToSpirv(const std::string& source,
            shaderc_shader_kind kind,
            const std::string& name = "");
        bool createTriangleAssetHashBased(const std::string& output_path);


        bool createTriangleAsset(const std::string& output_path);

    private:
        // Helper methods
        bool createShaderChunk(Taffy::Asset& asset,
            const std::vector<uint32_t>& mesh_spirv,
            const std::vector<uint32_t>& frag_spirv);
        bool createGeometryChunk(Taffy::Asset& asset);
        bool createMaterialChunk(Taffy::Asset& asset);

        // Shader source getters (implementation will provide the actual GLSL)
        std::string getTriangleMeshShaderGLSL() const;
        std::string getTriangleFragmentShaderGLSL() const;
    };

    class ShaderVariantGenerator {
    public:
        static std::string generateWireframeMeshShader();
        static std::string generateAnimatedMeshShader();
    };

}