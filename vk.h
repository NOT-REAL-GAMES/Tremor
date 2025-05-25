#pragma once

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE  // Important for Vulkan depth range

#include "main.h"
#include "RenderBackendBase.h"
#include "res.h"
#include "mem.h"
#include <shaderc/shaderc.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <spirv_cross/spirv_cross.hpp>
#include <spirv_cross/spirv_glsl.hpp>

#include "gfx.h"
#include "quan.h"
#include "taffy/taffy.h"
#include "renderer/taffy_mesh.h"

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

// Helper function to copy buffer data
void copyBuffer(VkDevice device, VkCommandPool commandPool, VkQueue queue,
    VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {
    try {
        // Create command buffer for transfer
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = commandPool;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer;
        VkResult result = vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);
        if (result != VK_SUCCESS) {
            Logger::get().error("Failed to allocate transfer command buffer: {}", (int)result);
            return;
        }

        // Begin recording
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        result = vkBeginCommandBuffer(commandBuffer, &beginInfo);
        if (result != VK_SUCCESS) {
            Logger::get().error("Failed to begin command buffer: {}", (int)result);
            vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
            return;
        }

        // Copy from source to destination
        VkBufferCopy copyRegion{};
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = 0;
        copyRegion.size = size;
        vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

        // End recording
        result = vkEndCommandBuffer(commandBuffer);
        if (result != VK_SUCCESS) {
            Logger::get().error("Failed to end command buffer: {}", (int)result);
            vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
            return;
        }

        // Submit and wait
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        result = vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
        if (result != VK_SUCCESS) {
            Logger::get().error("Failed to submit transfer command buffer: {}", (int)result);
            vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
            return;
        }

        // Wait for the transfer to complete
        result = vkQueueWaitIdle(queue);
        if (result != VK_SUCCESS) {
            Logger::get().error("Failed to wait for queue idle: {}", (int)result);
        }

        // Free the temporary command buffer
        vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);

        Logger::get().info("Buffer copy completed successfully: {} bytes", size);
    }
    catch (const std::exception& e) {
        Logger::get().error("Exception during buffer copy: {}", e.what());
    }
}

namespace tremor::gfx {

        
    

        static std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions() {
            std::vector<VkVertexInputAttributeDescription> attributes(4);

            attributes[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MeshVertex, position) };
            attributes[1] = { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MeshVertex, normal) };
            attributes[2] = { 2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(MeshVertex, texCoord) };
            attributes[3] = { 3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(MeshVertex, tangent) };

            return attributes;
        }



    class Buffer {
    public:
        Buffer() = default;

        Buffer(VkDevice device, VkPhysicalDevice physicalDevice,
            VkDeviceSize size, VkBufferUsageFlags usage,
            VkMemoryPropertyFlags memoryProps)
            : m_device(device), m_size(size) {

            // Create buffer
            VkBufferCreateInfo bufferInfo{};
            bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferInfo.size = size;
            bufferInfo.usage = usage;
            bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            VkBuffer bufferHandle = VK_NULL_HANDLE;
            if (vkCreateBuffer(device, &bufferInfo, nullptr, &bufferHandle) != VK_SUCCESS) {
                throw std::runtime_error("Failed to create buffer");
            }
            m_buffer = BufferResource(device, bufferHandle);

            // Get memory requirements
            VkMemoryRequirements memRequirements;
            vkGetBufferMemoryRequirements(device, m_buffer, &memRequirements);

            // Allocate memory
            VkMemoryAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize = memRequirements.size;
            allocInfo.memoryTypeIndex = findMemoryType(
                physicalDevice, memRequirements.memoryTypeBits, memoryProps);

            VkDeviceMemory memoryHandle = VK_NULL_HANDLE;
            if (vkAllocateMemory(device, &allocInfo, nullptr, &memoryHandle) != VK_SUCCESS) {
                throw std::runtime_error("Failed to allocate buffer memory");
            }
            m_memory = DeviceMemoryResource(device, memoryHandle);

            // Bind memory to buffer
            vkBindBufferMemory(device, m_buffer, m_memory, 0);
        }

        // Map memory and update buffer data
        void update(const void* data, VkDeviceSize size, VkDeviceSize offset = 0) {
            if (!m_memory) {
                Logger::get().error("Attempting to update buffer with invalid memory");
                return;
            }

            if (size > m_size) {
                Logger::get().error("Buffer update size ({}) exceeds buffer size ({})", size, m_size);
                return;
            }

            void* mappedData = nullptr;
            VkResult result = vkMapMemory(m_device, m_memory, offset, size, 0, &mappedData);

            if (result != VK_SUCCESS) {
                Logger::get().error("Failed to map buffer memory: {}", (int)result);
                return;
            }

            memcpy(mappedData, data, static_cast<size_t>(size));

            VkMappedMemoryRange range = {
                VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
                nullptr,
                m_memory,
                offset,
                size
            };
            vkFlushMappedMemoryRanges(m_device, 1, &range);


            vkUnmapMemory(m_device, m_memory);
        }


        // Accessors
        VkBuffer getBuffer() const { return m_buffer; }
        VkDeviceSize getSize() const { return m_size; }
		VkDeviceMemory getMemory() const { return m_memory; }

    private:
        VkDevice m_device = VK_NULL_HANDLE;
        BufferResource m_buffer;
        DeviceMemoryResource m_memory;
        VkDeviceSize m_size = 0;

        // Helper function to find memory type
        uint32_t findMemoryType(VkPhysicalDevice physicalDevice,
            uint32_t typeFilter,
            VkMemoryPropertyFlags properties) {
            VkPhysicalDeviceMemoryProperties memProperties;
            vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

            for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
                if ((typeFilter & (1 << i)) &&
                    (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                    return i;
                }
            }

            throw std::runtime_error("Failed to find suitable memory type");
        }
    };


    // Helper function to infer shader type from filename
    inline ShaderType inferShaderTypeFromFilename(const std::string& filename) {
        if (filename.find(".vert") != std::string::npos) return ShaderType::Vertex;
        if (filename.find(".frag") != std::string::npos) return ShaderType::Fragment;
        if (filename.find(".comp") != std::string::npos) return ShaderType::Compute;
        if (filename.find(".geom") != std::string::npos) return ShaderType::Geometry;
        if (filename.find(".tesc") != std::string::npos) return ShaderType::TessControl;
        if (filename.find(".tese") != std::string::npos) return ShaderType::TessEvaluation;
        if (filename.find(".mesh") != std::string::npos) return ShaderType::Mesh;
        if (filename.find(".task") != std::string::npos) return ShaderType::Task;
        if (filename.find(".rgen") != std::string::npos) return ShaderType::RayGen;
        if (filename.find(".rmiss") != std::string::npos) return ShaderType::RayMiss;
        if (filename.find(".rchit") != std::string::npos) return ShaderType::RayClosestHit;
        if (filename.find(".rahit") != std::string::npos) return ShaderType::RayAnyHit;
        if (filename.find(".rint") != std::string::npos) return ShaderType::RayIntersection;
        if (filename.find(".rcall") != std::string::npos) return ShaderType::Callable;

        // Default to vertex if unknown
        return ShaderType::Vertex;
    }


    class ShaderCompiler {
    public:
        // Options for shader compilation
        struct CompileOptions {
            bool optimize = true;
            bool generateDebugInfo = false;
            std::vector<std::string> includePaths;
            std::unordered_map<std::string, std::string> macros;
        };

        // Initialize the compiler
        ShaderCompiler() {
            m_compiler = std::make_unique<shaderc::Compiler>();
            m_options = std::make_unique<shaderc::CompileOptions>();


            // Set proper SPIR-V version (1.6 is good for Vulkan 1.4)
            m_options->SetTargetSpirv(shaderc_spirv_version_1_6);

            m_options->SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);

            // Set target environment

            // Suppress warnings that might clutter your output
            //m_options->SetSuppressWarnings(); // Set to true if too verbose
            //m_options->SetWarningsAsErrors();  // Good for development
        }

        // Compile GLSL or HLSL source to SPIR-V
        std::vector<uint32_t> compileToSpv(
            const std::string& source,
            ShaderType type,
            const std::string& filename,
            int flags = 0) {

            // Create completely fresh options for this compilation
            shaderc::CompileOptions options;
            options.SetTargetSpirv(shaderc_spirv_version_1_6);
            options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);

            std::cout << "=== COMPILING: " << filename << " ===" << std::endl;
            std::cout << "Source preview: " << source.substr(0, 100) << std::endl;

            shaderc_shader_kind kind = getShaderKind(type);

            // Use the fresh options, not m_options
            shaderc::SpvCompilationResult result = m_compiler->CompileGlslToSpv(
                source, kind, filename.c_str(), options);  // Use 'options', not '*m_options'

            if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
                std::cout << "FULL ERROR MESSAGE:" << std::endl;
                std::cout << result.GetErrorMessage() << std::endl;
                return {};
            }

            std::cout << "SUCCESS!" << std::endl;
            return std::vector<uint32_t>(result.cbegin(), result.cend());
        }

        // Compile a shader file to SPIR-V
        std::vector<uint32_t> compileFileToSpv(
            const std::string& filename,
            ShaderType type,
            const CompileOptions& options = {}) {

            // Read file content
            std::ifstream file(filename);
            if (!file.is_open()) {
                Logger::get().error("Failed to open shader file: {}", filename);
                return {};
            }

            //size_t fileSize = static_cast<size_t>(file.tellg());
            //std::vector<char> shaderCode(fileSize);
            //file.seekg(0);
            //file.read(shaderCode.data(), fileSize);
            //file.close();

            //std::string sourceCode(shaderCode.begin(), shaderCode.end());

            std::stringstream buffer;
            buffer << file.rdbuf();
            file.close();

            // Compile the source
            return compileToSpv(buffer.str(), type, filename);

            // Compile the source
            //return compileToSpv(sourceCode, type, filename);
        }

    private:
        // Convert ShaderType to shaderc shader kind
        shaderc_shader_kind getShaderKind(ShaderType type) {
            switch (type) {
            case ShaderType::Vertex:
                return shaderc_vertex_shader;
            case ShaderType::Fragment:
                return shaderc_fragment_shader;
            case ShaderType::Compute:
                return shaderc_compute_shader;
            case ShaderType::Geometry:
                return shaderc_geometry_shader;
            case ShaderType::TessControl:
                return shaderc_tess_control_shader;
            case ShaderType::TessEvaluation:
                return shaderc_tess_evaluation_shader;
            case ShaderType::Mesh:
                return shaderc_mesh_shader;
            case ShaderType::Task:
                return shaderc_task_shader;
            case ShaderType::RayGen:
                return shaderc_raygen_shader;
            case ShaderType::RayMiss:
                return shaderc_miss_shader;
            case ShaderType::RayClosestHit:
                return shaderc_closesthit_shader;
            case ShaderType::RayAnyHit:
                return shaderc_anyhit_shader;
            case ShaderType::RayIntersection:
                return shaderc_intersection_shader;
            case ShaderType::Callable:
                return shaderc_callable_shader;
            default:
                return shaderc_vertex_shader;
            }
        }

        std::unique_ptr<shaderc::Compiler> m_compiler;
        std::unique_ptr<shaderc::CompileOptions> m_options;
    };


    class ShaderReflection {
    public:

        enum class ShaderStageType {
            Vertex,
            Fragment,
            Compute,
            Geometry,
            TessControl,
            TessEvaluation,
            Task,
            Mesh
            // Add others as needed
        };

        // Convert from VkShaderStageFlags to ShaderStageType
        ShaderStageType getStageType(VkShaderStageFlags flags) {
            if (flags & VK_SHADER_STAGE_VERTEX_BIT) return ShaderStageType::Vertex;
            if (flags & VK_SHADER_STAGE_FRAGMENT_BIT) return ShaderStageType::Fragment;
            if (flags & VK_SHADER_STAGE_COMPUTE_BIT) return ShaderStageType::Compute;
            if (flags & VK_SHADER_STAGE_GEOMETRY_BIT) return ShaderStageType::Geometry;
            if (flags & VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT) return ShaderStageType::TessControl;
            if (flags & VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT) return ShaderStageType::TessEvaluation;
            if (flags & VK_SHADER_STAGE_TASK_BIT_EXT) return ShaderStageType::Task;         // Add this
            if (flags & VK_SHADER_STAGE_MESH_BIT_EXT) return ShaderStageType::Mesh;         // Add this

            return ShaderStageType::Vertex; // Default
        }

        // Change the map to use the enum
        std::unordered_map<VkShaderStageFlags, std::vector<uint32_t>> m_spirvCode;


        struct ResourceBinding {
            uint32_t set;
            uint32_t binding;
            VkDescriptorType descriptorType;
            uint32_t count;  // Array size
            VkShaderStageFlags stageFlags;
            std::string name;
        };

        struct UBOMember {
            std::string name;
            uint32_t offset;
            uint32_t size;

            // Store essential type information rather than SPIRType directly
            struct TypeInfo {
                spirv_cross::SPIRType::BaseType baseType;
                uint32_t vecSize = 1;
                uint32_t columns = 1;
                std::vector<uint32_t> arrayDims;
            } typeInfo;
        };

        struct UniformBuffer {
            uint32_t set;
            uint32_t binding;
            uint32_t size;
            VkShaderStageFlags stageFlags;
            std::string name;
            uint32_t typeId;
            uint32_t baseTypeId;

            // Add this field to store members directly
            std::vector<UBOMember> members;
        };


        // Then update the getUBOMembers method
        // Then modify getUBOMembers to create a compiler on demand
        std::vector<UBOMember> getUBOMembers(const UniformBuffer& ubo) const {
            // Try to find SPIRV code for any stage that this UBO uses
            const VkShaderStageFlags stagesToTry[] = {
                VK_SHADER_STAGE_VERTEX_BIT,
                VK_SHADER_STAGE_FRAGMENT_BIT,
                VK_SHADER_STAGE_COMPUTE_BIT
                // Add others as needed
            };

            for (VkShaderStageFlags stage : stagesToTry) {
                if (ubo.stageFlags & stage) {
                    auto it = m_spirvCode.find(stage);
                    if (it != m_spirvCode.end()) {
                        // Create a compiler for this stage's SPIRV code
                        spirv_cross::CompilerGLSL compiler(it->second);

                        // Extract UBO members
                        std::vector<UBOMember> members;
                        const spirv_cross::SPIRType& type = compiler.get_type(ubo.baseTypeId);

                        for (uint32_t i = 0; i < type.member_types.size(); i++) {
                            UBOMember member;
                            member.name = compiler.get_member_name(ubo.baseTypeId, i);
                            member.offset = compiler.type_struct_member_offset(type, i);
                            member.size = compiler.get_declared_struct_member_size(type, i);

                            // Get member type information
                            const spirv_cross::SPIRType& memberType = compiler.get_type(type.member_types[i]);
                            member.typeInfo.baseType = memberType.basetype;
                            member.typeInfo.vecSize = memberType.vecsize;
                            member.typeInfo.columns = memberType.columns;
                            member.typeInfo.arrayDims = std::vector<uint32_t>(memberType.array.begin(), memberType.array.end());

                            members.push_back(member);
                        }

                        return members;
                    }
                }
            }

            return {}; // No compiler found for any relevant stage
        }


        struct PushConstantRange {
            VkShaderStageFlags stageFlags;
            uint32_t offset;
            uint32_t size;
        };

        ShaderReflection() = default;

        void reflect(const std::vector<uint32_t>& spirvCode, VkShaderStageFlags stageFlags) {
            // Convert flags to enum
            // Store the SPIRV code for this stage
            m_spirvCode[stageFlags] = spirvCode;

            // Create a local compiler for reflection (not stored)
            spirv_cross::CompilerGLSL compiler(spirvCode);
            spirv_cross::ShaderResources resources = compiler.get_shader_resources();
            Logger::get().info("Shader reflection found: {} uniform buffers, {} sampled images",
                resources.uniform_buffers.size(), resources.sampled_images.size());

            // REMOVE THIS ENTIRE SECTION - This is the first occurrence that's causing duplication
            // ----------------------------------------------------------------------------------
            // // Print all resources by name
            // for (const auto& resource : resources.sampled_images) {
            //     ResourceBinding binding;
            //     binding.set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
            //     binding.binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
            //     binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            //
            //     // Check if this is an array
            //     spirv_cross::SPIRType type = compiler.get_type(resource.type_id);
            //     binding.count = type.array.empty() ? 1 : type.array[0];
            //
            //     // Ensure stage flags are not zero
            //     binding.stageFlags = stageFlags;
            //     binding.name = resource.name;
            //
            //     Logger::get().info("Resource: {} (set {}, binding {})", binding.name, binding.set, binding.binding);
            //
            //     m_resourceBindings.push_back(binding);
            // }
            // ----------------------------------------------------------------------------------

            // Process uniform buffers
            for (const auto& resource : resources.uniform_buffers) {
                UniformBuffer ubo;
                // Set basic properties
                ubo.set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
                ubo.binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
                ubo.name = resource.name;
                ubo.stageFlags = stageFlags;
                ubo.typeId = resource.type_id;
                ubo.baseTypeId = resource.base_type_id;

                // Get UBO size
                const spirv_cross::SPIRType& type = compiler.get_type(resource.base_type_id);
                ubo.size = compiler.get_declared_struct_size(type);

                // Extract members immediately
                for (uint32_t i = 0; i < type.member_types.size(); i++) {
                    UBOMember member;
                    member.name = compiler.get_member_name(resource.base_type_id, i);
                    member.offset = compiler.type_struct_member_offset(type, i);
                    member.size = compiler.get_declared_struct_member_size(type, i);

                    // Get member type info
                    const spirv_cross::SPIRType& memberType = compiler.get_type(type.member_types[i]);
                    member.typeInfo.baseType = memberType.basetype;
                    member.typeInfo.vecSize = memberType.vecsize;
                    member.typeInfo.columns = memberType.columns;
                    member.typeInfo.arrayDims = std::vector<uint32_t>(memberType.array.begin(), memberType.array.end());

                    ubo.members.push_back(member);
                }

                // Store UBO
                m_uniformBuffers.push_back(ubo);

                // Also add to resource bindings
                ResourceBinding binding;
                binding.set = ubo.set;
                binding.binding = ubo.binding;
                binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                binding.count = 1;
                binding.stageFlags = stageFlags;
                binding.name = ubo.name;
                m_resourceBindings.push_back(binding);
            }

            // Process storage buffers
            for (const auto& resource : resources.storage_buffers) {
                ResourceBinding binding;
                binding.set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
                binding.binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
                binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                binding.count = 1;
                binding.stageFlags = stageFlags;
                binding.name = resource.name;
                m_resourceBindings.push_back(binding);
            }

            // Process combined image samplers - THIS IS THE CORRECT ONE TO KEEP
            for (const auto& resource : resources.sampled_images) {
                ResourceBinding binding;
                binding.set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
                binding.binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
                binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

                // Check if this is an array
                spirv_cross::SPIRType type = compiler.get_type(resource.type_id);
                binding.count = type.array.empty() ? 1 : type.array[0];

                binding.stageFlags = stageFlags;
                binding.name = resource.name;

                // If you want to keep the logging, move it here
                Logger::get().info("Resource: {} (set {}, binding {})", binding.name, binding.set, binding.binding);

                m_resourceBindings.push_back(binding);
            }

            // Process separate images
            for (const auto& resource : resources.separate_images) {
                ResourceBinding binding;
                binding.set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
                binding.binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
                binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;

                spirv_cross::SPIRType type = compiler.get_type(resource.type_id);
                binding.count = type.array.empty() ? 1 : type.array[0];

                binding.stageFlags = stageFlags;
                binding.name = resource.name;
                m_resourceBindings.push_back(binding);
            }

            // Process separate samplers
            for (const auto& resource : resources.separate_samplers) {
                ResourceBinding binding;
                binding.set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
                binding.binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
                binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;

                spirv_cross::SPIRType type = compiler.get_type(resource.type_id);
                binding.count = type.array.empty() ? 1 : type.array[0];

                binding.stageFlags = stageFlags;
                binding.name = resource.name;
                m_resourceBindings.push_back(binding);
            }

            // Process storage images
            for (const auto& resource : resources.storage_images) {
                ResourceBinding binding;
                binding.set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
                binding.binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
                binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;

                spirv_cross::SPIRType type = compiler.get_type(resource.type_id);
                binding.count = type.array.empty() ? 1 : type.array[0];

                binding.stageFlags = stageFlags;
                binding.name = resource.name;
                m_resourceBindings.push_back(binding);
            }

            // Process push constants
            for (const auto& resource : resources.push_constant_buffers) {
                const spirv_cross::SPIRType& type = compiler.get_type(resource.base_type_id);
                uint32_t size = compiler.get_declared_struct_size(type);

                PushConstantRange range;
                range.stageFlags = stageFlags;
                range.offset = 0; // Will be calculated when merging with other stages
                range.size = size;
                m_pushConstantRanges.push_back(range);
            }

            // Process vertex input attributes (for vertex shaders only)
            if (stageFlags & VK_SHADER_STAGE_VERTEX_BIT) {
                for (const auto& resource : resources.stage_inputs) {
                    const uint32_t location = compiler.get_decoration(resource.id, spv::DecorationLocation);
                    const spirv_cross::SPIRType& type = compiler.get_type(resource.type_id);

                    VertexAttribute attr;
                    attr.location = location;
                    attr.name = resource.name;

                    // Determine format based on type
                    attr.format = getFormatFromType(type);

                    m_vertexAttributes.push_back(attr);
                }
            }
        }

        // Merge reflection data from multiple shaders (e.g., vertex + fragment)
        void merge(const ShaderReflection& other) {
            // Merge resource bindings, combining stage flags for duplicate bindings
            for (const auto& binding : other.m_resourceBindings) {
                auto it = std::find_if(m_resourceBindings.begin(), m_resourceBindings.end(),
                    [&binding](const ResourceBinding& existing) {
                        return existing.set == binding.set &&
                            existing.binding == binding.binding &&
                            existing.descriptorType == binding.descriptorType;
                    });

                if (it != m_resourceBindings.end()) {
                    it->stageFlags |= binding.stageFlags;
                }
                else {
                    m_resourceBindings.push_back(binding);
                }
            }

            // For UBOs
            for (const auto& ubo : other.m_uniformBuffers) {
                auto it = std::find_if(m_uniformBuffers.begin(), m_uniformBuffers.end(),
                    [&ubo](const UniformBuffer& existing) {
                        return existing.set == ubo.set && existing.binding == ubo.binding;
                    });

                if (it != m_uniformBuffers.end()) {
                    // Combine stage flags
                    it->stageFlags |= ubo.stageFlags;

                    // Preserve member info if we don't have it yet
                    if (it->members.empty() && !ubo.members.empty()) {
                        it->members = ubo.members;
                    }
                }
                else {
                    m_uniformBuffers.push_back(ubo);
                }
            }

            // Merge push constant ranges with proper offsets
            for (const auto& range : other.m_pushConstantRanges) {
                // For simplicity we'll just add the new range
                // A more complete implementation would merge overlapping ranges
                m_pushConstantRanges.push_back(range);
            }

            // Merge vertex attributes (typically only from vertex shader, but included for completeness)
            for (const auto& attr : other.m_vertexAttributes) {
                if (std::find_if(m_vertexAttributes.begin(), m_vertexAttributes.end(),
                    [&attr](const VertexAttribute& existing) {
                        return existing.location == attr.location;
                    }) == m_vertexAttributes.end()) {
                    m_vertexAttributes.push_back(attr);
                }
            }
        }

        std::unique_ptr<DescriptorSetLayoutResource> createDescriptorSetLayout(VkDevice device, uint32_t setNumber) const {

            std::vector<VkDescriptorSetLayoutBinding> bindings;

            // Debug output before creating layout
            Logger::get().info("Creating descriptor set layout for set {}", setNumber);

            for (const auto& binding : m_resourceBindings) {
                if (binding.set == setNumber) {
                    VkDescriptorSetLayoutBinding layoutBinding{};
                    layoutBinding.binding = binding.binding;
                    layoutBinding.descriptorType = binding.descriptorType;
                    layoutBinding.descriptorCount = binding.count;
                    layoutBinding.stageFlags = binding.stageFlags;
                    layoutBinding.pImmutableSamplers = nullptr;

                    // Log each binding we're adding
                    const char* typeStr = "Unknown";
                    switch (binding.descriptorType) {
                    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER: typeStr = "UBO"; break;
                    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: typeStr = "Sampler"; break;
                    default: typeStr = "Other"; break;
                    }

                    Logger::get().info("  Adding binding {}.{}: {} (count={}, stages=0x{:X})",
                        binding.set, binding.binding, typeStr, binding.count, binding.stageFlags);

                    bindings.push_back(layoutBinding);
                }
            }

            if (bindings.empty()) {
                Logger::get().info("No bindings found for set {}", setNumber);
                //return nullptr;  // No bindings for this set, continue anyway...
            }

            VkDescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
            layoutInfo.pBindings = bindings.data();

            Logger::get().info("Creating descriptor set layout with {} bindings", bindings.size());

            auto layout = std::make_unique<DescriptorSetLayoutResource>(device);

            VkResult result = vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &layout->handle());
            if (result != VK_SUCCESS) {
                Logger::get().error("Failed to create descriptor set layout for set {}: Error code {}",
                    setNumber, static_cast<int>(result));
                return nullptr;
            }

            Logger::get().info("Successfully created descriptor set layout for set {}", setNumber);
            return layout;
        }


        // Create pipeline layout with all descriptor set layouts and push constants
        std::unique_ptr<PipelineLayoutResource> createPipelineLayout(VkDevice device) const {
            // First determine how many unique descriptor sets we need
            uint32_t maxSet = 0;
            for (const auto& binding : m_resourceBindings) {
                maxSet = std::max(maxSet, binding.set);
            }

            std::vector<std::unique_ptr<DescriptorSetLayoutResource>> setLayouts;
            std::vector<VkDescriptorSetLayout> rawSetLayouts;

            // Create a layout for each set
            for (uint32_t i = 0; i <= maxSet; i++) {
                auto layout = createDescriptorSetLayout(device, i);
                if (layout != nullptr) {
                    rawSetLayouts.push_back(layout->handle());
                    setLayouts.push_back(std::move(layout));
                }
                else {
                    // No bindings for this set, but we need to maintain the set indexes
                    // Create an empty layout
                    VkDescriptorSetLayoutCreateInfo emptyLayoutInfo{};
                    emptyLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
                    emptyLayoutInfo.bindingCount = 0;

                    auto emptyLayout = std::make_unique<DescriptorSetLayoutResource>(device);
                    if (vkCreateDescriptorSetLayout(device, &emptyLayoutInfo, nullptr, &emptyLayout->handle()) != VK_SUCCESS) {
                        Logger::get().error("Failed to create empty descriptor set layout for set {}", i);
                        return nullptr;
                    }

                    rawSetLayouts.push_back(emptyLayout->handle());
                    setLayouts.push_back(std::move(emptyLayout));
                }
            }

            // Organize push constant ranges
            std::vector<VkPushConstantRange> pushConstantRanges;
            for (const auto& range : m_pushConstantRanges) {
                VkPushConstantRange vkRange{};
                vkRange.stageFlags = range.stageFlags;
                vkRange.offset = range.offset;
                vkRange.size = range.size;
                pushConstantRanges.push_back(vkRange);
            }

            // Create the pipeline layout
            VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
            pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(rawSetLayouts.size());
            pipelineLayoutInfo.pSetLayouts = rawSetLayouts.data();
            pipelineLayoutInfo.pushConstantRangeCount = static_cast<uint32_t>(pushConstantRanges.size());
            pipelineLayoutInfo.pPushConstantRanges = pushConstantRanges.empty() ? nullptr : pushConstantRanges.data();

            VkPipelineLayout pipelineLayout;
            if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
                Logger::get().error("Failed to create pipeline layout");
                return nullptr;
            }

            return std::make_unique<PipelineLayoutResource>(device, pipelineLayout);
        }

        // Create a descriptor pool based on the reflected shader needs
        std::unique_ptr<DescriptorPoolResource> createDescriptorPool(VkDevice device, uint32_t maxSets = 100) const {
            // Count needed descriptors by type
            std::unordered_map<VkDescriptorType, uint32_t> typeCount;

            for (const auto& binding : m_resourceBindings) {
                typeCount[binding.descriptorType] += binding.count;
            }

            // Create pool sizes for each needed descriptor type
            std::vector<VkDescriptorPoolSize> poolSizes;
            for (const auto& [type, count] : typeCount) {
                VkDescriptorPoolSize poolSize{};
                poolSize.type = type;
                poolSize.descriptorCount = count * maxSets; // Multiply by max sets to allow multiple allocations
                poolSizes.push_back(poolSize);
            }

            // Create the descriptor pool
            VkDescriptorPoolCreateInfo poolInfo{};
            poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT; // Allow freeing individual sets
            poolInfo.maxSets = maxSets;
            poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
            poolInfo.pPoolSizes = poolSizes.data();

            auto pool = std::make_unique<DescriptorPoolResource>(device);
            if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool->handle()) != VK_SUCCESS) {
                Logger::get().error("Failed to create descriptor pool");
                return nullptr;
            }

            return pool;
        }

        // Generate vertex input state based on vertex attributes
        VkPipelineVertexInputStateCreateInfo createVertexInputState() const {
            // For simplicity, we'll assume a single binding
            // A more complete implementation would support multiple vertex buffers

            // Sort attributes by location
            std::vector<VertexAttribute> sortedAttrs = m_vertexAttributes;
            std::sort(sortedAttrs.begin(), sortedAttrs.end(),
                [](const VertexAttribute& a, const VertexAttribute& b) {
                    return a.location < b.location;
                });

            // Create attribute descriptions
            m_attributeDescriptions.clear();
            for (const auto& attr : sortedAttrs) {
                VkVertexInputAttributeDescription attrDesc{};
                attrDesc.location = attr.location;
                attrDesc.binding = 0;  // Single binding
                attrDesc.format = attr.format;

                // Calculate offset based on previous attributes
                uint32_t offset = 0;
                for (size_t i = 0; i < m_attributeDescriptions.size(); i++) {
                    offset += getFormatSize(m_attributeDescriptions[i].format);
                }
                attrDesc.offset = offset;

                m_attributeDescriptions.push_back(attrDesc);
            }

            // Create binding description
            uint32_t stride = 0;
            for (const auto& attr : m_attributeDescriptions) {
                stride += getFormatSize(attr.format);
            }

            m_bindingDescription.binding = 0;
            m_bindingDescription.stride = stride;
            m_bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

            // Create vertex input state
            m_vertexInputState = {};
            m_vertexInputState.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            m_vertexInputState.vertexBindingDescriptionCount = 1;
            m_vertexInputState.pVertexBindingDescriptions = &m_bindingDescription;
            m_vertexInputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(m_attributeDescriptions.size());
            m_vertexInputState.pVertexAttributeDescriptions = m_attributeDescriptions.data();

            return m_vertexInputState;
        }

        struct VertexAttribute {
            uint32_t location;
            std::string name;
            VkFormat format;
        };

        // Getters for reflected data
        const std::vector<ResourceBinding>& getResourceBindings() const { return m_resourceBindings; }
        const std::vector<UniformBuffer>& getUniformBuffers() const { return m_uniformBuffers; }
        const std::vector<PushConstantRange>& getPushConstantRanges() const { return m_pushConstantRanges; }
        const std::vector<VertexAttribute>& getVertexAttributes() const { return m_vertexAttributes; }

    private:


        // Helper to determine VkFormat from SPIR-V type
        VkFormat getFormatFromType(const spirv_cross::SPIRType& type) const {
            // Basic mapping from SPIR-V types to Vulkan formats
            if (type.basetype == spirv_cross::SPIRType::Float) {
                if (type.vecsize == 1) return VK_FORMAT_R32_SFLOAT;
                if (type.vecsize == 2) return VK_FORMAT_R32G32_SFLOAT;
                if (type.vecsize == 3) return VK_FORMAT_R32G32B32_SFLOAT;
                if (type.vecsize == 4) return VK_FORMAT_R32G32B32A32_SFLOAT;
            }
            else if (type.basetype == spirv_cross::SPIRType::Int) {
                if (type.vecsize == 1) return VK_FORMAT_R32_SINT;
                if (type.vecsize == 2) return VK_FORMAT_R32G32_SINT;
                if (type.vecsize == 3) return VK_FORMAT_R32G32B32_SINT;
                if (type.vecsize == 4) return VK_FORMAT_R32G32B32A32_SINT;
            }
            else if (type.basetype == spirv_cross::SPIRType::UInt) {
                if (type.vecsize == 1) return VK_FORMAT_R32_UINT;
                if (type.vecsize == 2) return VK_FORMAT_R32G32_UINT;
                if (type.vecsize == 3) return VK_FORMAT_R32G32B32_UINT;
                if (type.vecsize == 4) return VK_FORMAT_R32G32B32A32_UINT;
            }

            // Default for unknown type
            return VK_FORMAT_UNDEFINED;
        }

        // Helper to get size of a format in bytes
        uint32_t getFormatSize(VkFormat format) const {
            switch (format) {
            case VK_FORMAT_R32_SFLOAT:
            case VK_FORMAT_R32_UINT:
            case VK_FORMAT_R32_SINT:
                return 4;
            case VK_FORMAT_R32G32_SFLOAT:
            case VK_FORMAT_R32G32_UINT:
            case VK_FORMAT_R32G32_SINT:
                return 8;
            case VK_FORMAT_R32G32B32_SFLOAT:
            case VK_FORMAT_R32G32B32_UINT:
            case VK_FORMAT_R32G32B32_SINT:
                return 12;
            case VK_FORMAT_R32G32B32A32_SFLOAT:
            case VK_FORMAT_R32G32B32A32_UINT:
            case VK_FORMAT_R32G32B32A32_SINT:
                return 16;
            default:
                return 0;
            }
        }

        std::vector<ResourceBinding> m_resourceBindings;
        std::vector<UniformBuffer> m_uniformBuffers;
        std::vector<PushConstantRange> m_pushConstantRanges;
        std::vector<VertexAttribute> m_vertexAttributes;

        // Store created descriptions for vertex input state
        mutable VkVertexInputBindingDescription m_bindingDescription;
        mutable std::vector<VkVertexInputAttributeDescription> m_attributeDescriptions;
        mutable VkPipelineVertexInputStateCreateInfo m_vertexInputState;
    };


    class ShaderModule {
    public:

        const std::vector<uint32_t>& getSPIRVCode() const { return m_spirvCode; }

        // Default constructor
        ShaderModule() = default;

        // Constructor with device and raw module
        ShaderModule(VkDevice device, VkShaderModule rawModule, ShaderType type = ShaderType::Vertex)
            : m_device(device), m_type(type), m_entryPoint("main") {
            if (rawModule != VK_NULL_HANDLE) {
                m_module = std::make_unique<ShaderModuleResource>(device, rawModule);
                Logger::get().info("Shader module created with raw handle");
                if (m_spirvCode.size() > 0) {
                    m_reflection = std::make_unique<ShaderReflection>();
                    m_reflection->reflect(m_spirvCode, getShaderStageFlagBits());
                }
            }
        }

        // Destructor - resource is automatically cleaned up by RAII
        ~ShaderModule() = default;

        std::unique_ptr<ShaderReflection> m_reflection;

        const ShaderReflection* getReflection() const { if (!m_reflection) { Logger::get().error("NO REFLECTION FOUND"); } return m_reflection.get(); }
        ShaderReflection* getReflection() { if (!m_reflection) { Logger::get().error("NO REFLECTION FOUND"); } return m_reflection.get(); }

        // Load from precompiled SPIR-V file
        static std::unique_ptr<ShaderModule> loadFromFile(VkDevice device, const std::string& filename,
            ShaderType type = ShaderType::Vertex,
            const std::string& entryPoint = "main") {
            // Read file...
            std::ifstream file(filename);

            if (!file.is_open()) {
                Logger::get().error("Failed to open shader file: {}", filename);
                return nullptr;
            }

            size_t fileSize = static_cast<size_t>(file.tellg());
            std::vector<char> shaderCode(fileSize);

            file.seekg(0);
            file.read(shaderCode.data(), fileSize);
            file.close();

            // Create module
            VkShaderModuleCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            createInfo.codeSize = fileSize;
            createInfo.pCode = reinterpret_cast<const uint32_t*>(shaderCode.data());

            VkShaderModule shaderModule = VK_NULL_HANDLE;
            if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
                Logger::get().error("Failed to create shader module from file: {}", filename);
                return nullptr;
            }

            // Create ShaderModule object
            auto result = std::make_unique<ShaderModule>();
            result->m_device = device;
            result->m_module = std::make_unique<ShaderModuleResource>(device, shaderModule);
            result->m_type = type;
            result->m_entryPoint = entryPoint;
            result->m_filename = filename;

            // ADD THIS: Store the SPIRV code for reflection
            result->m_spirvCode.resize(fileSize / sizeof(uint32_t));
            memcpy(result->m_spirvCode.data(), shaderCode.data(), fileSize);

            // ADD THIS: Initialize reflection
            result->m_reflection = std::make_unique<ShaderReflection>();
            result->m_reflection->reflect(result->m_spirvCode, result->getShaderStageFlagBits());

            return result;
        }

        // Create shader stage info for pipeline creation
        VkPipelineShaderStageCreateInfo createShaderStageInfo() const {
            VkPipelineShaderStageCreateInfo stageInfo{};
            stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stageInfo.stage = getShaderStageFlagBits();
            stageInfo.module = m_module ? m_module->handle() : VK_NULL_HANDLE;
            stageInfo.pName = m_entryPoint.c_str();
            return stageInfo;
        }

        // Get the raw Vulkan handle directly
        VkShaderModule getHandle() const {
            return m_module ? m_module->handle() : VK_NULL_HANDLE;
        }

        // Check if valid
        bool isValid() const {
            return m_module && m_module->handle() != VK_NULL_HANDLE;
        }

        // Explicit conversion operator
        explicit operator bool() const {
            return isValid();
        }

        // Access methods
        ShaderType getType() const { return m_type; }
        const std::string& getEntryPoint() const { return m_entryPoint; }
        const std::string& getFilename() const { return m_filename; }

        // Compile and load from GLSL/HLSL source
        static std::unique_ptr<ShaderModule> compileFromSource(
            VkDevice device,
            const std::string& source,
            ShaderType type,
            const std::string& filename = "unnamed_shader",
            const std::string& entryPoint = "main",
            const ShaderCompiler::CompileOptions& options = {}) {

            // Compile to SPIR-V
            static ShaderCompiler compiler;
            auto spirv = compiler.compileToSpv(source, type, filename);

            if (spirv.empty()) {
                // Compilation failed
                return nullptr;
            }

            // Create shader module
            VkShaderModuleCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            createInfo.codeSize = spirv.size() * sizeof(uint32_t);
            createInfo.pCode = spirv.data();

            VkShaderModule shaderModule = VK_NULL_HANDLE;
            if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
                Logger::get().error("Failed to create shader module from compiled source");
                return nullptr;
            }

            // Create ShaderModule object
            auto result = std::make_unique<ShaderModule>();
            result->m_device = device;
            result->m_module = std::make_unique<ShaderModuleResource>(device, shaderModule);
            result->m_type = type;
            result->m_entryPoint = entryPoint;
            result->m_filename = filename;
            result->m_spirvCode = std::move(spirv);

            return result;
        }

        // Compile and load from GLSL/HLSL file
        static std::unique_ptr<ShaderModule> compileFromFile(
            VkDevice device,
            const std::string& filename,
            const std::string& entryPoint = "main",
            int flags = 0) {

            // Detect shader type from filename
            ShaderType type = inferShaderTypeFromFilename(filename);

            // Compile file to SPIR-V
            static ShaderCompiler compiler;
            auto spirv = compiler.compileFileToSpv(filename, type);

            if (spirv.empty()) {
                // Compilation failed
                return nullptr;
            }

            // Create shader module
            VkShaderModuleCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            createInfo.codeSize = spirv.size() * sizeof(uint32_t);
            createInfo.pCode = spirv.data();

            VkShaderModule shaderModule = VK_NULL_HANDLE;
            if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
                Logger::get().error("Failed to create shader module from compiled file: {}", filename);
                return nullptr;
            }

            // Create ShaderModule object
            auto result = std::make_unique<ShaderModule>();
            result->m_device = device;
            result->m_module = std::make_unique<ShaderModuleResource>(device, shaderModule);
            result->m_type = type;
            result->m_entryPoint = entryPoint;
            result->m_filename = filename;
            result->m_spirvCode = std::move(spirv);

            // ADD THIS: Initialize reflection
            result->m_reflection = std::make_unique<ShaderReflection>();
            result->m_reflection->reflect(result->m_spirvCode, result->getShaderStageFlagBits());

            return result;
        }

    private:
        std::vector<uint32_t> m_spirvCode; // Store SPIR-V code

        // Convert shader type to Vulkan stage flag
        VkShaderStageFlagBits getShaderStageFlagBits() const {
            switch (m_type) {
            case ShaderType::Vertex:         return VK_SHADER_STAGE_VERTEX_BIT;
            case ShaderType::Fragment:       return VK_SHADER_STAGE_FRAGMENT_BIT;
            case ShaderType::Compute:        return VK_SHADER_STAGE_COMPUTE_BIT;
            case ShaderType::Geometry:       return VK_SHADER_STAGE_GEOMETRY_BIT;
            case ShaderType::TessControl:    return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
            case ShaderType::TessEvaluation: return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
            case ShaderType::Mesh:           return VK_SHADER_STAGE_MESH_BIT_EXT;
            case ShaderType::Task:           return VK_SHADER_STAGE_TASK_BIT_EXT;
            case ShaderType::RayGen:         return VK_SHADER_STAGE_RAYGEN_BIT_KHR;
            case ShaderType::RayMiss:        return VK_SHADER_STAGE_MISS_BIT_KHR;
            case ShaderType::RayClosestHit:  return VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
            case ShaderType::RayAnyHit:      return VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
            case ShaderType::RayIntersection:return VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
            case ShaderType::Callable:       return VK_SHADER_STAGE_CALLABLE_BIT_KHR;
            default:                         return VK_SHADER_STAGE_VERTEX_BIT;
            }
        }

    private:
        VkDevice m_device = VK_NULL_HANDLE;
        std::unique_ptr<ShaderModuleResource> m_module;
        ShaderType m_type = ShaderType::Vertex;
        std::string m_entryPoint = "main";
        std::string m_filename;
    };


    // Forward declarations
    class Camera;
    class RenderContext;

    // Forward declaration for camera planes





    // ===============================
    // Octree Implementation for 64-bit coordinates
    // ===============================


    // Represents a cluster in our 3D grid
    

    // A light with all properties needed for shading
    

    // Information about an object to be rendered




    // Standard floating-point AABB

    // Quantized Axis-Aligned Bounding Box for 64-bit coordinates

    // Helper functions for AABB/frustum testing




    // ===============================
    // Clustered Renderer
    // ===============================

    // Cluster grid dimensions (typically 16x16x24)
    

    // Enhanced renderable object

        // Vulkan-specific command buffer wrapper
    class VulkanRenderCommandBuffer : public RenderCommandBuffer {
    public:
        VulkanRenderCommandBuffer(VkCommandBuffer cmdBuffer) : m_cmdBuffer(cmdBuffer) {}
        VkCommandBuffer getHandle() const { return m_cmdBuffer; }

    private:
        VkCommandBuffer m_cmdBuffer;
    };

    class VulkanClusteredRenderer : public ClusteredRenderer {
    public:
        VulkanClusteredRenderer(VkDevice device, VkPhysicalDevice physicalDevice,
            VkQueue graphicsQueue, uint32_t graphicsQueueFamily,
            VkCommandPool commandPool, const ClusterConfig& config);
        ~VulkanClusteredRenderer() override;

        void createClusterGrid() override;

        // Implement pure virtual methods
        bool initialize(Format colorFormat, Format depthFormat) override;
        void shutdown() override;
        uint32_t loadMesh(const std::vector<MeshVertex>& vertices, const std::vector<uint32_t>& indices, const std::string& name = "") override;
        uint32_t createMaterial(const PBRMaterial& material) override;
        void render(RenderCommandBuffer* cmdBuffer, Camera* camera) override{}
        void updateGPUBuffers() override;

        void buildClusters(Camera* camera, Octree<RenderableObject>& octree);

        void updateLights(const std::vector<ClusterLight>& lights) override;

        // Vulkan-specific methods
        void render(VkCommandBuffer cmdBuffer, Camera* camera); // Convenience overload

        // Getters for Vulkan resources
        VkDevice getDevice() const { return m_device; }

        Taffy::Asset test_asset_;
        Tremor::TaffyMesh test_mesh_;

    protected:
        // Override virtual methods from base class
        void onClustersUpdated() override{}
        void onLightsUpdated() override{}
        void onMeshDataUpdated() override{}

    private:
        // Vulkan-specific members
        VkDevice m_device;
        ClusterConfig m_config;
        VkPhysicalDevice m_physicalDevice;
        VkCommandPool m_commandPool;
        VkQueue m_graphicsQueue;
        uint32_t m_graphicsQueueFamily;

        uint32_t m_totalClusters;

        // Rendering configuration
        VkFormat* m_colorFormat;
        VkFormat* m_depthFormat;

        // Shader modules
        std::unique_ptr<ShaderModule> m_taskShader;
        std::unique_ptr<ShaderModule> m_meshShader;
        std::unique_ptr<ShaderModule> m_fragmentShader;
        std::unique_ptr<ShaderModule> m_debugTaskShader;
        std::unique_ptr<ShaderModule> m_debugMeshShader;

        // Pipeline resources
        std::unique_ptr<PipelineLayoutResource> m_pipelineLayout;
        std::unique_ptr<PipelineResource> m_pipeline;
        std::unique_ptr<PipelineResource> m_wireframePipeline;
        std::unique_ptr<PipelineResource> m_debugPipeline;
        std::unique_ptr<PipelineResource> m_testBufferPipeline;
        std::unique_ptr<PipelineResource> m_workingMeshPipeline;
        std::unique_ptr<PipelineLayoutResource> m_workingMeshPipelineLayout;

        // Descriptor resources
        std::unique_ptr<DescriptorSetLayoutResource> m_descriptorSetLayout;
        std::unique_ptr<DescriptorPoolResource> m_descriptorPool;
        std::unique_ptr<DescriptorSetResource> m_descriptorSet;

        // GPU buffers
        std::unique_ptr<Buffer> m_clusterBuffer;
        std::unique_ptr<Buffer> m_objectBuffer;
        std::unique_ptr<Buffer> m_lightBuffer;
        std::unique_ptr<Buffer> m_indexBuffer;
        std::unique_ptr<Buffer> m_uniformBuffer;
        std::unique_ptr<Buffer> m_vertexBuffer;
        std::unique_ptr<Buffer> m_meshIndexBuffer;
        std::unique_ptr<Buffer> m_meshInfoBuffer;
        std::unique_ptr<Buffer> m_materialBuffer;

        // Default textures
        std::unique_ptr<ImageResource> m_defaultAlbedoTexture;
        std::unique_ptr<ImageViewResource> m_defaultAlbedoView;
        std::unique_ptr<ImageResource> m_defaultNormalTexture;
        std::unique_ptr<ImageViewResource> m_defaultNormalView;
        std::unique_ptr<SamplerResource> m_defaultSampler;

        // Vulkan-specific private methods
        bool createShaderResources();
        bool createMeshBuffers();
        bool createDefaultTextures();
        bool createGraphicsPipeline();
        void updateDescriptorSet();
        void updateUniformBuffers(Camera* camera);
        void updateMeshBuffers();
        void updateMaterialBuffer();
        uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
        VkFormat convertFormat(Format format);
    };


    // UBO for enhanced cluster rendering
    struct alignas(16) EnhancedClusterUBO {
        glm::mat4 viewMatrix;
        glm::mat4 projMatrix;
        glm::mat4 invViewMatrix;
        glm::mat4 invProjMatrix;
        glm::vec4 cameraPos;
        glm::uvec4 clusterDimensions;
        glm::vec4 zPlanes;
        glm::vec4 screenSize;
        uint32_t numLights;
        uint32_t numObjects;
        uint32_t numClusters;
        uint32_t frameNumber;
        float time;
        float deltaTime;
        uint32_t flags;
        uint32_t _padding; // Ensure 16-byte alignment
    };

    uint32_t VulkanClusteredRenderer::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProperties);

        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
            if ((typeFilter & (1 << i)) &&
                (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }

        throw std::runtime_error("Failed to find suitable memory type");
    }

    // Implementation
    VulkanClusteredRenderer::VulkanClusteredRenderer(VkDevice device, VkPhysicalDevice physicalDevice,
        VkQueue graphicsQueue, uint32_t graphicsQueueFamily,
        VkCommandPool commandPool, const ClusterConfig& config)
        : m_device(device)
        , m_physicalDevice(physicalDevice)
        , m_graphicsQueue(graphicsQueue)
        , m_graphicsQueueFamily(graphicsQueueFamily)
        , m_commandPool(commandPool)
        , m_config(config)
        , m_totalClusters(config.xSlices* config.ySlices* config.zSlices)
    {
        Logger::get().info("Creating VulkanClusteredRenderer with {} clusters ({}x{}x{})",
            m_totalClusters, config.xSlices, config.ySlices, config.zSlices);
    }

    VulkanClusteredRenderer::~VulkanClusteredRenderer() {
        shutdown();
    }

    void VulkanClusteredRenderer::createClusterGrid() {
        m_clusters.resize(m_totalClusters);

        // Initialize all clusters
        for (auto& cluster : m_clusters) {
            cluster.lightOffset = 0;
            cluster.lightCount = 0;
            cluster.objectOffset = 0;
            cluster.objectCount = 0;
        }

        Logger::get().info("Created cluster grid: {} total clusters", m_totalClusters);
    }

    void VulkanClusteredRenderer::buildClusters(Camera* camera, Octree<RenderableObject>& octree) {

        Logger::get().info("=== BYPASS MODE: FORCE RENDER ALL OBJECTS ===");




        // Clear and reset
        m_clusterLightIndices.clear();
        m_clusterObjectIndices.clear();
        m_visibleObjects.clear();



        // Get ALL objects from octree and mark them visible
        auto allObjects = octree.getAllObjects();
        Logger::get().info("Found {} objects in octree", allObjects.size());

        if (allObjects.empty()) {
            Logger::get().error("CRITICAL: Octree is empty!");
            return;
        }



        // Copy all objects to visible list
        m_visibleObjects = allObjects;

        Logger::get().info("OCTREE DEBUG: getAllObjects() returned {} objects", allObjects.size());
        for (size_t i = 0; i < std::min(size_t(25), allObjects.size()); i++) {
            const auto& obj = allObjects[i];
            glm::vec3 pos = obj.transform[3];
            Logger::get().info("OCTREE Object {}: pos=({:.2f}, {:.2f}, {:.2f})", i, pos.x, pos.y, pos.z);
        }

        // Force all objects into cluster 0
        for (size_t i = 0; i < m_visibleObjects.size(); i++) {
            m_clusterObjectIndices.push_back(static_cast<uint32_t>(i));
        }

        // Set cluster 0 to contain all objects
        for (auto& cluster : m_clusters) {
            cluster.objectOffset = 0;
            cluster.objectCount = 0;
            cluster.lightOffset = 0;
            cluster.lightCount = 0;

        }

        m_clusters[0].objectOffset = 0;
        m_clusters[0].objectCount = static_cast<uint32_t>(m_visibleObjects.size());


        Logger::get().info("BYPASS: Forced {} objects into cluster 0", m_visibleObjects.size());





        // Update GPU buffers and continue
        updateGPUBuffers();
        updateUniformBuffers(camera);


        Logger::get().info("BYPASS MODE COMPLETE");
        return;  // Skip all normal clustering logic
    }
    bool VulkanClusteredRenderer::initialize(Format color, Format depth) {
        m_colorFormat = &color.format;
        m_depthFormat = &depth.format;

        Logger::get().info("Initializing VulkanClusteredRenderer...");

        try {
            // Create GPU buffers
            if (!createMeshBuffers()) {
                Logger::get().error("Failed to create mesh buffers");
                return false;
            }

            // Create default textures
            if (!createDefaultTextures()) {
                Logger::get().error("Failed to create default textures");
                return false;
            }

            createClusterGrid();

            // Create shader resources
            if (!createShaderResources()) {
                Logger::get().error("Failed to create shader resources");
                return false;
            }

            std::cout << "=== Taffy Integration Test ===" << std::endl;

            // Load the test Taffy asset
            if (!test_asset_.load_from_file("assets/test_triangle.taf")) {
                std::cerr << "Failed to load test Taffy asset!" << std::endl;
                return false;
            }

            std::cout << "✓ Taffy asset loaded successfully!" << std::endl;
            std::cout << "  Creator: " << test_asset_.get_creator() << std::endl;
            std::cout << "  Description: " << test_asset_.get_header().description << std::endl;
            std::cout << "  Features: " << std::hex << static_cast<uint64_t>(test_asset_.get_header().feature_flags) << std::dec << std::endl;

            // Check what chunks are available
            if (test_asset_.has_chunk(Taffy::ChunkType::GEOM)) {
                std::cout << "  ✓ Contains geometry chunk" << std::endl;
            }

            // Load into renderable mesh
            if (!test_mesh_.load_from_asset(test_asset_)) {
                std::cerr << "Failed to load Taffy asset into mesh!" << std::endl;
                return false;
            }

            std::cout << "✓ Taffy mesh created successfully!" << std::endl;
            std::cout << "  Bounds: [" << test_mesh_.get_bounds_min().x << "," << test_mesh_.get_bounds_min().y << "," << test_mesh_.get_bounds_min().z
                << "] to [" << test_mesh_.get_bounds_max().x << "," << test_mesh_.get_bounds_max().y << "," << test_mesh_.get_bounds_max().z << "]" << std::endl;

            // Create default material
            //createDefaultMaterial();

            Logger::get().info("VulkanClusteredRenderer initialized successfully");
            return true;
        }
        catch (const std::exception& e) {
            Logger::get().error("Exception during VulkanClusteredRenderer initialization: {}", e.what());
            return false;
        }
    }

    void VulkanClusteredRenderer::shutdown() {
        // RAII will handle cleanup
        Logger::get().info("VulkanClusteredRenderer shutdown complete");
    }

    uint32_t VulkanClusteredRenderer::loadMesh(const std::vector<MeshVertex>& vertices,
        const std::vector<uint32_t>& indices,
        const std::string& name) {
        if (vertices.empty()) {
            Logger::get().warning("Attempting to load empty mesh");
            return UINT32_MAX;
        }

        // Check if mesh with this name already exists
        if (!name.empty()) {
            auto it = m_meshNameToID.find(name);
            if (it != m_meshNameToID.end()) {
                Logger::get().info("Mesh '{}' already loaded, returning existing ID {}", name, it->second);
                return it->second;
            }
        }

        MeshInfo meshInfo{};
        meshInfo.vertexOffset = static_cast<uint32_t>(m_allVertices.size());
        meshInfo.vertexCount = static_cast<uint32_t>(vertices.size());
        meshInfo.indexOffset = static_cast<uint32_t>(m_allIndices.size());
        meshInfo.indexCount = static_cast<uint32_t>(indices.size());

        // Calculate bounds
        meshInfo.boundsMin = vertices[0].position.toFloat();
        meshInfo.boundsMax = vertices[0].position.toFloat();

        for (const auto& vertex : vertices) {
            meshInfo.boundsMin = glm::min((glm::vec3)meshInfo.boundsMin, (glm::vec3)vertex.position.toFloat());
            meshInfo.boundsMax = glm::max((glm::vec3)meshInfo.boundsMax, (glm::vec3)vertex.position.toFloat());
        }

        // Store mesh info
        uint32_t meshID = static_cast<uint32_t>(m_meshInfos.size());
        m_meshInfos.push_back(meshInfo);

        // Store name mapping
        if (!name.empty()) {
            m_meshNameToID[name] = meshID;
        }

        // Append to vertex and index arrays
        m_allVertices.insert(m_allVertices.end(), vertices.begin(), vertices.end());
        m_allIndices.insert(m_allIndices.end(), indices.begin(), indices.end());

        // Update GPU buffers
        updateMeshBuffers();

        Logger::get().info("Loaded mesh '{}' with ID {}: {} vertices, {} indices",
            name.empty() ? "unnamed" : name, meshID, vertices.size(), indices.size());

        return meshID;
    }

    uint32_t VulkanClusteredRenderer::createMaterial(const PBRMaterial& material) {
        uint32_t materialID = static_cast<uint32_t>(m_materials.size());
        m_materials.push_back(material);

        updateMaterialBuffer();

        Logger::get().info("Created material with ID {}", materialID);
        return materialID;
    }

    




     void  VulkanClusteredRenderer::updateLights(const std::vector<ClusterLight>& lights) {
        m_lights = lights;

        // Update light buffer
        if (!m_lights.empty() && m_lightBuffer) {
            VkDeviceSize lightBufferSize = m_lights.size() * sizeof(ClusterLight);
            if (lightBufferSize <= m_lightBuffer->getSize()) {
                m_lightBuffer->update(m_lights.data(), lightBufferSize);
            }
            else {
                Logger::get().warning("Light buffer too small for {} lights", m_lights.size());
            }
        }

        Logger::get().info("Updated {} lights", m_lights.size());
    }

    void VulkanClusteredRenderer::render(VkCommandBuffer cmdBuffer, Camera* camera) {
		//renderDebug(cmdBuffer, camera);

        updateUniformBuffers(camera);

        Logger::get().info("C++ EnhancedClusterUBO size: {}", sizeof(EnhancedClusterUBO));
        Logger::get().info("C++ time offset: {}", offsetof(EnhancedClusterUBO, time));

        
        Logger::get().info("=== CLUSTERED RENDERER DEBUG START ===");
        Logger::get().info("Pipeline valid: {}", m_pipeline ? "YES" : "NO");
        Logger::get().info("Pipeline layout valid: {}", m_pipelineLayout ? "YES" : "NO");
        Logger::get().info("Descriptor set valid: {}", m_descriptorSet ? "YES" : "NO");

        Logger::get().info("Data summary:");
        Logger::get().info("  Visible objects: {}", m_visibleObjects.size());
        Logger::get().info("  Cluster object indices: {}", m_clusterObjectIndices.size());
        Logger::get().info("  Cluster light indices: {}", m_clusterLightIndices.size());
        Logger::get().info("  Total clusters: {}", m_totalClusters);
        Logger::get().info("  Lights: {}", m_lights.size());

        // Count non-empty clusters
        uint32_t clustersWithObjects = 0;
        uint32_t clustersWithLights = 0;
        for (const auto& cluster : m_clusters) {
            if (cluster.objectCount > 0) clustersWithObjects++;
            if (cluster.lightCount > 0) clustersWithLights++;
        }
        Logger::get().info("  Clusters with objects: {}", clustersWithObjects);
        Logger::get().info("  Clusters with lights: {}", clustersWithLights);

        if (m_visibleObjects.empty()) {
            Logger::get().error("CRITICAL: No visible objects to render!");
            return;
        }

        if (m_clusterObjectIndices.empty()) {
            Logger::get().error("CRITICAL: No cluster object indices!");
            return;
        }

        if (!m_pipeline) {
            Logger::get().error("Pipeline is NULL!");
            return;
        }

        if (!m_pipelineLayout) {
            Logger::get().error("Pipeline layout is NULL!");
            return;
        }

        if (!m_descriptorSet) {
            Logger::get().error("Descriptor set is NULL!");
            return;
        }

        Logger::get().info("All pipeline resources valid");

        // Memory barriers for buffer updates
        std::vector<VkBufferMemoryBarrier> barriers;

		updateGPUBuffers();

        auto addBarrier = [&](VkBuffer buffer, VkDeviceSize size) {
            VkBufferMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.buffer = buffer;
            barrier.offset = 0;
            barrier.size = size;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers.push_back(barrier);
            };

        addBarrier(m_uniformBuffer->getBuffer(), m_uniformBuffer->getSize());
        addBarrier(m_clusterBuffer->getBuffer(), m_clusterBuffer->getSize());
        addBarrier(m_objectBuffer->getBuffer(), m_objectBuffer->getSize());
        addBarrier(m_lightBuffer->getBuffer(), m_lightBuffer->getSize());
        addBarrier(m_indexBuffer->getBuffer(), m_indexBuffer->getSize());


        vkCmdPipelineBarrier(
            cmdBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TASK_SHADER_BIT_EXT | VK_PIPELINE_STAGE_MESH_SHADER_BIT_EXT,
            0,
            0, nullptr,
            static_cast<uint32_t>(barriers.size()), barriers.data(),
            0, nullptr
        );

        // Choose pipeline based on mode
        VkPipeline currentPipeline = m_debugClusters ?
            (m_debugPipeline ? m_debugPipeline->handle() : m_pipeline->handle()) :
            (m_wireframeMode && m_wireframePipeline ? m_wireframePipeline->handle() : m_pipeline->handle());

		Logger::get().info("Using pipeline: {} | (bind {})",
			m_wireframeMode ? "Wireframe" : (m_debugClusters ? "Debug" : "Normal"), (void*)currentPipeline);

        if (currentPipeline) {
            vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, currentPipeline);
            Logger::get().info("Pipeline bound successfully");
        } else {
			Logger::get().error("Failed to bind pipeline!");
			return;
		}

        // Bind pipeline
        updateDescriptorSet();

        
        if (m_descriptorSet) {
            vkCmdBindDescriptorSets(
                cmdBuffer,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                m_pipelineLayout->handle(),
                0, 1,
                &m_descriptorSet->handle(),
                0, nullptr
            );
            Logger::get().info("Descriptor sets bound successfully");
        }
        else {
            Logger::get().error("No descriptor set to bind!");
            return;
        }

        // Set viewport and scissor
        VkExtent2D extent = camera->extent;

        Logger::get().info("EXTENT: ({}, {})", extent.width, extent.height);

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(extent.width);
        viewport.height = static_cast<float>(extent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmdBuffer, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = { 0, 0 };
        scissor.extent = extent;
        vkCmdSetScissor(cmdBuffer, 0, 1, &scissor);

        // Calculate task shader workgroups
        uint32_t taskGroupX = (m_totalClusters + 31) / 32;
        taskGroupX = std::max(taskGroupX, 1u);

        Logger::get().info("Dispatching {} task groups", taskGroupX);

        // Dispatch mesh shaders
        vkCmdDrawMeshTasksEXT(cmdBuffer, taskGroupX, 1, 1);
        
    }

    // Private implementation methods...

    bool VulkanClusteredRenderer::createMeshBuffers() {
        try {
            // Large pre-allocated buffers for scalability
            const VkDeviceSize VERTEX_BUFFER_SIZE = sizeof(MeshVertex) * 1000000; // 1M vertices
            const VkDeviceSize INDEX_BUFFER_SIZE = sizeof(uint32_t) * 3000000;    // 3M indices
            const VkDeviceSize MESH_INFO_SIZE = sizeof(MeshInfo) * 10000;         // 10K meshes
            const VkDeviceSize MATERIAL_SIZE = sizeof(PBRMaterial) * 1000;        // 1K materials
            const VkDeviceSize CLUSTER_SIZE = sizeof(Cluster) * m_totalClusters;
            const VkDeviceSize OBJECT_SIZE = sizeof(RenderableObject) * 25; // 100K objects
            const VkDeviceSize LIGHT_SIZE = sizeof(ClusterLight) * 1;         // 10K lights
            const VkDeviceSize INDEX_BUFFER_SIZE_CLUSTER = sizeof(uint32_t) * 1000000; // 1M indices
            const VkDeviceSize UBO_SIZE = sizeof(EnhancedClusterUBO);

            // Create all buffers
            m_vertexBuffer = std::make_unique<Buffer>(
                m_device, m_physicalDevice, VERTEX_BUFFER_SIZE,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            );

            m_meshIndexBuffer = std::make_unique<Buffer>(
                m_device, m_physicalDevice, INDEX_BUFFER_SIZE,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            );

            m_meshInfoBuffer = std::make_unique<Buffer>(
                m_device, m_physicalDevice, MESH_INFO_SIZE,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            );

            m_materialBuffer = std::make_unique<Buffer>(
                m_device, m_physicalDevice, MATERIAL_SIZE,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            );

            m_clusterBuffer = std::make_unique<Buffer>(
                m_device, m_physicalDevice, CLUSTER_SIZE,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            );

            m_objectBuffer = std::make_unique<Buffer>(
                m_device, m_physicalDevice, OBJECT_SIZE,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            );

            m_lightBuffer = std::make_unique<Buffer>(
                m_device, m_physicalDevice, LIGHT_SIZE,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            );

            m_indexBuffer = std::make_unique<Buffer>(
                m_device, m_physicalDevice, INDEX_BUFFER_SIZE_CLUSTER,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            );

            m_uniformBuffer = std::make_unique<Buffer>(
                m_device, m_physicalDevice, UBO_SIZE,
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            );

            Logger::get().info("Created all mesh buffers successfully");
            return true;
        }
        catch (const std::exception& e) {
            Logger::get().error("Failed to create mesh buffers: {}", e.what());
            return false;
        }
    }

    // Fixed createDefaultTextures method for VulkanClusteredRenderer
    bool VulkanClusteredRenderer::createDefaultTextures() {
        try {
            Logger::get().info("Creating default textures for VulkanClusteredRenderer...");

            // Create a simple 4x4 white texture for debugging
            const uint32_t size = 4;
            const uint32_t pixelCount = size * size;
            std::vector<uint8_t> whitePixels(pixelCount * 4);

            // Fill with pure white pixels
            for (uint32_t i = 0; i < pixelCount; i++) {
                whitePixels[i * 4 + 0] = 255; // R
                whitePixels[i * 4 + 1] = 255; // G  
                whitePixels[i * 4 + 2] = 255; // B
                whitePixels[i * 4 + 3] = 255; // A
            }

            Logger::get().info("Creating staging buffer for texture data...");

            // Create staging buffer
            VkDeviceSize imageSize = whitePixels.size();

            auto stagingBuffer = std::make_unique<Buffer>(
                m_device, m_physicalDevice, imageSize,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            );

            // Upload data to staging buffer
            stagingBuffer->update(whitePixels.data(), imageSize);
            Logger::get().info("Uploaded {} bytes to staging buffer", imageSize);

            // Create albedo texture image
            VkImageCreateInfo imageInfo{};
            imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.extent = { size, size, 1 };
            imageInfo.mipLevels = 1;
            imageInfo.arrayLayers = 1;
            imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

            // Create albedo image
            m_defaultAlbedoTexture = std::make_unique<ImageResource>(m_device);
            if (vkCreateImage(m_device, &imageInfo, nullptr, &m_defaultAlbedoTexture->handle()) != VK_SUCCESS) {
                Logger::get().error("Failed to create default albedo image");
                return false;
            }

            // Allocate memory for albedo
            VkMemoryRequirements memRequirements;
            vkGetImageMemoryRequirements(m_device, m_defaultAlbedoTexture->handle(), &memRequirements);

            VkMemoryAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize = memRequirements.size;
            allocInfo.memoryTypeIndex = findMemoryType(
                memRequirements.memoryTypeBits,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
            );

            VkDeviceMemory albedoMemory;
            if (vkAllocateMemory(m_device, &allocInfo, nullptr, &albedoMemory) != VK_SUCCESS) {
                Logger::get().error("Failed to allocate albedo memory");
                return false;
            }

            vkBindImageMemory(m_device, m_defaultAlbedoTexture->handle(), albedoMemory, 0);

            // Create normal texture (same process)
            m_defaultNormalTexture = std::make_unique<ImageResource>(m_device);
            if (vkCreateImage(m_device, &imageInfo, nullptr, &m_defaultNormalTexture->handle()) != VK_SUCCESS) {
                Logger::get().error("Failed to create default normal image");
                return false;
            }

            // Allocate memory for normal texture
            vkGetImageMemoryRequirements(m_device, m_defaultNormalTexture->handle(), &memRequirements);
            allocInfo.allocationSize = memRequirements.size;
            allocInfo.memoryTypeIndex = findMemoryType(
                memRequirements.memoryTypeBits,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
            );

            VkDeviceMemory normalMemory;
            if (vkAllocateMemory(m_device, &allocInfo, nullptr, &normalMemory) != VK_SUCCESS) {
                Logger::get().error("Failed to allocate normal memory");
                return false;
            }

            vkBindImageMemory(m_device, m_defaultNormalTexture->handle(), normalMemory, 0);

            Logger::get().info("Transitioning image layouts and copying data...");

            // Transition image layouts and copy data
            VkCommandBufferAllocateInfo cmdAllocInfo{};
            cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            cmdAllocInfo.commandPool = m_commandPool;
            cmdAllocInfo.commandBufferCount = 1;

            VkCommandBuffer commandBuffer;
            vkAllocateCommandBuffers(m_device, &cmdAllocInfo, &commandBuffer);

            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

            vkBeginCommandBuffer(commandBuffer, &beginInfo);

            // Transition both images to transfer destination
            VkImageMemoryBarrier barriers[2] = {};

            // Albedo image barrier
            barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barriers[0].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barriers[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[0].image = m_defaultAlbedoTexture->handle();
            barriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barriers[0].subresourceRange.baseMipLevel = 0;
            barriers[0].subresourceRange.levelCount = 1;
            barriers[0].subresourceRange.baseArrayLayer = 0;
            barriers[0].subresourceRange.layerCount = 1;
            barriers[0].srcAccessMask = 0;
            barriers[0].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

            // Normal image barrier (same settings)
            barriers[1] = barriers[0];
            barriers[1].image = m_defaultNormalTexture->handle();

            vkCmdPipelineBarrier(
                commandBuffer,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                0,
                0, nullptr,
                0, nullptr,
                2, barriers
            );

            // Copy data from staging buffer to both images
            VkBufferImageCopy region{};
            region.bufferOffset = 0;
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = 0;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount = 1;
            region.imageOffset = { 0, 0, 0 };
            region.imageExtent = { size, size, 1 };

            vkCmdCopyBufferToImage(
                commandBuffer,
                stagingBuffer->getBuffer(),
                m_defaultAlbedoTexture->handle(),
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1,
                &region
            );

            vkCmdCopyBufferToImage(
                commandBuffer,
                stagingBuffer->getBuffer(),
                m_defaultNormalTexture->handle(),
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1,
                &region
            );

            // Transition both images to shader read layout
            barriers[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barriers[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barriers[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            barriers[1] = barriers[0];
            barriers[1].image = m_defaultNormalTexture->handle();

            vkCmdPipelineBarrier(
                commandBuffer,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0,
                0, nullptr,
                0, nullptr,
                2, barriers
            );

            vkEndCommandBuffer(commandBuffer);

            // Submit command buffer
            VkSubmitInfo submitInfo{};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &commandBuffer;

            vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
            vkQueueWaitIdle(m_graphicsQueue);

            // Free command buffer
            vkFreeCommandBuffers(m_device, m_commandPool, 1, &commandBuffer);

            Logger::get().info("Creating image views...");

            // Create image views
            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 1;

            // Albedo view
            viewInfo.image = m_defaultAlbedoTexture->handle();
            m_defaultAlbedoView = std::make_unique<ImageViewResource>(m_device);
            if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_defaultAlbedoView->handle()) != VK_SUCCESS) {
                Logger::get().error("Failed to create albedo image view");
                return false;
            }

            // Normal view
            viewInfo.image = m_defaultNormalTexture->handle();
            m_defaultNormalView = std::make_unique<ImageViewResource>(m_device);
            if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_defaultNormalView->handle()) != VK_SUCCESS) {
                Logger::get().error("Failed to create normal image view");
                return false;
            }

            Logger::get().info("Creating sampler...");

            // Create sampler
            VkSamplerCreateInfo samplerInfo{};
            samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            samplerInfo.magFilter = VK_FILTER_NEAREST; // Use NEAREST for debugging
            samplerInfo.minFilter = VK_FILTER_NEAREST;
            samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.anisotropyEnable = VK_FALSE;
            samplerInfo.maxAnisotropy = 1.0f;
            samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
            samplerInfo.unnormalizedCoordinates = VK_FALSE;
            samplerInfo.compareEnable = VK_FALSE;
            samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            samplerInfo.mipLodBias = 0.0f;
            samplerInfo.minLod = 0.0f;
            samplerInfo.maxLod = 0.0f;

            m_defaultSampler = std::make_unique<SamplerResource>(m_device);
            if (vkCreateSampler(m_device, &samplerInfo, nullptr, &m_defaultSampler->handle()) != VK_SUCCESS) {
                Logger::get().error("Failed to create default sampler");
                return false;
            }

            Logger::get().info("Default textures created successfully!");
            Logger::get().info("  Albedo texture: {}x{} white", size, size);
            Logger::get().info("  Normal texture: {}x{} white", size, size);
            Logger::get().info("  Sampler: NEAREST filtering");

            return true;
        }
        catch (const std::exception& e) {
            Logger::get().error("Exception in createDefaultTextures: {}", e.what());
            return false;
        }
    }

    // UBO for cluster rendering
    struct ClusterUBO {
         glm::mat4 viewMatrix;
         glm::mat4 projMatrix;
         glm::vec4 cameraPos;
         glm::uvec4 clusterDimensions;  // x, y, z, pad
         glm::vec4 zPlanes;             // near, far, clustersPerZ, pad
         uint32_t numLights;
         uint32_t numObjects;
         uint32_t numClusters;
         uint32_t padding;
    };

    // ===============================
    // Implementation
    // ===============================

    
} // namespace tremor::gfx

namespace tremor::gfx { 



    

    // Cluster grid dimensions (typically 16x16x24)
    

    // Single light for cluster-based lighting
    

    // Main cluster data management class

    const char* getDescriptorTypeName(VkDescriptorType type) {
        switch (type) {
        case VK_DESCRIPTOR_TYPE_SAMPLER: return "SAMPLER";
        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: return "COMBINED_IMAGE_SAMPLER";
        case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE: return "SAMPLED_IMAGE";
        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE: return "STORAGE_IMAGE";
        case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER: return "UNIFORM_TEXEL_BUFFER";
        case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER: return "STORAGE_TEXEL_BUFFER";
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER: return "UNIFORM_BUFFER";
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER: return "STORAGE_BUFFER";
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC: return "UNIFORM_BUFFER_DYNAMIC";
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC: return "STORAGE_BUFFER_DYNAMIC";
        case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT: return "INPUT_ATTACHMENT";
        default: return "UNKNOWN";
        }
    }


    struct PBRMaterialUBO {
        // Basic properties
        alignas(16) float baseColor[4];   // vec4 in shader
         float metallic;        // float in shader
         float roughness;       // float in shader
         float ao;              // float in shader
        alignas(16) float emissive[3];    // vec3 in shader (padded to 16 bytes)

        // Texture availability flags
         int hasAlbedoMap;
         int hasNormalMap;
         int hasMetallicRoughnessMap;
         int hasAoMap;
         int hasEmissiveMap;
    };

    

    // Vertex Buffer Class
    class VertexBuffer {
    public:
        VertexBuffer() = default;

        template<typename T>
        VertexBuffer(VkDevice device, VkPhysicalDevice physicalDevice,
            VkCommandPool commandPool, VkQueue queue,
            const std::vector<T>& vertices)
            : m_vertexCount(static_cast<uint32_t>(vertices.size())),
            m_stride(sizeof(T)) {

            VkDeviceSize bufferSize = vertices.size() * sizeof(T);

            // Create staging buffer (host visible)
            Buffer stagingBuffer(
                device, physicalDevice, bufferSize,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            );

            // Copy data to staging buffer
            stagingBuffer.update(vertices.data(), bufferSize);

            // Create vertex buffer (device local)
            m_buffer = std::make_unique<Buffer>(
                device, physicalDevice, bufferSize,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
            );

            // Copy from staging buffer to vertex buffer
            copyBuffer(device, commandPool, queue,
                stagingBuffer.getBuffer(), m_buffer->getBuffer(), bufferSize);
        }

        // Bind vertex buffer to command buffer
        void bind(VkCommandBuffer cmdBuffer, uint32_t binding = 0) const {
            if (m_buffer) {
                VkBuffer vertexBuffers[] = { m_buffer->getBuffer() };
                VkDeviceSize offsets[] = { 0 };
                vkCmdBindVertexBuffers(cmdBuffer, binding, 1, vertexBuffers, offsets);
            }
        }

        // Getters
        uint32_t getVertexCount() const { return m_vertexCount; }
        uint32_t getStride() const { return m_stride; }

    private:
        std::unique_ptr<Buffer> m_buffer;
        uint32_t m_vertexCount = 0;
        uint32_t m_stride = 0;
    };

    // Index Buffer Class
    class IndexBuffer {
    public:
        IndexBuffer() = default;

        template<typename T>
        IndexBuffer(VkDevice device, VkPhysicalDevice physicalDevice,
            VkCommandPool commandPool, VkQueue queue,
            const std::vector<T>& indices)
            : m_indexCount(static_cast<uint32_t>(indices.size())) {

            static_assert(std::is_same_v<T, uint16_t> || std::is_same_v<T, uint32_t>,
                "Index type must be uint16_t or uint32_t");

            m_indexType = sizeof(T) == 2 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;

            VkDeviceSize bufferSize = indices.size() * sizeof(T);

            // Create staging buffer (host visible)
            Buffer stagingBuffer(
                device, physicalDevice, bufferSize,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            );

            // Copy data to staging buffer
            stagingBuffer.update(indices.data(), bufferSize);

            // Create index buffer (device local)
            m_buffer = std::make_unique<Buffer>(
                device, physicalDevice, bufferSize,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
            );

            // Copy from staging buffer to index buffer
            copyBuffer(device, commandPool, queue,
                stagingBuffer.getBuffer(), m_buffer->getBuffer(), bufferSize);
        }

        // Bind index buffer to command buffer
        void bind(VkCommandBuffer cmdBuffer) const {
            if (m_buffer) {
                vkCmdBindIndexBuffer(cmdBuffer, m_buffer->getBuffer(), 0, m_indexType);
            }
        }

        // Getters
        uint32_t getIndexCount() const { return m_indexCount; }
        VkIndexType getIndexType() const { return m_indexType; }

    private:
        std::unique_ptr<Buffer> m_buffer;
        uint32_t m_indexCount = 0;
        VkIndexType m_indexType = VK_INDEX_TYPE_UINT32;
    };

    // First, add these as member variables if they don't exist already
    class CommandPoolResource {
    private:
        VkDevice m_device = VK_NULL_HANDLE;
        VkCommandPool m_handle = VK_NULL_HANDLE;

    public:
        CommandPoolResource() = default;

        CommandPoolResource(VkDevice device, VkCommandPool handle = VK_NULL_HANDLE)
            : m_device(device), m_handle(handle) {
        }

        ~CommandPoolResource() {
        }

        // Disable copying
        CommandPoolResource(const CommandPoolResource&) = delete;
        CommandPoolResource& operator=(const CommandPoolResource&) = delete;

        // Enable moving
        CommandPoolResource(CommandPoolResource&& other) noexcept
            : m_device(other.m_device), m_handle(other.m_handle) {
            other.m_handle = VK_NULL_HANDLE;
        }

        CommandPoolResource& operator=(CommandPoolResource&& other) noexcept {
            if (this != &other) {
                cleanup();
                m_device = other.m_device;
                m_handle = other.m_handle;
                other.m_handle = VK_NULL_HANDLE;
            }
            return *this;
        }

        // Accessors
        VkCommandPool& handle() { return m_handle; }
        const VkCommandPool& handle() const { return m_handle; }
        operator VkCommandPool() const { return m_handle; }

        // Check if valid
        operator bool() const { return m_handle != VK_NULL_HANDLE; }

        // Release ownership without destroying
        VkCommandPool release() {
            VkCommandPool temp = m_handle;
            m_handle = VK_NULL_HANDLE;
            return temp;
        }

        // Reset with a new handle
        void reset(VkCommandPool newHandle = VK_NULL_HANDLE) {
            cleanup();
            m_handle = newHandle;
        }

    private:
        void cleanup() {
            if (m_handle != VK_NULL_HANDLE && m_device != VK_NULL_HANDLE) {
                vkDestroyCommandPool(m_device, m_handle, nullptr);
                m_handle = VK_NULL_HANDLE;
            }
        }
    };

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





    
    // Convenience function for loading shaders with automatic type detection
    inline std::unique_ptr<ShaderModule> loadShader(VkDevice device, const std::string& filename, const std::string& entryPoint = "main") {
        ShaderType type = inferShaderTypeFromFilename(filename);
        return ShaderModule::loadFromFile(device, filename, type, entryPoint);
    }


    class ShaderManager {
    public:
        ShaderManager(VkDevice device) : m_device(device) {}

        // Load a shader with automatic compilation
        std::shared_ptr<ShaderModule> loadShader(
            const std::string& filename,
            const std::string& entryPoint = "main",
            const ShaderCompiler::CompileOptions& options = {}) {

            // Check if already loaded
            auto it = m_shaders.find(filename);
            if (it != m_shaders.end()) {
                return it->second;
            }

            // Determine if it's a SPIR-V or source file
            bool isSpirv = filename.ends_with(".spv");

            // Load the shader
            std::shared_ptr<ShaderModule> shader;
            if (isSpirv) {
                shader = std::shared_ptr<ShaderModule>(
                    ShaderModule::loadFromFile(m_device, filename,
                        inferShaderTypeFromFilename(filename),
                        entryPoint).release());
            }
            else {
                shader = std::shared_ptr<ShaderModule>(
                    ShaderModule::compileFromFile(m_device, filename, entryPoint).release());
            }

            // Store and return
            if (shader) {
                m_shaders[filename] = shader;
                m_shaderFileTimestamps[filename] = getFileTimestamp(filename);
            }

            return shader;
        }

        // Check for shader file changes and reload if needed
        void checkForChanges() {
            for (auto& [filename, shader] : m_shaders) {
                auto currentTimestamp = getFileTimestamp(filename);
                if (currentTimestamp > m_shaderFileTimestamps[filename]) {
                    Logger::get().info("Shader file changed, reloading: {}", filename);

                    // Get current options
                    bool isSpirv = filename.ends_with(".spv");
                    auto entryPoint = shader->getEntryPoint();

                    // Reload the shader
                    std::shared_ptr<ShaderModule> newShader;
                    if (isSpirv) {
                        newShader = std::shared_ptr<ShaderModule>(
                            ShaderModule::loadFromFile(m_device, filename,
                                inferShaderTypeFromFilename(filename),
                                entryPoint).release());
                    }
                    else {
                        ShaderCompiler::CompileOptions options;
                        newShader = std::shared_ptr<ShaderModule>(
                            ShaderModule::compileFromFile(m_device, filename, entryPoint).release());
                    }

                    // Update the shader if reload succeeded
                    if (newShader) {
                        m_shaders[filename] = newShader;
                        m_shaderFileTimestamps[filename] = currentTimestamp;

                        // Notify any systems that need to know about shader changes
                        // This could include pipeline objects that need to be rebuilt
                        notifyShaderReloaded(filename, newShader);
                    }
                }
            }
        }

    private:
        // Get file modification timestamp
        std::filesystem::file_time_type getFileTimestamp(const std::string& filename) {
            try {
                return std::filesystem::last_write_time(filename);
            }
            catch (const std::exception& e) {
                Logger::get().error("Failed to get file timestamp: {}", e.what());
                return std::filesystem::file_time_type();
            }
        }

        // Notify systems about shader reloads
        void notifyShaderReloaded(const std::string& filename, std::shared_ptr<ShaderModule> shader) {
            // You would implement this to notify pipeline cache or other systems
            Logger::get().info("Shader reloaded: {}", filename);
        }

        VkDevice m_device;
        std::unordered_map<std::string, std::shared_ptr<ShaderModule>> m_shaders;
        std::unordered_map<std::string, std::filesystem::file_time_type> m_shaderFileTimestamps;
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

    class RenderPass {
    public:
        struct Attachment {
            VkFormat format = VK_FORMAT_UNDEFINED;
            VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
            VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            VkAttachmentStoreOp storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            VkAttachmentLoadOp stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            VkAttachmentStoreOp stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            VkImageLayout finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        };

        struct SubpassDependency {
            uint32_t srcSubpass = VK_SUBPASS_EXTERNAL;
            uint32_t dstSubpass = 0;
            VkPipelineStageFlags srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            VkPipelineStageFlags dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            VkAccessFlags srcAccessMask = 0;
            VkAccessFlags dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            VkDependencyFlags dependencyFlags = 0;
        };

        struct CreateInfo {
            std::vector<Attachment> attachments;
            std::vector<SubpassDependency> dependencies;
        };

        RenderPass(VkDevice device, const CreateInfo& createInfo);
        ~RenderPass() = default; // RAII handles cleanup

        // No copying
        RenderPass(const RenderPass&) = delete;
        RenderPass& operator=(const RenderPass&) = delete;

        // Moving is allowed
        RenderPass(RenderPass&&) noexcept = default;
        RenderPass& operator=(RenderPass&&) noexcept = default;

        // Getters
        VkRenderPass handle() const { return m_renderPass; }
        operator VkRenderPass() const { return m_renderPass; }

        // Begin a render pass
        void begin(VkCommandBuffer cmdBuffer, VkFramebuffer framebuffer, const VkRect2D& renderArea,
            const std::vector<VkClearValue>& clearValues);

        // End a render pass
        void end(VkCommandBuffer cmdBuffer);

    private:
        VkDevice m_device;
        RenderPassResource m_renderPass;  // RAII wrapper
    };

    class PipelineState {
    public:
        // Shader stages
        std::vector<VkPipelineShaderStageCreateInfo> shaderStages;

        // Vertex input state
        VkPipelineVertexInputStateCreateInfo vertexInputState{};

        // Input assembly state
        VkPipelineInputAssemblyStateCreateInfo inputAssemblyState{};

        // Viewport state
        VkPipelineViewportStateCreateInfo viewportState{};

        // Rasterization state
        VkPipelineRasterizationStateCreateInfo rasterizationState{};

        // Multisample state
        VkPipelineMultisampleStateCreateInfo multisampleState{};

        // Depth stencil state
        VkPipelineDepthStencilStateCreateInfo depthStencilState{};

        // Color blend state
        VkPipelineColorBlendStateCreateInfo colorBlendState{};

        // Dynamic state
        VkPipelineDynamicStateCreateInfo dynamicState{};

        void setupFromShaderReflection(const std::vector<std::shared_ptr<ShaderModule>>& shaders) {
            // Combine shader reflection data
            ShaderReflection combinedReflection;

            for (const auto& shader : shaders) {
                const ShaderReflection* reflection = shader->getReflection();
                if (reflection) {
                    combinedReflection.merge(*reflection);
                }
            }

            // Setup vertex input state from reflection
            if (shaders[0]->getType() == ShaderType::Vertex) {
                vertexInputState = combinedReflection.createVertexInputState();
            }

            // The rest of the pipeline state would typically be set manually
            // or from other configuration sources
        }
    };

    class DescriptorSetBuilder {
    public:
        DescriptorSetBuilder(VkDevice device, ShaderReflection& reflection)
            : m_device(device), m_reflection(reflection) {
        }

        // Create descriptor sets for all shader resource sets
        std::vector<std::unique_ptr<DescriptorSetResource>> createDescriptorSets() {
            std::vector<std::unique_ptr<DescriptorSetResource>> result;

            // Create pool
            auto pool = m_reflection.createDescriptorPool(m_device);
            if (!pool) {
                return result;
            }

            // Find the max set number used in the shaders
            uint32_t maxSet = 0;
            for (const auto& binding : m_reflection.getResourceBindings()) {
                maxSet = std::max(maxSet, binding.set);
            }

            // Create layouts for each set
            std::vector<std::unique_ptr<DescriptorSetLayoutResource>> layouts;
            std::vector<VkDescriptorSetLayout> rawLayouts;

			Logger::get().info("Creating descriptor sets for {} sets", maxSet + 1);

            for (uint32_t i = 0; i <= maxSet; i++) {
                auto layout = m_reflection.createDescriptorSetLayout(m_device, i);
                if (layout != nullptr) {
                    rawLayouts.push_back(layout->handle());
                    layouts.push_back(std::move(layout));
                }
                else {
                    // Create an empty layout for this set
                    Logger::get().info("Creating empty descriptor set layout for set {}", i);

                    VkDescriptorSetLayoutCreateInfo emptyLayoutInfo{};
                    emptyLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
                    emptyLayoutInfo.bindingCount = 0;

                    auto emptyLayout = std::make_unique<DescriptorSetLayoutResource>(m_device);
                    if (vkCreateDescriptorSetLayout(m_device, &emptyLayoutInfo, nullptr, &emptyLayout->handle()) != VK_SUCCESS) {
                        Logger::get().error("Failed to create empty descriptor set layout for set {}", i);
                        continue;
                    }

                    rawLayouts.push_back(emptyLayout->handle());
                    layouts.push_back(std::move(emptyLayout));
                }
            }

            if (rawLayouts.empty()) {
                return result;  // No descriptor sets needed
            }

            // Allocate descriptor sets
            VkDescriptorSetAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocInfo.descriptorPool = pool->handle();
            allocInfo.descriptorSetCount = static_cast<uint32_t>(rawLayouts.size());
            allocInfo.pSetLayouts = rawLayouts.data();

            std::vector<VkDescriptorSet> descriptorSets(rawLayouts.size());
            if (vkAllocateDescriptorSets(m_device, &allocInfo, descriptorSets.data()) != VK_SUCCESS) {
                Logger::get().error("Failed to allocate descriptor sets");
                return result;
            }

            // Create RAII wrappers
            for (auto& set : descriptorSets) {
                result.push_back(std::make_unique<DescriptorSetResource>(m_device, set));
            }

            // Store for later use when updating
            m_pool = std::move(pool);
            m_layouts = std::move(layouts);

            return result;
        }

        // Update descriptor set for a uniform buffer
        void updateUniformBuffer(VkDescriptorSet set, uint32_t binding, VkBuffer buffer,
            VkDeviceSize offset, VkDeviceSize range) {
            VkDescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = buffer;
            bufferInfo.offset = offset;
            bufferInfo.range = range;

            VkWriteDescriptorSet descriptorWrite{};
            descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrite.dstSet = set;
            descriptorWrite.dstBinding = binding;
            descriptorWrite.dstArrayElement = 0;
            descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            descriptorWrite.descriptorCount = 1;
            descriptorWrite.pBufferInfo = &bufferInfo;

            vkUpdateDescriptorSets(m_device, 1, &descriptorWrite, 0, nullptr);
        }

        // Update descriptor set for a combined image sampler
        void updateCombinedImageSampler(VkDescriptorSet set, uint32_t binding,
            VkImageView imageView, VkSampler sampler,
            VkImageLayout imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            VkDescriptorImageInfo imageInfo{};
            imageInfo.imageLayout = imageLayout;
            imageInfo.imageView = imageView;
            imageInfo.sampler = sampler;

            VkWriteDescriptorSet descriptorWrite{};
            descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrite.dstSet = set;
            descriptorWrite.dstBinding = binding;
            descriptorWrite.dstArrayElement = 0;
            descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrite.descriptorCount = 1;
            descriptorWrite.pImageInfo = &imageInfo;

            vkUpdateDescriptorSets(m_device, 1, &descriptorWrite, 0, nullptr);
        }

    private:
        VkDevice m_device;
        ShaderReflection& m_reflection;
        std::unique_ptr<DescriptorPoolResource> m_pool;
        std::vector<std::unique_ptr<DescriptorSetLayoutResource>> m_layouts;
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

        // Begin dynamic rendering
        void begin(VkCommandBuffer cmdBuffer, const RenderingInfo& renderingInfo) {
            // Setup color attachments
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

            // Setup depth-stencil attachment
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

            // Configure rendering info
            VkRenderingInfoKHR info{};
            info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR;
            info.renderArea = renderingInfo.renderArea;
            info.layerCount = renderingInfo.layerCount;
            info.viewMask = renderingInfo.viewMask;
            info.colorAttachmentCount = static_cast<uint32_t>(colorAttachmentInfos.size());
            info.pColorAttachments = colorAttachmentInfos.data();
            info.pDepthAttachment = renderingInfo.depthStencilAttachment ? &depthAttachmentInfo : nullptr;
            info.pStencilAttachment = renderingInfo.depthStencilAttachment ? &stencilAttachmentInfo : nullptr;

            // Begin dynamic rendering
            vkCmdBeginRendering(cmdBuffer, &info);
        }

        // End dynamic rendering
        void end(VkCommandBuffer cmdBuffer) {
            vkCmdEndRendering(cmdBuffer);
        }
    };

    // Implementation
    RenderPass::RenderPass(VkDevice device, const CreateInfo& createInfo)
        : m_device(device), m_renderPass(device) {

        std::vector<VkAttachmentDescription> attachmentDescriptions;
        attachmentDescriptions.reserve(createInfo.attachments.size());

        for (const auto& attachment : createInfo.attachments) {
            VkAttachmentDescription desc{};
            desc.format = attachment.format;
            desc.samples = attachment.samples;
            desc.loadOp = attachment.loadOp;
            desc.storeOp = attachment.storeOp;
            desc.stencilLoadOp = attachment.stencilLoadOp;
            desc.stencilStoreOp = attachment.stencilStoreOp;
            desc.initialLayout = attachment.initialLayout;
            desc.finalLayout = attachment.finalLayout;
            attachmentDescriptions.push_back(desc);
        }

        // Setup color and depth attachment references
        VkAttachmentReference colorAttachmentRef{};
        colorAttachmentRef.attachment = 0;  // First attachment
        colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depthAttachmentRef{};
        if (attachmentDescriptions.size() > 1) {
            depthAttachmentRef.attachment = 1;  // Second attachment for depth
            depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        }

        // Configure the subpass
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = (attachmentDescriptions.size() > 0) ? 1 : 0;
        subpass.pColorAttachments = (attachmentDescriptions.size() > 0) ? &colorAttachmentRef : nullptr;
        subpass.pDepthStencilAttachment = (attachmentDescriptions.size() > 1) ? &depthAttachmentRef : nullptr;

        // Configure dependencies
        std::vector<VkSubpassDependency> dependencies;
        dependencies.reserve(createInfo.dependencies.size());

        for (const auto& dependency : createInfo.dependencies) {
            VkSubpassDependency dep{};
            dep.srcSubpass = dependency.srcSubpass;
            dep.dstSubpass = dependency.dstSubpass;
            dep.srcStageMask = dependency.srcStageMask;
            dep.dstStageMask = dependency.dstStageMask;
            dep.srcAccessMask = dependency.srcAccessMask;
            dep.dstAccessMask = dependency.dstAccessMask;
            dep.dependencyFlags = dependency.dependencyFlags;
            dependencies.push_back(dep);
        }

        // Create the render pass
        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = static_cast<uint32_t>(attachmentDescriptions.size());
        renderPassInfo.pAttachments = attachmentDescriptions.data();
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
        renderPassInfo.pDependencies = dependencies.empty() ? nullptr : dependencies.data();

        if (vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_renderPass.handle()) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create render pass");
        }
    }

    void RenderPass::begin(VkCommandBuffer cmdBuffer, VkFramebuffer framebuffer,
        const VkRect2D& renderArea, const std::vector<VkClearValue>& clearValues) {
        VkRenderPassBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        beginInfo.renderPass = m_renderPass;
        beginInfo.framebuffer = framebuffer;
        beginInfo.renderArea = renderArea;
        beginInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        beginInfo.pClearValues = clearValues.data();

        vkCmdBeginRenderPass(cmdBuffer, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);
    }

    void RenderPass::end(VkCommandBuffer cmdBuffer) {
        vkCmdEndRenderPass(cmdBuffer);
    }

    // Framebuffer class to accompany the RenderPass
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
        ~Framebuffer() = default;  // RAII handles cleanup

        // No copying
        Framebuffer(const Framebuffer&) = delete;
        Framebuffer& operator=(const Framebuffer&) = delete;

        // Moving is allowed
        Framebuffer(Framebuffer&&) noexcept = default;
        Framebuffer& operator=(Framebuffer&&) noexcept = default;

        // Getters
        VkFramebuffer handle() const { return m_framebuffer; }
        operator VkFramebuffer() const { return m_framebuffer; }

        uint32_t width() const { return m_width; }
        uint32_t height() const { return m_height; }

    private:
        VkDevice m_device;
        FramebufferResource m_framebuffer;  // RAII wrapper
        uint32_t m_width;
        uint32_t m_height;
        uint32_t m_layers;
    };

    // Implementation
    Framebuffer::Framebuffer(VkDevice device, const CreateInfo& createInfo)
        : m_device(device), m_framebuffer(device),
        m_width(createInfo.width), m_height(createInfo.height), m_layers(createInfo.layers) {

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = createInfo.renderPass;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(createInfo.attachments.size());
        framebufferInfo.pAttachments = createInfo.attachments.data();
        framebufferInfo.width = m_width;
        framebufferInfo.height = m_height;
        framebufferInfo.layers = m_layers;

        if (vkCreateFramebuffer(m_device, &framebufferInfo, nullptr, &m_framebuffer.handle()) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create framebuffer");
        }
    }

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
        const std::vector<ImageViewResource>& imageViews() const { return m_imageViews; }
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
        std::vector<ImageViewResource> m_imageViews;

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

        m_swapChain = SwapchainResource(m_device.device(), m_swapChain.handle());

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

            if (vkCreateImageView(m_device.device(), &createInfo, nullptr, &m_imageViews[i].handle()) != VK_SUCCESS) {
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

    class DescriptorAllocator {
    public:
        // Main initialization with sensible defaults for various descriptor types
        DescriptorAllocator(VkDevice device, uint32_t maxSets = 1000)
            : m_device(device) {
            // Create descriptor pool with capacity for all common descriptor types
            std::vector<VkDescriptorPoolSize> poolSizes = {
                {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 100},
                {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100},
                {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 100},
                {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 100},
                {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 100}
            };

            VkDescriptorPoolCreateInfo poolInfo{};
            poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT; 
            poolInfo.maxSets = maxSets;
            poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
            poolInfo.pPoolSizes = poolSizes.data();

            vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_pool);
        }

        ~DescriptorAllocator() {
            if (m_pool != VK_NULL_HANDLE) {
                vkDestroyDescriptorPool(m_device, m_pool, nullptr);
            }
        }

        // Allocate a descriptor set with the given layout
        VkDescriptorSet allocate(VkDescriptorSetLayout layout) {
            VkDescriptorSetAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocInfo.descriptorPool = m_pool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &layout;

            VkDescriptorSet set;
            VkResult result = vkAllocateDescriptorSets(m_device, &allocInfo, &set);

            if (result == VK_ERROR_OUT_OF_POOL_MEMORY) {
                // Pool is full, create a new one
                resetPool();
                // Try again
                result = vkAllocateDescriptorSets(m_device, &allocInfo, &set);
            }

            if (result != VK_SUCCESS) {
                Logger::get().error("Failed to allocate descriptor set: {}", static_cast<int>(result));
                return VK_NULL_HANDLE;
            }

            return set;
        }

        // Reset the pool to reuse memory
        void resetPool() {
            vkResetDescriptorPool(m_device, m_pool, 0);
        }

    private:
        VkDevice m_device;
        VkDescriptorPool m_pool = VK_NULL_HANDLE;
    };

    // Layout cache to avoid recreating the same layouts
    class DescriptorLayoutCache {
    public:
        DescriptorLayoutCache(VkDevice device) : m_device(device) {}

        ~DescriptorLayoutCache() {
            for (auto& pair : m_layouts) {
                vkDestroyDescriptorSetLayout(m_device, pair.second, nullptr);
            }
        }

        // Get or create a descriptor set layout
        VkDescriptorSetLayout getLayout(const std::vector<VkDescriptorSetLayoutBinding>& bindings) {
            // Create a hash for the bindings
            size_t hash = 0;
            for (const auto& binding : bindings) {
                // Combine hash with binding parameters
                hash = hash_combine(hash, binding.binding);
                hash = hash_combine(hash, binding.descriptorType);
                hash = hash_combine(hash, binding.descriptorCount);
                hash = hash_combine(hash, binding.stageFlags);
            }

            // Check if layout already exists
            auto it = m_layouts.find(hash);
            if (it != m_layouts.end()) {
                return it->second;
            }

            // Create a new layout
            VkDescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
            layoutInfo.pBindings = bindings.data();

            VkDescriptorSetLayout layout;
            vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &layout);

            // Cache and return
            m_layouts[hash] = layout;
            return layout;
        }

    private:
        // Hash combining function
        size_t hash_combine(size_t seed, size_t value) const {
            return seed ^ (value + 0x9e3779b9 + (seed << 6) + (seed >> 2));
        }

        VkDevice m_device;
        std::unordered_map<size_t, VkDescriptorSetLayout> m_layouts;
    };

    // High-level descriptor writer for easier updates
    class DescriptorWriter {


    public:        
        struct WriteInfo {
            uint32_t binding;
            VkDescriptorType type;
            int32_t bufferIndex;
            int32_t imageIndex;
        };

        DescriptorLayoutCache& m_layoutCache;
        DescriptorAllocator& m_allocator;
        VkDevice m_device;

        DescriptorWriter(DescriptorLayoutCache& layoutCache, DescriptorAllocator& allocator)
            : m_layoutCache(layoutCache), m_allocator(allocator) {
        }
        std::vector<VkDescriptorSetLayoutBinding> m_bindings;
        std::vector<VkDescriptorBufferInfo> m_bufferInfos;
        std::vector<VkDescriptorImageInfo> m_imageInfos;
        std::vector<WriteInfo> m_writes;


        // Add buffer binding
        DescriptorWriter& writeBuffer(uint32_t binding, VkBuffer buffer,
            VkDeviceSize offset, VkDeviceSize range,
            VkDescriptorType type, VkShaderStageFlags stageFlags) {
            // Add binding to layout bindings
            VkDescriptorSetLayoutBinding layoutBinding{};
            layoutBinding.binding = binding;
            layoutBinding.descriptorType = type;
            layoutBinding.descriptorCount = 1;
            layoutBinding.stageFlags = stageFlags;

            m_bindings.push_back(layoutBinding);

            // Add write info
            VkDescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = buffer;
            bufferInfo.offset = offset;
            bufferInfo.range = range;

            m_bufferInfos.push_back(bufferInfo);

            // Keep track of the write
            m_writes.push_back({ binding, type, static_cast<int32_t>(m_bufferInfos.size() - 1), -1 });

            return *this;
        }

        // Add image binding
        DescriptorWriter& writeImage(uint32_t binding, VkImageView imageView, VkSampler sampler,
            VkImageLayout layout, VkDescriptorType type,
            VkShaderStageFlags stageFlags) {
            // Add binding to layout bindings
            VkDescriptorSetLayoutBinding layoutBinding{};
            layoutBinding.binding = binding;
            layoutBinding.descriptorType = type;
            layoutBinding.descriptorCount = 1;
            layoutBinding.stageFlags = stageFlags;

            m_bindings.push_back(layoutBinding);

            // Add write info
            VkDescriptorImageInfo imageInfo{};
            imageInfo.imageView = imageView;
            imageInfo.sampler = sampler;
            imageInfo.imageLayout = layout;

            m_imageInfos.push_back(imageInfo);

            // Keep track of the write
            m_writes.push_back({ binding, type, -1, static_cast<int32_t>(m_imageInfos.size() - 1) });

            return *this;
        }

        // Build and update a descriptor set
        bool build(VkDescriptorSet& set) {
            // Create layout from bindings
            VkDescriptorSetLayout layout = m_layoutCache.getLayout(m_bindings);

            // Allocate the descriptor set
            set = m_allocator.allocate(layout);
            if (set == VK_NULL_HANDLE) {
                return false;
            }

            // Update the descriptor set
            return update(set);
        }

        // Update an existing descriptor set
        bool update(VkDescriptorSet set) {
            std::vector<VkWriteDescriptorSet> writes;
            writes.reserve(m_writes.size());

            for (const auto& write : m_writes) {
                VkWriteDescriptorSet descriptorWrite{};
                descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                descriptorWrite.dstSet = set;
                descriptorWrite.dstBinding = write.binding;
                descriptorWrite.dstArrayElement = 0;
                descriptorWrite.descriptorType = write.type;
                descriptorWrite.descriptorCount = 1;

                if (write.bufferIndex != -1) {
                    descriptorWrite.pBufferInfo = &m_bufferInfos[write.bufferIndex];
                }
                else if (write.imageIndex != -1) {
                    descriptorWrite.pImageInfo = &m_imageInfos[write.imageIndex];
                }

                writes.push_back(descriptorWrite);
            }

            vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
            return true;
        }

    };

    class DescriptorBuilder {
    public:
        DescriptorBuilder(VkDevice device, const ShaderReflection& reflection,
            std::unique_ptr<DescriptorPoolResource>& pool)
            : m_device(device), m_reflection(reflection), m_descriptorPool(pool) {
        }

        void takeDescriptorSets(std::vector<std::unique_ptr<DescriptorSetResource>>& outSets) {
            outSets.clear();
            for (auto& set : m_descriptorSets) {
                outSets.push_back(std::move(set));
            }
            m_descriptorSets.clear();
        }


        // Initialize from shader reflection
        bool buildFromReflection() {
            // First, create set layouts from reflection
            createDescriptorSetLayouts();

            // Then allocate descriptor sets
            if (!allocateDescriptorSets()) {
                return false;
            }

            // Finally, update the descriptor sets based on reflection data
            return updateDescriptorSetsFromReflection();
        }

        // Get the descriptor sets
        const std::vector<std::unique_ptr<DescriptorSetResource>>& getDescriptorSets() const {
            return std::move(m_descriptorSets);
        }

        // Register a uniform buffer for automatic binding
        DescriptorBuilder& registerUniformBuffer(const std::string& name, Buffer* buffer,
            size_t size) {
            m_registeredBuffers[name] = { buffer, size };
            return *this;
        }

        DescriptorBuilder& registerTexture(const std::string& name, ImageViewResource* imageView,
            SamplerResource* sampler) {
            m_registeredTextures[name] = { imageView, sampler };
            return *this;
        }

        DescriptorBuilder& setDefaultTexture(ImageViewResource* imageView, SamplerResource* sampler) {
            m_defaultImageView = imageView;
            m_defaultSampler = sampler;
            return *this;
        }

    private:
        struct BufferInfo {
            Buffer* buffer;
            size_t size;
        };

        struct TextureInfo {
            ImageViewResource* imageView;
            SamplerResource* sampler;
        };

        bool createDescriptorSetLayouts() {
            // Use reflection data to create layouts
            std::unordered_map<uint32_t, std::vector<VkDescriptorSetLayoutBinding>> setBindings;

            // Add UBO bindings
            for (const auto& ubo : m_reflection.getUniformBuffers()) {
                VkDescriptorSetLayoutBinding binding{};
                binding.binding = ubo.binding;
                binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                binding.descriptorCount = 1;
                binding.stageFlags = ubo.stageFlags;

                setBindings[ubo.set].push_back(binding);
            }

            // Add resource bindings (textures, etc.)
            for (const auto& resource : m_reflection.getResourceBindings()) {
                VkDescriptorSetLayoutBinding binding{};
                binding.binding = resource.binding;
                binding.descriptorType = resource.descriptorType;
                binding.descriptorCount = 1;
                binding.stageFlags = resource.stageFlags;

                setBindings[resource.set].push_back(binding);
            }

            // Create layouts for each set
            m_setLayouts.clear();
            for (const auto& [set, bindings] : setBindings) {
                VkDescriptorSetLayoutCreateInfo layoutInfo{};
                layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
                layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
                layoutInfo.pBindings = bindings.data();

                VkDescriptorSetLayout layout;
                if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &layout) != VK_SUCCESS) {
                    Logger::get().error("Failed to create descriptor set layout for set {}", set);
                    return false;
                }

                m_setLayouts[set] = layout;
            }

            return true;
        }

        bool allocateDescriptorSets() {
            // Skip if no layouts
            if (m_setLayouts.empty()) {
                return true;
            }

            // Approach 1: Only include valid layouts without gaps
            std::vector<VkDescriptorSetLayout> rawLayouts;
            std::vector<uint32_t> setIndices;  // Keep track of which set each layout corresponds to

            for (const auto& [set, layout] : m_setLayouts) {
                Logger::get().info("Set {}: layout = {}", set,
                    layout != VK_NULL_HANDLE ? "VALID" : "NULL");
                if (layout != VK_NULL_HANDLE) {  // Ensure layout is valid
                    rawLayouts.push_back(layout);
                    setIndices.push_back(set);
                }
            }

            // Allocate descriptor sets for valid layouts
            VkDescriptorSetAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocInfo.descriptorPool = m_descriptorPool->handle();
            allocInfo.descriptorSetCount = static_cast<uint32_t>(rawLayouts.size());
            allocInfo.pSetLayouts = rawLayouts.data();

            std::vector<VkDescriptorSet> rawSets(rawLayouts.size());
            if (vkAllocateDescriptorSets(m_device, &allocInfo, rawSets.data()) != VK_SUCCESS) {
                Logger::get().error("Failed to allocate descriptor sets");
                return false;
            }

            // Create descriptor set wrappers, placing them at their correct set indices
            m_descriptorSets.resize(setIndices.empty() ? 0 : *std::max_element(setIndices.begin(), setIndices.end()) + 1);
            for (size_t i = 0; i < rawSets.size(); i++) {
                m_descriptorSets[setIndices[i]] = std::make_unique<DescriptorSetResource>(m_device, rawSets[i]);
            }

            return true;
        }


        bool updateDescriptorSetsFromReflection() {
            // Pre-allocate infos
            std::vector<VkDescriptorBufferInfo> bufferInfos;
            std::vector<VkDescriptorImageInfo> imageInfos;
            std::vector<std::pair<VkWriteDescriptorSet, size_t>> descriptorWritesWithIndices;

            // Process UBOs
            for (const auto& ubo : m_reflection.getUniformBuffers()) {
                // Find the buffer for this UBO
                BufferInfo bufferInfo = { nullptr, 0 };

                // Try to find registered buffer by name
                auto it = m_registeredBuffers.find(ubo.name);
                if (it != m_registeredBuffers.end()) {
                    bufferInfo = it->second;
                }
                else {
                    Logger::get().warning("UBO {} not registered, skipping", ubo.name);
                    continue;
                }

                if (!bufferInfo.buffer) {
                    Logger::get().warning("Buffer for UBO {} is null, skipping", ubo.name);
                    continue;
                }

                if (ubo.set >= m_descriptorSets.size()) {
                    Logger::get().error("UBO references set {} which doesn't exist", ubo.set);
                    continue;
                }

                // Add buffer info
                size_t bufferInfoIndex = bufferInfos.size();
                bufferInfos.push_back({
                    bufferInfo.buffer->getBuffer(),
                    0,
                    bufferInfo.size
                    });

                // Create write descriptor
                VkWriteDescriptorSet write{};
                write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.dstSet = m_descriptorSets[ubo.set]->handle();
                write.dstBinding = ubo.binding;
                write.dstArrayElement = 0;
                write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                write.descriptorCount = 1;
                write.pBufferInfo = nullptr; // Will set later

                descriptorWritesWithIndices.push_back({ write, bufferInfoIndex });
                Logger::get().info("Set up UBO: {} with size {} bytes", ubo.name, bufferInfo.size);
            }

            // Process resources (textures, etc.)
            for (const auto& resource : m_reflection.getResourceBindings()) {
                if (resource.descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
                    // Find the texture for this resource
                    TextureInfo textureInfo = { nullptr, nullptr };

                    // Try to find registered texture by name
                    auto it = m_registeredTextures.find(resource.name);
                    if (it != m_registeredTextures.end()) {
                        textureInfo = it->second;
                    }
                    else {
                        // Use default texture
                        if (m_defaultImageView && m_defaultSampler) {
                            textureInfo = { m_defaultImageView, m_defaultSampler };
                            Logger::get().info("Using default texture for {}", resource.name);
                        }
                        else {
                            Logger::get().warning("Texture {} not registered and no default texture, skipping", resource.name);
                            continue;
                        }
                    }

                    if (!textureInfo.imageView || !textureInfo.sampler) {
                        Logger::get().warning("Image view or sampler for texture {} is null, skipping", resource.name);
                        continue;
                    }

                    if (resource.set >= m_descriptorSets.size()) {
                        Logger::get().error("Resource references set {} which doesn't exist", resource.set);
                        continue;
                    }

                    // Add image info
                    size_t imageInfoIndex = imageInfos.size();
                    imageInfos.push_back({
                        textureInfo.sampler->handle(),
                        textureInfo.imageView->handle(),
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                        });

                    // Create write descriptor
                    VkWriteDescriptorSet write{};
                    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    write.dstSet = m_descriptorSets[resource.set]->handle();
                    write.dstBinding = resource.binding;
                    write.dstArrayElement = 0;
                    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    write.descriptorCount = 1;
                    write.pImageInfo = nullptr; // Will set later

                    descriptorWritesWithIndices.push_back({ write, imageInfoIndex });
                    Logger::get().info("Set up texture: {}", resource.name);
                }
                // Add support for other descriptor types here as needed
            }

            // Create final descriptor writes with correct pointers
            std::vector<VkWriteDescriptorSet> descriptorWrites;
            descriptorWrites.reserve(descriptorWritesWithIndices.size());

            for (const auto& [write, index] : descriptorWritesWithIndices) {
                VkWriteDescriptorSet finalWrite = write;
                if (write.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
                    finalWrite.pBufferInfo = &bufferInfos[index];
                }
                else if (write.descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
                    finalWrite.pImageInfo = &imageInfos[index];
                }
                descriptorWrites.push_back(finalWrite);
            }

            // Update descriptors
            if (!descriptorWrites.empty()) {
                vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(descriptorWrites.size()),
                    descriptorWrites.data(), 0, nullptr);
                Logger::get().info("Updated {} descriptor writes", descriptorWrites.size());
            }

            return true;
        }

        VkDevice m_device;
        const ShaderReflection& m_reflection;
        std::unique_ptr<DescriptorPoolResource>& m_descriptorPool;

        std::unordered_map<uint32_t, VkDescriptorSetLayout> m_setLayouts;
        std::vector<std::unique_ptr<DescriptorSetResource>> m_descriptorSets;

        std::unordered_map<std::string, BufferInfo> m_registeredBuffers;
        std::unordered_map<std::string, TextureInfo> m_registeredTextures;

        ImageViewResource* m_defaultImageView = nullptr;
        SamplerResource* m_defaultSampler = nullptr;
    };

    class VertexBufferSimple {
    public:
        VertexBufferSimple(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, size_t vertexCount)
            : m_device(device), m_buffer(buffer), m_memory(memory), m_vertexCount(vertexCount) {
        }

        ~VertexBufferSimple() {
            if (m_buffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(m_device, m_buffer, nullptr);
                m_buffer = VK_NULL_HANDLE;
            }

            if (m_memory != VK_NULL_HANDLE) {
                vkFreeMemory(m_device, m_memory, nullptr);
                m_memory = VK_NULL_HANDLE;
            }
        }

        // Bind the vertex buffer
        void bind(VkCommandBuffer cmdBuffer) const {
            VkDeviceSize offsets[] = { 0 };
            vkCmdBindVertexBuffers(cmdBuffer, 0, 1, &m_buffer, offsets);
        }

        // Get vertex count
        uint32_t getVertexCount() const { return static_cast<uint32_t>(m_vertexCount); }

        VkBuffer getBuffer() const { return m_buffer; }

    private:
        VkDevice m_device;
        VkBuffer m_buffer = VK_NULL_HANDLE;
        VkDeviceMemory m_memory = VK_NULL_HANDLE;
        size_t m_vertexCount = 0;
    };

    class MeshRegistry {
    public:
        struct alignas(16) MeshData {
            uint32_t vertexCount;
            uint32_t indexCount;
            VkBuffer vertexBuffer;
            VkBuffer indexBuffer;
            VkIndexType indexType;
            AABBF bounds;
        };

        // Register a mesh and get its ID
        uint32_t registerMesh(const MeshData& mesh, const std::string& name = "") {
            uint32_t id = static_cast<uint32_t>(m_meshes.size());
            m_meshes.push_back(mesh);

            if (!name.empty()) {
                m_meshNames[name] = id;
            }

            return id;
        }

        // Register your existing vertex buffer
        uint32_t registerMesh(VertexBufferSimple* vertexBuffer, const std::string& name = "") {
            MeshData mesh;
            mesh.vertexCount = vertexBuffer->getVertexCount();
            mesh.indexCount = 0; // No indices for this simple mesh
            mesh.vertexBuffer = vertexBuffer->getBuffer(); // Assuming you add a getter
            mesh.indexBuffer = VK_NULL_HANDLE;
            mesh.indexType = VK_INDEX_TYPE_UINT32; // Default

            // Calculate bounds from vertices or use fixed bounds for the cube
            mesh.bounds = {
                glm::vec3(-0.5f, -0.5f, -0.5f),
                glm::vec3(0.5f, 0.5f, 0.5f)
            };

            return registerMesh(mesh, name);
        }

        // Get a mesh by ID
        const MeshData* getMesh(uint32_t id) const {
            if (id < m_meshes.size()) {
                return &m_meshes[id];
            }
            return nullptr;
        }

        // Get a mesh by name
        const MeshData* getMesh(const std::string& name) const {
            auto it = m_meshNames.find(name);
            if (it != m_meshNames.end()) {
                return &m_meshes[it->second];
            }
            return nullptr;
        }

        // Get a mesh ID by name
        uint32_t getMeshID(const std::string& name) const {
            auto it = m_meshNames.find(name);
            if (it != m_meshNames.end()) {
                return it->second;
            }
            return UINT32_MAX; // Invalid ID
        }

    private:
        std::vector<MeshData> m_meshes;
        std::unordered_map<std::string, uint32_t> m_meshNames;
    };

    class VulkanBackend : public RenderBackend {
    public:

        uint32_t loadMeshFromFile(const std::string& filename);

        uint32_t createMaterialFromDesc(const MaterialDesc& desc);

        void addObjectToScene(uint32_t meshID, uint32_t materialID, const glm::mat4& transform);

        std::vector<uint32_t> m_materialIDs;
        uint32_t m_cubeMeshID;

        void createEnhancedScene() {
            Logger::get().info("=== CREATING ENHANCED SCENE (FIXED) ===");

            // CLEAR any existing octree
            AABBQ worldBounds{
                Vec3Q::fromFloat(glm::vec3(-20.0f, -20.0f, -20.0f)),
                Vec3Q::fromFloat(glm::vec3(20.0f,  20.0f,  20.0f))
            };
            m_sceneOctree = tremor::gfx::Octree<tremor::gfx::RenderableObject>(worldBounds);

            Logger::get().info("Creating exactly 25 objects...");

            // Create exactly 25 objects - no more, no less
            for (int i = 0; i < 25; i++) {
                tremor::gfx::RenderableObject obj;
                obj.meshID = m_cubeMeshID;
                obj.materialID = m_materialIDs[i % m_materialIDs.size()];
                obj.instanceID = i;
                obj.flags = 1; // Visible

                // Simple grid positioning
                float spacing = 2.5f;
                float x = (i % 5 - 2) * spacing; // -6, -3, 0, 3, 6
                float z = (i / 5 - 2) * spacing; // -6, -3, 0, 3, 6  
                float y = 5.0f;

                obj.transform = glm::translate(glm::mat4(1.0f), glm::vec3(x, y, z));
                obj.prevTransform = obj.transform;

                glm::mat4& transform = obj.transform;
                Logger::get().info("Object {}: matrix row 0=({:.2f}, {:.2f}, {:.2f}, {:.2f})",
                    i, transform[0][0], transform[0][1], transform[0][2], transform[0][3]);
                Logger::get().info("Object {}: matrix row 1=({:.2f}, {:.2f}, {:.2f}, {:.2f})",
                    i, transform[1][0], transform[1][1], transform[1][2], transform[1][3]);
                Logger::get().info("Object {}: matrix row 2=({:.2f}, {:.2f}, {:.2f}, {:.2f})",
                    i, transform[2][0], transform[2][1], transform[2][2], transform[2][3]);
                Logger::get().info("Object {}: matrix row 3=({:.2f}, {:.2f}, {:.2f}, {:.2f})",
                    i, transform[3][0], transform[3][1], transform[3][2], transform[3][3]);

                // Calculate bounds
                AABBF localBounds{ glm::vec3(-0.5f), glm::vec3(0.5f) };
                AABBF worldBounds = transformAABB(obj.transform, localBounds);
                obj.bounds = AABBQ::fromFloat(worldBounds);

                Logger::get().info("Creating object {}: pos=({:.1f},{:.1f},{:.1f})", i, x, y, z);

                // Insert ONCE into octree
                try {
                    m_sceneOctree.insert(obj, obj.bounds);
                    Logger::get().info("  Inserted object {} successfully", i);
                }
                catch (const std::exception& e) {
                    Logger::get().error("  Failed to insert object {}: {}", i, e.what());
                }
            }

            // Verify exactly 25 objects
            auto allOctreeObjects = m_sceneOctree.getAllObjects();
            Logger::get().info("VERIFICATION: Expected 25 objects, octree has {}", allOctreeObjects.size());

            if (allOctreeObjects.size() != 25) {
                Logger::get().error("CRITICAL: Object count mismatch! Expected 25, got {}",
                    allOctreeObjects.size());

                // Log all objects to find duplicates
                std::map<uint32_t, int> instanceCounts;
                for (const auto& obj : allOctreeObjects) {
                    instanceCounts[obj.instanceID]++;
                }

                for (const auto& [instanceID, count] : instanceCounts) {
                    if (count > 1) {
                        Logger::get().error("  Instance {} appears {} times (DUPLICATE!)",
                            instanceID, count);
                    }
                }
            }
            m_clusteredRenderer->updateGPUBuffers();

            // Create simple lighting
            std::vector<tremor::gfx::ClusterLight> lights;
            tremor::gfx::ClusterLight mainLight;
            mainLight.position = glm::vec3(0.0f, 10.0f, 5.0f);
            mainLight.color = glm::vec3(1.0f, 1.0f, 1.0f);
            mainLight.intensity = 3.0f;
            mainLight.radius = 50.0f;
            mainLight.type = 0;
            lights.push_back(mainLight);

            m_clusteredRenderer->updateLights(lights);
            Logger::get().info("Scene creation complete");
        }

        void createTaffyScene() {
            Logger::get().info("=== CREATING TAFFY-BASED SCENE ===");

            // Clear existing octree
            AABBQ worldBounds{
                Vec3Q::fromFloat(glm::vec3(-50.0f, -50.0f, -50.0f)),
                Vec3Q::fromFloat(glm::vec3(50.0f, 50.0f, 50.0f))
            };
            m_sceneOctree = tremor::gfx::Octree<tremor::gfx::RenderableObject>(worldBounds);

            if (loaded_assets_.empty()) {
                Logger::get().error("No loaded assets to create scene from");
                return;
            }

            // Create objects using loaded Taffy assets
            int object_count = 0;
            const int grid_size = 5;
            const float spacing = 8.0f;

            for (int x = 0; x < grid_size; ++x) {
                for (int z = 0; z < grid_size; ++z) {
                    // Pick a random asset (or cycle through them)
                    const auto& asset = loaded_assets_[object_count % loaded_assets_.size()];

                    tremor::gfx::RenderableObject obj;
                    obj.meshID = asset->get_primary_mesh_id();
                    obj.materialID = asset->get_primary_material_id();
                    obj.instanceID = object_count;
                    obj.flags = 1; // Visible

                    // Position in grid
                    float pos_x = (x - grid_size / 2) * spacing;
                    float pos_z = (z - grid_size / 2) * spacing;
                    float pos_y = 0.0f; // Ground level

                    obj.transform = glm::translate(glm::mat4(1.0f), glm::vec3(pos_x, pos_y, pos_z));
                    obj.prevTransform = obj.transform;

                    // Use bounds from the Taffy mesh
                    glm::vec3 bounds_min = asset->meshes[0]->getBoundsMin();
                    glm::vec3 bounds_max = asset->meshes[0]->getBoundsMax();

                    // Transform bounds to world space
                    AABBF localBounds{ bounds_min, bounds_max };
                    AABBF worldBounds = transformAABB(obj.transform, localBounds);
                    obj.bounds = AABBQ::fromFloat(worldBounds);

                    Logger::get().info("Creating Taffy object {}: pos=({:.1f},{:.1f},{:.1f})",
                        object_count, pos_x, pos_y, pos_z);

                    // Insert into octree
                    try {
                        m_sceneOctree.insert(obj, obj.bounds);
                        Logger::get().info("  Inserted object {} successfully", object_count);
                    }
                    catch (const std::exception& e) {
                        Logger::get().error("  Failed to insert object {}: {}", object_count, e.what());
                    }

                    object_count++;
                }
            }

            // Update GPU buffers
            m_clusteredRenderer->updateGPUBuffers();

            // Create lighting
            createSceneLighting();

            Logger::get().info("Taffy scene creation complete with {} objects", object_count);
        }

        void createSceneLighting() {
            std::vector<tremor::gfx::ClusterLight> lights;

            tremor::gfx::ClusterLight mainLight;
            mainLight.position = glm::vec3(0.0f, 20.0f, 10.0f);
            mainLight.color = glm::vec3(1.0f, 1.0f, 1.0f);
            mainLight.intensity = 5.0f;
            mainLight.radius = 100.0f;
            mainLight.type = 0;
            lights.push_back(mainLight);

            m_clusteredRenderer->updateLights(lights);
        }

        std::vector<std::unique_ptr<Taffy::Asset>> loaded_assets_;

        void createTaffyMeshes() {
            Logger::get().info("=== LOADING TAFFY ASSETS ===");

            // Initialize the Taffy loader
            taffy_loader_ = std::make_unique<Tremor::TaffyAssetLoader>(*m_clusteredRenderer);

            // Try to load some Taffy assets
            std::vector<std::string> asset_paths = {
                "assets/cube.taf",
                "assets/sphere.taf",
                "assets/complex_building.taf"
            };

            for (const auto& path : asset_paths) {
                auto loaded_asset = taffy_loader_->load_asset(path);
                if (loaded_asset) {
                    loaded_assets_.push_back(std::move(loaded_asset));
                    Logger::get().info("Successfully loaded Taffy asset: {}", path);
                }
                else {
                    Logger::get().warning("Failed to load Taffy asset: {}", path);

                    // Fallback to creating a simple mesh manually
                    if (path.find("cube") != std::string::npos) {
                        createFallbackCube();
                    }
                }
            }

            // If no assets loaded, create fallback content
            if (loaded_assets_.empty()) {
                Logger::get().info("No Taffy assets loaded, creating fallback content");
                createFallbackContent();
            }
            else {
                Logger::get().info("Loaded {} Taffy assets", loaded_assets_.size());
            }
        }

        MeshRegistry m_meshRegistry;

        std::unique_ptr<tremor::gfx::VulkanClusteredRenderer> m_clusteredRenderer;
        tremor::gfx::Octree<tremor::gfx::RenderableObject> m_sceneOctree;

        tremor::gfx::Camera cam;

		SwapChain::CreateInfo ci{};

        std::unique_ptr<VulkanDevice> vkDevice;
        std::unique_ptr<SwapChain> vkSwapchain;

        InstanceResource instance;
        SurfaceResource surface;

        SDL_Window* w;

        std::unique_ptr<VulkanResourceManager> res;

        std::unique_ptr<PipelineResource> m_meshShaderPipeline;
        std::unique_ptr<PipelineLayoutResource> m_meshShaderPipelineLayout;

        // Collection of loaded shaders for the current pipeline
        std::vector<std::shared_ptr<ShaderModule>> m_pipelineShaders;

        // Storage for descriptor set layouts
        std::vector<std::unique_ptr<DescriptorSetLayoutResource>> m_descriptorSetLayouts;

        // Storage for descriptor sets
        std::vector<std::unique_ptr<DescriptorSetResource>> m_descriptorSets;

#if _DEBUG
        VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
#endif

        VulkanBackend() : m_sceneOctree(AABBQ{
            {-1'000'000'000, -1'000'000'000, -1'000'000'000}, // min bounds
            { 1'000'000'000,  1'000'000'000,  1'000'000'000}  // max bounds
            }) {
        }

        ~VulkanBackend() override = default;

        struct UniformBufferObject {
            alignas(16) glm::mat4 model;
            alignas(16) glm::mat4 view;
            alignas(16) glm::mat4 proj;
            alignas(16) glm::vec3 cameraPos;
        };

        // Add this to your VulkanBackend class as a member variable
        std::unique_ptr<Buffer> m_uniformBuffer;

        // Create a method to create the UBO
        bool createUniformBuffer() {
            // Create the uniform buffer
            VkDeviceSize bufferSize = sizeof(UniformBufferObject);
            m_uniformBuffer = std::make_unique<Buffer>(
                device,
                physicalDevice,
                bufferSize,
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            );

            // Initialize the UBO with identity matrices and a camera position
            updateUniformBuffer();

            return true;
        }

        bool createMinimalMeshShaderPipeline() {
            // Load shaders
            auto taskShader = ShaderModule::compileFromFile(device, "shaders/diag.task");
            auto meshShader = ShaderModule::compileFromFile(device, "shaders/diag.mesh");
            auto fragShader = ShaderModule::compileFromFile(device, "shaders/diag.frag");

            if (!taskShader || !meshShader || !fragShader) {
                Logger::get().error("Failed to compile mesh shaders");
                return false;
            }

            // Create simple pipeline layout (no descriptors yet)
            VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
            pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

            VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
            if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
                Logger::get().error("Failed to create pipeline layout");
                return false;
            }

            m_meshShaderPipelineLayout = std::make_unique<PipelineLayoutResource>(device, pipelineLayout);
            Logger::get().info("Created mesh shader pipeline layout");

            // Create shader stages
            std::vector<VkPipelineShaderStageCreateInfo> shaderStages = {
                taskShader->createShaderStageInfo(),
                meshShader->createShaderStageInfo(),
                fragShader->createShaderStageInfo()
            };

            // Simple pipeline state
            VkPipelineViewportStateCreateInfo viewportState{};
            viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            viewportState.viewportCount = 1;
            viewportState.scissorCount = 1;

            VkPipelineRasterizationStateCreateInfo rasterizer{};
            rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            rasterizer.depthClampEnable = VK_FALSE;
            rasterizer.rasterizerDiscardEnable = VK_FALSE;
            rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
            rasterizer.lineWidth = 1.0f;
            rasterizer.cullMode = VK_CULL_MODE_NONE;  // No culling to start
            rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            rasterizer.depthBiasEnable = VK_FALSE;

            VkPipelineMultisampleStateCreateInfo multisampling{};
            multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            multisampling.sampleShadingEnable = VK_FALSE;
            multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

            VkPipelineColorBlendAttachmentState colorBlendAttachment{};
            colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            colorBlendAttachment.blendEnable = VK_FALSE;

            VkPipelineColorBlendStateCreateInfo colorBlending{};
            colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            colorBlending.logicOpEnable = VK_FALSE;
            colorBlending.attachmentCount = 1;
            colorBlending.pAttachments = &colorBlendAttachment;

            VkDynamicState dynamicStates[] = {
                VK_DYNAMIC_STATE_VIEWPORT,
                VK_DYNAMIC_STATE_SCISSOR
            };

            VkPipelineDynamicStateCreateInfo dynamicState{};
            dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dynamicState.dynamicStateCount = 2;
            dynamicState.pDynamicStates = dynamicStates;

            // Setup the pipeline using dynamic rendering
            VkPipelineRenderingCreateInfoKHR renderingInfo{};
            renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
            renderingInfo.colorAttachmentCount = 1;
            VkFormat colorFormat = vkSwapchain->imageFormat();
            renderingInfo.pColorAttachmentFormats = &colorFormat;

            VkGraphicsPipelineCreateInfo pipelineInfo{};
            pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            pipelineInfo.pNext = &renderingInfo;  // Use dynamic rendering
            pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
            pipelineInfo.pStages = shaderStages.data();
            pipelineInfo.pViewportState = &viewportState;
            pipelineInfo.pRasterizationState = &rasterizer;
            pipelineInfo.pMultisampleState = &multisampling;
            pipelineInfo.pColorBlendState = &colorBlending;
            pipelineInfo.pDynamicState = &dynamicState;
            pipelineInfo.layout = pipelineLayout;

            // Create the pipeline
            VkPipeline pipeline = VK_NULL_HANDLE;
            VkResult result = vkCreateGraphicsPipelines(
                device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);

            if (result != VK_SUCCESS) {
                Logger::get().error("Failed to create mesh shader pipeline: {}", (int)result);
                return false;
            }

            m_meshShaderPipeline = std::make_unique<PipelineResource>(device, pipeline);
            Logger::get().info("Created mesh shader pipeline successfully");

            return true;
        }

        // Add a method to update the UBO every frame
        void updateUniformBuffer() {
            static auto startTime = std::chrono::high_resolution_clock::now();
            auto currentTime = std::chrono::high_resolution_clock::now();
            float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

            // Position camera to see the 5x5 grid clearly


            UniformBufferObject ubo{};
            ubo.model = glm::mat4(1.0f); // Identity - objects have their own transforms
            ubo.view = cam.getViewMatrix();
            ubo.proj = cam.getProjectionMatrix();
            ubo.cameraPos = cam.getLocalPosition();

            m_uniformBuffer->update(&ubo, sizeof(ubo));

            // Debug camera position occasionally
            static int frameCount = 0;
            if (++frameCount % 60 == 0) { // Every 60 frames
                glm::vec3 pos = cam.getLocalPosition();
                glm::vec3 forward = cam.getForward();
                Logger::get().info("Camera: pos=({:.1f},{:.1f},{:.1f}), forward=({:.2f},{:.2f},{:.2f})",
                    pos.x, pos.y, pos.z, forward.x, forward.y, forward.z);
            }


        }

        std::unique_ptr<Buffer> m_lightBuffer;

        // Add this struct to your class
        struct alignas(16) LightUBO {
            alignas(16) glm::vec3 position;
            alignas(16) glm::vec3 color;
            float ambientStrength;
            float diffuseStrength;
            float specularStrength;
            float shininess;
        };

        // Add this function to create and initialize the light buffer
        bool createLightBuffer() {
            // Create the light uniform buffer
            VkDeviceSize bufferSize = sizeof(LightUBO);
            m_lightBuffer = std::make_unique<Buffer>(
                device,
                physicalDevice,
                bufferSize,
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            );

            // Initialize with default light values
            LightUBO light{};
            light.position = glm::vec3(0.0f, 0.0f, 5.0f);  // Light position above and in front
            light.color = glm::vec3(1.0f, 1.0f, 1.0f);     // White light
            light.ambientStrength = 0.1f;                  // Subtle ambient light
            light.diffuseStrength = 0.7f;                  // Strong diffuse component
            light.specularStrength = 0.5f;                 // Medium specular highlights
            light.shininess = 32.0f;                       // Moderately focused highlights

            m_lightBuffer->update(&light, sizeof(light));
			Logger::get().info("Light buffer created successfully");
            return true;
        }

        bool updateLight() {
            static auto startTime = std::chrono::high_resolution_clock::now();
            auto currentTime = std::chrono::high_resolution_clock::now();
            float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();


            LightUBO light{};
            light.position = glm::vec3(sin(time)*5, 0.0, cos(time)*5);  // Light position above and in front
            light.color = glm::vec3(1.0f, 1.0f, 1.0f);     // White light
            light.ambientStrength = 0.1f;                  // Subtle ambient light
            light.diffuseStrength = 0.7f;                  // Strong diffuse component
            light.specularStrength = 0.5f;                 // Medium specular highlights
            light.shininess = 32.0f;                       // Moderately focused highlights

            m_lightBuffer->update(&light, sizeof(light));
            Logger::get().info("Light buffer created successfully");
            return true;

        }

        std::unique_ptr<Buffer> m_materialBuffer;


        // Add this struct to your class
        struct alignas(16) MaterialUBO {
            alignas(16) glm::vec4 baseColor;
            float metallic;
            float roughness;
            float ao;
            float emissiveFactor;
            alignas(16) glm::vec3 emissiveColor;
            float padding;

            // Flags for available textures
            int hasAlbedoMap;
            int hasNormalMap;
            int hasMetallicRoughnessMap;
            int hasEmissiveMap;
            int hasOcclusionMap;
        };

        // Add this function to create and initialize the material buffer
        bool createMaterialBuffer() {
            // Create the material uniform buffer
            VkDeviceSize bufferSize = sizeof(MaterialUBO);
            m_materialBuffer = std::make_unique<Buffer>(
                device,
                physicalDevice,
                bufferSize,
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            );

            // Initialize with default PBR material values
            MaterialUBO material{};
            material.baseColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);  // White base color
            material.metallic = 0.0f;                                // Non-metallic by default
            material.roughness = 0.5f;                               // Medium roughness
            material.ao = 1.0f;                                      // Full ambient occlusion
            material.emissiveFactor = 0.0f;                          // No emission by default
            material.emissiveColor = glm::vec3(1.0f, 1.0f, 1.0f);    // White emission color
            material.padding = 0.0f;                                 // Padding for alignment

            // No textures by default
            material.hasAlbedoMap = 1;
            material.hasNormalMap = 0;
            material.hasMetallicRoughnessMap = 0;
            material.hasEmissiveMap = 0;
            material.hasOcclusionMap = 0;

            m_materialBuffer->update(&material, sizeof(material));
            Logger::get().info("Material buffer created successfully");
            return true;
        }


        bool createCommandPool() {
            // Get the graphics queue family index
            uint32_t queueFamilyIndex = vkDevice->graphicsQueueFamily();

            // Command pool creation info
            VkCommandPoolCreateInfo poolInfo{};
            poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            poolInfo.queueFamilyIndex = queueFamilyIndex;
            poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; // Allow individual command buffer reset

            // Create the command pool
            VkCommandPool commandPool = VK_NULL_HANDLE;
            if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
                Logger::get().error("Failed to create command pool");
                return false;
            }

            // Store in RAII wrapper
            m_commandPool = std::make_unique<CommandPoolResource>(device, commandPool);
            Logger::get().info("Command pool created successfully");

            return true;
        }

        bool createCommandBuffers() {
            // Make sure we have a command pool
            if (!m_commandPool || !*m_commandPool) {
                Logger::get().error("Cannot create command buffers without a valid command pool");
                return false;
            }

            // Resize the command buffer vector
            m_commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);

            // Allocate command buffers
            VkCommandBufferAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocInfo.commandPool = *m_commandPool;
            allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocInfo.commandBufferCount = static_cast<uint32_t>(m_commandBuffers.size());

            if (vkAllocateCommandBuffers(device, &allocInfo, m_commandBuffers.data()) != VK_SUCCESS) {
                Logger::get().error("Failed to allocate command buffers");
                return false;
            }

            Logger::get().info("Command buffers created successfully: {}", m_commandBuffers.size());
            return true;
        }

        bool createSyncObjects() {
            // Resize sync object vectors
            m_imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
            m_renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
            m_inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

            // Create semaphores and fences for each frame
            VkSemaphoreCreateInfo semaphoreInfo{};
            semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

            VkFenceCreateInfo fenceInfo{};
            fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // Start signaled so first frame doesn't wait indefinitely

            for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
                // Create image available semaphore
                VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
                if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphore) != VK_SUCCESS) {
                    Logger::get().error("Failed to create image available semaphore for frame {}", i);
                    return false;
                }
                m_imageAvailableSemaphores[i] = SemaphoreResource(device, imageAvailableSemaphore);

                // Create render finished semaphore
                VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE;
                if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphore) != VK_SUCCESS) {
                    Logger::get().error("Failed to create render finished semaphore for frame {}", i);
                    return false;
                }
                m_renderFinishedSemaphores[i] = SemaphoreResource(device, renderFinishedSemaphore);

                // Create in-flight fence
                VkFence inFlightFence = VK_NULL_HANDLE;
                if (vkCreateFence(device, &fenceInfo, nullptr, &inFlightFence) != VK_SUCCESS) {
                    Logger::get().error("Failed to create in-flight fence for frame {}", i);
                    return false;
                }
                m_inFlightFences[i] = FenceResource(device, inFlightFence);
            }

            Logger::get().info("Synchronization objects created successfully");
            return true;
        }

        struct BlinnPhongVertex {
            float position[3];  // XYZ position - location 0
            float normal[3];    // Normal vector - location 1
            float texCoord[2];  // UV coordinates - location 3

            static VkVertexInputBindingDescription getBindingDescription() {
                VkVertexInputBindingDescription bindingDescription{};
                bindingDescription.binding = 0;
                bindingDescription.stride = sizeof(BlinnPhongVertex);
                bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
                return bindingDescription;
            }

            static std::array<VkVertexInputAttributeDescription, 3> getAttributeDescriptions() {
                std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions{};

                // Position attribute
                attributeDescriptions[0].binding = 0;
                attributeDescriptions[0].location = 0;
                attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
                attributeDescriptions[0].offset = offsetof(BlinnPhongVertex, position);

                // Normal attribute - location 1
                attributeDescriptions[1].binding = 0;
                attributeDescriptions[1].location = 1;
                attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
                attributeDescriptions[1].offset = offsetof(BlinnPhongVertex, normal);

                // Texture coordinate attribute - location 2 (not 3!)
                attributeDescriptions[2].binding = 0;
                attributeDescriptions[2].location = 2;
                attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
                attributeDescriptions[2].offset = offsetof(BlinnPhongVertex, texCoord);
                return attributeDescriptions;
            }
        };

        std::vector<BlinnPhongVertex> createCube() {
            // Create a cube with proper normals for each face
            std::vector<BlinnPhongVertex> vertices;

            // Front face (Z+)
            vertices.push_back({ {-0.5f, -0.5f,  0.5f}, { 0.0f,  0.0f,  1.0f}, {0.0f, 0.0f} });
            vertices.push_back({ { 0.5f, -0.5f,  0.5f}, { 0.0f,  0.0f,  1.0f}, {1.0f, 0.0f} });
            vertices.push_back({ { 0.5f,  0.5f,  0.5f}, { 0.0f,  0.0f,  1.0f}, {1.0f, 1.0f} });
            vertices.push_back({ {-0.5f, -0.5f,  0.5f}, { 0.0f,  0.0f,  1.0f}, {0.0f, 0.0f} });
            vertices.push_back({ { 0.5f,  0.5f,  0.5f}, { 0.0f,  0.0f,  1.0f}, {1.0f, 1.0f} });
            vertices.push_back({ {-0.5f,  0.5f,  0.5f}, { 0.0f,  0.0f,  1.0f}, {0.0f, 1.0f} });

            // Back face (Z-)
            vertices.push_back({ { 0.5f, -0.5f, -0.5f}, { 0.0f,  0.0f, -1.0f}, {0.0f, 0.0f} });
            vertices.push_back({ {-0.5f, -0.5f, -0.5f}, { 0.0f,  0.0f, -1.0f}, {1.0f, 0.0f} });
            vertices.push_back({ {-0.5f,  0.5f, -0.5f}, { 0.0f,  0.0f, -1.0f}, {1.0f, 1.0f} });
            vertices.push_back({ { 0.5f, -0.5f, -0.5f}, { 0.0f,  0.0f, -1.0f}, {0.0f, 0.0f} });
            vertices.push_back({ {-0.5f,  0.5f, -0.5f}, { 0.0f,  0.0f, -1.0f}, {1.0f, 1.0f} });
            vertices.push_back({ { 0.5f,  0.5f, -0.5f}, { 0.0f,  0.0f, -1.0f}, {0.0f, 1.0f} });

            // Right face (X+)
            vertices.push_back({ { 0.5f, -0.5f,  0.5f}, { 1.0f,  0.0f,  0.0f}, {0.0f, 0.0f} });
            vertices.push_back({ { 0.5f, -0.5f, -0.5f}, { 1.0f,  0.0f,  0.0f}, {1.0f, 0.0f} });
            vertices.push_back({ { 0.5f,  0.5f, -0.5f}, { 1.0f,  0.0f,  0.0f}, {1.0f, 1.0f} });
            vertices.push_back({ { 0.5f, -0.5f,  0.5f}, { 1.0f,  0.0f,  0.0f}, {0.0f, 0.0f} });
            vertices.push_back({ { 0.5f,  0.5f, -0.5f}, { 1.0f,  0.0f,  0.0f}, {1.0f, 1.0f} });
            vertices.push_back({ { 0.5f,  0.5f,  0.5f}, { 1.0f,  0.0f,  0.0f}, {0.0f, 1.0f} });

            // Left face (X-)
            vertices.push_back({ {-0.5f, -0.5f, -0.5f}, {-1.0f,  0.0f,  0.0f}, {0.0f, 0.0f} });
            vertices.push_back({ {-0.5f, -0.5f,  0.5f}, {-1.0f,  0.0f,  0.0f}, {1.0f, 0.0f} });
            vertices.push_back({ {-0.5f,  0.5f,  0.5f}, {-1.0f,  0.0f,  0.0f}, {1.0f, 1.0f} });
            vertices.push_back({ {-0.5f, -0.5f, -0.5f}, {-1.0f,  0.0f,  0.0f}, {0.0f, 0.0f} });
            vertices.push_back({ {-0.5f,  0.5f,  0.5f}, {-1.0f,  0.0f,  0.0f}, {1.0f, 1.0f} });
            vertices.push_back({ {-0.5f,  0.5f, -0.5f}, {-1.0f,  0.0f,  0.0f}, {0.0f, 1.0f} });

            // Top face (Y+)
            vertices.push_back({ {-0.5f,  0.5f,  0.5f}, { 0.0f,  1.0f,  0.0f}, {0.0f, 0.0f} });
            vertices.push_back({ { 0.5f,  0.5f,  0.5f}, { 0.0f,  1.0f,  0.0f}, {1.0f, 0.0f} });
            vertices.push_back({ { 0.5f,  0.5f, -0.5f}, { 0.0f,  1.0f,  0.0f}, {1.0f, 1.0f} });
            vertices.push_back({ {-0.5f,  0.5f,  0.5f}, { 0.0f,  1.0f,  0.0f}, {0.0f, 0.0f} });
            vertices.push_back({ { 0.5f,  0.5f, -0.5f}, { 0.0f,  1.0f,  0.0f}, {1.0f, 1.0f} });
            vertices.push_back({ {-0.5f,  0.5f, -0.5f}, { 0.0f,  1.0f,  0.0f}, {0.0f, 1.0f} });

            // Bottom face (Y-)
            vertices.push_back({ {-0.5f, -0.5f, -0.5f}, { 0.0f, -1.0f,  0.0f}, {0.0f, 0.0f} });
            vertices.push_back({ { 0.5f, -0.5f, -0.5f}, { 0.0f, -1.0f,  0.0f}, {1.0f, 0.0f} });
            vertices.push_back({ { 0.5f, -0.5f,  0.5f}, { 0.0f, -1.0f,  0.0f}, {1.0f, 1.0f} });
            vertices.push_back({ {-0.5f, -0.5f, -0.5f}, { 0.0f, -1.0f,  0.0f}, {0.0f, 0.0f} });
            vertices.push_back({ { 0.5f, -0.5f,  0.5f}, { 0.0f, -1.0f,  0.0f}, {1.0f, 1.0f} });
            vertices.push_back({ {-0.5f, -0.5f,  0.5f}, { 0.0f, -1.0f,  0.0f}, {0.0f, 1.0f} });

            return vertices;
        }

        bool createCubeMesh() {
            try {
                // Create command pool for transfers if needed
                if (!m_transferCommandPool) {
                    VkCommandPoolCreateInfo poolInfo{};
                    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
                    poolInfo.queueFamilyIndex = vkDevice->graphicsQueueFamily();
                    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

                    VkCommandPool cmdPool;
                    if (vkCreateCommandPool(device, &poolInfo, nullptr, &cmdPool) != VK_SUCCESS) {
                        Logger::get().error("Failed to create transfer command pool");
                        return false;
                    }
                    m_transferCommandPool = CommandPoolResource(device, cmdPool);
                    Logger::get().info("Transfer command pool created");
                }

                // Create a cube with proper normals
                std::vector<BlinnPhongVertex> vertices = createCube();

                // Create vertex buffer
                VkDeviceSize vertexBufferSize = sizeof(vertices[0]) * vertices.size();

                // Create buffer
                VkBufferCreateInfo bufferInfo{};
                bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
                bufferInfo.size = vertexBufferSize;
                bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
                bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

                VkBuffer vertexBufferHandle;
                if (vkCreateBuffer(device, &bufferInfo, nullptr, &vertexBufferHandle) != VK_SUCCESS) {
                    Logger::get().error("Failed to create vertex buffer");
                    return false;
                }

                // Get memory requirements
                VkMemoryRequirements memRequirements;
                vkGetBufferMemoryRequirements(device, vertexBufferHandle, &memRequirements);

                // Find suitable memory type
                uint32_t memoryTypeIndex = res->findMemoryType(
                    memRequirements.memoryTypeBits,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                );

                // Allocate memory
                VkMemoryAllocateInfo allocInfo{};
                allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
                allocInfo.allocationSize = memRequirements.size;
                allocInfo.memoryTypeIndex = memoryTypeIndex;

                VkDeviceMemory vertexBufferMemory;  // This was missing!
                if (vkAllocateMemory(device, &allocInfo, nullptr, &vertexBufferMemory) != VK_SUCCESS) {
                    vkDestroyBuffer(device, vertexBufferHandle, nullptr);
                    Logger::get().error("Failed to allocate vertex buffer memory");
                    return false;
                }

                // Bind memory to buffer
                if (vkBindBufferMemory(device, vertexBufferHandle, vertexBufferMemory, 0) != VK_SUCCESS) {
                    vkFreeMemory(device, vertexBufferMemory, nullptr);
                    vkDestroyBuffer(device, vertexBufferHandle, nullptr);
                    Logger::get().error("Failed to bind buffer memory");
                    return false;
                }

                // Map and copy the vertex data
                void* data;
                if (vkMapMemory(device, vertexBufferMemory, 0, vertexBufferSize, 0, &data) != VK_SUCCESS) {
                    vkFreeMemory(device, vertexBufferMemory, nullptr);
                    vkDestroyBuffer(device, vertexBufferHandle, nullptr);
                    Logger::get().error("Failed to map memory");
                    return false;
                }

                memcpy(data, vertices.data(), (size_t)vertexBufferSize);
                vkUnmapMemory(device, vertexBufferMemory);

                // Store in member variables
                m_vertexBuffer = std::make_unique<VertexBufferSimple>(device, vertexBufferHandle, vertexBufferMemory, vertices.size());
                Logger::get().info("Cube vertex buffer created successfully with {} vertices", vertices.size());

                return true;
            }
            catch (const std::exception& e) {
                Logger::get().error("Exception in createCubeMesh: {}", e.what());
                return false;
            }
        }
        // Add a simple vertex buffer class
        bool createFramebuffers() {
            // Get swapchain image count
            const auto& swapchainImages = vkSwapchain.get()->imageViews();
            const auto& swapchainExtent = vkSwapchain.get()->extent();

            // Resize framebuffer container
            m_framebuffers.resize(swapchainImages.size());

            // Create a framebuffer for each swapchain image view
            for (size_t i = 0; i < swapchainImages.size(); i++) {
                // Each framebuffer needs the color and depth attachments
                std::vector<VkImageView> attachments = {
                    swapchainImages[i],        // Color attachment
                    *m_depthImageView           // Depth attachment
                };

                Framebuffer::CreateInfo framebufferInfo{};
                framebufferInfo.renderPass = *rp;
                framebufferInfo.attachments = attachments;
                framebufferInfo.width = swapchainExtent.width;
                framebufferInfo.height = swapchainExtent.height;
                framebufferInfo.layers = 1;

                try {
                    m_framebuffers[i] = std::make_unique<Framebuffer>(device, framebufferInfo);
                }
                catch (const std::exception& e) {
                    Logger::get().error("Failed to create framebuffer {}: {}", i, e.what());
                    return false;
                }
            }

            Logger::get().info("Created {} framebuffers", m_framebuffers.size());
            return true;
        }

        // Add member variable to hold framebuffers
    private:
        std::vector<std::unique_ptr<Framebuffer>> m_framebuffers;

        // Add these member variables to your VulkanBackend class
        std::unique_ptr<CommandPoolResource> m_commandPool;
        std::vector<VkCommandBuffer> m_commandBuffers;
        std::vector<SemaphoreResource> m_imageAvailableSemaphores;
        std::vector<SemaphoreResource> m_renderFinishedSemaphores;
        std::vector<FenceResource> m_inFlightFences;

        std::unique_ptr<VertexBufferSimple> m_vertexBuffer;
        std::unique_ptr<IndexBuffer> m_indexBuffer;

        // Add a command pool for transfer operations
        CommandPoolResource m_transferCommandPool;


        void createRenderPass() {
            // Create render pass configuration
            RenderPass::CreateInfo renderPassInfo{};

            // Color attachment (will be the swapchain image)
            RenderPass::Attachment colorAttachment{};
            colorAttachment.format = vkSwapchain.get()->imageFormat();  // Use swapchain format
            colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
            colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;  // Clear on load
            colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;  // Store after use
            colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;  // No stencil for color
            colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;  // No stencil for color
            colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;  // Don't care about initial layout
            colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;  // Ready for presentation
            renderPassInfo.attachments.push_back(colorAttachment);

            // Depth attachment
            RenderPass::Attachment depthAttachment{};
            depthAttachment.format = m_depthFormat;  // Use the depth format you selected
            depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
            depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;  // Clear depth on load
            depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;  // No need to store depth
            depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;  // Depends on if stencil used
            depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;  // Depends on if stencil used
            depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;  // Don't care about initial layout
            depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;  // Optimal for depth
            renderPassInfo.attachments.push_back(depthAttachment);

            // Add dependencies for proper synchronization

            // Dependency 1: Wait for color attachment output before writing to it
            RenderPass::SubpassDependency colorDependency{};
            colorDependency.srcSubpass = VK_SUBPASS_EXTERNAL;  // External means before/after the render pass
            colorDependency.dstSubpass = 0;  // Our only subpass
            colorDependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;  // Wait on this stage
            colorDependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;  // Before this stage
            colorDependency.srcAccessMask = 0;  // No access needed before
            colorDependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;  // For this access
            colorDependency.dependencyFlags = 0;  // No special flags
            renderPassInfo.dependencies.push_back(colorDependency);

            // Dependency 2: Wait for early fragment tests before writing depth
            RenderPass::SubpassDependency depthDependency{};
            depthDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
            depthDependency.dstSubpass = 0;
            depthDependency.srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            depthDependency.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            depthDependency.srcAccessMask = 0;
            depthDependency.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            depthDependency.dependencyFlags = 0;
            renderPassInfo.dependencies.push_back(depthDependency);

            // Create the render pass
            try {
                rp = std::make_unique<RenderPass>(device, renderPassInfo);
                Logger::get().info("Render pass created successfully");
            }
            catch (const std::exception& e) {
                Logger::get().error("Failed to create render pass: {}", e.what());
                throw; // Rethrow to be caught by initialize
            }
        }

        // Make sure you have a method to create depth resources
        bool createDepthResources() {
            // Find suitable depth format
            m_depthFormat = findDepthFormat();

            // Create depth image and view
            VkExtent2D extent = vkSwapchain.get()->extent();

            // Create depth image
            VkImageCreateInfo imageInfo{};
            imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.extent.width = extent.width;
            imageInfo.extent.height = extent.height;
            imageInfo.extent.depth = 1;
            imageInfo.mipLevels = 1;
            imageInfo.arrayLayers = 1;
            imageInfo.format = m_depthFormat;
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

            // Create the image with RAII wrapper
            m_depthImage = std::make_unique<ImageResource>(device);
            if (vkCreateImage(device, &imageInfo, nullptr, &m_depthImage->handle()) != VK_SUCCESS) {
                Logger::get().error("Failed to create depth image");
                return false;
            }

            // Allocate memory
            VkMemoryRequirements memRequirements;
            vkGetImageMemoryRequirements(device, *m_depthImage, &memRequirements);

            VkMemoryAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize = memRequirements.size;
            allocInfo.memoryTypeIndex = res.get()->findMemoryType(memRequirements.memoryTypeBits,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

            m_depthImageMemory = std::make_unique<DeviceMemoryResource>(device);
            if (vkAllocateMemory(device, &allocInfo, nullptr, &m_depthImageMemory->handle()) != VK_SUCCESS) {
                Logger::get().error("Failed to allocate depth image memory");
                return false;
            }

            vkBindImageMemory(device, *m_depthImage, *m_depthImageMemory, 0);

            // Create image view
            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = *m_depthImage;
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = m_depthFormat;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 1;

            m_depthImageView = std::make_unique<ImageViewResource>(device);
            if (vkCreateImageView(device, &viewInfo, nullptr, &m_depthImageView->handle()) != VK_SUCCESS) {
                Logger::get().error("Failed to create depth image view");
                return false;
            }

            Logger::get().info("Depth resources created successfully");
            return true;
        }

        // Helper function to find a suitable depth format
        VkFormat findDepthFormat() {
            // Try to find a supported depth format in order of preference
            std::vector<VkFormat> candidates = {
                VK_FORMAT_D32_SFLOAT_S8_UINT,
                VK_FORMAT_D32_SFLOAT,
                VK_FORMAT_D24_UNORM_S8_UINT,
                VK_FORMAT_D16_UNORM_S8_UINT,
                VK_FORMAT_D16_UNORM
            };

            for (VkFormat format : candidates) {
                VkFormatProperties props;
                vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);

                // Check if optimal tiling supports depth attachment
                if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
                    return format;
                }
            }

            throw std::runtime_error("Failed to find supported depth format");
        }


        void beginFrame() override {



            cam.extent = vkSwapchain.get()->extent();

            updateUniformBuffer();
            updateLight();

            // Add camera debug info
            glm::vec3 camPos = cam.getLocalPosition();
            glm::vec3 camForward = cam.getForward();
            Logger::get().info("Camera pos: ({:.2f}, {:.2f}, {:.2f}), forward: ({:.2f}, {:.2f}, {:.2f})",
                camPos.x, camPos.y, camPos.z, camForward.x, camForward.y, camForward.z);

            m_clusteredRenderer->setCamera(&cam);
            m_clusteredRenderer->buildClusters(&cam, (m_sceneOctree));

            // Wait for previous frame to finish
            if (m_inFlightFences.size() > 0) {
                vkWaitForFences(device, 1, &m_inFlightFences[currentFrame].handle(), VK_TRUE, UINT64_MAX);
            }

            if (!vkSwapchain || !vkSwapchain.get()) {
                Logger::get().error("Swapchain is null in beginFrame()");
                return;
            }

            // Acquire next image
            uint32_t imageIndex;
            VkResult result = vkSwapchain.get()->acquireNextImage(UINT64_MAX,
                m_imageAvailableSemaphores[currentFrame],
                VK_NULL_HANDLE,
                imageIndex);

            if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
                // Recreate swapchain and return
                int width, height;
                SDL_GetWindowSize(w, &width, &height);
                vkSwapchain.get()->recreate(width,height);
                return;
            }
            else if (result != VK_SUCCESS) {
                throw std::runtime_error("Failed to acquire swap chain image");
            }

            // Reset fence for this frame
            vkResetFences(device, 1, &m_inFlightFences[currentFrame].handle());

            // Reset the command buffer for this frame
            vkResetCommandBuffer(m_commandBuffers[currentFrame], 0);

            // Begin recording commands
            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = 0;
            beginInfo.pInheritanceInfo = nullptr;

            if (vkBeginCommandBuffer(m_commandBuffers[currentFrame], &beginInfo) != VK_SUCCESS) {
                throw std::runtime_error("Failed to begin recording command buffer");
            }

            if (vkDevice.get()->capabilities().dynamicRendering) {
                // Setup rendering info for our dynamic renderer
                DynamicRenderer::RenderingInfo renderingInfo{};

                // Set render area
                renderingInfo.renderArea.offset = { 0, 0 };
                renderingInfo.renderArea.extent = vkSwapchain.get()->extent();
                renderingInfo.layerCount = 1;
                renderingInfo.viewMask = 0;  // No multiview (VR) for now

                // Configure color attachment
                DynamicRenderer::ColorAttachment colorAttachment{};
                colorAttachment.imageView = vkSwapchain.get()->imageViews()[imageIndex];
                colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                colorAttachment.clearValue.color = { {0.0f, 0.0f, 0.2f, 1.0f} };  // Dark blue

                // Add to the renderingInfo
                renderingInfo.colorAttachments.push_back(colorAttachment);

                // Configure depth-stencil attachment
                DynamicRenderer::DepthStencilAttachment depthStencilAttachment{};
                depthStencilAttachment.imageView = *m_depthImageView;
                depthStencilAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                depthStencilAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                depthStencilAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                depthStencilAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                depthStencilAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                depthStencilAttachment.clearValue.depthStencil = { 0.0f, 0 };  

                // Add to the renderingInfo
                renderingInfo.depthStencilAttachment = depthStencilAttachment;

                // Begin dynamic rendering
                dr.get()->begin(m_commandBuffers[currentFrame], renderingInfo);

                // Store current image index for endFrame
                m_currentImageIndex = imageIndex;
            }
            else {

                // Begin render pass
                VkRenderPassBeginInfo renderPassInfo{};
                renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                renderPassInfo.renderPass = *rp;
                renderPassInfo.framebuffer = *m_framebuffers[imageIndex];
                renderPassInfo.renderArea.offset = { 0, 0 };
                renderPassInfo.renderArea.extent = vkSwapchain.get()->extent();

                // Clear values for each attachment
                std::array<VkClearValue, 2> clearValues{};
                clearValues[0].color = { {1.0f, 0.0f, 0.3f, 1.0f} };  // Dark blue
                clearValues[1].depthStencil = { 1.0f, 0 };

                renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
                renderPassInfo.pClearValues = clearValues.data();

                // Begin the render pass using the command buffer
                vkCmdBeginRenderPass(m_commandBuffers[currentFrame], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

                // Store the current image index for use in endFrame
                m_currentImageIndex = imageIndex;
            }


            // Render using clustered renderer
            m_clusteredRenderer->render(m_commandBuffers[currentFrame], &cam);

        }

        void endFrame() override {
            if (vkDevice->capabilities().dynamicRendering) {
                dr.get()->end(m_commandBuffers[currentFrame]);
            }
            else {
                vkCmdEndRenderPass(m_commandBuffers[currentFrame]);
            }

            // End command buffer recording
            VkResult endResult = vkEndCommandBuffer(m_commandBuffers[currentFrame]);
            if (endResult != VK_SUCCESS) {
                Logger::get().error("Failed to end command buffer: {}", static_cast<int>(endResult));
                return;
            }
            Logger::get().info("Command buffer ended successfully");

            // Submit command buffer
            VkSubmitInfo submitInfo{};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

            VkSemaphore waitSemaphores[] = { m_imageAvailableSemaphores[currentFrame] };
            VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
            submitInfo.waitSemaphoreCount = 1;
            submitInfo.pWaitSemaphores = waitSemaphores;
            submitInfo.pWaitDstStageMask = waitStages;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &m_commandBuffers[currentFrame];

            VkSemaphore signalSemaphores[] = { m_renderFinishedSemaphores[currentFrame] };
            submitInfo.signalSemaphoreCount = 1;
            submitInfo.pSignalSemaphores = signalSemaphores;

            VkResult submitResult = vkQueueSubmit(graphicsQueue, 1, &submitInfo, m_inFlightFences[currentFrame].handle());
            if (submitResult != VK_SUCCESS) {
                Logger::get().error("Failed to submit command buffer: {}", static_cast<int>(submitResult));
                return;
            }
            Logger::get().info("Command buffer submitted successfully");

            // Present the image
            VkResult presentResult = vkSwapchain.get()->present(m_currentImageIndex, m_renderFinishedSemaphores[currentFrame]);

            Logger::get().info("Present result: {}", static_cast<int>(presentResult));

            if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
                Logger::get().info("Recreating swapchain");
                int width, height;
                SDL_GetWindowSize(w, &width, &height);
                vkSwapchain.get()->recreate(width, height);
            }
            else if (presentResult != VK_SUCCESS) {
                Logger::get().error("Failed to present: {}", static_cast<int>(presentResult));
                return;
            }

            // Move to next frame
            currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
        }

        // Add member variable to store current image index
private:
    uint32_t m_currentImageIndex = 0;
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;  // Double buffering

        // Also add member variables to hold depth resources
    private:
        std::unique_ptr<RenderPass> rp;

        // Depth resources
        std::unique_ptr<ImageResource> m_depthImage;
        std::unique_ptr<DeviceMemoryResource> m_depthImageMemory;
        std::unique_ptr<ImageViewResource> m_depthImageView;
        VkFormat m_depthFormat = VK_FORMAT_UNDEFINED;

        void createCubeRenderableObject() {
            // Register the cube mesh
            uint32_t cubeID = m_meshRegistry.registerMesh(m_vertexBuffer.get(), "cube");

            // Use material 0 for now
            uint32_t materialID = 0;

            // Create transform matrix
            glm::mat4 transform = glm::mat4(1.0f); // Identity matrix

            // Calculate AABBQ bounds
            AABBF localBounds = {
                glm::vec3(-0.5f, -0.5f, -0.5f),
                glm::vec3(0.5f, 0.5f, 0.5f)
            };

            // Transform bounds to world space
            AABBF worldBounds = transformAABB(transform, localBounds);

            // Convert to quantized space
            AABBQ quantizedBounds = AABBQ::fromFloat(worldBounds);

            // Create the renderable object
            tremor::gfx::RenderableObject cubeObject;
            cubeObject.meshID = cubeID;
            cubeObject.materialID = materialID;
            cubeObject.transform = transform;
            cubeObject.prevTransform = transform;
            cubeObject.bounds = quantizedBounds;

            // Add to octree
            //m_sceneOctree.insert(cubeObject, quantizedBounds);

            Logger::get().info("Cube added to octree as renderable object");
        }


        bool initialize(SDL_Window* window) override {
        
            ShaderReflection combinedReflection;
            m_combinedReflection = combinedReflection;

            w = window;

            createInstance();
            createDeviceAndSwapChain();

            createCommandPool();
            createCommandBuffers();


            createDepthResources();
            createUniformBuffer();
            createLightBuffer();
            createMaterialBuffer();

			cam = tremor::gfx::Camera(60, 16.0f / 9.0f, 0.1f, 100.0f);

            cam.setPosition(glm::vec3(0.0f, 0.0f, 5.0f));
			cam.lookAt(glm::vec3(0.0f, 0.0f, 0.0f));

            if (vkDevice.get()->capabilities().dynamicRendering) {
                dr = std::make_unique<DynamicRenderer>();
                Logger::get().info("Dynamic renderer created.");
            } else {
                createRenderPass();
                createFramebuffers();
            }

            sm = std::make_unique<ShaderManager>(vkDevice.get()->device());
            
            createCubeMesh();

			createTestTexture();            

			createDescriptorSetLayouts();

            createMinimalMeshShaderPipeline();
            createGraphicsPipeline();
            createSyncObjects();

            // Create world octree with 64-bit bounds
            AABBQ worldBounds{
                Vec3Q::fromFloat(glm::vec3(-20.0f, -20.0f, -20.0f)),
                Vec3Q::fromFloat(glm::vec3(20.0f,  20.0f,  20.0f))
            };
            m_sceneOctree = tremor::gfx::Octree<tremor::gfx::RenderableObject>(worldBounds);

            createCubeRenderableObject();

            // Replace clustered renderer initialization:
            tremor::gfx::ClusterConfig clusterConfig{};
            clusterConfig.xSlices = 16;
            clusterConfig.ySlices = 9;
            clusterConfig.zSlices = 24;
            clusterConfig.nearPlane = 0.1f;
            clusterConfig.farPlane = 1000.0f;
            clusterConfig.logarithmicZ = true;

            m_clusteredRenderer = std::make_unique<tremor::gfx::VulkanClusteredRenderer>(
                device,
                physicalDevice,
                graphicsQueue,        // Add this
                vkDevice->graphicsQueueFamily(),  // Add this
                *m_commandPool,       // Add this - pass the command pool
                clusterConfig
            );

            if (!m_clusteredRenderer->initialize((Format)vkDevice.get()->colorFormat(), (Format)vkDevice.get()->depthFormat())) {
                Logger::get().error("Failed to initialize enhanced clustered renderer");
                return false;
            }


            // Create enhanced content
            createSampleMeshes();
            createEnhancedScene();
            

            return true;
        };
        void shutdown() override {};

        void renderWithMeshShader(VkCommandBuffer cmdBuffer) {
            if (m_meshShaderPipeline && m_meshShaderPipelineLayout) {
                // Bind the pipeline
                vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, *m_meshShaderPipeline);

                // Set viewport and scissor
                VkViewport viewport{};
                viewport.width = static_cast<float>(vkSwapchain->extent().width);
                viewport.height = static_cast<float>(vkSwapchain->extent().height);
                viewport.minDepth = 0.0f;
                viewport.maxDepth = 1.0f;
                vkCmdSetViewport(cmdBuffer, 0, 1, &viewport);

                VkRect2D scissor{};
                scissor.extent = vkSwapchain->extent();
                vkCmdSetScissor(cmdBuffer, 0, 1, &scissor);

                // Draw a mesh - no need for vertex/index buffers!
                vkCmdDrawMeshTasksEXT(cmdBuffer, 1, 1, 1);
            }
        }


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

        // Synchronization
        size_t currentFrame = 0;

        bool get_surface_capabilities_2 = false;
        bool vulkan_1_4_available = false;

        bool debug_utils = false;
        bool memory_report = false;

        bool enableValidation = false;

        uint32_t m_gfxQueueFamilyIndex = 0;

        // Format information
        VkFormat m_colorFormat = VK_FORMAT_UNDEFINED;


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
            prefs.requireMeshShaders = true;  // Set based on your requirements
            prefs.requireRayQuery = true;     // Set based on your requirements
            prefs.requireSparseBinding = true; // Set based on your requirements


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

            res = std::make_unique<VulkanResourceManager>(device, physicalDevice);

            return true;
        }

        std::unique_ptr<DynamicRenderer> dr;

        std::unique_ptr<ShaderManager> sm;

        bool createTestTexture() {
            try {
                // Create a simple 2x2 checkerboard texture
                const uint32_t size = 256;
                std::vector<uint8_t> pixels(size * size * 4);

                // Fill with a checkerboard pattern
                for (uint32_t y = 0; y < size; y++) {
                    for (uint32_t x = 0; x < size; x++) {
                        uint8_t color = 255; //((x / 32 + y / 32) % 2) ? 255 : 0;
                        pixels[(y * size + x) * 4 + 0] = color;     // R
                        pixels[(y * size + x) * 4 + 1] = color;     // G
                        pixels[(y * size + x) * 4 + 2] = color;     // B
                        pixels[(y * size + x) * 4 + 3] = 255;       // A
                    }
                }

                // Create texture image
                VkDeviceSize imageSize = size * size * 4;

                // Create staging buffer
                VkBuffer stagingBuffer;
                VkDeviceMemory stagingBufferMemory;

                // Create buffer
                VkBufferCreateInfo bufferInfo{};
                bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
                bufferInfo.size = imageSize;
                bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
                bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

                if (vkCreateBuffer(device, &bufferInfo, nullptr, &stagingBuffer) != VK_SUCCESS) {
                    Logger::get().error("Failed to create staging buffer for texture");
                    return false;
                }

                // Get memory requirements
                VkMemoryRequirements memRequirements;
                vkGetBufferMemoryRequirements(device, stagingBuffer, &memRequirements);

                // Allocate memory
                VkMemoryAllocateInfo allocInfo{};
                allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
                allocInfo.allocationSize = memRequirements.size;
                allocInfo.memoryTypeIndex = res->findMemoryType(
                    memRequirements.memoryTypeBits,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                );

                if (vkAllocateMemory(device, &allocInfo, nullptr, &stagingBufferMemory) != VK_SUCCESS) {
                    vkDestroyBuffer(device, stagingBuffer, nullptr);
                    Logger::get().error("Failed to allocate staging buffer memory");
                    return false;
                }

                // Bind memory
                vkBindBufferMemory(device, stagingBuffer, stagingBufferMemory, 0);

                // Copy data to staging buffer
                void* data;
                vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &data);
                memcpy(data, pixels.data(), imageSize);
                vkUnmapMemory(device, stagingBufferMemory);

                // Create the texture image
                VkImageCreateInfo imageInfo{};
                imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
                imageInfo.imageType = VK_IMAGE_TYPE_2D;
                imageInfo.extent.width = size;
                imageInfo.extent.height = size;
                imageInfo.extent.depth = 1;
                imageInfo.mipLevels = 1;
                imageInfo.arrayLayers = 1;
                imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
                imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
                imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
                imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
                imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

                // Create image
                m_textureImage = std::make_unique<ImageResource>(device);
                if (vkCreateImage(device, &imageInfo, nullptr, &m_textureImage->handle()) != VK_SUCCESS) {
                    Logger::get().error("Failed to create texture image");
                    return false;
                }

                // Allocate memory for the image
                vkGetImageMemoryRequirements(device, *m_textureImage, &memRequirements);

                allocInfo.allocationSize = memRequirements.size;
                allocInfo.memoryTypeIndex = res->findMemoryType(
                    memRequirements.memoryTypeBits,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
                );

                m_textureImageMemory = std::make_unique<DeviceMemoryResource>(device);
                if (vkAllocateMemory(device, &allocInfo, nullptr, &m_textureImageMemory->handle()) != VK_SUCCESS) {
                    Logger::get().error("Failed to allocate texture image memory");
                    return false;
                }

                // Bind memory to image
                vkBindImageMemory(device, *m_textureImage, *m_textureImageMemory, 0);

                // Transition image layout and copy data from staging buffer
                VkCommandBuffer commandBuffer = beginSingleTimeCommands();

                // Transition to transfer destination layout
                VkImageMemoryBarrier barrier{};
                barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.image = *m_textureImage;
                barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                barrier.subresourceRange.baseMipLevel = 0;
                barrier.subresourceRange.levelCount = 1;
                barrier.subresourceRange.baseArrayLayer = 0;
                barrier.subresourceRange.layerCount = 1;
                barrier.srcAccessMask = 0;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

                vkCmdPipelineBarrier(
                    commandBuffer,
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                    0,
                    0, nullptr,
                    0, nullptr,
                    1, &barrier
                );

                // Copy data from buffer to image
                VkBufferImageCopy region{};
                region.bufferOffset = 0;
                region.bufferRowLength = 0;
                region.bufferImageHeight = 0;
                region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                region.imageSubresource.mipLevel = 0;
                region.imageSubresource.baseArrayLayer = 0;
                region.imageSubresource.layerCount = 1;
                region.imageOffset = { 0, 0, 0 };
                region.imageExtent = { size, size, 1 };

                vkCmdCopyBufferToImage(
                    commandBuffer,
                    stagingBuffer,
                    *m_textureImage,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    1,
                    &region
                );

                // Transition to shader read layout
                barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

                vkCmdPipelineBarrier(
                    commandBuffer,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    0,
                    0, nullptr,
                    0, nullptr,
                    1, &barrier
                );

                endSingleTimeCommands(commandBuffer);

                // Create image view
                VkImageViewCreateInfo viewInfo{};
                viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                viewInfo.image = *m_textureImage;
                viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
                viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
                viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                viewInfo.subresourceRange.baseMipLevel = 0;
                viewInfo.subresourceRange.levelCount = 1;
                viewInfo.subresourceRange.baseArrayLayer = 0;
                viewInfo.subresourceRange.layerCount = 1;

                m_missingTextureImageView = std::make_unique<ImageViewResource>(device);
                if (vkCreateImageView(device, &viewInfo, nullptr, &m_missingTextureImageView->handle()) != VK_SUCCESS) {
                    Logger::get().error("Failed to create texture image view");
                    return false;
                }

                // Create sampler
                VkSamplerCreateInfo samplerInfo{};
                samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
                samplerInfo.magFilter = VK_FILTER_LINEAR;
                samplerInfo.minFilter = VK_FILTER_LINEAR;
                samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
                samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
                samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
                samplerInfo.anisotropyEnable = VK_TRUE;
                samplerInfo.maxAnisotropy = 16.0f;
                samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
                samplerInfo.unnormalizedCoordinates = VK_FALSE;
                samplerInfo.compareEnable = VK_FALSE;
                samplerInfo.compareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
                samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
                samplerInfo.mipLodBias = 0.0f;
                samplerInfo.minLod = 0.0f;
                samplerInfo.maxLod = 0.0f;

                m_textureSampler = std::make_unique<SamplerResource>(device);
                if (vkCreateSampler(device, &samplerInfo, nullptr, &m_textureSampler->handle()) != VK_SUCCESS) {
                    Logger::get().error("Failed to create texture sampler");
                    return false;
                }

                // Clean up staging buffer
                vkDestroyBuffer(device, stagingBuffer, nullptr);
                vkFreeMemory(device, stagingBufferMemory, nullptr);

                Logger::get().info("Texture created successfully");
                return true;
            }
            catch (const std::exception& e) {
                Logger::get().error("Exception in createTestTexture: {}", e.what());
                return false;
            }
        }

        // Helper command for single-time command submission
        VkCommandBuffer beginSingleTimeCommands() {
            VkCommandBufferAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocInfo.commandPool = *m_commandPool;
            allocInfo.commandBufferCount = 1;

            VkCommandBuffer commandBuffer;
            vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

            vkBeginCommandBuffer(commandBuffer, &beginInfo);

            return commandBuffer;
        }

        void endSingleTimeCommands(VkCommandBuffer commandBuffer) {
            vkEndCommandBuffer(commandBuffer);

			Logger::get().info("Ending command buffer...");

            VkSubmitInfo submitInfo{};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &commandBuffer;

            VkResult submitResult = vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
            Logger::get().info("Queue submit result: {}", static_cast<int>(submitResult));

            VkResult waitResult = vkQueueWaitIdle(graphicsQueue);
            Logger::get().info("Queue wait result: {}", static_cast<int>(waitResult));

            vkFreeCommandBuffers(device, *m_commandPool, 1, &commandBuffer);
        }
        bool createDescriptorSetLayouts() {
            // Get the combined reflection data
            ShaderReflection combinedReflection;

            // Add all your shaders
            for (const auto& shader : m_pipelineShaders) {
                const ShaderReflection* reflection = shader->getReflection();
                if (reflection) {
                    combinedReflection.merge(*reflection);
                }
            }

            // Find highest set number to determine how many layouts to create
            uint32_t maxSetNumber = 0;
            for (const auto& binding : combinedReflection.getResourceBindings()) {
                maxSetNumber = std::max(maxSetNumber, binding.set);
            }

            // Create descriptor set layouts for each set
            m_descriptorSetLayouts.resize(maxSetNumber + 1);
            for (uint32_t i = 0; i <= maxSetNumber; i++) {
                m_descriptorSetLayouts[i] = combinedReflection.createDescriptorSetLayout(device, i);
                if (!m_descriptorSetLayouts[i]) {
                    Logger::get().error("Failed to create descriptor set layout for set {}", i);
                    return false;
                }
            }

            // Create pipeline layout using all descriptor set layouts
            m_pipelineLayout = combinedReflection.createPipelineLayout(device);
            if (!m_pipelineLayout) {
                Logger::get().error("Failed to create pipeline layout");
                return false;
            }

            // Create descriptor pool sized appropriately based on reflection
            m_descriptorPool = combinedReflection.createDescriptorPool(device);
            if (!m_descriptorPool) {
                Logger::get().error("Failed to create descriptor pool");
                return false;
            }

            return true;
        }

        bool createAndUpdateDescriptorSets() {
            return true;
        }


        // STEP 5: Check command buffer submission
        // Add this validation to your endFrame:
        bool createGraphicsPipeline() {
            try {
                Logger::get().info("=== CREATING BASIC GRAPHICS PIPELINE ===");

                // VERIFY: Are you using the right shaders?
                Logger::get().info("Loading basic vertex/fragment shaders...");

                auto vertShader = sm->loadShader("shaders/pbr.vert");
                auto fragShader = sm->loadShader("shaders/pbr.frag");

                if (!vertShader || !fragShader) {
                    Logger::get().error("Failed to load basic shaders!");

                    // FALLBACK: Try to create simple embedded shaders
                    Logger::get().info("Trying fallback shaders...");

                    std::string simpleVertSource = R"(
        #version 450

        layout(location = 0) in vec3 inPosition;
        layout(location = 1) in vec3 inNormal;
        layout(location = 2) in vec2 inTexCoord;

        layout(binding = 0) uniform UniformBufferObject {
            mat4 model;
            mat4 view;
            mat4 proj;
            vec3 cameraPos;
        } ubo;

        void main() {
            gl_Position = ubo.proj * ubo.view * ubo.model * vec4(inPosition, 1.0);
        }
        )";

                    std::string simpleFragSource = R"(
        #version 450

        layout(location = 0) out vec4 outColor;

        void main() {
            outColor = vec4(1.0, 0.5, 0.0, 1.0); // Orange color
        }
        )";

                    vertShader = ShaderModule::compileFromSource(device, simpleVertSource, ShaderType::Vertex, "fallback_vert");
                    fragShader = ShaderModule::compileFromSource(device, simpleFragSource, ShaderType::Fragment, "fallback_frag");

                    if (!vertShader || !fragShader) {
                        Logger::get().error("Even fallback shaders failed!");
                        return false;
                    }

                    Logger::get().info("Using fallback shaders");
                }

                m_pipelineShaders.clear();
                m_pipelineShaders.push_back(vertShader);
                m_pipelineShaders.push_back(fragShader);

                // Create shader stages
                std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
                for (const auto& shader : m_pipelineShaders) {
                    shaderStages.push_back(shader->createShaderStageInfo());
                }

                Logger::get().info("Created {} shader stages", shaderStages.size());

                // CRITICAL: Check if your vertex input state is correct
                auto bindingDescription = BlinnPhongVertex::getBindingDescription();
                auto attributeDescriptions = BlinnPhongVertex::getAttributeDescriptions();

                Logger::get().info("Vertex binding stride: {}", bindingDescription.stride);
                Logger::get().info("Vertex attributes: {}", attributeDescriptions.size());

                VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
                vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
                vertexInputInfo.vertexBindingDescriptionCount = 1;
                vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
                vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
                vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

                // SIMPLIFIED pipeline layout - just UBO for now
                VkDescriptorSetLayoutBinding uboLayoutBinding{};
                uboLayoutBinding.binding = 0;
                uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                uboLayoutBinding.descriptorCount = 1;
                uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

                VkDescriptorSetLayoutCreateInfo layoutInfo{};
                layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
                layoutInfo.bindingCount = 1;
                layoutInfo.pBindings = &uboLayoutBinding;

                // Create simple descriptor set layout
                VkDescriptorSetLayout descriptorSetLayout;
                if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
                    Logger::get().error("Failed to create simple descriptor set layout");
                    return false;
                }

                // Create pipeline layout
                VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
                pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
                pipelineLayoutInfo.setLayoutCount = 1;
                pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;

                VkPipelineLayout pipelineLayout;
                if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
                    Logger::get().error("Failed to create pipeline layout");
                    return false;
                }

                // Store the layout
                m_pipelineLayout = std::make_unique<PipelineLayoutResource>(device, pipelineLayout);

                // Simple input assembly
                VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
                inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
                inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
                inputAssembly.primitiveRestartEnable = VK_FALSE;

                // Simple viewport state
                VkPipelineViewportStateCreateInfo viewportState{};
                viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
                viewportState.viewportCount = 1;
                viewportState.scissorCount = 1;

                // Simple rasterizer
                VkPipelineRasterizationStateCreateInfo rasterizer{};
                rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
                rasterizer.depthClampEnable = VK_FALSE;
                rasterizer.rasterizerDiscardEnable = VK_FALSE;
                rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
                rasterizer.lineWidth = 1.0f;
                rasterizer.cullMode = VK_CULL_MODE_NONE; // No culling for debugging
                rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
                rasterizer.depthBiasEnable = VK_FALSE;

                // No multisampling
                VkPipelineMultisampleStateCreateInfo multisampling{};
                multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
                multisampling.sampleShadingEnable = VK_FALSE;
                multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

                // Simple color blending
                VkPipelineColorBlendAttachmentState colorBlendAttachment{};
                colorBlendAttachment.colorWriteMask =
                    VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                    VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
                colorBlendAttachment.blendEnable = VK_FALSE;

                VkPipelineColorBlendStateCreateInfo colorBlending{};
                colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
                colorBlending.logicOpEnable = VK_FALSE;
                colorBlending.attachmentCount = 1;
                colorBlending.pAttachments = &colorBlendAttachment;

                // DISABLE depth testing for debugging
                VkPipelineDepthStencilStateCreateInfo depthStencil{};
                depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
                depthStencil.depthTestEnable = VK_FALSE;
                depthStencil.depthWriteEnable = VK_FALSE;

                // Dynamic state
                VkDynamicState dynamicStates[] = {
                    VK_DYNAMIC_STATE_VIEWPORT,
                    VK_DYNAMIC_STATE_SCISSOR
                };

                VkPipelineDynamicStateCreateInfo dynamicState{};
                dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
                dynamicState.dynamicStateCount = 2;
                dynamicState.pDynamicStates = dynamicStates;

                // Create the pipeline
                VkGraphicsPipelineCreateInfo pipelineInfo{};
                pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;

                if (vkDevice->capabilities().dynamicRendering) {
                    // Dynamic rendering
                    VkPipelineRenderingCreateInfoKHR renderingInfo{};
                    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
                    renderingInfo.colorAttachmentCount = 1;
                    VkFormat colorFormat = vkSwapchain->imageFormat();
                    renderingInfo.pColorAttachmentFormats = &colorFormat;

                    pipelineInfo.pNext = &renderingInfo;
                    Logger::get().info("Using dynamic rendering");
                }
                else {
                    // Traditional render pass
                    pipelineInfo.renderPass = rp->handle();
                    pipelineInfo.subpass = 0;
                    Logger::get().info("Using traditional render pass");
                }

                pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
                pipelineInfo.pStages = shaderStages.data();
                pipelineInfo.pVertexInputState = &vertexInputInfo;
                pipelineInfo.pInputAssemblyState = &inputAssembly;
                pipelineInfo.pViewportState = &viewportState;
                pipelineInfo.pRasterizationState = &rasterizer;
                pipelineInfo.pMultisampleState = &multisampling;
                pipelineInfo.pDepthStencilState = &depthStencil;
                pipelineInfo.pColorBlendState = &colorBlending;
                pipelineInfo.pDynamicState = &dynamicState;
                pipelineInfo.layout = pipelineLayout;

                VkPipeline graphicsPipeline;
                VkResult result = vkCreateGraphicsPipelines(
                    device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline);

                if (result != VK_SUCCESS) {
                    Logger::get().error("Failed to create graphics pipeline: {}", static_cast<int>(result));
                    return false;
                }

                m_graphicsPipeline = std::make_unique<PipelineResource>(device, graphicsPipeline);

                Logger::get().info("=== BASIC GRAPHICS PIPELINE CREATED SUCCESSFULLY ===");
                return true;

            }
            catch (const std::exception& e) {
                Logger::get().error("Exception in createGraphicsPipeline: {}", e.what());
                return false;
            }
        }

        // Texture resources
        std::unique_ptr<ImageResource> m_textureImage;
        std::unique_ptr<DeviceMemoryResource> m_textureImageMemory;
        std::unique_ptr<ImageViewResource> m_missingTextureImageView;
        std::unique_ptr<SamplerResource> m_textureSampler;

        // Descriptor resources
        std::unique_ptr<DescriptorSetLayoutResource> m_descriptorSetLayout;
        std::unique_ptr<DescriptorPoolResource> m_descriptorPool;
        std::unique_ptr<DescriptorSetResource> m_descriptorSet;

        std::vector<tremor::gfx::ShaderReflection::UniformBuffer> m_uniformBuffers;
        std::vector<tremor::gfx::ShaderReflection::ResourceBinding> m_resourceBindings;

        ShaderReflection m_combinedReflection;


        // Helper method to load shader modules
        VkShaderModule loadShader(const std::string& filename) {
            // Read the file
            std::ifstream file(filename);

            if (!file.is_open()) {
                Logger::get().error("Failed to open shader file: {}", filename);
                return VK_NULL_HANDLE;
            }

            size_t fileSize = static_cast<size_t>(file.tellg());
            std::vector<char> shaderCode(fileSize);

            file.seekg(0);
            file.read(shaderCode.data(), fileSize);
            file.close();

            // Create shader module
            VkShaderModuleCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            createInfo.codeSize = fileSize;
            createInfo.pCode = reinterpret_cast<const uint32_t*>(shaderCode.data());

            VkShaderModule shaderModule;
            if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
                Logger::get().error("Failed to create shader module for: {}", filename);
                return VK_NULL_HANDLE;
            }

            return shaderModule;
        }

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

        // Add these member variables
        std::unique_ptr<PipelineLayoutResource> m_pipelineLayout;
        std::unique_ptr<PipelineResource> m_graphicsPipeline;



    };

    uint32_t VulkanBackend::loadMeshFromFile(const std::string& filename) {
        // This would integrate with your asset loading system
        // For now, just return the cube mesh
        return m_cubeMeshID;
    }

    // Function to create materials from descriptions
    uint32_t VulkanBackend::createMaterialFromDesc(const MaterialDesc& desc) {
        tremor::gfx::PBRMaterial material{};
        material.baseColor = desc.baseColor;
        material.metallic = desc.metallic;
        material.roughness = desc.roughness;
        // ... set other properties

        return m_clusteredRenderer->createMaterial(material);
    }

    // Function to add objects to the scene
    void VulkanBackend::addObjectToScene(uint32_t meshID, uint32_t materialID, const glm::mat4& transform) {
        tremor::gfx::RenderableObject obj;
        obj.meshID = meshID;
        obj.materialID = materialID;
        obj.instanceID = 0; // You could track this
        obj.flags = 1; // Visible
        obj.transform = transform;
        obj.prevTransform = transform;

        // Calculate bounds based on mesh
        AABBF localBounds{ glm::vec3(-0.5f), glm::vec3(0.5f) }; // This should come from mesh data
        AABBF worldBounds = transformAABB(transform, localBounds);
        obj.bounds = AABBQ::fromFloat(worldBounds);

        m_sceneOctree.insert(obj, obj.bounds);
    }
}

// Complete implementations for the missing VulkanClusteredRenderer methods
// Add these to your VulkanClusteredRenderer class implementation

namespace tremor::gfx {

    void VulkanClusteredRenderer::updateMeshBuffers() {
        if (!m_vertexBuffer || !m_meshIndexBuffer || !m_meshInfoBuffer) {
            Logger::get().error("Mesh buffers not initialized");
            return;
        }

        try {
            // Update vertex buffer
            if (!m_allVertices.empty()) {
                VkDeviceSize vertexSize = m_allVertices.size() * sizeof(MeshVertex);
                if (vertexSize <= m_vertexBuffer->getSize()) {
                    m_vertexBuffer->update(m_allVertices.data(), vertexSize);
                }
                else {
                    Logger::get().warning("Vertex buffer too small: need {}, have {}",
                        vertexSize, m_vertexBuffer->getSize());
                }
            }

            // Update mesh index buffer
            if (!m_allIndices.empty()) {
                VkDeviceSize indexSize = m_allIndices.size() * sizeof(uint32_t);
                if (indexSize <= m_meshIndexBuffer->getSize()) {
                    m_meshIndexBuffer->update(m_allIndices.data(), indexSize);
                }
                else {
                    Logger::get().warning("Mesh index buffer too small: need {}, have {}",
                        indexSize, m_meshIndexBuffer->getSize());
                }
            }

            // Update mesh info buffer
            if (!m_meshInfos.empty()) {
                VkDeviceSize meshInfoSize = m_meshInfos.size() * sizeof(MeshInfo);
                if (meshInfoSize <= m_meshInfoBuffer->getSize()) {
                    m_meshInfoBuffer->update(m_meshInfos.data(), meshInfoSize);
                }
                else {
                    Logger::get().warning("Mesh info buffer too small: need {}, have {}",
                        meshInfoSize, m_meshInfoBuffer->getSize());
                }
            }

            Logger::get().info("Updated mesh buffers: {} vertices, {} indices, {} meshes",
                m_allVertices.size(), m_allIndices.size(), m_meshInfos.size());
        }
        catch (const std::exception& e) {
            Logger::get().error("Exception in updateMeshBuffers: {}", e.what());
        }
    }

    void VulkanClusteredRenderer::updateMaterialBuffer() {
        if (!m_materialBuffer) {
            Logger::get().error("Material buffer not initialized");
            return;
        }

        try {
            if (!m_materials.empty()) {
                VkDeviceSize materialSize = m_materials.size() * sizeof(PBRMaterial);
                if (materialSize <= m_materialBuffer->getSize()) {
                    m_materialBuffer->update(m_materials.data(), materialSize);
                    Logger::get().info("Updated material buffer with {} materials", m_materials.size());
                }
                else {
                    Logger::get().warning("Material buffer too small: need {}, have {}",
                        materialSize, m_materialBuffer->getSize());
                }
            }
        }
        catch (const std::exception& e) {
            Logger::get().error("Exception in updateMaterialBuffer: {}", e.what());
        }
    }

    void VulkanClusteredRenderer::updateGPUBuffers() {
        try {
            Logger::get().info("=== UPDATING GPU BUFFERS ===");

            // Update cluster buffer
            if (!m_clusters.empty() && m_clusterBuffer) {
                VkDeviceSize clusterSize = m_clusters.size() * sizeof(Cluster);
                Logger::get().info("Updating cluster buffer: {} clusters, {} bytes",
                    m_clusters.size(), clusterSize);

                if (clusterSize <= m_clusterBuffer->getSize()) {
                    m_clusterBuffer->update(m_clusters.data(), clusterSize);

                    // Debug: Check first few clusters
                    for (size_t i = 0; i < std::min(size_t(5), m_clusters.size()); i++) {
                        const auto& cluster = m_clusters[i];
                        if (cluster.objectCount > 0) {
                            Logger::get().info("  Cluster {}: {} objects at offset {}",
                                i, cluster.objectCount, cluster.objectOffset);
                        }
                    }
                }
                else {
                    Logger::get().error("Cluster buffer too small!");
                }
            }

            // Update object buffer with detailed logging
            if (!m_visibleObjects.empty() && m_objectBuffer) {
                VkDeviceSize objectSize = m_visibleObjects.size() * sizeof(RenderableObject);
                Logger::get().info("Updating object buffer: {} objects, {} bytes",
                    m_visibleObjects.size(), objectSize);

                if (objectSize <= m_objectBuffer->getSize()) {
                    m_objectBuffer->update(m_visibleObjects.data(), objectSize);

                    void* mappedData;
                    vkMapMemory(m_device, m_objectBuffer->getMemory(), 0, sizeof(RenderableObject) * 25, 0, &mappedData);
                    RenderableObject* objects = static_cast<RenderableObject*>(mappedData);

                    for (int i = 0; i < 25; i++) { // Check first 5
                        glm::vec3 pos = objects[i].transform[3];
                        Logger::get().info("GPU Object {}: pos=({:.2f}, {:.2f}, {:.2f})", i, pos.x, pos.y, pos.z);
                    }
                    vkUnmapMemory(m_device, m_objectBuffer->getMemory());

                    // Debug: Check first few objects
                    for (size_t i = 0; i < std::min(size_t(25), m_visibleObjects.size()); i++) {
                        const auto& obj = m_visibleObjects[i];
                        Logger::get().info("  Object {}: meshID={}, materialID={}, instanceID={}",
                            i, obj.meshID, obj.materialID, obj.instanceID);
                    }
                }
                else {
                    Logger::get().error("Object buffer too small!");
                }
            }

            // Update cluster object indices with verification
            if (!m_clusterObjectIndices.empty() && m_indexBuffer) {
                VkDeviceSize indexSize = m_clusterObjectIndices.size() * sizeof(uint32_t);
                Logger::get().info("Updating cluster object indices: {} indices, {} bytes",
                    m_clusterObjectIndices.size(), indexSize);

                if (indexSize <= m_indexBuffer->getSize()) {
                    m_indexBuffer->update(m_clusterObjectIndices.data(), indexSize, 0);

                    // Debug: Check first few indices
                    for (size_t i = 0; i < std::min(size_t(10), m_clusterObjectIndices.size()); i++) {
                        Logger::get().info("  Index {}: object {}", i, m_clusterObjectIndices[i]);
                    }
                }
                else {
                    Logger::get().error("Index buffer too small!");
                }
            }

            // Update mesh data
            if (!m_allVertices.empty() && m_vertexBuffer) {
                VkDeviceSize vertexSize = m_allVertices.size() * sizeof(MeshVertex);
                Logger::get().info("Updating vertex buffer: {} vertices, {} bytes",
                    m_allVertices.size(), vertexSize);
                m_vertexBuffer->update(m_allVertices.data(), vertexSize);
            }

            if (!m_allIndices.empty() && m_meshIndexBuffer) {
                VkDeviceSize indexSize = m_allIndices.size() * sizeof(uint32_t);
                Logger::get().info("Updating mesh index buffer: {} indices, {} bytes",
                    m_allIndices.size(), indexSize);
                m_meshIndexBuffer->update(m_allIndices.data(), indexSize);
            }

            if (!m_meshInfos.empty() && m_meshInfoBuffer) {
                VkDeviceSize meshInfoSize = m_meshInfos.size() * sizeof(MeshInfo);
                Logger::get().info("Updating mesh info buffer: {} meshes, {} bytes",
                    m_meshInfos.size(), meshInfoSize);
                m_meshInfoBuffer->update(m_meshInfos.data(), meshInfoSize);
            }

            if (!m_materials.empty() && m_materialBuffer) {
                VkDeviceSize materialSize = m_materials.size() * sizeof(PBRMaterial);
                Logger::get().info("Updating material buffer: {} materials, {} bytes",
                    m_materials.size(), materialSize);
                m_materialBuffer->update(m_materials.data(), materialSize);
            }

            // Update lights
            if (!m_lights.empty() && m_lightBuffer) {
                VkDeviceSize lightSize = m_lights.size() * sizeof(ClusterLight);
                Logger::get().info("Updating light buffer: {} lights, {} bytes",
                    m_lights.size(), lightSize);
                m_lightBuffer->update(m_lights.data(), lightSize);
            }

            Logger::get().info("GPU buffer updates complete");

        }
        catch (const std::exception& e) {
            Logger::get().error("Exception in updateGPUBuffers: {}", e.what());
        }
    }

    void VulkanClusteredRenderer::updateUniformBuffers(Camera* camera) {
        if (!camera || !m_uniformBuffer) {
            Logger::get().error("Camera or uniform buffer is null");
            return;
        }

        try {
            EnhancedClusterUBO ubo{};
            static auto startTime = std::chrono::high_resolution_clock::now();
            auto currentTime = std::chrono::high_resolution_clock::now();
            auto time = std::chrono::duration<float>(currentTime - startTime).count();

            float radius = 25.0f; // Increased distance
            float height = 2.0f;  // Higher up
            camera->setPosition(glm::vec3(sin(time * 0.2f) * radius, height, cos(time * 0.2f) * radius));
            camera->lookAt(glm::vec3(0.0f, 0.0f, 0.0f)); // Look at center of grid
            camera->update(0.0f);


            ubo.viewMatrix = camera->getViewMatrix();
            ubo.projMatrix = camera->getProjectionMatrix();
            ubo.invViewMatrix = glm::inverse(ubo.viewMatrix);
            ubo.invProjMatrix = glm::inverse(ubo.projMatrix);
            ubo.cameraPos = glm::vec4(camera->getLocalPosition(), 1.0f);
            ubo.clusterDimensions = glm::uvec4(m_config.xSlices, m_config.ySlices, m_config.zSlices, 0);
            ubo.zPlanes = glm::vec4(m_config.nearPlane, m_config.farPlane, static_cast<float>(m_config.zSlices), 0.0f);

            // Screen size
            VkExtent2D extent = camera->extent;
            ubo.screenSize = glm::vec4(
                static_cast<float>(extent.width),
                static_cast<float>(extent.height),
                1.0f / static_cast<float>(extent.width),
                1.0f / static_cast<float>(extent.height)
            );

            ubo.numLights = static_cast<uint32_t>(m_lights.size());
            ubo.numObjects = static_cast<uint32_t>(m_visibleObjects.size());
            ubo.numClusters = m_totalClusters;

            // Frame data
            static uint32_t frameCounter = 0;

            ubo.frameNumber = frameCounter++;
            ubo.time = std::chrono::duration<float>(currentTime - startTime).count();
            ubo.deltaTime = 1.0f / 60.0f; // You should track real delta time
            ubo.flags = 0; // Debug flags, etc.

            m_uniformBuffer->update(&ubo, sizeof(ubo));

            glm::vec3 camPos = camera->getPosition().fractional;
            glm::vec3 camForward = camera->getForward();
            glm::mat4 view = camera->getViewMatrix();
            glm::mat4 proj = camera->getProjectionMatrix();

            Logger::get().info("Camera Debug:");
            Logger::get().info("  Position: ({:.2f}, {:.2f}, {:.2f})", camPos.x, camPos.y, camPos.z);
            Logger::get().info("  Forward: ({:.2f}, {:.2f}, {:.2f})", camForward.x, camForward.y, camForward.z);
            Logger::get().info("  View[3]: ({:.2f}, {:.2f}, {:.2f})", view[3][0], view[3][1], view[3][2]);

            // Test a specific object position
            glm::vec3 objPos(0.0f, 0.0f, 0.0f); // Center object
            glm::vec4 screenPos = proj * view * glm::vec4(objPos, 1.0f);
            Logger::get().info("  Object at origin projects to: ({:.2f}, {:.2f}, {:.2f}, {:.2f})",
                screenPos.x, screenPos.y, screenPos.z, screenPos.w);

            size_t requiredSize = sizeof(RenderableObject) * ubo.numObjects;
            size_t actualSize = m_objectBuffer->getSize();
            Logger::get().info("Required: {} bytes, Actual: {} bytes", requiredSize, actualSize);
        }
        catch (const std::exception& e) {
            Logger::get().error("Exception in updateUniformBuffers: {}", e.what());
        }
    }

    // In ClusteredRenderer::createShaderResources(), restore the full pipeline creation:

    bool VulkanClusteredRenderer::createShaderResources() {
        //return createDebugPipeline();
        
        try {
            // Load shaders with CORRECTED UBO layout
            m_taskShader = ShaderModule::compileFromFile(m_device, "shaders/cluster.task");
            m_meshShader = ShaderModule::compileFromFile(m_device, "shaders/cluster.mesh");
            m_fragmentShader = ShaderModule::compileFromFile(m_device, "shaders/cluster.frag");

            if (!m_taskShader || !m_meshShader || !m_fragmentShader) {
                Logger::get().error("Failed to compile enhanced cluster shaders");
                return false;
            }

            Logger::get().info("All cluster shaders compiled successfully");

            // Extract and combine reflection data
            ShaderReflection combinedReflection;
            combinedReflection.merge(*m_taskShader->getReflection());
            combinedReflection.merge(*m_meshShader->getReflection());
            combinedReflection.merge(*m_fragmentShader->getReflection());


            // Create descriptor set layout
            m_descriptorSetLayout = combinedReflection.createDescriptorSetLayout(m_device, 0);
            if (!m_descriptorSetLayout) {
                Logger::get().error("Failed to create descriptor set layout");
                return false;
            }

            // Create pipeline layout
            //m_pipelineLayout = combinedReflection.createPipelineLayout(m_device);
            //if (!m_pipelineLayout) {
            //    Logger::get().error("Failed to create pipeline layout");
            //    return false;
            //} //FIXME: REIMPLEMENT

            std::vector<VkDescriptorSetLayout> layouts = { m_descriptorSetLayout->handle() };

            VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
            pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            pipelineLayoutInfo.setLayoutCount = 1;
            pipelineLayoutInfo.pSetLayouts = layouts.data();

            VkPipelineLayout pipelineLayout;
            if (vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
                return false;
            }

            m_pipelineLayout = std::make_unique<PipelineLayoutResource>(m_device, pipelineLayout);
			if (!m_pipelineLayout) {
				Logger::get().error("Failed to create pipeline layout");
				return false;
			}

            // Create descriptor pool and allocate sets
            m_descriptorPool = combinedReflection.createDescriptorPool(m_device);
            if (!m_descriptorPool) {
                Logger::get().error("Failed to create descriptor pool");
                return false;
            }

            // Allocate descriptor set
            VkDescriptorSetAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocInfo.descriptorPool = m_descriptorPool->handle();
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &m_descriptorSetLayout->handle();

            VkDescriptorSet descriptorSet;
            if (vkAllocateDescriptorSets(m_device, &allocInfo, &descriptorSet) != VK_SUCCESS) {
                Logger::get().error("Failed to allocate descriptor set");
                return false;
            }

            m_descriptorSet = std::make_unique<DescriptorSetResource>(m_device, descriptorSet);

            // Update descriptor set with buffer bindings
            updateDescriptorSet();

            

            // Create graphics pipeline
            if (!createGraphicsPipeline()) {
                Logger::get().error("Failed to create graphics pipeline");
                return false;
            }

            Logger::get().info("Enhanced shader resources created successfully");
            return true;
        }
        catch (const std::exception& e) {
            Logger::get().error("Exception in createShaderResources: {}", e.what());
            return false;
        }
        
    }

    bool VulkanClusteredRenderer::createGraphicsPipeline() {
        try {
            // Create shader stages
            std::vector<VkPipelineShaderStageCreateInfo> shaderStages = {
                m_taskShader->createShaderStageInfo(),
                m_meshShader->createShaderStageInfo(),
                m_fragmentShader->createShaderStageInfo()
            };

            // CRITICAL: Mesh shader pipelines need NULL vertex input state
            VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
            vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            vertexInputInfo.vertexBindingDescriptionCount = 0;
            vertexInputInfo.pVertexBindingDescriptions = nullptr;
            vertexInputInfo.vertexAttributeDescriptionCount = 0;
            vertexInputInfo.pVertexAttributeDescriptions = nullptr;

            // Input assembly (not used by mesh shaders but required)
            VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
            inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            inputAssembly.primitiveRestartEnable = VK_FALSE;


            // Pipeline state setup
            VkPipelineViewportStateCreateInfo viewportState{};
            viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            viewportState.viewportCount = 1;
            viewportState.scissorCount = 1;

            VkPipelineRasterizationStateCreateInfo rasterizer{};
            rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            rasterizer.depthClampEnable = VK_FALSE;
            rasterizer.rasterizerDiscardEnable = VK_FALSE;
            rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
            rasterizer.lineWidth = 1.0f;
            rasterizer.cullMode = VK_CULL_MODE_NONE;
            rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            rasterizer.depthBiasEnable = VK_FALSE;

            VkPipelineMultisampleStateCreateInfo multisampling{};
            multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            multisampling.sampleShadingEnable = VK_FALSE;
            multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

            VkPipelineColorBlendAttachmentState colorBlendAttachment{};
            colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            colorBlendAttachment.blendEnable = VK_FALSE;

            VkPipelineColorBlendStateCreateInfo colorBlending{};
            colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            colorBlending.logicOpEnable = VK_FALSE;
            colorBlending.attachmentCount = 1;
            colorBlending.pAttachments = &colorBlendAttachment;

            VkPipelineDepthStencilStateCreateInfo depthStencil{};
            depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
            depthStencil.depthTestEnable = VK_TRUE;
            depthStencil.depthWriteEnable = VK_TRUE;
            depthStencil.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL; // Reverse depth
            depthStencil.depthBoundsTestEnable = VK_FALSE;
            depthStencil.stencilTestEnable = VK_FALSE;

            VkDynamicState dynamicStates[] = {
                VK_DYNAMIC_STATE_VIEWPORT,
                VK_DYNAMIC_STATE_SCISSOR
            };

            VkPipelineDynamicStateCreateInfo dynamicState{};
            dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dynamicState.dynamicStateCount = 2;
            dynamicState.pDynamicStates = dynamicStates;

            // Setup for dynamic rendering
            VkPipelineRenderingCreateInfoKHR renderingInfo{};
            renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
            renderingInfo.colorAttachmentCount = 1;
            renderingInfo.pColorAttachmentFormats = m_colorFormat;
            renderingInfo.depthAttachmentFormat = *m_depthFormat;

            if (m_pipelineLayout) {
				Logger::get().info("Pipeline layout is valid");
            }
            else {
                Logger::get().error("Pipeline layout is null!!!");
                return false;
            }

            VkGraphicsPipelineCreateInfo pipelineInfo{};
            pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            pipelineInfo.pNext = &renderingInfo;
            pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
            pipelineInfo.pStages = shaderStages.data();
            pipelineInfo.pVertexInputState = &vertexInputInfo;     // IMPORTANT: Include this even if empty
            pipelineInfo.pInputAssemblyState = &inputAssembly;     // IMPORTANT: Include this too
            pipelineInfo.pViewportState = &viewportState;
            pipelineInfo.pRasterizationState = &rasterizer;
            pipelineInfo.pMultisampleState = &multisampling;
            pipelineInfo.pDepthStencilState = &depthStencil;
            pipelineInfo.pColorBlendState = &colorBlending;
            pipelineInfo.pDynamicState = &dynamicState;
            pipelineInfo.layout = m_pipelineLayout->handle();

            VkPipeline pipeline;
            VkResult result = vkCreateGraphicsPipelines(
                m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);

            if (result != VK_SUCCESS) {
                Logger::get().error("Failed to create graphics pipeline: {}", static_cast<int>(result));
                return false;
            }

            m_pipeline = std::make_unique<PipelineResource>(m_device, pipeline);
            Logger::get().info("Created enhanced graphics pipeline successfully");

            return true;
        }
        catch (const std::exception& e) {
            Logger::get().error("Exception in createGraphicsPipeline: {}", e.what());
            return false;
        }
    }

    void VulkanClusteredRenderer::updateDescriptorSet() {
        if (!m_descriptorSet) return;

        // Check all buffers before binding
        if (!m_uniformBuffer) { Logger::get().error("UBO is null!"); return; }
        if (!m_clusterBuffer) { Logger::get().error("Cluster buffer is null!"); return; }
        if (!m_objectBuffer) { Logger::get().error("Object buffer is null!"); return; }
        if (!m_lightBuffer) { Logger::get().error("Light buffer is null!"); return; }
        if (!m_indexBuffer) { Logger::get().error("Index buffer is null!"); return; }

        Logger::get().info("Checking texture bindings:");
        Logger::get().info("  m_defaultAlbedoTexture: {}", m_defaultAlbedoTexture ? "Valid" : "NULL");
        Logger::get().info("  m_defaultAlbedoView: {}", m_defaultAlbedoView ? "Valid" : "NULL");
        Logger::get().info("  m_defaultNormalTexture: {}", m_defaultNormalTexture ? "Valid" : "NULL");
        Logger::get().info("  m_defaultNormalView: {}", m_defaultNormalView ? "Valid" : "NULL");
        Logger::get().info("  m_defaultSampler: {}", m_defaultSampler ? "Valid" : "NULL");

        Logger::get().info("All buffers valid, proceeding with descriptor update...");

        // Create descriptor writes with inline buffer/image info (no separate vectors!)
        std::vector<VkWriteDescriptorSet> descriptorWrites;

        Logger::get().info("Uniform buffer size: {}", m_uniformBuffer->getSize());
        
        // Binding 0: Enhanced UBO
        VkDescriptorBufferInfo uboInfo{
            m_uniformBuffer->getBuffer(),
            0,
			m_uniformBuffer->getSize()
        };

        descriptorWrites.push_back({
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            nullptr,
            m_descriptorSet->handle(),
            0, 0, 1,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            nullptr,
            &uboInfo,
            nullptr
            });

        Logger::get().info("Cluster buffer size: {}", m_clusterBuffer->getSize());


        // Binding 1: Cluster Buffer
        VkDescriptorBufferInfo clusterInfo{
            m_clusterBuffer->getBuffer(),
            0,
			m_clusterBuffer->getSize()
        };

        descriptorWrites.push_back({
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            nullptr,
            m_descriptorSet->handle(),
            1, 0, 1,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            nullptr,
            &clusterInfo,
            nullptr
            });

        Logger::get().info("Object buffer size: {}", m_objectBuffer->getSize());


        // Binding 2: Object Buffer
        VkDescriptorBufferInfo objectInfo{
            m_objectBuffer->getBuffer(),
            0,
			m_objectBuffer->getSize()
        };

        descriptorWrites.push_back({
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            nullptr,
            m_descriptorSet->handle(),
            2, 0, 1,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            nullptr,
            &objectInfo,
            nullptr
            });

        Logger::get().info("Light buffer size: {}", m_lightBuffer->getSize());

        // Binding 3: Light Buffer
        VkDescriptorBufferInfo lightInfo{
            m_lightBuffer->getBuffer(),
            0,
			m_lightBuffer->getSize()
        };

        descriptorWrites.push_back({
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            nullptr,
            m_descriptorSet->handle(),
            3, 0, 1,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            nullptr,
            &lightInfo,
            nullptr
            });

        Logger::get().info("Index buffer size: {}", m_indexBuffer->getSize());

        // Binding 4: Index Buffer
        VkDescriptorBufferInfo indexInfo{
            m_indexBuffer->getBuffer(),
            0,
			m_indexBuffer->getSize()
        };

        descriptorWrites.push_back({
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            nullptr,
            m_descriptorSet->handle(),
            4, 0, 1,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            nullptr,
            &indexInfo,
            nullptr
            });

        Logger::get().info("Mesh info buffer size: {}", m_meshInfoBuffer->getSize());

        // Binding 5: Mesh Info Buffer
        VkDescriptorBufferInfo meshInfoInfo{
            m_meshInfoBuffer->getBuffer(),
            0,
			m_meshInfoBuffer->getSize()
        };

        descriptorWrites.push_back({
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            nullptr,
            m_descriptorSet->handle(),
            5, 0, 1,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            nullptr,
            &meshInfoInfo,
            nullptr
            });

        Logger::get().info("Vertex buffer size: {}", m_vertexBuffer->getSize());


        // Binding 6: Vertex Buffer
        VkDescriptorBufferInfo vertexInfo{
            m_vertexBuffer->getBuffer(),
            0,
            m_vertexBuffer->getSize()
        };

        descriptorWrites.push_back({
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            nullptr,
            m_descriptorSet->handle(),
            6, 0, 1,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            nullptr,
            &vertexInfo,
            nullptr
            });

        Logger::get().info("Mesh index buffer size: {}", m_meshIndexBuffer->getSize());

        // Binding 7: Mesh Index Buffer
        VkDescriptorBufferInfo meshIndexInfo{
            m_meshIndexBuffer->getBuffer(),
            0,
			m_meshIndexBuffer->getSize()
        };

        descriptorWrites.push_back({
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            nullptr,
            m_descriptorSet->handle(),
            7, 0, 1,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            nullptr,
            &meshIndexInfo,
            nullptr
            });

        Logger::get().info("Material buffer size: {}", m_materialBuffer->getSize());


        // Binding 8: Material Buffer
        VkDescriptorBufferInfo materialInfo{
            m_materialBuffer->getBuffer(),
            0,
			m_materialBuffer->getSize()
        };

        descriptorWrites.push_back({
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            nullptr,
            m_descriptorSet->handle(),
            8, 0, 1,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            nullptr,
            &materialInfo,
            nullptr
            });

        // Only add texture bindings if textures exist
        VkDescriptorImageInfo albedoImageInfo{};
        VkDescriptorImageInfo normalImageInfo{};

        // Binding 9: Default Albedo Texture
        if (m_defaultAlbedoView && m_defaultSampler) {
            albedoImageInfo = {
                m_defaultSampler->handle(),
                m_defaultAlbedoView->handle(),
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            };

            descriptorWrites.push_back({
                VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                nullptr,
                m_descriptorSet->handle(),
                9, 0, 1,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                &albedoImageInfo,
                nullptr,
                nullptr
                });
        }

        // Binding 10: Default Normal Texture
        if (m_defaultNormalView && m_defaultSampler) {
            normalImageInfo = {
                m_defaultSampler->handle(),
                m_defaultNormalView->handle(),
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            };

            descriptorWrites.push_back({
                VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                nullptr,
                m_descriptorSet->handle(),
                10, 0, 1,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                &normalImageInfo,
                nullptr,
                nullptr
                });
        }

        Logger::get().info("=== DESCRIPTOR UPDATE DEBUG ===");

        // Validate each buffer before binding
        std::vector<std::pair<std::string, VkBuffer>> buffers = {
            {"UBO", m_uniformBuffer->getBuffer()},
            {"Cluster", m_clusterBuffer->getBuffer()},
            {"Object", m_objectBuffer->getBuffer()},
            {"Light", m_lightBuffer->getBuffer()},
            {"Index", m_indexBuffer->getBuffer()},
            {"MeshInfo", m_meshInfoBuffer->getBuffer()},
            {"Vertex", m_vertexBuffer->getBuffer()},
            {"MeshIndex", m_meshIndexBuffer->getBuffer()},
            {"Material", m_materialBuffer->getBuffer()}
        };

        for (const auto& [name, buffer] : buffers) {
            if (buffer == VK_NULL_HANDLE) {
                Logger::get().error("{} buffer is NULL!", name);
            }
            else {
                Logger::get().info("{} buffer: valid", name);
            }
        }

        Logger::get().info("About to update {} descriptor writes", descriptorWrites.size());

        // Now all the buffer/image info structs are in scope when we call this!
        vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(descriptorWrites.size()),
            descriptorWrites.data(), 0, nullptr);

        Logger::get().info("Descriptor set updated successfully!");
    }

} // namespace tremor::gfx