#pragma once
#include "main.h"
#include "RenderBackendBase.h"
#include "res.h"
#include "mem.h"

// Define concepts for Vulkan types
template<typename T>
concept VulkanStructure = requires(T t) {
    { t.sType } -> std::convertible_to<VkStructureType>;
    { t.pNext } -> std::convertible_to<void*>;
};

// Type-safe structure creation
template<VulkanStructure T>
T createVulkanStructure() {
    T result{};
    result.sType = getVulkanStructureType<T>();
    return result;
}

// Instead of ZEROED_STRUCT macro:
template<typename T>
T createStructure() {
    T result{};
    result.sType = getStructureType<T>();
    return result;
}

// Instead of CHAIN_PNEXT macro:
template<typename T>
void chainStructure(void** ppNext, T& structure) {
    *ppNext = &structure;
    ppNext = &structure.pNext;
}

namespace tremor::gfx { 

    // Instance RAII wrapper
    class InstanceResource {
    private:
        VkInstance m_handle = VK_NULL_HANDLE;

    public:
        InstanceResource() = default;

        explicit InstanceResource(VkInstance handle) : m_handle(handle) {}

        ~InstanceResource() {
        }

        // Disable copying
        InstanceResource(const InstanceResource&) = delete;
        InstanceResource& operator=(const InstanceResource&) = delete;

        // Enable moving
        InstanceResource(InstanceResource&& other) noexcept
            : m_handle(other.m_handle) {
            other.m_handle = VK_NULL_HANDLE;
        }

        InstanceResource& operator=(InstanceResource&& other) noexcept {
            if (this != &other) {
                cleanup();
                m_handle = other.m_handle;
                other.m_handle = VK_NULL_HANDLE;
            }
            return *this;
        }

        // Accessors
        VkInstance& handle() { return m_handle; }
        const VkInstance& handle() const { return m_handle; }
        operator VkInstance() const { return m_handle; }

        // Check if valid
        operator bool() const { return m_handle != VK_NULL_HANDLE; }

        // Release ownership without destroying
        VkInstance release() {
            VkInstance temp = m_handle;
            m_handle = VK_NULL_HANDLE;
            return temp;
        }

        // Reset with a new handle
        void reset(VkInstance newHandle = VK_NULL_HANDLE) {
            cleanup();
            m_handle = newHandle;
        }

    private:
        void cleanup() {
            if (m_handle != VK_NULL_HANDLE) {
                vkDestroyInstance(m_handle, nullptr);
                m_handle = VK_NULL_HANDLE;
            }
        }
    };

    // Surface RAII wrapper
    class SurfaceResource {
    private:
        VkInstance m_instance = VK_NULL_HANDLE;
        VkSurfaceKHR m_handle = VK_NULL_HANDLE;

    public:
        SurfaceResource() = default;

        SurfaceResource(VkInstance instance, VkSurfaceKHR handle = VK_NULL_HANDLE)
            : m_instance(instance), m_handle(handle) {
        }

        ~SurfaceResource() {
            cleanup();
        }

        // Disable copying
        SurfaceResource(const SurfaceResource&) = delete;
        SurfaceResource& operator=(const SurfaceResource&) = delete;

        // Enable moving
        SurfaceResource(SurfaceResource&& other) noexcept
            : m_instance(other.m_instance), m_handle(other.m_handle) {
            other.m_handle = VK_NULL_HANDLE;
        }

        SurfaceResource& operator=(SurfaceResource&& other) noexcept {
            if (this != &other) {
                cleanup();
                m_instance = other.m_instance;
                m_handle = other.m_handle;
                other.m_handle = VK_NULL_HANDLE;
            }
            return *this;
        }

        // Accessors
        VkSurfaceKHR& handle() { return m_handle; }
        const VkSurfaceKHR& handle() const { return m_handle; }
        operator VkSurfaceKHR() const { return m_handle; }

        // Check if valid
        operator bool() const { return m_handle != VK_NULL_HANDLE; }

        // Release ownership without destroying
        VkSurfaceKHR release() {
            VkSurfaceKHR temp = m_handle;
            m_handle = VK_NULL_HANDLE;
            return temp;
        }

        // Reset with a new handle
        void reset(VkSurfaceKHR newHandle = VK_NULL_HANDLE) {
            cleanup();
            m_handle = newHandle;
        }

        // Update the instance (needed if instance changes)
        void setInstance(VkInstance instance) {
            m_instance = instance;
        }

    private:
        void cleanup() {
            if (m_handle != VK_NULL_HANDLE && m_instance != VK_NULL_HANDLE) {
                vkDestroySurfaceKHR(m_instance, m_handle, nullptr);
                m_handle = VK_NULL_HANDLE;
            }
        }
    };

    // Debug utils messenger wrapper (for debug builds)
    class DebugMessengerResource {
    private:
        VkInstance m_instance = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT m_handle = VK_NULL_HANDLE;

    public:
        DebugMessengerResource() = default;

        DebugMessengerResource(VkInstance instance, VkDebugUtilsMessengerEXT handle = VK_NULL_HANDLE)
            : m_instance(instance), m_handle(handle) {
        }

        ~DebugMessengerResource() {
            cleanup();
        }

        // Disable copying
        DebugMessengerResource(const DebugMessengerResource&) = delete;
        DebugMessengerResource& operator=(const DebugMessengerResource&) = delete;

        // Enable moving
        DebugMessengerResource(DebugMessengerResource&& other) noexcept
            : m_instance(other.m_instance), m_handle(other.m_handle) {
            other.m_handle = VK_NULL_HANDLE;
        }

        DebugMessengerResource& operator=(DebugMessengerResource&& other) noexcept {
            if (this != &other) {
                cleanup();
                m_instance = other.m_instance;
                m_handle = other.m_handle;
                other.m_handle = VK_NULL_HANDLE;
            }
            return *this;
        }

        // Accessors
        VkDebugUtilsMessengerEXT& handle() { return m_handle; }
        const VkDebugUtilsMessengerEXT& handle() const { return m_handle; }
        operator VkDebugUtilsMessengerEXT() const { return m_handle; }

        // Check if valid
        operator bool() const { return m_handle != VK_NULL_HANDLE; }

    private:
        void cleanup() {
            if (m_handle != VK_NULL_HANDLE && m_instance != VK_NULL_HANDLE) {
                // Using the function pointer directly since this is an extension function
                auto vkDestroyDebugUtilsMessengerEXT =
                    (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
                        m_instance, "vkDestroyDebugUtilsMessengerEXT");

                if (vkDestroyDebugUtilsMessengerEXT) {
                    vkDestroyDebugUtilsMessengerEXT(m_instance, m_handle, nullptr);
                }

                m_handle = VK_NULL_HANDLE;
            }
        }
    };

    // Example resource manager using these RAII resources
    class VulkanResourceManager {
    private:
        VkDevice m_device;
        VkPhysicalDevice m_physicalDevice;
        VkPhysicalDeviceMemoryProperties m_memProperties;

        std::unordered_map<uint32_t, std::unique_ptr<VulkanTexture>> m_textures;
        std::atomic<uint32_t> m_nextTextureId{ 1 };

    public:
        VulkanResourceManager(VkDevice device, VkPhysicalDevice physicalDevice)
            : m_device(device), m_physicalDevice(physicalDevice) {

            vkGetPhysicalDeviceMemoryProperties(physicalDevice, &m_memProperties);
        }

        // Find memory type helper
        uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
            for (uint32_t i = 0; i < m_memProperties.memoryTypeCount; i++) {
                if ((typeFilter & (1 << i)) &&
                    (m_memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                    return i;
                }
            }
            throw std::runtime_error("Failed to find suitable memory type");
        }

        // Example texture creation method
        TextureHandle createTexture(const TextureDesc& desc) {
            auto texture = std::make_unique<VulkanTexture>(m_device);
            texture->width = desc.width;
            texture->height = desc.height;
            texture->mipLevels = desc.mipLevels;
            texture->format = (desc.format);

            // Create image
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

            // Allocate memory
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

            // Bind memory to image
            vkBindImageMemory(m_device, texture->image, texture->memory, 0);

            // Create image view
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

            // Create sampler
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

            // Register texture and return handle
            TextureHandle handle; handle.fromId(m_nextTextureId++);
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

    class VulkanDevice {
    public:
        // Structure for tracking physical device capabilities
        struct VulkanDeviceCapabilities {
            bool dedicatedAllocation = false;
            bool fullScreenExclusive = false;
            bool rayQuery = false;
            bool meshShaders = false;
            bool bresenhamLineRasterization = false;
            bool nonSolidFill = false;
            bool multiDrawIndirect = false;
            bool sparseBinding = false;  // For megatextures
            bool bufferDeviceAddress = false;  // For ray tracing
            bool dynamicRendering = false;  // Modern rendering without render passes
        };

        // Structure for tracking preferences in device selection
        struct DevicePreferences {
            bool preferDiscreteGPU = true;
            bool requireMeshShaders = false;
            bool requireRayQuery = true;
            bool requireSparseBinding = true;  // For megatextures
            int preferredDeviceIndex = -1;     // -1 means auto-select
        };

        // Constructor
        VulkanDevice(VkInstance instance, VkSurfaceKHR surface,
            const DevicePreferences& preferences = {});

        // Destructor - automatically cleans up Vulkan resources
        ~VulkanDevice() {
            if (m_device != VK_NULL_HANDLE) {
                vkDestroyDevice(m_device, nullptr);
                m_device = VK_NULL_HANDLE;
            }
        }

        // Delete copy operations to prevent double-free
        VulkanDevice(const VulkanDevice&) = delete;
        VulkanDevice& operator=(const VulkanDevice&) = delete;

        // Move operations
        VulkanDevice(VulkanDevice&& other) noexcept;
        VulkanDevice& operator=(VulkanDevice&& other) noexcept;

        // Access the Vulkan handles
        VkDevice device() const { return m_device; }
        VkPhysicalDevice physicalDevice() const { return m_physicalDevice; }
        VkQueue graphicsQueue() const { return m_graphicsQueue; }
        uint32_t graphicsQueueFamily() const { return m_graphicsQueueFamily; }

        // Get device capabilities and properties
        const VulkanDeviceCapabilities& capabilities() const { return m_capabilities; }
        const VkPhysicalDeviceProperties& properties() const { return m_deviceProperties; }
        const VkPhysicalDeviceMemoryProperties& memoryProperties() const { return m_memoryProperties; }

        // Format information
        VkFormat colorFormat() const { return m_colorFormat; }
        VkFormat depthFormat() const { return m_depthFormat; }

        // Utility functions
        std::optional<uint32_t> findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;

        // Setup functions for specialized features
        void setupBresenhamLineRasterization(VkPipelineRasterizationStateCreateInfo& rasterInfo) const;
        void setupFloatingOriginUniforms(VkDescriptorSetLayoutCreateInfo& layoutInfo) const;

    private:
        // Vulkan handles
        VkInstance m_instance = VK_NULL_HANDLE;
        VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
        VkDevice m_device = VK_NULL_HANDLE;
        VkQueue m_graphicsQueue = VK_NULL_HANDLE;
        uint32_t m_graphicsQueueFamily = 0;

        // Device properties
        VkPhysicalDeviceProperties m_deviceProperties{};
        VkPhysicalDeviceFeatures2 m_deviceFeatures2{};
        VkPhysicalDeviceMemoryProperties m_memoryProperties{};

        // Formats
        VkFormat m_colorFormat = VK_FORMAT_UNDEFINED;
        VkFormat m_depthFormat = VK_FORMAT_UNDEFINED;

        // Capabilities
        VulkanDeviceCapabilities m_capabilities{};

        // The surface we're rendering to
        VkSurfaceKHR m_surface = VK_NULL_HANDLE;

        // Initialization helpers
        void selectPhysicalDevice(const DevicePreferences& preferences);
        void createLogicalDevice(const DevicePreferences& preferences);
        void determineFormats();
        void logDeviceInfo() const;

        // Template for structure initialization with sType
        template<typename T>
        static T createStructure() {
            T result{};
            result.sType = getStructureType<T>();
            return result;
        }

        // Helper to get correct sType for Vulkan structures
        template<typename T>
        static VkStructureType getStructureType();
    };



    class SwapChain {
    public:
        struct CreateInfo {
            uint32_t width = 0;
            uint32_t height = 0;
            bool vsync = true;
            bool hdr = false;
            uint32_t imageCount = 2;  // Double buffering by default
            VkFormat preferredFormat = VK_FORMAT_B8G8R8A8_UNORM;
            VkColorSpaceKHR preferredColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        };


        // Constructor takes a device, surface, and creation info
        SwapChain(const VulkanDevice& device, VkSurfaceKHR surface, const CreateInfo& createInfo) : m_device(device), m_surface(surface) {
            // Get logger from device if available
            // (This assumes VulkanDevice has a getLogger method - add if needed)
            // m_logger = device.getLogger();

            createSwapChain(createInfo);
            createImageViews();

                Logger::get().info("Swap chain created: {}x{}, {} images, format: {}, {}",
                    (int)m_extent.width, (int)m_extent.height, (int)m_images.size(),
                    (int)m_imageFormat, m_vsync ? "VSync" : "No VSync");
            
        }

        // Destructor to clean up Vulkan resources
        ~SwapChain();

        // No copying
        SwapChain(const SwapChain&) = delete;
        SwapChain& operator=(const SwapChain&) = delete;

        // Moving is allowed
        SwapChain(SwapChain&& other) noexcept;
        SwapChain& operator=(SwapChain&& other) noexcept;

        // Recreate the swap chain (e.g., on window resize)
        void recreate(uint32_t width, uint32_t height);

        // Get the next image index for rendering
        VkResult acquireNextImage(uint64_t timeout, VkSemaphore signalSemaphore, VkFence fence, uint32_t& outImageIndex);

        // Present the current image
        VkResult present(uint32_t imageIndex, VkSemaphore waitSemaphore);

        // Getters
        VkSwapchainKHR handle() const { return m_swapChain; }
        VkFormat imageFormat() const { return m_imageFormat; }
        VkExtent2D extent() const { return m_extent; }
        const std::vector<VkImage>& images() const { return m_images; }
        const std::vector<VkImageView>& imageViews() const { return m_imageViews; }
        uint32_t imageCount() const { return static_cast<uint32_t>(m_images.size()); }
        bool isVSync() const { return m_vsync; }
        bool isHDR() const { return m_hdr; }

    private:
        // References to external objects
        const VulkanDevice& m_device;
        VkSurfaceKHR m_surface;

        // Swap chain objects
        SwapchainResource m_swapChain;
        std::vector<VkImage> m_images;
        std::vector<VkImageView> m_imageViews;

        // Properties
        VkFormat m_imageFormat;
        VkColorSpaceKHR m_colorSpace;
        VkExtent2D m_extent;
        bool m_vsync = true;
        bool m_hdr = false;

        // Internal helpers
        void createSwapChain(const CreateInfo& createInfo);
        void cleanup();
        void createImageViews();

        // Choose swap chain properties
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

    // Implementation

    SwapChain::~SwapChain() {
        cleanup();
    }
    

    SwapChain::SwapChain(SwapChain&& other) noexcept
        : m_device(other.m_device),
        m_surface(other.m_surface),
        m_swapChain(std::move(other.m_swapChain)),  // Use std::move here!
        m_images(std::move(other.m_images)),
        m_imageViews(std::move(other.m_imageViews)),
        m_imageFormat(other.m_imageFormat),
        m_colorSpace(other.m_colorSpace),
        m_extent(other.m_extent),
        m_vsync(other.m_vsync),
        m_hdr(other.m_hdr) {

        // No need to reset m_swapChain since the move constructor of VulkanResource already handles that
        // But you might still want to clear these vectors if they're not already handled
        other.m_images.clear();
    }

    SwapChain& SwapChain::operator=(SwapChain&& other) noexcept {
        if (this != &other) {
            // Clean up existing resources
            cleanup();

            // Move resources from other
            m_swapChain = std::move(other.m_swapChain);
            m_images = std::move(other.m_images);
            m_imageViews = std::move(other.m_imageViews);
            m_imageFormat = other.m_imageFormat;
            m_colorSpace = other.m_colorSpace;
            m_extent = other.m_extent;
            m_vsync = other.m_vsync;
            m_hdr = other.m_hdr;

            // Invalidate the other swap chain
            other.m_swapChain = VK_NULL_HANDLE;
            other.m_images.clear();
            other.m_imageViews.clear();
        }

        return *this;
    }

    void SwapChain::recreate(uint32_t width, uint32_t height) {
        // Save current settings
        CreateInfo createInfo{};
        createInfo.width = width;
        createInfo.height = height;
        createInfo.vsync = m_vsync;
        createInfo.hdr = m_hdr;
        createInfo.imageCount = static_cast<uint32_t>(m_images.size());
        createInfo.preferredFormat = m_imageFormat;
        createInfo.preferredColorSpace = m_colorSpace;

        // Clean up existing swap chain
        cleanup();

        // Create new swap chain
        createSwapChain(createInfo);
        createImageViews();

        Logger::get().info("Swap chain recreated: {}x{}", m_extent.width, m_extent.height);
        
    }

    VkResult SwapChain::acquireNextImage(uint64_t timeout, VkSemaphore signalSemaphore, VkFence fence, uint32_t& outImageIndex) {
        return vkAcquireNextImageKHR(m_device.device(), m_swapChain, timeout, signalSemaphore, fence, &outImageIndex);
    }

    VkResult SwapChain::present(uint32_t imageIndex, VkSemaphore waitSemaphore) {
        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

        presentInfo.waitSemaphoreCount = waitSemaphore ? 1 : 0;
        presentInfo.pWaitSemaphores = waitSemaphore ? &waitSemaphore : nullptr;

        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &m_swapChain.handle();
        presentInfo.pImageIndices = &imageIndex;
        presentInfo.pResults = nullptr;

        return vkQueuePresentKHR(m_device.graphicsQueue(), &presentInfo);
    }

    void SwapChain::createSwapChain(const CreateInfo& createInfo) {
        // Query surface capabilities
        VkSurfaceCapabilitiesKHR capabilities;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_device.physicalDevice(), m_surface, &capabilities);

        // Query supported formats
        uint32_t formatCount;
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_device.physicalDevice(), m_surface, &formatCount, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_device.physicalDevice(), m_surface, &formatCount, formats.data());

        // Query supported present modes
        uint32_t presentModeCount;
        vkGetPhysicalDeviceSurfacePresentModesKHR(m_device.physicalDevice(), m_surface, &presentModeCount, nullptr);
        std::vector<VkPresentModeKHR> presentModes(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(m_device.physicalDevice(), m_surface, &presentModeCount, presentModes.data());

        // Choose format, present mode, and extent
        VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(formats, createInfo.preferredFormat, createInfo.preferredColorSpace);
        VkPresentModeKHR presentMode = chooseSwapPresentMode(presentModes, createInfo.vsync);
        VkExtent2D extent = chooseSwapExtent(capabilities, createInfo.width, createInfo.height);

        // Determine image count
        uint32_t imageCount = createInfo.imageCount;
        if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
            imageCount = capabilities.maxImageCount;
        }
        if (imageCount < capabilities.minImageCount) {
            imageCount = capabilities.minImageCount;
        }

        // Create swap chain
        VkSwapchainCreateInfoKHR swapchainInfo{};
        swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swapchainInfo.surface = m_surface;
        swapchainInfo.minImageCount = imageCount;
        swapchainInfo.imageFormat = surfaceFormat.format;
        swapchainInfo.imageColorSpace = surfaceFormat.colorSpace;
        swapchainInfo.imageExtent = extent;
        swapchainInfo.imageArrayLayers = 1;
        swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        // Add transfer usage for screenshots if needed
        swapchainInfo.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

        // Handle queue families
        swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        swapchainInfo.queueFamilyIndexCount = 0;
        swapchainInfo.pQueueFamilyIndices = nullptr;

        swapchainInfo.preTransform = capabilities.currentTransform;
        swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        swapchainInfo.presentMode = presentMode;
        swapchainInfo.clipped = VK_TRUE;
        swapchainInfo.oldSwapchain = VK_NULL_HANDLE;

        // Check for HDR support if requested
        m_hdr = createInfo.hdr && (surfaceFormat.colorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR);

        // Create the swap chain
        if (vkCreateSwapchainKHR(m_device.device(), &swapchainInfo, nullptr, &m_swapChain.handle()) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create swap chain");
        }

        // Get swap chain images
        vkGetSwapchainImagesKHR(m_device.device(), m_swapChain, &imageCount, nullptr);
        m_images.resize(imageCount);
        vkGetSwapchainImagesKHR(m_device.device(), m_swapChain, &imageCount, m_images.data());

        // Store format and extent
        m_imageFormat = surfaceFormat.format;
        m_colorSpace = surfaceFormat.colorSpace;
        m_extent = extent;
        m_vsync = (presentMode == VK_PRESENT_MODE_FIFO_KHR || presentMode == VK_PRESENT_MODE_FIFO_RELAXED_KHR);
    }

    void SwapChain::createImageViews() {
        m_imageViews.resize(m_images.size());

        for (size_t i = 0; i < m_images.size(); i++) {
            VkImageViewCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            createInfo.image = m_images[i];
            createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            createInfo.format = m_imageFormat;

            // Default color mapping
            createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

            // Only using color aspect
            createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            createInfo.subresourceRange.baseMipLevel = 0;
            createInfo.subresourceRange.levelCount = 1;
            createInfo.subresourceRange.baseArrayLayer = 0;
            createInfo.subresourceRange.layerCount = 1;

            if (vkCreateImageView(m_device.device(), &createInfo, nullptr, &m_imageViews[i]) != VK_SUCCESS) {
                throw std::runtime_error("Failed to create image view");
            }
        }
    }

    void SwapChain::cleanup() {
        // Destroy image views
        
        m_imageViews.clear();

        // Destroy swap chain
		m_swapChain.reset();

        // Images are owned by the swap chain and destroyed with it
        m_images.clear();
    }

    VkSurfaceFormatKHR SwapChain::chooseSwapSurfaceFormat(
        const std::vector<VkSurfaceFormatKHR>& availableFormats,
        VkFormat preferredFormat,
        VkColorSpaceKHR preferredColorSpace) {

        // First check for preferred format and color space
        for (const auto& availableFormat : availableFormats) {
            if (availableFormat.format == preferredFormat &&
                availableFormat.colorSpace == preferredColorSpace) {
                return availableFormat;
            }
        }

        // Check for HDR formats (for HDR displays)
        if (m_hdr) {
            const std::array<VkFormat, 3> hdrFormats = {
                VK_FORMAT_R16G16B16A16_SFLOAT,
                VK_FORMAT_A2B10G10R10_UNORM_PACK32,
                VK_FORMAT_R10X6G10X6B10X6A10X6_UNORM_4PACK16
            };

            for (auto hdrFormat : hdrFormats) {
                for (const auto& availableFormat : availableFormats) {
                    if (availableFormat.format == hdrFormat &&
                        availableFormat.colorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                        return availableFormat;
                    }
                }
            }
        }

        // Standard 8-bit formats
        for (const auto& availableFormat : availableFormats) {
            if (availableFormat.format == VK_FORMAT_B8G8R8A8_UNORM ||
                availableFormat.format == VK_FORMAT_R8G8B8A8_UNORM) {
                return availableFormat;
            }
        }

        // Fallback to first available format
        return availableFormats[0];
    }

    VkPresentModeKHR SwapChain::chooseSwapPresentMode(
        const std::vector<VkPresentModeKHR>& availablePresentModes,
        bool vsync) {

        if (vsync) {
            // FIFO is guaranteed to be available and provides proper vsync
            return VK_PRESENT_MODE_FIFO_KHR;
        }
        else {
            // Try to find immediate mode for no vsync
            for (const auto& mode : availablePresentModes) {
                if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
                    return mode;
                }
            }

            // Try mailbox for triple buffering
            for (const auto& mode : availablePresentModes) {
                if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
                    return mode;
                }
            }

            // Fallback to FIFO which is guaranteed to be available
            return VK_PRESENT_MODE_FIFO_KHR;
        }
    }

    VkExtent2D SwapChain::chooseSwapExtent(
        const VkSurfaceCapabilitiesKHR& capabilities,
        uint32_t width,
        uint32_t height) {

        if (capabilities.currentExtent.width != UINT32_MAX) {
            // Surface size is fixed
            return capabilities.currentExtent;
        }
        else {
            // Surface size can vary, clamp to allowed range
            VkExtent2D actualExtent = { width, height };

            actualExtent.width = std::clamp(actualExtent.width,
                capabilities.minImageExtent.width,
                capabilities.maxImageExtent.width);

            actualExtent.height = std::clamp(actualExtent.height,
                capabilities.minImageExtent.height,
                capabilities.maxImageExtent.height);

            return actualExtent;
        }
    }

    // Vulkan-specific device selection helper
    struct QueueFamilyIndices {
        std::optional<uint32_t> graphicsFamily;
        std::optional<uint32_t> presentFamily;

        bool isComplete() const {
            return graphicsFamily.has_value() && presentFamily.has_value();
        }
    };

    // Vulkan-specific swap chain information
    struct SwapChainSupportDetails {
        VkSurfaceCapabilitiesKHR capabilities;
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };

    // Class for managing Vulkan device and related resources
    // Template specializations for structure types
    template<> inline VkStructureType VulkanDevice::getStructureType<VkDeviceCreateInfo>() { return VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO; }
    template<> inline VkStructureType VulkanDevice::getStructureType<VkPhysicalDeviceFeatures2>() { return VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2; }
    template<> inline VkStructureType VulkanDevice::getStructureType<VkPhysicalDeviceVulkan12Features>() { return VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES; }
    template<> inline VkStructureType VulkanDevice::getStructureType<VkPhysicalDeviceVulkan13Features>() { return VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES; }
    template<> inline VkStructureType VulkanDevice::getStructureType<VkPhysicalDeviceProperties2>() { return VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2; }
    // Add more as needed...

    // Implementation

    VulkanDevice::VulkanDevice(VkInstance instance, VkSurfaceKHR surface,
        const DevicePreferences& preferences)
        : m_instance(instance), m_surface(surface) {

        // Initialize Volk
        VkResult volkResult = volkInitialize();
        if (volkResult != VK_SUCCESS) {
            throw std::runtime_error("Failed to initialize Volk");
        }

        // Load Vulkan instance-level functions
        volkLoadInstance(instance);

        // Select physical device
        selectPhysicalDevice(preferences);

        // Create logical device
        createLogicalDevice(preferences);

        // Load device-level functions through Volk
        volkLoadDevice(m_device);

        // Get device formats
        determineFormats();

        // Log device information
        logDeviceInfo();
    }

    void VulkanDevice::selectPhysicalDevice(const DevicePreferences& preferences) {
        // Count available devices
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);

        if (deviceCount == 0) {
            throw std::runtime_error("Failed to find any Vulkan physical devices");
        }

        // Get physical devices
        std::vector<VkPhysicalDevice> physicalDevices(deviceCount);
        vkEnumeratePhysicalDevices(m_instance, &deviceCount, physicalDevices.data());

        // Check if a specific device was requested
        if (preferences.preferredDeviceIndex >= 0 &&
            preferences.preferredDeviceIndex < static_cast<int>(deviceCount)) {
            m_physicalDevice = physicalDevices[preferences.preferredDeviceIndex];
        }
        else {
            // Select the best device based on preferences
            struct DeviceRanking {
                VkPhysicalDevice device;
                int score = 0;
            };

            std::vector<DeviceRanking> rankings;

            for (const auto& device : physicalDevices) {
                DeviceRanking ranking{ device, 0 };

                // Get device properties
                VkPhysicalDeviceProperties deviceProperties;
                vkGetPhysicalDeviceProperties(device, &deviceProperties);

                // Check for discrete GPU
                if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                    ranking.score += 1000;
                }

                // Check for queue families
                uint32_t queueFamilyCount = 0;
                vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
                std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
                vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

                bool hasGraphicsQueue = false;

                for (uint32_t i = 0; i < queueFamilyCount; i++) {
                    VkBool32 presentSupport = false;
                    vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_surface, &presentSupport);

                    if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT && presentSupport) {
                        hasGraphicsQueue = true;
                        break;
                    }
                }

                if (!hasGraphicsQueue) {
                    continue;  // Skip devices without graphics queue
                }

                // Check for extensions
                uint32_t extensionCount;
                vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
                std::vector<VkExtensionProperties> availableExtensions(extensionCount);
                vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

                bool hasSwapchainExtension = false;
                bool hasMeshShaderExtension = false;
                bool hasRayQueryExtension = false;
                bool hasSparseBindingSupport = false;
                bool hasBresenhamLineRasterization = false;

                for (const auto& extension : availableExtensions) {
                    if (strcmp(extension.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
                        hasSwapchainExtension = true;
                    }
                    if (strcmp(extension.extensionName, VK_EXT_MESH_SHADER_EXTENSION_NAME) == 0) {
                        hasMeshShaderExtension = true;
                        ranking.score += 100;
                    }
                    if (strcmp(extension.extensionName, VK_KHR_RAY_QUERY_EXTENSION_NAME) == 0) {
                        hasRayQueryExtension = true;
                        ranking.score += 200;
                    }
                    if (strcmp(extension.extensionName, VK_EXT_LINE_RASTERIZATION_EXTENSION_NAME) == 0) {
                        hasBresenhamLineRasterization = true;
                        ranking.score += 50;
                    }
                }

                // Check for sparse binding support
                VkPhysicalDeviceFeatures features;
                vkGetPhysicalDeviceFeatures(device, &features);
                hasSparseBindingSupport = features.sparseBinding;

                if (hasSparseBindingSupport) {
                    ranking.score += 150;  // Important for megatextures
                }

                // Mandatory extensions
                if (!hasSwapchainExtension) {
                    continue;  // Skip devices without swapchain
                }

                // Required features based on preferences
                if (preferences.requireMeshShaders && !hasMeshShaderExtension) {
                    continue;
                }

                if (preferences.requireRayQuery && !hasRayQueryExtension) {
                    continue;
                }

                if (preferences.requireSparseBinding && !hasSparseBindingSupport) {
                    continue;
                }

                // Score based on device properties
                ranking.score += deviceProperties.limits.maxImageDimension2D / 256;

                rankings.push_back(ranking);
            }

            // Sort by score
            std::sort(rankings.begin(), rankings.end(),
                [](const DeviceRanking& a, const DeviceRanking& b) {
                    return a.score > b.score;
                });

            if (rankings.empty()) {
                throw std::runtime_error("No suitable Vulkan device found");
            }

            m_physicalDevice = rankings[0].device;
        }

        // Get physical device properties
        vkGetPhysicalDeviceProperties(m_physicalDevice, &m_deviceProperties);
        vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &m_memoryProperties);
    }

    void VulkanDevice::createLogicalDevice(const DevicePreferences& preferences) {
        // Find queue family with graphics and present support
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, queueFamilies.data());

        // Find graphics queue family
        bool found = false;
        for (uint32_t i = 0; i < queueFamilyCount; i++) {
            VkBool32 presentSupport = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(m_physicalDevice, i, m_surface, &presentSupport);

            if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT && presentSupport) {
                m_graphicsQueueFamily = i;
                found = true;
                break;
            }
        }

        if (!found) {
            throw std::runtime_error("Could not find a queue family with both graphics and present support");
        }

        // Query device extensions
        uint32_t extensionCount;
        vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &extensionCount, nullptr);
        std::vector<VkExtensionProperties> availableExtensions(extensionCount);
        vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &extensionCount, availableExtensions.data());

        // Check for required extensions and set capabilities
        for (const auto& extension : availableExtensions) {
            if (strcmp(extension.extensionName, VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME) == 0) {
                m_capabilities.dedicatedAllocation = true;
            }
            else if (strcmp(extension.extensionName, "VK_EXT_full_screen_exclusive") == 0) {
                m_capabilities.fullScreenExclusive = true;
            }
            else if (strcmp(extension.extensionName, VK_KHR_RAY_QUERY_EXTENSION_NAME) == 0) {
                m_capabilities.rayQuery = true;
            }
            else if (strcmp(extension.extensionName, VK_EXT_MESH_SHADER_EXTENSION_NAME) == 0) {
                m_capabilities.meshShaders = true;
            }
            else if (strcmp(extension.extensionName, VK_EXT_LINE_RASTERIZATION_EXTENSION_NAME) == 0) {
                m_capabilities.bresenhamLineRasterization = true;
            }
        }

        // Set up device features
        // Vulkan 1.4 uses a chain of features structures
        auto features2 = createStructure<VkPhysicalDeviceFeatures2>();
        auto vulkan12Features = createStructure<VkPhysicalDeviceVulkan12Features>();
        auto vulkan13Features = createStructure<VkPhysicalDeviceVulkan13Features>();

        // Chain them together
        features2.pNext = &vulkan12Features;
        vulkan12Features.pNext = &vulkan13Features;

        // Get the features the device supports
        vkGetPhysicalDeviceFeatures2(m_physicalDevice, &features2);

        // Setup mesh shader features if available
        VkPhysicalDeviceMeshShaderFeaturesEXT meshShaderFeatures{};
        if (m_capabilities.meshShaders) {
            meshShaderFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;
            meshShaderFeatures.pNext = nullptr;

            // Chain it to the end
            vulkan13Features.pNext = &meshShaderFeatures;
        }

        // Setup line rasterization features if available
        VkPhysicalDeviceLineRasterizationFeaturesEXT lineRasterizationFeatures{};
        if (m_capabilities.bresenhamLineRasterization) {
            lineRasterizationFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_FEATURES_EXT;

            // Chain it properly
            if (m_capabilities.meshShaders) {
                lineRasterizationFeatures.pNext = nullptr;
                meshShaderFeatures.pNext = &lineRasterizationFeatures;
            }
            else {
                lineRasterizationFeatures.pNext = nullptr;
                vulkan13Features.pNext = &lineRasterizationFeatures;
            }
        }

        // Settings we want to enable
        m_capabilities.nonSolidFill = features2.features.fillModeNonSolid;
        m_capabilities.multiDrawIndirect = features2.features.multiDrawIndirect;
        m_capabilities.sparseBinding = features2.features.sparseBinding;
        m_capabilities.bufferDeviceAddress = vulkan12Features.bufferDeviceAddress;
        m_capabilities.dynamicRendering = vulkan13Features.dynamicRendering;

        // Create list of extensions we want to enable
        std::vector<const char*> deviceExtensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
        };

        // Add optional extensions based on capabilities
        if (m_capabilities.dedicatedAllocation) {
            deviceExtensions.push_back(VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME);
            deviceExtensions.push_back(VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME);
        }

        if (m_capabilities.fullScreenExclusive) {
            deviceExtensions.push_back("VK_EXT_full_screen_exclusive");
        }

        if (m_capabilities.rayQuery) {
            deviceExtensions.push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);
            deviceExtensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
            deviceExtensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
            deviceExtensions.push_back(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
        }

        if (m_capabilities.meshShaders) {
            deviceExtensions.push_back(VK_EXT_MESH_SHADER_EXTENSION_NAME);
        }

        if (m_capabilities.bresenhamLineRasterization) {
            deviceExtensions.push_back(VK_EXT_LINE_RASTERIZATION_EXTENSION_NAME);
        }

        // Create the logical device
        float queuePriority = 1.0f;
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = m_graphicsQueueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;

        // Create physical device features
        VkPhysicalDeviceFeatures deviceFeatures{};
        deviceFeatures.samplerAnisotropy = VK_TRUE;
        deviceFeatures.fillModeNonSolid = m_capabilities.nonSolidFill ? VK_TRUE : VK_FALSE;
        deviceFeatures.multiDrawIndirect = m_capabilities.multiDrawIndirect ? VK_TRUE : VK_FALSE;
        deviceFeatures.sparseBinding = m_capabilities.sparseBinding ? VK_TRUE : VK_FALSE;

        // Enable Vulkan 1.2 features
        vulkan12Features.bufferDeviceAddress = m_capabilities.bufferDeviceAddress ? VK_TRUE : VK_FALSE;
        vulkan12Features.descriptorIndexing = VK_TRUE;  // Needed for ray tracing

        // Enable Vulkan 1.3 features
        vulkan13Features.dynamicRendering = m_capabilities.dynamicRendering ? VK_TRUE : VK_FALSE;

        // Enable mesh shader features if available
        if (m_capabilities.meshShaders) {
            meshShaderFeatures.taskShader = VK_TRUE;
            meshShaderFeatures.meshShader = VK_TRUE;
        }

        // Enable line rasterization features if available
        if (m_capabilities.bresenhamLineRasterization) {
            lineRasterizationFeatures.bresenhamLines = VK_TRUE;
            lineRasterizationFeatures.rectangularLines = VK_TRUE;
        }

        // Create the device
        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.pNext = &features2;  // Chain of feature structures
        createInfo.queueCreateInfoCount = 1;
        createInfo.pQueueCreateInfos = &queueCreateInfo;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
        createInfo.ppEnabledExtensionNames = deviceExtensions.data();
        createInfo.pEnabledFeatures = nullptr;  // Using pNext chain instead

        if (vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create logical device");
        }

        // Get device queue
        vkGetDeviceQueue(m_device, m_graphicsQueueFamily, 0, &m_graphicsQueue);
    }

    void VulkanDevice::determineFormats() {
        // Find best color format
        m_colorFormat = VK_FORMAT_R8G8B8A8_UNORM;  // Default

        // Check if we can use higher bit depth
        VkFormatProperties formatProps;
        vkGetPhysicalDeviceFormatProperties(m_physicalDevice, VK_FORMAT_A2B10G10R10_UNORM_PACK32, &formatProps);

        constexpr VkFormatFeatureFlags requiredColorFeatures =
            VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
            VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT;

        if ((formatProps.optimalTilingFeatures & requiredColorFeatures) == requiredColorFeatures) {
            m_colorFormat = VK_FORMAT_A2B10G10R10_UNORM_PACK32;  // Higher precision
        }

        // Find best depth format
        const std::array<VkFormat, 3> depthFormats = {
            VK_FORMAT_D32_SFLOAT_S8_UINT,
            VK_FORMAT_D24_UNORM_S8_UINT,
            VK_FORMAT_D16_UNORM_S8_UINT
        };

        for (auto format : depthFormats) {
            vkGetPhysicalDeviceFormatProperties(m_physicalDevice, format, &formatProps);
            if (formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
                m_depthFormat = format;
                break;
            }
        }

        if (m_depthFormat == VK_FORMAT_UNDEFINED) {
            throw std::runtime_error("Failed to find supported depth format");
        }
    }

    void VulkanDevice::logDeviceInfo() const {
        //if (!m_logger) return;

        // Log device information
        std::string vendorName;
        switch (m_deviceProperties.vendorID) {
        case 0x1002: vendorName = "AMD"; break;
        case 0x10DE: vendorName = "NVIDIA"; break;
        case 0x8086: vendorName = "Intel"; break;
        case 0x13B5: vendorName = "ARM"; break;
        case 0x5143: vendorName = "Qualcomm"; break;
        default: vendorName = std::format("Unknown (0x{:X})", m_deviceProperties.vendorID);
        }

        // Log basic device info
        Logger::get().info("Selected GPU: {} ({})", m_deviceProperties.deviceName, vendorName);
        Logger::get().info("Driver version: {}.{}.{}",
            VK_VERSION_MAJOR(m_deviceProperties.driverVersion),
            VK_VERSION_MINOR(m_deviceProperties.driverVersion),
            VK_VERSION_PATCH(m_deviceProperties.driverVersion));

        // Log color and depth formats
        Logger::get().info("Color format: {}", m_colorFormat == VK_FORMAT_A2B10G10R10_UNORM_PACK32 ?
            "A2B10G10R10 (10-bit)" : "R8G8B8A8 (8-bit)");

        std::string depthFormatStr;
        switch (m_depthFormat) {
        case VK_FORMAT_D32_SFLOAT_S8_UINT: depthFormatStr = "D32_S8 (32-bit)"; break;
        case VK_FORMAT_D24_UNORM_S8_UINT: depthFormatStr = "D24_S8 (24-bit)"; break;
        case VK_FORMAT_D16_UNORM_S8_UINT: depthFormatStr = "D16_S8 (16-bit)"; break;
        default: depthFormatStr = "Unknown";
        }
        Logger::get().info("Depth format: {}", depthFormatStr);

        // Log capabilities
        Logger::get().info("Device capabilities:");
        Logger::get().info("  - Ray Query: {}", m_capabilities.rayQuery ? "Yes" : "No");
        Logger::get().info("  - Mesh Shaders: {}", m_capabilities.meshShaders ? "Yes" : "No");
        Logger::get().info("  - Bresenham Line Rasterization: {}", m_capabilities.bresenhamLineRasterization ? "Yes" : "No");
        Logger::get().info("  - Sparse Binding (MegaTextures): {}", m_capabilities.sparseBinding ? "Yes" : "No");
        Logger::get().info("  - Dynamic Rendering: {}", m_capabilities.dynamicRendering ? "Yes" : "No");
        Logger::get().info("  - Buffer Device Address: {}", m_capabilities.bufferDeviceAddress ? "Yes" : "No");
    }

    std::optional<uint32_t> VulkanDevice::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const {
        for (uint32_t i = 0; i < m_memoryProperties.memoryTypeCount; i++) {
            if ((typeFilter & (1 << i)) &&
                (m_memoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }

        return std::nullopt;
    }

    void VulkanDevice::setupBresenhamLineRasterization(VkPipelineRasterizationStateCreateInfo& rasterInfo) const {
        if (!m_capabilities.bresenhamLineRasterization) {
            return;  // Not supported
        }

        // Create line rasterization info
        VkPipelineRasterizationLineStateCreateInfoEXT lineRasterInfo{};
        lineRasterInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_LINE_STATE_CREATE_INFO_EXT;
        lineRasterInfo.lineRasterizationMode = VK_LINE_RASTERIZATION_MODE_BRESENHAM_EXT;
        lineRasterInfo.stippledLineEnable = VK_FALSE;

        // Link it to the rasterization state
        rasterInfo.pNext = &lineRasterInfo;
    }

    void VulkanDevice::setupFloatingOriginUniforms(VkDescriptorSetLayoutCreateInfo& layoutInfo) const {
        // Define bindings for camera-relative rendering
        std::vector<VkDescriptorSetLayoutBinding> bindings = {
            // Camera world position as 64-bit integers
            {
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
            }
        };

        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
    }

    class VulkanBackend : public RenderBackend {
    public:

		SwapChain::CreateInfo ci{};

        std::unique_ptr<VulkanDevice> vkDevice;
        std::unique_ptr<SwapChain> vkSwapchain;

        InstanceResource instance;
        SurfaceResource surface;

        SDL_Window* w;

#if _DEBUG
        VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
#endif

		VulkanBackend() = default;
        ~VulkanBackend() override = default;

        bool initialize(SDL_Window* window) override {
        

            w = window;

            createInstance();
            createDeviceAndSwapChain();

            return true;
        };
        void shutdown() override {};

        void beginFrame() override {};
        void endFrame() override {};

        TextureHandle createTexture(const TextureDesc& desc) {
            // Implementation of texture creation with Vulkan
            // This would include:
            // 1. Create VkImage
            // 2. Allocate and bind memory
            // 3. Create VkImageView
            // 4. Create VkSampler if needed
            // 5. Return a handle to the texture

            // Simplified implementation for example
            VulkanTexture* texture = new VulkanTexture(device);

            // Image creation
            VkImageCreateInfo imageInfo{};
            imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.extent.width = desc.width;
            imageInfo.extent.height = desc.height;
            imageInfo.extent.depth = 1;
            imageInfo.mipLevels = desc.mipLevels;
            imageInfo.arrayLayers = 1;
            imageInfo.format = convertFormat(desc.format);
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

            if (vkCreateImage(device, &imageInfo, nullptr, &texture->image.handle()) != VK_SUCCESS) {
                delete texture;
                return {};
            }
        }
        BufferHandle createBuffer(const BufferDesc& desc);
        ShaderHandle createShader(const ShaderDesc& desc);

        // Vulkan-specific methods
        VkDevice getDevice() const { return device; }
        VkPhysicalDevice getPhysicalDevice() const { return physicalDevice; }
        VkQueue getGraphicsQueue() const { return graphicsQueue; }

    private:
        // Vulkan instance and devices
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;

        // Queues
        VkQueue graphicsQueue = VK_NULL_HANDLE;
        VkQueue presentQueue = VK_NULL_HANDLE;

        // Swap chain
        VkSwapchainKHR swapChain = VK_NULL_HANDLE;
        std::vector<VkImage> swapChainImages;
        std::vector<VkImageView> swapChainImageViews;
        VkFormat swapChainImageFormat;
        VkExtent2D swapChainExtent;

        // Command submission
        VkCommandPool commandPool = VK_NULL_HANDLE;
        std::vector<VkCommandBuffer> commandBuffers;

        // Synchronization
        std::vector<VkSemaphore> imageAvailableSemaphores;
        std::vector<VkSemaphore> renderFinishedSemaphores;
        std::vector<VkFence> inFlightFences;
        size_t currentFrame = 0;

        bool get_surface_capabilities_2 = false;
        bool vulkan_1_4_available = false;

        bool debug_utils = false;
        bool memory_report = false;

        bool enableValidation = false;

        uint32_t m_gfxQueueFamilyIndex = 0;

        // Format information
        VkFormat m_colorFormat = VK_FORMAT_UNDEFINED;
        VkFormat m_depthFormat = VK_FORMAT_UNDEFINED;


        // Properties
        VkPhysicalDeviceMemoryProperties m_memoryProperties{};
        VkPhysicalDeviceProperties m_deviceProperties{};
        VkPhysicalDeviceFeatures m_deviceFeatures{};

        // Window reference
        SDL_Window* window = nullptr;

        // Validation layers
        bool enableValidationLayers = true;
        const std::vector<const char*> validationLayers = {
            "VK_LAYER_KHRONOS_validation"
        };

        // Device extensions
        const std::vector<const char*> deviceExtensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
        };

        bool createInstance() {
            unsigned int sdl_extension_count;
            VkResult	 err;
            uint32_t	 i;

            err = volkInitialize();

            if (err != VK_SUCCESS) {
                return false;
            }

            VkApplicationInfo appInfo{};
            appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
            appInfo.pApplicationName = "Tremor";
            appInfo.applicationVersion = VK_MAKE_VERSION(0, 0, 1);
            appInfo.pEngineName = "Tremor Engine";
            appInfo.engineVersion = VK_MAKE_VERSION(0, 0, 1);
            appInfo.apiVersion = VK_API_VERSION_1_4;  // Targeting Vulkan 1.4

            // Get SDL Vulkan extensions
            unsigned int sdlExtensionCount = 0;
            if (!SDL_Vulkan_GetInstanceExtensions(window, &sdlExtensionCount, nullptr)) {
                Logger::get().error( "SDL_Vulkan_GetInstanceExtensions failed: {}", SDL_GetError());
                return false;
            }

            // Allocate space for extensions (SDL + additional ones)
            auto instanceExtensions = tremor::mem::ScopedAlloc<const char*>(sdlExtensionCount + 5);
            if (!SDL_Vulkan_GetInstanceExtensions(window, &sdlExtensionCount, instanceExtensions.get())) {
                Logger::get().error("SDL_Vulkan_GetInstanceExtensions failed: {}", SDL_GetError());
                return false;
            }

            // Track our added extensions
            uint32_t additionalExtensionCount = 0;

            // Query available extensions
            uint32_t availableExtensionCount = 0;
            err = vkEnumerateInstanceExtensionProperties(nullptr, &availableExtensionCount, nullptr);
            if (err != VK_SUCCESS) {
                Logger::get().error("Failed to query instance extension count");
                return false;
            }

            // Check for optional extensions
            bool hasSurfaceCapabilities2 = false;
            bool hasDebugUtils = false;

            if (availableExtensionCount > 0) {
                auto extensionProps = tremor::mem::ScopedAlloc<VkExtensionProperties>(availableExtensionCount);

                err = vkEnumerateInstanceExtensionProperties(nullptr, &availableExtensionCount, extensionProps.get());
                if (err != VK_SUCCESS) {
                    Logger::get().error("Failed to enumerate instance extensions" );
                    return false;
                }

                for (uint32_t i = 0; i < availableExtensionCount; ++i) {
                    const char* extName = extensionProps[i].extensionName;

                    if (strcmp(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME, extName) == 0) {
                        hasSurfaceCapabilities2 = true;
                    }

#if _DEBUG
                    if (strcmp(VK_EXT_DEBUG_UTILS_EXTENSION_NAME, extName) == 0) {
                        hasDebugUtils = true;
                    }
#endif
                }
            }

            // Add optional extensions
            if (hasSurfaceCapabilities2) {
                instanceExtensions[sdlExtensionCount + additionalExtensionCount++] =
                    VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME;
            }

#if _DEBUG
            if (hasDebugUtils) {
                instanceExtensions[sdlExtensionCount + additionalExtensionCount++] =
                    VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
            }
#endif

            // Setup validation layers for debug builds
            std::vector<const char*> validationLayers;

#if _DEBUG
            // Check for the validation layer
            uint32_t layerCount;
            vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
            std::vector<VkLayerProperties> availableLayers(layerCount);
            vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

            // Print available layers
            Logger::get().info("Available Vulkan layers:");
            for (const auto& layer : availableLayers) {
                Logger::get().info("  {}", layer.layerName);

                // Check if it's the validation layer
                if (strcmp("VK_LAYER_KHRONOS_validation", layer.layerName) == 0) {
                    enableValidation = true;
                }
            }

            if (!enableValidation) {
                Logger::get().warning("Validation layer not found. Continuing without validation.");
                Logger::get().warning("To enable validation, use vkconfig from the Vulkan SDK.");
            }
            else {
                Logger::get().info("Validation layer found and enabled.");
            }
#else
            enableValidation = false;
#endif

            // Setup debug messenger creation info
            VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
#if _DEBUG
            if (enableValidation && hasDebugUtils) {
                debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
                debugCreateInfo.messageSeverity =
                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
                debugCreateInfo.messageType =
                    VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                    VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                    VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
                debugCreateInfo.pfnUserCallback = debugCallback;
                debugCreateInfo.pUserData = nullptr;
            }
#endif

            // Create the Vulkan instance
            VkInstanceCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
            createInfo.pApplicationInfo = &appInfo;
            createInfo.enabledExtensionCount = sdlExtensionCount + additionalExtensionCount;
            createInfo.ppEnabledExtensionNames = instanceExtensions.get();

#if _DEBUG
            if (enableValidation) {
                createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
                createInfo.ppEnabledLayerNames = validationLayers.data();
                createInfo.pNext = &debugCreateInfo;
            }
            else {
                createInfo.enabledLayerCount = 0;
                createInfo.pNext = nullptr;
            }
#else
            createInfo.enabledLayerCount = 0;
            createInfo.pNext = nullptr;
#endif

            // Create the instance
            VkInstance inst;
            err = vkCreateInstance(&createInfo, nullptr, &inst);
            if (err != VK_SUCCESS) {
                Logger::get().error("Failed to create Vulkan instance: {}", (int)err);
                return false;
            }

            instance.reset(inst);

            volkLoadInstance(instance);

            Logger::get().info("Vulkan instance created successfully");

            // Load instance-level functions
            volkLoadInstance(instance);

            // Setup debug messenger if enabled
#if _DEBUG
            if (enableValidation && hasDebugUtils) {
                err = vkCreateDebugUtilsMessengerEXT(
                    instance,
                    &debugCreateInfo,
                    nullptr,
                    &debugMessenger
                );

                if (err != VK_SUCCESS) {
                    Logger::get().error("Failed to set up debug messenger: {}", (int)err);
                    // Continue anyway, this is not fatal
                }
            }
#endif
            VkSurfaceKHR surf;
            if (!SDL_Vulkan_CreateSurface(w, instance, &surf)) {
                Logger::get().error("Failed to create Vulkan surface : {}", (int)err);
                return false;
            }

			surface = SurfaceResource(instance,surf);

            Logger::get().info("Vulkan surface created successfully");

            return true;
        }

#if _DEBUG
        // Debug callback function
        static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
            VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
            VkDebugUtilsMessageTypeFlagsEXT messageType,
            const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
            void* pUserData) {

            // Ignore some verbose messages
            if (strstr(pCallbackData->pMessage, "UNASSIGNED-CoreValidation-DrawState-ClearCmdBeforeDraw") != nullptr) {
                return VK_FALSE;
            }

            if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
                std::cerr << "Validation layer: " << pCallbackData->pMessage << std::endl;
            }

            return VK_FALSE;
        }
#endif
        

        bool createDeviceAndSwapChain() {

            VulkanDevice::DevicePreferences prefs;
            prefs.preferDiscreteGPU = true;
            prefs.requireMeshShaders = false;  // Set based on your requirements
            prefs.requireRayQuery = false;     // Set based on your requirements
            prefs.requireSparseBinding = false; // Set based on your requirements

            // Create device
            vkDevice = std::make_unique<VulkanDevice>(instance, surface, prefs);

            // Create swap chain
            SwapChain::CreateInfo swapChainInfo;
            int width, height;
            SDL_GetWindowSize(w, &width, &height);
            swapChainInfo.width = static_cast<uint32_t>(width);
            swapChainInfo.height = static_cast<uint32_t>(height);

            vkSwapchain = std::make_unique<SwapChain>(*vkDevice, surface, swapChainInfo);

            // Cache common device properties for convenience
            physicalDevice = vkDevice->physicalDevice();
            device = vkDevice->device();
            graphicsQueue = vkDevice->graphicsQueue();
            m_colorFormat = vkDevice->colorFormat();
            m_depthFormat = vkDevice->depthFormat();

            return true;
        }
        bool createImageViews();
        bool createRenderPass();
        bool createGraphicsPipeline();
        bool createFramebuffers();
        bool createCommandPool();
        bool createCommandBuffers();
        bool createSyncObjects();

        // Physical device selection
        bool isDeviceSuitable(VkPhysicalDevice device);
        QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);
        bool checkDeviceExtensionSupport(VkPhysicalDevice device);
        SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device);

        // Swap chain configuration
        VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
        VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
        VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);

        // Submission/presentation
        void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex);
    };

}