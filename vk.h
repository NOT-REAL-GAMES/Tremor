#pragma once
#include "main.h"
#include "RenderBackendBase.h"
#include "res.h"

namespace tremor::gfx {

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



    class VulkanBackend : public RenderBackend {
    public:

        SDL_Window* w;

        VulkanBackend() {
            
        };
        ~VulkanBackend() override {};

        bool initialize(SDL_Window* window) override { 
        
            window = SDL_CreateWindow("Tremor Engine", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1280, 720, SDL_WINDOW_VULKAN);

            w = window;

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
            VulkanTexture* texture = new VulkanTexture();

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

            if (vkCreateImage(device, &imageInfo, nullptr, &texture->image) != VK_SUCCESS) {
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
        VkInstance instance = VK_NULL_HANDLE;
        VkSurfaceKHR surface = VK_NULL_HANDLE;
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

        // Private helper methods - non-virtual for better optimization
        bool createInstance();
        bool setupDebugMessenger();
        bool createSurface();
        bool pickPhysicalDevice();
        bool createLogicalDevice();
        bool createSwapChain();
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