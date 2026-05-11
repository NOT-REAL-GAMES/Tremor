#pragma once

#include "../../../tremor_graphics_platform.h"
#include "../../../handle.h"

#include <cfloat>
#include <cmath>

#undef min
#undef max

namespace tremor::gfx {

    enum class TextureFormat {
        Unknown,
        R8_UNORM,
        R8G8_UNORM,
        R8G8B8A8_UNORM,
        R8G8B8A8_SRGB,
        B8G8R8A8_UNORM,
        B8G8R8A8_SRGB,
        R16_FLOAT,
        R16G16_FLOAT,
        R16G16B16A16_FLOAT,
        R32_FLOAT,
        R32G32_FLOAT,
        R32G32B32_FLOAT,
        R32G32B32A32_FLOAT,
        D16_UNORM,
        D24_UNORM_S8_UINT,
        D32_FLOAT,
        BC1_RGB_UNORM,
        BC1_RGB_SRGB,
        BC1_RGBA_UNORM,
        BC1_RGBA_SRGB,
        BC3_UNORM,
        BC3_SRGB,
        BC5_UNORM,
        BC7_UNORM,
        BC7_SRGB
    };

    inline VkFormat convertFormat(TextureFormat format) {
        switch (format) {
        case TextureFormat::Unknown:            return VK_FORMAT_UNDEFINED;
        case TextureFormat::R8_UNORM:           return VK_FORMAT_R8_UNORM;
        case TextureFormat::R8G8_UNORM:         return VK_FORMAT_R8G8_UNORM;
        case TextureFormat::R8G8B8A8_UNORM:     return VK_FORMAT_R8G8B8A8_UNORM;
        case TextureFormat::R8G8B8A8_SRGB:      return VK_FORMAT_R8G8B8A8_SRGB;
        case TextureFormat::B8G8R8A8_UNORM:     return VK_FORMAT_B8G8R8A8_UNORM;
        case TextureFormat::B8G8R8A8_SRGB:      return VK_FORMAT_B8G8R8A8_SRGB;
        case TextureFormat::R16_FLOAT:          return VK_FORMAT_R16_SFLOAT;
        case TextureFormat::R16G16_FLOAT:       return VK_FORMAT_R16G16_SFLOAT;
        case TextureFormat::R16G16B16A16_FLOAT: return VK_FORMAT_R16G16B16A16_SFLOAT;
        case TextureFormat::R32_FLOAT:          return VK_FORMAT_R32_SFLOAT;
        case TextureFormat::R32G32_FLOAT:       return VK_FORMAT_R32G32_SFLOAT;
        case TextureFormat::R32G32B32_FLOAT:    return VK_FORMAT_R32G32B32_SFLOAT;
        case TextureFormat::R32G32B32A32_FLOAT: return VK_FORMAT_R32G32B32A32_SFLOAT;
        case TextureFormat::D16_UNORM:          return VK_FORMAT_D16_UNORM;
        case TextureFormat::D24_UNORM_S8_UINT:  return VK_FORMAT_D24_UNORM_S8_UINT;
        case TextureFormat::D32_FLOAT:          return VK_FORMAT_D32_SFLOAT;
        case TextureFormat::BC1_RGB_UNORM:      return VK_FORMAT_BC1_RGB_UNORM_BLOCK;
        case TextureFormat::BC1_RGB_SRGB:       return VK_FORMAT_BC1_RGB_SRGB_BLOCK;
        case TextureFormat::BC1_RGBA_UNORM:     return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
        case TextureFormat::BC1_RGBA_SRGB:      return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
        case TextureFormat::BC3_UNORM:          return VK_FORMAT_BC3_UNORM_BLOCK;
        case TextureFormat::BC3_SRGB:           return VK_FORMAT_BC3_SRGB_BLOCK;
        case TextureFormat::BC5_UNORM:          return VK_FORMAT_BC5_UNORM_BLOCK;
        case TextureFormat::BC7_UNORM:          return VK_FORMAT_BC7_UNORM_BLOCK;
        case TextureFormat::BC7_SRGB:           return VK_FORMAT_BC7_SRGB_BLOCK;
        default:
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Unsupported texture format: %d", static_cast<int>(format));
            return VK_FORMAT_R8G8B8A8_UNORM;
        }
    }

    enum class TextureUsage : uint32_t {
        None = 0,
        ShaderRead = 1 << 0,
        ShaderWrite = 1 << 1,
        RenderTarget = 1 << 2,
        DepthStencil = 1 << 3,
        TransferSrc = 1 << 4,
        TransferDst = 1 << 5,
        Storage = 1 << 6,
        GenerateMipmaps = 1 << 7,
    };

    inline TextureUsage operator|(TextureUsage a, TextureUsage b) {
        return static_cast<TextureUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }

    inline TextureUsage& operator|=(TextureUsage& a, TextureUsage b) {
        return a = a | b;
    }

    inline bool hasFlag(TextureUsage flags, TextureUsage flag) {
        return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(flag)) != 0;
    }

    enum class AddressMode {
        Repeat,
        MirroredRepeat,
        ClampToEdge,
        ClampToBorder,
        MirrorClampToEdge
    };

    enum class FilterMode {
        Nearest,
        Linear,
        Cubic
    };

    enum class BufferUsage : uint32_t {
        None = 0,
        VertexBuffer = 1 << 0,
        IndexBuffer = 1 << 1,
        UniformBuffer = 1 << 2,
        StorageBuffer = 1 << 3,
        IndirectBuffer = 1 << 4,
        TransferSrc = 1 << 5,
        TransferDst = 1 << 6,
        ShaderDeviceAddress = 1 << 7
    };

    inline BufferUsage operator|(BufferUsage a, BufferUsage b) {
        return static_cast<BufferUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }

    inline BufferUsage& operator|=(BufferUsage& a, BufferUsage b) {
        return a = a | b;
    }

    inline bool hasFlag(BufferUsage flags, BufferUsage flag) {
        return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(flag)) != 0;
    }

    enum class MemoryType {
        GpuOnly,
        CpuToGpu,
        GpuToCpu,
        CpuGpuShared
    };

    enum class ShaderType {
        Vertex,
        Fragment,
        Compute,
        Geometry,
        TessControl,
        TessEvaluation,
        Mesh,
        Task,
        RayGen,
        RayAnyHit,
        RayClosestHit,
        RayMiss,
        RayIntersection,
        Callable
    };

    enum class ShaderSourceType {
        GLSL,
        HLSL,
        SPIRV,
        WGSL,
        CSO,
        SPV_ASM,
        MSL
    };

    enum class CompareOp {
        Never,
        Less,
        Equal,
        LessOrEqual,
        Greater,
        NotEqual,
        GreaterOrEqual,
        Always
    };

    struct TextureData {
        const void* data = nullptr;
        uint32_t bytesPerRow = 0;
        uint32_t bytesPerImage = 0;
        uint32_t mipLevel = 0;
        uint32_t arrayLayer = 0;
    };

    struct TextureDesc {
        uint32_t width = 1;
        uint32_t height = 1;
        uint32_t depth = 1;
        uint32_t arrayLayers = 1;
        uint32_t mipLevels = 1;
        uint32_t sampleCount = 1;

        TextureFormat format = TextureFormat::R8G8B8A8_UNORM;
        TextureUsage usage = TextureUsage::ShaderRead | TextureUsage::TransferDst;

        bool createView = true;
        bool isRenderTarget = false;

        struct SamplerParams {
            FilterMode magFilter = FilterMode::Linear;
            FilterMode minFilter = FilterMode::Linear;
            FilterMode mipmapFilter = FilterMode::Linear;
            AddressMode addressModeU = AddressMode::Repeat;
            AddressMode addressModeV = AddressMode::Repeat;
            AddressMode addressModeW = AddressMode::Repeat;
            float maxAnisotropy = 1.0f;
            bool compareEnable = false;
            CompareOp compareOp = CompareOp::Always;
            float minLod = 0.0f;
            float maxLod = FLT_MAX;
            float mipLodBias = 0.0f;
            float borderColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        } sampler;

        TextureData initialData;
        const char* debugName = nullptr;

        static TextureDesc Texture2D(
            uint32_t width, uint32_t height,
            TextureFormat format = TextureFormat::R8G8B8A8_UNORM,
            TextureUsage usage = TextureUsage::ShaderRead | TextureUsage::TransferDst)
        {
            TextureDesc desc;
            desc.width = width;
            desc.height = height;
            desc.format = format;
            desc.usage = usage;

            if (hasFlag(usage, TextureUsage::GenerateMipmaps)) {
                desc.mipLevels = 1 + static_cast<uint32_t>(std::floor(std::log2(std::max(width, height))));
            }

            return desc;
        }

        static TextureDesc RenderTarget(
            uint32_t width, uint32_t height,
            TextureFormat format = TextureFormat::R8G8B8A8_UNORM,
            uint32_t sampleCount = 1)
        {
            TextureDesc desc;
            desc.width = width;
            desc.height = height;
            desc.format = format;
            desc.usage = TextureUsage::RenderTarget | TextureUsage::ShaderRead;
            desc.sampleCount = sampleCount;
            desc.isRenderTarget = true;
            return desc;
        }

        static TextureDesc DepthStencil(
            uint32_t width, uint32_t height,
            TextureFormat format = TextureFormat::D24_UNORM_S8_UINT,
            uint32_t sampleCount = 1)
        {
            TextureDesc desc;
            desc.width = width;
            desc.height = height;
            desc.format = format;
            desc.usage = TextureUsage::DepthStencil | TextureUsage::ShaderRead;
            desc.sampleCount = sampleCount;
            desc.isRenderTarget = true;
            return desc;
        }
    };

    struct BufferDesc {
        size_t size = 0;
        BufferUsage usage = BufferUsage::None;
        MemoryType memoryType = MemoryType::GpuOnly;
        uint32_t stride = 0;
        const void* initialData = nullptr;
        const char* debugName = nullptr;

        static BufferDesc VertexBuffer(
            size_t size,
            MemoryType memoryType = MemoryType::CpuToGpu,
            const void* initialData = nullptr)
        {
            BufferDesc desc;
            desc.size = size;
            desc.usage = BufferUsage::VertexBuffer | BufferUsage::TransferDst;
            desc.memoryType = memoryType;
            desc.initialData = initialData;
            return desc;
        }

        static BufferDesc IndexBuffer(
            size_t size,
            MemoryType memoryType = MemoryType::CpuToGpu,
            const void* initialData = nullptr)
        {
            BufferDesc desc;
            desc.size = size;
            desc.usage = BufferUsage::IndexBuffer | BufferUsage::TransferDst;
            desc.memoryType = memoryType;
            desc.initialData = initialData;
            return desc;
        }

        static BufferDesc UniformBuffer(
            size_t size,
            MemoryType memoryType = MemoryType::CpuToGpu)
        {
            BufferDesc desc;
            desc.size = size;
            desc.usage = BufferUsage::UniformBuffer;
            desc.memoryType = memoryType;
            return desc;
        }

        static BufferDesc StorageBuffer(
            size_t size,
            uint32_t stride = 0,
            MemoryType memoryType = MemoryType::GpuOnly,
            const void* initialData = nullptr)
        {
            BufferDesc desc;
            desc.size = size;
            desc.usage = BufferUsage::StorageBuffer | BufferUsage::TransferDst;
            desc.stride = stride;
            desc.memoryType = memoryType;
            desc.initialData = initialData;
            return desc;
        }

        template<typename T>
        static BufferDesc ForData(
            const std::vector<T>& data,
            BufferUsage usage = BufferUsage::VertexBuffer,
            MemoryType memoryType = MemoryType::CpuToGpu)
        {
            BufferDesc desc;
            desc.size = data.size() * sizeof(T);
            desc.usage = usage | BufferUsage::TransferDst;
            desc.memoryType = memoryType;
            desc.initialData = data.data();

            if (std::is_class_v<T> && sizeof(T) > 4) {
                desc.stride = sizeof(T);
            }

            return desc;
        }
    };

    struct ShaderIncludePath {
        const char* name = nullptr;
        const char* path = nullptr;
    };

    struct ShaderDefine {
        const char* name = nullptr;
        const char* value = nullptr;
    };

    struct ShaderDesc {
        ShaderType type = ShaderType::Vertex;
        ShaderSourceType sourceType = ShaderSourceType::GLSL;
        const char* sourceCode = nullptr;
        const void* byteCode = nullptr;
        size_t byteCodeSize = 0;
        const char* filename = nullptr;
        const char* entryPoint = "main";
        bool optimize = true;
        std::vector<ShaderIncludePath> includePaths;
        std::vector<ShaderDefine> defines;
        const char* debugName = nullptr;

        static ShaderDesc FromGLSL(
            ShaderType type,
            const char* sourceCode,
            const char* entryPoint = "main")
        {
            ShaderDesc desc;
            desc.type = type;
            desc.sourceType = ShaderSourceType::GLSL;
            desc.sourceCode = sourceCode;
            desc.entryPoint = entryPoint;
            return desc;
        }

        static ShaderDesc FromHLSL(
            ShaderType type,
            const char* sourceCode,
            const char* entryPoint = "main")
        {
            ShaderDesc desc;
            desc.type = type;
            desc.sourceType = ShaderSourceType::HLSL;
            desc.sourceCode = sourceCode;
            desc.entryPoint = entryPoint;
            return desc;
        }

        static ShaderDesc FromSPIRV(
            ShaderType type,
            const void* byteCode,
            size_t byteCodeSize)
        {
            ShaderDesc desc;
            desc.type = type;
            desc.sourceType = ShaderSourceType::SPIRV;
            desc.byteCode = byteCode;
            desc.byteCodeSize = byteCodeSize;
            return desc;
        }

        static ShaderDesc FromFile(
            ShaderType type,
            const char* filename,
            ShaderSourceType sourceType = ShaderSourceType::GLSL,
            const char* entryPoint = "main")
        {
            ShaderDesc desc;
            desc.type = type;
            desc.sourceType = sourceType;
            desc.filename = filename;
            desc.entryPoint = entryPoint;
            return desc;
        }

        ShaderDesc& addDefine(const char* name, const char* value = nullptr) {
            ShaderDefine define;
            define.name = name;
            define.value = value;
            defines.push_back(define);
            return *this;
        }

        ShaderDesc& addIncludePath(const char* name, const char* path) {
            ShaderIncludePath includePath;
            includePath.name = name;
            includePath.path = path;
            includePaths.push_back(includePath);
            return *this;
        }
    };

    inline VkFilter convertFilterMode(FilterMode filter) {
        switch (filter) {
        case FilterMode::Nearest: return VK_FILTER_NEAREST;
        case FilterMode::Linear:  return VK_FILTER_LINEAR;
        case FilterMode::Cubic:   return VK_FILTER_CUBIC_EXT;
        default:                  return VK_FILTER_LINEAR;
        }
    }

    inline VkSamplerAddressMode convertAddressMode(AddressMode mode) {
        switch (mode) {
        case AddressMode::Repeat:            return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        case AddressMode::MirroredRepeat:    return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        case AddressMode::ClampToEdge:       return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case AddressMode::ClampToBorder:     return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        case AddressMode::MirrorClampToEdge: return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
        default:                             return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        }
    }

    inline VkCompareOp convertCompareOp(CompareOp op) {
        switch (op) {
        case CompareOp::Never:          return VK_COMPARE_OP_NEVER;
        case CompareOp::Less:           return VK_COMPARE_OP_LESS;
        case CompareOp::Equal:          return VK_COMPARE_OP_EQUAL;
        case CompareOp::LessOrEqual:    return VK_COMPARE_OP_LESS_OR_EQUAL;
        case CompareOp::Greater:        return VK_COMPARE_OP_GREATER;
        case CompareOp::NotEqual:       return VK_COMPARE_OP_NOT_EQUAL;
        case CompareOp::GreaterOrEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case CompareOp::Always:         return VK_COMPARE_OP_ALWAYS;
        default:                        return VK_COMPARE_OP_ALWAYS;
        }
    }

} // namespace tremor::gfx
