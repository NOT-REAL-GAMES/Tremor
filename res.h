#pragma once
#include "Handle.h"

namespace tremor::gfx {

    // Texture formats
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
            SDL_LogError(SDL_LOG_PRIORITY_ERROR, "Unsupported texture format: {}", static_cast<int>(format));
            return VK_FORMAT_R8G8B8A8_UNORM; // Default to a common format
        }
    }

    // Texture usage flags (can be combined)
    enum class TextureUsage : uint32_t {
        None = 0,
        ShaderRead = 1 << 0,         // Texture can be read in shaders
        ShaderWrite = 1 << 1,        // Texture can be written in shaders
        RenderTarget = 1 << 2,       // Texture can be used as a render target
        DepthStencil = 1 << 3,       // Texture can be used as depth/stencil buffer
        TransferSrc = 1 << 4,        // Texture can be used as source in transfer ops
        TransferDst = 1 << 5,        // Texture can be used as destination in transfer ops
        Storage = 1 << 6,            // Texture can be used as storage image
        GenerateMipmaps = 1 << 7,    // Texture supports automatic mipmap generation
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

    // Texture addressing modes
    enum class AddressMode {
        Repeat,
        MirroredRepeat,
        ClampToEdge,
        ClampToBorder,
        MirrorClampToEdge
    };

    // Texture filtering modes
    enum class FilterMode {
        Nearest,
        Linear,
        Cubic
    };

    // Buffer usage flags (can be combined)
    enum class BufferUsage : uint32_t {
        None = 0,
        VertexBuffer = 1 << 0,      // Buffer can be used as vertex buffer
        IndexBuffer = 1 << 1,       // Buffer can be used as index buffer
        UniformBuffer = 1 << 2,     // Buffer can be used as uniform buffer
        StorageBuffer = 1 << 3,     // Buffer can be used as storage buffer
        IndirectBuffer = 1 << 4,    // Buffer can be used for indirect draw commands
        TransferSrc = 1 << 5,       // Buffer can be used as source in transfer ops
        TransferDst = 1 << 6,       // Buffer can be used as destination in transfer ops
        ShaderDeviceAddress = 1 << 7 // Buffer address can be queried and used in shaders
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

    // Memory properties
    enum class MemoryType {
        GpuOnly,        // Visible only to GPU, fastest access
        CpuToGpu,       // CPU can write, GPU can read, good for uploading
        GpuToCpu,       // GPU can write, CPU can read, good for readbacks
        CpuGpuShared    // Visible to both CPU and GPU, slower access
    };

    // Shader types
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

    // Shader source types
    enum class ShaderSourceType {
        GLSL,       // OpenGL Shading Language
        HLSL,       // High-Level Shading Language
        SPIRV,      // SPIR-V binary
        WGSL,       // WebGPU Shading Language
        CSO,        // Compiled Shader Object (D3D)
        SPV_ASM,    // SPIR-V assembly text
        MSL         // Metal Shading Language
    };

} // namespace tremor::gfx

#if defined(USING_VULKAN)
 // Template for RAII Vulkan objects with type-based specialization
template<typename T>
class VulkanResource {
private:
    VkDevice m_device = VK_NULL_HANDLE;
    T m_handle = VK_NULL_HANDLE;

public:
    VulkanResource() = default;

    VulkanResource(VkDevice device, T handle = VK_NULL_HANDLE)
        : m_device(device), m_handle(handle) {
    }

    ~VulkanResource() {
        cleanup();
    }

    // Disable copying
    VulkanResource(const VulkanResource&) = delete;
    VulkanResource& operator=(const VulkanResource&) = delete;

    // Enable moving
    VulkanResource(VulkanResource&& other) noexcept
        : m_device(other.m_device), m_handle(other.m_handle) {
        other.m_handle = VK_NULL_HANDLE;
    }

    VulkanResource& operator=(VulkanResource&& other) noexcept {
        if (this != &other) {
            cleanup();
            m_device = other.m_device;
            m_handle = other.m_handle;
            other.m_handle = VK_NULL_HANDLE;
        }
        return *this;
    }

    // Accessors
    T& handle() { return m_handle; }
    const T& handle() const { return m_handle; }
    operator T() const {
        if (this == nullptr) {            
            Logger::get().error("RESOURCE DOES NOT EXIST. FUCK");
            return nullptr;
        }
        return m_handle; }

    // Check if valid
    operator bool() const { return m_handle != VK_NULL_HANDLE; }

    // Release ownership without destroying
    T release() {
        T temp = m_handle;
        m_handle = VK_NULL_HANDLE;
        return temp;
    }

    // Reset with a new handle
    void reset(T newHandle = VK_NULL_HANDLE) {
        cleanup();
        m_handle = newHandle;
    }

private:
    void cleanup() {
        if (m_handle != VK_NULL_HANDLE && m_device != VK_NULL_HANDLE) {
            destroy(m_device, m_handle);
        }
        m_handle = VK_NULL_HANDLE;
    }

    static void destroy(VkDevice device, T handle);
};

// Specializations for each Vulkan type
template<> inline void VulkanResource<VkImage>::destroy(VkDevice device, VkImage handle) {
    vkDestroyImage(device, handle, nullptr);
}

template<> inline void VulkanResource<VkImageView>::destroy(VkDevice device, VkImageView handle) {
    vkDestroyImageView(device, handle, nullptr);
}

template<> inline void VulkanResource<VkBuffer>::destroy(VkDevice device, VkBuffer handle) {
    vkDestroyBuffer(device, handle, nullptr);
}

template<> inline void VulkanResource<VkDeviceMemory>::destroy(VkDevice device, VkDeviceMemory handle) {
    vkFreeMemory(device, handle, nullptr);
}

template<> inline void VulkanResource<VkPipeline>::destroy(VkDevice device, VkPipeline handle) {
    vkDestroyPipeline(device, handle, nullptr);
}

template<> inline void VulkanResource<VkShaderModule>::destroy(VkDevice device, VkShaderModule handle) {
    vkDestroyShaderModule(device, handle, nullptr);
}

template<> inline void VulkanResource<VkDescriptorSetLayout>::destroy(VkDevice device, VkDescriptorSetLayout handle) {
    vkDestroyDescriptorSetLayout(device, handle, nullptr);
}

template<> inline void VulkanResource<VkPipelineLayout>::destroy(VkDevice device, VkPipelineLayout handle) {
    vkDestroyPipelineLayout(device, handle, nullptr);
}

template<> inline void VulkanResource<VkSampler>::destroy(VkDevice device, VkSampler handle) {
    vkDestroySampler(device, handle, nullptr);
}

template<> inline void VulkanResource<VkSwapchainKHR>::destroy(VkDevice device, VkSwapchainKHR handle) {
    vkDestroySwapchainKHR(device, handle, nullptr);
}

template<> inline void VulkanResource<VkCommandPool>::destroy(VkDevice device, VkCommandPool handle) {
    vkDestroyCommandPool(device, handle, nullptr);
}

template<> inline void VulkanResource<VkFence>::destroy(VkDevice device, VkFence handle) {
    vkDestroyFence(device, handle, nullptr);
}

template<> inline void VulkanResource<VkSemaphore>::destroy(VkDevice device, VkSemaphore handle) {
    vkDestroySemaphore(device, handle, nullptr);
}

template<> inline void VulkanResource<VkFramebuffer>::destroy(VkDevice device, VkFramebuffer handle) {
    vkDestroyFramebuffer(device, handle, nullptr);
}

template<> inline void VulkanResource<VkRenderPass>::destroy(VkDevice device, VkRenderPass handle) {
    vkDestroyRenderPass(device, handle, nullptr);
}

template<> inline void VulkanResource<VkDescriptorPool>::destroy(VkDevice device, VkDescriptorPool handle) {
    vkDestroyDescriptorPool(device, handle, nullptr);
}

template<> inline void VulkanResource<VkDescriptorSet>::destroy(VkDevice device, VkDescriptorSet handle) {
}


// Type aliases for convenience
using ImageResource = VulkanResource<VkImage>;
using ImageViewResource = VulkanResource<VkImageView>;
using BufferResource = VulkanResource<VkBuffer>;
using DeviceMemoryResource = VulkanResource<VkDeviceMemory>;
using PipelineResource = VulkanResource<VkPipeline>;
using ShaderModuleResource = VulkanResource<VkShaderModule>;
using DescriptorSetLayoutResource = VulkanResource<VkDescriptorSetLayout>;
using DescriptorPoolResource = VulkanResource<VkDescriptorPool>;
using DescriptorSetResource = VulkanResource<VkDescriptorSet>;
using PipelineLayoutResource = VulkanResource<VkPipelineLayout>;
using SamplerResource = VulkanResource<VkSampler>;
using SwapchainResource = VulkanResource<VkSwapchainKHR>;
using CommandPoolResource = VulkanResource<VkCommandPool>;
using FenceResource = VulkanResource<VkFence>;
using SemaphoreResource = VulkanResource<VkSemaphore>;
using FramebufferResource = VulkanResource<VkFramebuffer>;
using RenderPassResource = VulkanResource<VkRenderPass>;
#endif

namespace tremor::gfx {


    // Comparison operations (used for depth comparison and stencil operations)
    enum class CompareOp {
        Never,          // Always evaluates to false
        Less,           // Passes if source is less than destination
        Equal,          // Passes if source is equal to destination
        LessOrEqual,    // Passes if source is less than or equal to destination
        Greater,        // Passes if source is greater than destination
        NotEqual,       // Passes if source is not equal to destination
        GreaterOrEqual, // Passes if source is greater than or equal to destination
        Always          // Always evaluates to true
    };


    // Texture resource
    class Texture : public Resource {
    public:
        virtual ~Texture() = default;

        // Common texture interface methods
        virtual uint32_t getWidth() const = 0;
        virtual uint32_t getHeight() const = 0;
        virtual TextureFormat getFormat() const = 0;

    protected:
        Texture() = default;

        // Friendship with backend for creation
        friend class VulkanBackend;
        friend class D3D12Backend;
    };

    /*// Buffer resource
    class Buffer : public Resource {
    public:
        virtual ~Buffer() = default;

        // Common buffer interface methods
        virtual size_t getSize() const = 0;
        virtual void* map() = 0;
        virtual void unmap() = 0;

    protected:
        Buffer() = default;

        // Friendship with backend for creation
        friend class VulkanBackend;
        friend class D3D12Backend;
    };*/

    // Shader resource
    class Shader : public Resource {
    public:
        virtual ~Shader() = default;

        // Common shader interface methods
        virtual ShaderType getType() const = 0;

    protected:
        Shader() = default;

        // Friendship with backend for creation
        friend class VulkanBackend;
        friend class D3D12Backend;
    };

    // Specializations for different backend implementations
    class VulkanTexture : public Texture {
    public:

        uint32_t getWidth() const override { return width; }
        uint32_t getHeight() const override { return height; }
        TextureFormat getFormat() const override { return format; }

        // Vulkan-specific accessors
        VkImage getImage() const { return image; }
        VkImageView getImageView() const { return view; }

        ImageResource image;
        ImageViewResource view;
        DeviceMemoryResource memory;
        SamplerResource sampler;

        TextureFormat format = TextureFormat::Unknown;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t mipLevels = 1;

        explicit VulkanTexture(VkDevice device)
            : image(device), view(device), memory(device), sampler(device) {
        }

        
        // Allow backend to create this resource
        friend class VulkanBackend;
    };

    // Similarly for VulkanBuffer, VulkanShader, etc.

} // namespace tremor::gfx

namespace tremor::gfx {

    // Specific handle types with appropriate type checking
    using TextureHandle = Handle<Texture>;
    using BufferHandle = Handle<Buffer>;
    using ShaderHandle = Handle<Shader>;
    using PipelineHandle = Handle<Pipeline>;

    // Backend-specific handle types if needed
    using VulkanTextureHandle = Handle<VulkanTexture>;

    // Handle casting utilities
    template<typename To, typename From>
    Handle<To> handle_cast(const Handle<From>& handle) {
        return Handle<To>(dynamic_cast<To*>(handle.get()));
    }

}

namespace tremor::gfx {

    // Initial data for texture creation
    struct TextureData {
        const void* data = nullptr;          // Pointer to pixel data
        uint32_t bytesPerRow = 0;            // Bytes per row (for alignment)
        uint32_t bytesPerImage = 0;          // Bytes per image (for 3D textures or arrays)
        uint32_t mipLevel = 0;               // Mip level to initialize
        uint32_t arrayLayer = 0;             // Array layer to initialize
    };

    // Full texture creation descriptor
    struct TextureDesc {
        // Basic parameters
        uint32_t width = 1;                  // Texture width
        uint32_t height = 1;                 // Texture height
        uint32_t depth = 1;                  // Texture depth (for 3D textures)
        uint32_t arrayLayers = 1;            // Number of array layers
        uint32_t mipLevels = 1;              // Number of mip levels
        uint32_t sampleCount = 1;            // Number of samples for MSAA

        // Format and usage
        TextureFormat format = TextureFormat::R8G8B8A8_UNORM;
        TextureUsage usage = TextureUsage::ShaderRead | TextureUsage::TransferDst;

        // For texture view creation
        bool createView = true;              // Whether to create a default view
        bool isRenderTarget = false;         // Whether this is a render target

        // Sampler parameters (if needed)
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

        // Initial data (optional)
        TextureData initialData;

        // Debug name
        const char* debugName = nullptr;

        // Convenience constructor for 2D textures
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

            // Calculate default mip levels if using mipmaps
            if (hasFlag(usage, TextureUsage::GenerateMipmaps)) {
                desc.mipLevels = 1 + static_cast<uint32_t>(std::floor(std::log2(std::max(width, height))));
            }

            return desc;
        }

        // Convenience method for render targets
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

        // Convenience method for depth-stencil buffers
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

} // namespace tremor::gfx

namespace tremor::gfx {

    // Buffer descriptor for creation
    struct BufferDesc {
        // Basic parameters
        size_t size = 0;                      // Buffer size in bytes
        BufferUsage usage = BufferUsage::None; // Buffer usage flags
        MemoryType memoryType = MemoryType::GpuOnly; // Memory property

        // For structured buffers
        uint32_t stride = 0;                  // Stride for structured buffers (0 for raw buffers)

        // Initial data (optional)
        const void* initialData = nullptr;    // Initial buffer data

        // Debug name
        const char* debugName = nullptr;

        // Convenience methods for common buffer types
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

} // namespace tremor::gfx

namespace tremor::gfx {

    // Include path for shader compilation
    struct ShaderIncludePath {
        const char* name = nullptr;    // Include name/identifier
        const char* path = nullptr;    // Physical include path
    };

    // Define for shader compilation
    struct ShaderDefine {
        const char* name = nullptr;    // Define name
        const char* value = nullptr;   // Define value (can be nullptr for flag defines)
    };

    // Shader descriptor for creation
    struct ShaderDesc {
        // Basic parameters
        ShaderType type = ShaderType::Vertex;           // Shader stage/type
        ShaderSourceType sourceType = ShaderSourceType::GLSL; // Source language

        // Source code (one of these must be set)
        const char* sourceCode = nullptr;               // Text source code
        const void* byteCode = nullptr;                 // Binary byte code
        size_t byteCodeSize = 0;                        // Size of byte code

        // Source file (alternative to direct source)
        const char* filename = nullptr;                 // Source file path

        // Entry point
        const char* entryPoint = "main";                // Entry point function name

        // Compilation options
        bool optimize = true;                           // Enable optimizations

        // Include paths for resolving #include directives
        std::vector<ShaderIncludePath> includePaths;

        // Preprocessor defines
        std::vector<ShaderDefine> defines;

        // Debug name
        const char* debugName = nullptr;

        // Convenience method for GLSL shader from source code
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

        // Convenience method for HLSL shader from source code
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

        // Convenience method for SPIR-V shader from binary
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

        // Convenience method for shader from file
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

        // Add a preprocessor define
        ShaderDesc& addDefine(const char* name, const char* value = nullptr) {
            ShaderDefine define;
            define.name = name;
            define.value = value;
            defines.push_back(define);
            return *this;
        }

        // Add an include path
        ShaderDesc& addIncludePath(const char* name, const char* path) {
            ShaderIncludePath includePath;
            includePath.name = name;
            includePath.path = path;
            includePaths.push_back(includePath);
            return *this;
        }
    };

    // Filter mode conversion
    inline VkFilter convertFilterMode(FilterMode filter) {
        switch (filter) {
        case FilterMode::Nearest: return VK_FILTER_NEAREST;
        case FilterMode::Linear:  return VK_FILTER_LINEAR;
        case FilterMode::Cubic:   return VK_FILTER_CUBIC_EXT;
        default:                  return VK_FILTER_LINEAR;
        }
    }

    // Address mode conversion
    inline VkSamplerAddressMode convertAddressMode(AddressMode mode) {
        switch (mode) {
        case AddressMode::Repeat:          return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        case AddressMode::MirroredRepeat:  return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        case AddressMode::ClampToEdge:     return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case AddressMode::ClampToBorder:   return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        case AddressMode::MirrorClampToEdge: return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
        default:                           return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        }
    }

    // Compare op conversion
    inline VkCompareOp convertCompareOp(CompareOp op) {
        switch (op) {
        case CompareOp::Never:         return VK_COMPARE_OP_NEVER;
        case CompareOp::Less:          return VK_COMPARE_OP_LESS;
        case CompareOp::Equal:         return VK_COMPARE_OP_EQUAL;
        case CompareOp::LessOrEqual:   return VK_COMPARE_OP_LESS_OR_EQUAL;
        case CompareOp::Greater:       return VK_COMPARE_OP_GREATER;
        case CompareOp::NotEqual:      return VK_COMPARE_OP_NOT_EQUAL;
        case CompareOp::GreaterOrEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case CompareOp::Always:        return VK_COMPARE_OP_ALWAYS;
        default:                       return VK_COMPARE_OP_ALWAYS;
        }
    }

} // namespace tremor::gfx