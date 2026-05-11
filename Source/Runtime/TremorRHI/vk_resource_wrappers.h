#pragma once

#include "../TremorRenderCore/gfx_resource_handles.h"

#if defined(USING_VULKAN)
template<typename T, typename Destroyer>
class VulkanResource {
private:
    VkDevice m_device = VK_NULL_HANDLE;
    T m_handle = VK_NULL_HANDLE;

public:
    VulkanResource() = default;

    VulkanResource(VkDevice device, T handle = VK_NULL_HANDLE)
        : m_device(device), m_handle(handle) {
    }

    ~VulkanResource() = default;

    VulkanResource(const VulkanResource&) = delete;
    VulkanResource& operator=(const VulkanResource&) = delete;

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

    T& handle() { return m_handle; }
    const T& handle() const { return m_handle; }

    operator T() const {
        if (this == nullptr) {
            Logger::get().error("RESOURCE DOES NOT EXIST. FUCK");
            return VK_NULL_HANDLE;
        }
        return m_handle;
    }

    operator bool() const { return m_handle != VK_NULL_HANDLE; }

    T release() {
        T temp = m_handle;
        m_handle = VK_NULL_HANDLE;
        return temp;
    }

    void reset(T newHandle = VK_NULL_HANDLE) {
        cleanup();
        m_handle = newHandle;
    }

private:
    void cleanup() {
        if (m_handle != VK_NULL_HANDLE && m_device != VK_NULL_HANDLE) {
            Destroyer::destroy(m_device, m_handle);
        }
    }
};

struct DestroyVkImage { static void destroy(VkDevice device, VkImage handle) { vkDestroyImage(device, handle, nullptr); } };
struct DestroyVkImageView { static void destroy(VkDevice device, VkImageView handle) { vkDestroyImageView(device, handle, nullptr); } };
struct DestroyVkBuffer { static void destroy(VkDevice device, VkBuffer handle) { vkDestroyBuffer(device, handle, nullptr); } };
struct DestroyVkDeviceMemory { static void destroy(VkDevice device, VkDeviceMemory handle) { vkFreeMemory(device, handle, nullptr); } };
struct DestroyVkPipeline { static void destroy(VkDevice device, VkPipeline handle) { vkDestroyPipeline(device, handle, nullptr); } };
struct DestroyVkShaderModule { static void destroy(VkDevice device, VkShaderModule handle) { vkDestroyShaderModule(device, handle, nullptr); } };
struct DestroyVkDescriptorSetLayout { static void destroy(VkDevice device, VkDescriptorSetLayout handle) { vkDestroyDescriptorSetLayout(device, handle, nullptr); } };
struct DestroyVkPipelineLayout { static void destroy(VkDevice device, VkPipelineLayout handle) { vkDestroyPipelineLayout(device, handle, nullptr); } };
struct DestroyVkSampler { static void destroy(VkDevice device, VkSampler handle) { vkDestroySampler(device, handle, nullptr); } };
struct DestroyVkSwapchain { static void destroy(VkDevice device, VkSwapchainKHR handle) { vkDestroySwapchainKHR(device, handle, nullptr); } };
struct DestroyVkCommandPool { static void destroy(VkDevice device, VkCommandPool handle) { vkDestroyCommandPool(device, handle, nullptr); } };
struct DestroyVkFence { static void destroy(VkDevice device, VkFence handle) { vkDestroyFence(device, handle, nullptr); } };
struct DestroyVkSemaphore { static void destroy(VkDevice device, VkSemaphore handle) { vkDestroySemaphore(device, handle, nullptr); } };
struct DestroyVkFramebuffer { static void destroy(VkDevice device, VkFramebuffer handle) { vkDestroyFramebuffer(device, handle, nullptr); } };
struct DestroyVkRenderPass { static void destroy(VkDevice device, VkRenderPass handle) { vkDestroyRenderPass(device, handle, nullptr); } };
struct DestroyVkDescriptorPool { static void destroy(VkDevice device, VkDescriptorPool handle) { vkDestroyDescriptorPool(device, handle, nullptr); } };
struct DestroyVkDescriptorSet { static void destroy(VkDevice, VkDescriptorSet) {} };

using ImageResource = VulkanResource<VkImage, DestroyVkImage>;
using ImageViewResource = VulkanResource<VkImageView, DestroyVkImageView>;
using BufferResource = VulkanResource<VkBuffer, DestroyVkBuffer>;
using DeviceMemoryResource = VulkanResource<VkDeviceMemory, DestroyVkDeviceMemory>;
using PipelineResource = VulkanResource<VkPipeline, DestroyVkPipeline>;
using ShaderModuleResource = VulkanResource<VkShaderModule, DestroyVkShaderModule>;
using DescriptorSetLayoutResource = VulkanResource<VkDescriptorSetLayout, DestroyVkDescriptorSetLayout>;
using DescriptorPoolResource = VulkanResource<VkDescriptorPool, DestroyVkDescriptorPool>;
using DescriptorSetResource = VulkanResource<VkDescriptorSet, DestroyVkDescriptorSet>;
using PipelineLayoutResource = VulkanResource<VkPipelineLayout, DestroyVkPipelineLayout>;
using SamplerResource = VulkanResource<VkSampler, DestroyVkSampler>;
using SwapchainResource = VulkanResource<VkSwapchainKHR, DestroyVkSwapchain>;
using CommandPoolResource = VulkanResource<VkCommandPool, DestroyVkCommandPool>;
using FenceResource = VulkanResource<VkFence, DestroyVkFence>;
using SemaphoreResource = VulkanResource<VkSemaphore, DestroyVkSemaphore>;
using FramebufferResource = VulkanResource<VkFramebuffer, DestroyVkFramebuffer>;
using RenderPassResource = VulkanResource<VkRenderPass, DestroyVkRenderPass>;
#endif

namespace tremor::gfx {

    class VulkanTexture : public Texture {
    public:
        uint32_t getWidth() const override { return width; }
        uint32_t getHeight() const override { return height; }
        TextureFormat getFormat() const override { return format; }

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
    };

    using VulkanTextureHandle = Handle<VulkanTexture>;

} // namespace tremor::gfx
