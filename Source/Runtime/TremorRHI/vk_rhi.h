#pragma once

#include "../../../tremor_core.h"
#include "../../../tremor_graphics_platform.h"
#include "../../../gfx.h"
#include "../../../handle.h"
#include "../TremorRenderCore/gfx_resource_types.h"
#include "../TremorRenderCore/gfx_resource_handles.h"
#include "vk_resource_wrappers.h"

// Define concepts for Vulkan types
template<typename T>
concept VulkanStructure = requires(T t) {
    { t.sType } -> std::convertible_to<VkStructureType>;
    { t.pNext } -> std::convertible_to<void*>;
};

// Template function declarations - these need to be specialized for each type
template<typename T>
VkStructureType getVulkanStructureType();

template<typename T>
VkStructureType getStructureType();

// Template specializations for common Vulkan structures
template<> inline VkStructureType getVulkanStructureType<VkDeviceCreateInfo>() { return VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO; }
template<> inline VkStructureType getVulkanStructureType<VkPhysicalDeviceFeatures2>() { return VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2; }
template<> inline VkStructureType getVulkanStructureType<VkPhysicalDeviceVulkan12Features>() { return VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES; }
template<> inline VkStructureType getVulkanStructureType<VkPhysicalDeviceVulkan13Features>() { return VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES; }
template<> inline VkStructureType getVulkanStructureType<VkPhysicalDeviceProperties2>() { return VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2; }

// Alias getStructureType to getVulkanStructureType for consistency
template<typename T>
inline VkStructureType getStructureType() { return getVulkanStructureType<T>(); }

// Type-safe structure creation - KEEP INLINE
template<VulkanStructure T>
inline T createVulkanStructure() {
    T result{};
    result.sType = getVulkanStructureType<T>();
    return result;
}

// Instead of ZEROED_STRUCT macro - KEEP INLINE
template<typename T>
inline T createStructure() {
    T result{};
    result.sType = getStructureType<T>();
    return result;
}

// Instead of CHAIN_PNEXT macro - KEEP INLINE
template<typename T>
inline void chainStructure(void** ppNext, T& structure) {
    *ppNext = &structure;
    ppNext = &structure.pNext;
}

// Helper function to copy buffer data - DECLARATION ONLY
void copyBuffer(VkDevice device, VkCommandPool commandPool, VkQueue queue,
    VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);

namespace tremor::gfx {

class VulkanDevice {
public:
    struct VulkanDeviceCapabilities {
        bool dedicatedAllocation = false;
        bool fullScreenExclusive = false;
        bool rayQuery = false;
        bool meshShaders = false;
        bool bresenhamLineRasterization = false;
        bool nonSolidFill = false;
        bool multiDrawIndirect = false;
        bool sparseBinding = false;
        bool bufferDeviceAddress = false;
        bool dynamicRendering = false;
    };

    struct DevicePreferences {
        bool preferDiscreteGPU = true;
        bool requireMeshShaders = false;
        bool requireRayQuery = true;
        bool requireSparseBinding = true;
        int preferredDeviceIndex = -1;
    };

    VulkanDevice(VkInstance instance, VkSurfaceKHR surface,
        const DevicePreferences& preferences);

    ~VulkanDevice() {
        if (m_device != VK_NULL_HANDLE) {
            vkDestroyDevice(m_device, nullptr);
            m_device = VK_NULL_HANDLE;
        }
    }

    VulkanDevice(const VulkanDevice&) = delete;
    VulkanDevice& operator=(const VulkanDevice&) = delete;

    VulkanDevice(VulkanDevice&& other) noexcept;
    VulkanDevice& operator=(VulkanDevice&& other) noexcept;

    VkDevice device() const { return m_device; }
    VkPhysicalDevice physicalDevice() const { return m_physicalDevice; }
    VkQueue graphicsQueue() const { return m_graphicsQueue; }
    uint32_t graphicsQueueFamily() const { return m_graphicsQueueFamily; }

    const VulkanDeviceCapabilities& capabilities() const { return m_capabilities; }
    const VkPhysicalDeviceProperties& properties() const { return m_deviceProperties; }
    const VkPhysicalDeviceMemoryProperties& memoryProperties() const { return m_memoryProperties; }

    VkFormat colorFormat() const { return m_colorFormat; }
    VkFormat depthFormat() const { return m_depthFormat; }

    std::optional<uint32_t> findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;

    void setupBresenhamLineRasterization(VkPipelineRasterizationStateCreateInfo& rasterInfo) const;
    void setupFloatingOriginUniforms(VkDescriptorSetLayoutCreateInfo& layoutInfo) const;

private:
    VkInstance m_instance = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    uint32_t m_graphicsQueueFamily = 0;

    VkPhysicalDeviceProperties m_deviceProperties{};
    VkPhysicalDeviceFeatures2 m_deviceFeatures2{};
    VkPhysicalDeviceMemoryProperties m_memoryProperties{};

    VkFormat m_colorFormat = VK_FORMAT_UNDEFINED;
    VkFormat m_depthFormat = VK_FORMAT_UNDEFINED;

    VulkanDeviceCapabilities m_capabilities{};
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;

    void selectPhysicalDevice(const DevicePreferences& preferences);
    void createLogicalDevice(const DevicePreferences& preferences);
    void determineFormats();
    void logDeviceInfo() const;

    template<typename T>
    static T createStructure() {
        T result{};
        result.sType = getStructureType<T>();
        return result;
    }

    template<typename T>
    static VkStructureType getStructureType();
};

class Framebuffer {
public:
    struct CreateInfo {
        VkRenderPass renderPass;
        std::vector<VkImageView> attachments;
        uint32_t width;
        uint32_t height;
        uint32_t layers = 1;
    };

    Framebuffer(VkDevice device, const CreateInfo& createInfo);
    ~Framebuffer() = default;

    Framebuffer(const Framebuffer&) = delete;
    Framebuffer& operator=(const Framebuffer&) = delete;

    Framebuffer(Framebuffer&&) noexcept = default;
    Framebuffer& operator=(Framebuffer&&) noexcept = default;

    VkFramebuffer handle() const { return m_framebuffer; }
    operator VkFramebuffer() const { return m_framebuffer; }

    uint32_t width() const { return m_width; }
    uint32_t height() const { return m_height; }

private:
    VkDevice m_device;
    FramebufferResource m_framebuffer;
    uint32_t m_width;
    uint32_t m_height;
    uint32_t m_layers;
};

class VulkanResourceManager {
private:
    VkDevice m_device;
    VkPhysicalDevice m_physicalDevice;
    VkPhysicalDeviceMemoryProperties m_memProperties;

    std::unordered_map<uint32_t, std::unique_ptr<VulkanTexture>> m_textures;
    std::atomic<uint32_t> m_nextTextureId{ 1 };

public:
    VkDevice device() const { return m_device; }
    VkPhysicalDevice physicalDevice() const { return m_physicalDevice; }

    VulkanResourceManager(VkDevice device, VkPhysicalDevice physicalDevice)
        : m_device(device), m_physicalDevice(physicalDevice) {
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &m_memProperties);
    }

    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
        for (uint32_t i = 0; i < m_memProperties.memoryTypeCount; i++) {
            if ((typeFilter & (1 << i)) &&
                (m_memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }
        throw std::runtime_error("Failed to find suitable memory type");
    }

    TextureHandle createTexture(const TextureDesc& desc) {
        auto texture = std::make_unique<VulkanTexture>(m_device);
        texture->width = desc.width;
        texture->height = desc.height;
        texture->mipLevels = desc.mipLevels;
        texture->format = (desc.format);

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = { desc.width, desc.height, 1 };
        imageInfo.mipLevels = desc.mipLevels;
        imageInfo.arrayLayers = 1;
        imageInfo.format = convertFormat(texture->format);
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

        if (vkCreateImage(m_device, &imageInfo, nullptr, &texture->image.handle()) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create image");
        }

        VkMemoryRequirements memRequirements;
        vkGetImageMemoryRequirements(m_device, texture->image, &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(
            memRequirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
        );

        if (vkAllocateMemory(m_device, &allocInfo, nullptr, &texture->memory.handle()) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate image memory");
        }

        vkBindImageMemory(m_device, texture->image, texture->memory, 0);

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = texture->image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = convertFormat(texture->format);
        viewInfo.components = {
            VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY
        };
        viewInfo.subresourceRange = {
            VK_IMAGE_ASPECT_COLOR_BIT,
            0, desc.mipLevels,
            0, 1
        };

        if (vkCreateImageView(m_device, &viewInfo, nullptr, &texture->view.handle()) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create image view");
        }

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.mipLodBias = 0.0f;
        samplerInfo.anisotropyEnable = VK_TRUE;
        samplerInfo.maxAnisotropy = 16.0f;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = static_cast<float>(desc.mipLevels);

        if (vkCreateSampler(m_device, &samplerInfo, nullptr, &texture->sampler.handle()) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create sampler");
        }

        TextureHandle handle;
        handle.fromId(m_nextTextureId++);
        m_textures[handle.id] = std::move(texture);
        return handle;
    }

    VulkanTexture* getTexture(TextureHandle handle) {
        auto it = m_textures.find(handle.id);
        return it != m_textures.end() ? it->second.get() : nullptr;
    }

    void destroyTexture(TextureHandle handle) {
        m_textures.erase(handle.id);
    }
};

class SwapChain {
public:
    struct CreateInfo {
        uint32_t width = 0;
        uint32_t height = 0;
        bool vsync = true;
        bool hdr = false;
        uint32_t imageCount = 2;
        VkFormat preferredFormat = VK_FORMAT_B8G8R8A8_UNORM;
        VkColorSpaceKHR preferredColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    };

    VulkanDevice& m_device;

    SwapChain(VulkanDevice& device, VkSurfaceKHR surface, const CreateInfo& createInfo)
        : m_device(device), m_surface(surface), m_swapChain(device.device()) {
        createSwapChain(createInfo);
        createImageViews();

        Logger::get().info("Swap chain created: {}x{}, {} images, format: {}, {}",
            (int)m_extent.width, (int)m_extent.height, (int)m_images.size(),
            (int)m_imageFormat, m_vsync ? "VSync" : "No VSync");
    }

    ~SwapChain();

    SwapChain(const SwapChain&) = delete;
    SwapChain& operator=(const SwapChain&) = delete;

    SwapChain(SwapChain&& other) noexcept;
    SwapChain& operator=(SwapChain&& other) noexcept;

    void recreate(uint32_t width, uint32_t height);

    VkResult acquireNextImage(uint64_t timeout, VkSemaphore signalSemaphore, VkFence fence, uint32_t& outImageIndex);
    VkResult present(uint32_t imageIndex, VkSemaphore waitSemaphore);

    VkSwapchainKHR handle() const { return m_swapChain; }
    VkFormat imageFormat() const { return m_imageFormat; }
    VkExtent2D extent() const { return m_extent; }
    const std::vector<VkImage>& images() const { return m_images; }
    const std::vector<ImageViewResource>& imageViews() const { return m_imageViews; }
    uint32_t imageCount() const { return static_cast<uint32_t>(m_images.size()); }
    bool isVSync() const { return m_vsync; }
    bool isHDR() const { return m_hdr; }

private:
    VkSurfaceKHR m_surface;
    SwapchainResource m_swapChain;
    std::vector<VkImage> m_images;
    std::vector<ImageViewResource> m_imageViews;

    VkFormat m_imageFormat;
    VkColorSpaceKHR m_colorSpace;
    VkExtent2D m_extent;
    bool m_vsync = true;
    bool m_hdr = false;

    void createSwapChain(const CreateInfo& createInfo, VkSwapchainKHR oldSwapchain = VK_NULL_HANDLE);
    void cleanup();
    void createImageViews();

    VkSurfaceFormatKHR chooseSwapSurfaceFormat(
        const std::vector<VkSurfaceFormatKHR>& availableFormats,
        VkFormat preferredFormat,
        VkColorSpaceKHR preferredColorSpace);

    VkPresentModeKHR chooseSwapPresentMode(
        const std::vector<VkPresentModeKHR>& availablePresentModes,
        bool vsync);

    VkExtent2D chooseSwapExtent(
        const VkSurfaceCapabilitiesKHR& capabilities,
        uint32_t width,
        uint32_t height);
};

class DynamicRenderer {
public:
    struct ColorAttachment {
        VkImageView imageView = VK_NULL_HANDLE;
        VkImageLayout imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        VkResolveModeFlagBits resolveMode = VK_RESOLVE_MODE_NONE;
        VkImageView resolveImageView = VK_NULL_HANDLE;
        VkImageLayout resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        VkAttachmentStoreOp storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        VkClearValue clearValue{};
    };

    struct DepthStencilAttachment {
        VkImageView imageView = VK_NULL_HANDLE;
        VkImageLayout imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        VkAttachmentStoreOp storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        VkAttachmentLoadOp stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        VkAttachmentStoreOp stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        VkClearValue clearValue{};
    };

    struct RenderingInfo {
        VkRect2D renderArea{};
        uint32_t layerCount = 1;
        uint32_t viewMask = 0;
        std::vector<ColorAttachment> colorAttachments;
        std::optional<DepthStencilAttachment> depthStencilAttachment;
    };

    DynamicRenderer() = default;
    ~DynamicRenderer() = default;

    void begin(VkCommandBuffer cmdBuffer, const RenderingInfo& renderingInfo) {
        std::vector<VkRenderingAttachmentInfoKHR> colorAttachmentInfos;
        colorAttachmentInfos.reserve(renderingInfo.colorAttachments.size());

        for (const auto& colorAttachment : renderingInfo.colorAttachments) {
            VkRenderingAttachmentInfoKHR attachmentInfo{};
            attachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
            attachmentInfo.imageView = colorAttachment.imageView;
            attachmentInfo.imageLayout = colorAttachment.imageLayout;
            attachmentInfo.resolveMode = colorAttachment.resolveMode;
            attachmentInfo.resolveImageView = colorAttachment.resolveImageView;
            attachmentInfo.resolveImageLayout = colorAttachment.resolveImageLayout;
            attachmentInfo.loadOp = colorAttachment.loadOp;
            attachmentInfo.storeOp = colorAttachment.storeOp;
            attachmentInfo.clearValue = colorAttachment.clearValue;

            colorAttachmentInfos.push_back(attachmentInfo);
        }

        VkRenderingAttachmentInfoKHR depthAttachmentInfo{};
        if (renderingInfo.depthStencilAttachment) {
            depthAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
            depthAttachmentInfo.imageView = renderingInfo.depthStencilAttachment->imageView;
            depthAttachmentInfo.imageLayout = renderingInfo.depthStencilAttachment->imageLayout;
            depthAttachmentInfo.loadOp = renderingInfo.depthStencilAttachment->loadOp;
            depthAttachmentInfo.storeOp = renderingInfo.depthStencilAttachment->storeOp;
            depthAttachmentInfo.clearValue = renderingInfo.depthStencilAttachment->clearValue;
        }

        VkRenderingAttachmentInfoKHR stencilAttachmentInfo{};
        if (renderingInfo.depthStencilAttachment) {
            stencilAttachmentInfo = depthAttachmentInfo;
            stencilAttachmentInfo.loadOp = renderingInfo.depthStencilAttachment->stencilLoadOp;
            stencilAttachmentInfo.storeOp = renderingInfo.depthStencilAttachment->stencilStoreOp;
        }

        VkRenderingInfoKHR info{};
        info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR;
        info.renderArea = renderingInfo.renderArea;
        info.layerCount = renderingInfo.layerCount;
        info.viewMask = renderingInfo.viewMask;
        info.colorAttachmentCount = static_cast<uint32_t>(colorAttachmentInfos.size());
        info.pColorAttachments = colorAttachmentInfos.data();
        info.pDepthAttachment = renderingInfo.depthStencilAttachment ? &depthAttachmentInfo : nullptr;
        info.pStencilAttachment = renderingInfo.depthStencilAttachment ? &stencilAttachmentInfo : nullptr;

        vkCmdBeginRendering(cmdBuffer, &info);
    }

    void end(VkCommandBuffer cmdBuffer) {
        vkCmdEndRendering(cmdBuffer);
    }
};

} // namespace tremor::gfx
