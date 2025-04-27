// Copyright 2025 NOT REAL GAMES
//
// Permission is hereby granted, free of charge, 
// to any person obtaining a copy of this software 
// and associated documentation files(the "Software"), 
// to deal in the Software without restriction, 
// including without limitation the rights to use, copy, 
// modify, merge, publish, distribute, sublicense, and/or 
// sell copies of the Software, and to permit persons to 
// whom the Software is furnished to do so, subject to the 
// following conditions:
//
// The above copyright notice and this permission notice shall 
// be included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, 
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES 
// OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-
// INFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
// BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN 
// AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF 
// OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS 
// IN THE SOFTWARE.

// This file is part of Tremor: a game engine built on a 
// rewrite of the Quake Engine as a foundation. The original 
// Quake Engine is licensed under the GPLv2 license. 

// The Tremor project is not affiliated with or endorsed by id Software.
// idTech 2's dependencies on Quake will be removed from the Tremor project. 

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <vulkan/vulkan.hpp>

#define NUM_COLOR_BUFFERS 2

#define WORLD_PIPELINE_COUNT		16
#define MODEL_PIPELINE_COUNT		6
#define FTE_PARTICLE_PIPELINE_COUNT 16
#define MAX_BATCH_SIZE				65536
#define NUM_WORLD_CBX				6
#define NUM_ENTITIES_CBX			6

#define MAX_SWAP_CHAIN_IMAGES 8
#define DOUBLE_BUFFERED 2

#define ZEROED_STRUCT(type, name) \
	type name;                    \
	memset (&name, 0, sizeof (type));


#define GET_INSTANCE_PROC_ADDR(entrypoint)                                                              \
	{                                                                                                   \
		fp##entrypoint = (PFN_vk##entrypoint)fpGetInstanceProcAddr (vulkan_instance, "vk" #entrypoint); \
		if (fp##entrypoint == NULL)                                                                     \
			SDL_LogError (SDL_LOG_PRIORITY_ERROR,"vkGetInstanceProcAddr failed to find vk" #entrypoint);                          \
	}

typedef struct vulkan_pipeline_layout_s
{
	VkPipelineLayout	handle;
	VkPushConstantRange push_constant_range;
} vulkan_pipeline_layout_t;

typedef struct vulkan_pipeline_s
{
	VkPipeline				 handle;
	vulkan_pipeline_layout_t layout;
} vulkan_pipeline_t;

typedef struct vulkan_desc_set_layout_s
{
	VkDescriptorSetLayout handle;
	int					  num_combined_image_samplers;
	int					  num_ubos;
	int					  num_ubos_dynamic;
	int					  num_storage_buffers;
	int					  num_input_attachments;
	int					  num_storage_images;
	int					  num_sampled_images;
	int					  num_acceleration_structures;
} vulkan_desc_set_layout_t;

typedef enum
{
	VULKAN_MEMORY_TYPE_DEVICE,
	VULKAN_MEMORY_TYPE_HOST,
	VULKAN_MEMORY_TYPE_NONE,
} vulkan_memory_type_t;

typedef enum
{
	PCBX_BUILD_ACCELERATION_STRUCTURES,
	PCBX_UPDATE_LIGHTMAPS,
	PCBX_UPDATE_WARP,
	PCBX_RENDER_PASSES,
	PCBX_NUM,
} primary_cb_contexts_t;

typedef enum
{
	// Main render pass:
	SCBX_WORLD,
	SCBX_ENTITIES,
	SCBX_SKY,
	SCBX_ALPHA_ENTITIES_ACROSS_WATER,
	SCBX_WATER,
	SCBX_ALPHA_ENTITIES,
	SCBX_PARTICLES,
	SCBX_VIEW_MODEL,
	// UI render Pass:
	SCBX_GUI,
	SCBX_POST_PROCESS,
	SCBX_NUM,
} secondary_cb_contexts_t;

typedef struct vulkan_memory_s
{
	VkDeviceMemory		 handle;
	size_t				 size;
	vulkan_memory_type_t type;
} vulkan_memory_t;

typedef enum
{
	CANVAS_NONE,
	CANVAS_DEFAULT,
	CANVAS_CONSOLE,
	CANVAS_MENU,
	CANVAS_SBAR,
	CANVAS_WARPIMAGE,
	CANVAS_CROSSHAIR,
	CANVAS_BOTTOMLEFT,
	CANVAS_BOTTOMRIGHT,
	CANVAS_TOPRIGHT,
	CANVAS_CSQC,
	CANVAS_INVALID = -1
} canvastype;

typedef struct cb_context_s
{
	VkCommandBuffer	  cb;
	canvastype		  current_canvas;
	VkRenderPass	  render_pass;
	int				  render_pass_index;
	int				  subpass;
	vulkan_pipeline_t current_pipeline;
	uint32_t		  vbo_indices[MAX_BATCH_SIZE];
	unsigned int	  num_vbo_indices;
} cb_context_t;

typedef struct
{
	VkDevice						 device;
	bool						 device_idle;
	bool						 validation;
	bool						 debug_utils;
	VkQueue							 queue;
	cb_context_t					 primary_cb_contexts[PCBX_NUM];
	cb_context_t* secondary_cb_contexts[SCBX_NUM];
	VkClearValue					 color_clear_value;
	VkFormat						 swap_chain_format;
	bool						 want_full_screen_exclusive;
	bool						 swap_chain_full_screen_exclusive;
	bool						 swap_chain_full_screen_acquired;
	VkPhysicalDeviceProperties		 device_properties;
	VkPhysicalDeviceFeatures		 device_features;
	VkPhysicalDeviceMemoryProperties memory_properties;
	uint32_t						 gfx_queue_family_index;
	VkFormat						 color_format;
	VkFormat						 depth_format;
	VkSampleCountFlagBits			 sample_count;
	bool						 supersampling;
	bool						 non_solid_fill;
	bool						 multi_draw_indirect;
	bool						 screen_effects_sops;

	// Instance extensions
	bool get_surface_capabilities_2;
	bool get_physical_device_properties_2;
	bool vulkan_1_1_available;

	// Device extensions
	bool dedicated_allocation;
	bool full_screen_exclusive;
	bool ray_query;

	// Buffers
	VkImage color_buffers[NUM_COLOR_BUFFERS];

	// Index buffers
	VkBuffer fan_index_buffer;

	// Staging buffers
	int staging_buffer_size;

	// Render passes
	VkRenderPass main_render_pass[2]; // stencil clear, stencil dont_care
	VkRenderPass warp_render_pass;

	// Pipelines
	vulkan_pipeline_t		 basic_alphatest_pipeline[2];
	vulkan_pipeline_t		 basic_blend_pipeline[2];
	vulkan_pipeline_t		 basic_notex_blend_pipeline[2];
	vulkan_pipeline_layout_t basic_pipeline_layout;
	vulkan_pipeline_t		 world_pipelines[WORLD_PIPELINE_COUNT];
	vulkan_pipeline_layout_t world_pipeline_layout;
	vulkan_pipeline_t		 raster_tex_warp_pipeline;
	vulkan_pipeline_t		 particle_pipeline;
	vulkan_pipeline_t		 sprite_pipeline;
	vulkan_pipeline_layout_t sky_pipeline_layout[2]; // one texture (cubemap-like), two textures (animated layers)
	vulkan_pipeline_t		 sky_stencil_pipeline[2];
	vulkan_pipeline_t		 sky_color_pipeline[2];
	vulkan_pipeline_t		 sky_box_pipeline;
	vulkan_pipeline_t		 sky_cube_pipeline[2];
	vulkan_pipeline_t		 sky_layer_pipeline[2];
	vulkan_pipeline_t		 alias_pipelines[MODEL_PIPELINE_COUNT];
	vulkan_pipeline_t		 md5_pipelines[MODEL_PIPELINE_COUNT];
	vulkan_pipeline_t		 postprocess_pipeline;
	vulkan_pipeline_t		 screen_effects_pipeline;
	vulkan_pipeline_t		 screen_effects_scale_pipeline;
	vulkan_pipeline_t		 screen_effects_scale_sops_pipeline;
	vulkan_pipeline_t		 cs_tex_warp_pipeline;
	vulkan_pipeline_t		 showtris_pipeline;
	vulkan_pipeline_t		 showtris_indirect_pipeline;
	vulkan_pipeline_t		 showtris_depth_test_pipeline;
	vulkan_pipeline_t		 showtris_indirect_depth_test_pipeline;
	vulkan_pipeline_t		 showbboxes_pipeline;
	vulkan_pipeline_t		 update_lightmap_pipeline;
	vulkan_pipeline_t		 update_lightmap_rt_pipeline;
	vulkan_pipeline_t		 indirect_draw_pipeline;
	vulkan_pipeline_t		 indirect_clear_pipeline;
	vulkan_pipeline_t		 ray_debug_pipeline;
#ifdef PSET_SCRIPT
	vulkan_pipeline_t fte_particle_pipelines[FTE_PARTICLE_PIPELINE_COUNT];
#endif

	// Descriptors
	VkDescriptorPool		 descriptor_pool;
	vulkan_desc_set_layout_t ubo_set_layout;
	vulkan_desc_set_layout_t single_texture_set_layout;
	vulkan_desc_set_layout_t input_attachment_set_layout;
	VkDescriptorSet			 screen_effects_desc_set;
	vulkan_desc_set_layout_t screen_effects_set_layout;
	vulkan_desc_set_layout_t single_texture_cs_write_set_layout;
	vulkan_desc_set_layout_t lightmap_compute_set_layout;
	VkDescriptorSet			 indirect_compute_desc_set;
	vulkan_desc_set_layout_t indirect_compute_set_layout;
	vulkan_desc_set_layout_t lightmap_compute_rt_set_layout;
	VkDescriptorSet			 ray_debug_desc_set;
	vulkan_desc_set_layout_t ray_debug_set_layout;
	vulkan_desc_set_layout_t joints_buffer_set_layout;

	// Samplers
	VkSampler point_sampler;
	VkSampler linear_sampler;
	VkSampler point_aniso_sampler;
	VkSampler linear_aniso_sampler;
	VkSampler point_sampler_lod_bias;
	VkSampler linear_sampler_lod_bias;
	VkSampler point_aniso_sampler_lod_bias;
	VkSampler linear_aniso_sampler_lod_bias;

	// Matrices
	float projection_matrix[16];
	float view_matrix[16];
	float view_projection_matrix[16];

	// Dispatch table
	PFN_vkCmdBindPipeline			vk_cmd_bind_pipeline;
	PFN_vkCmdPushConstants			vk_cmd_push_constants;
	PFN_vkCmdBindDescriptorSets		vk_cmd_bind_descriptor_sets;
	PFN_vkCmdBindIndexBuffer		vk_cmd_bind_index_buffer;
	PFN_vkCmdBindVertexBuffers		vk_cmd_bind_vertex_buffers;
	PFN_vkCmdDraw					vk_cmd_draw;
	PFN_vkCmdDrawIndexed			vk_cmd_draw_indexed;
	PFN_vkCmdDrawIndexedIndirect	vk_cmd_draw_indexed_indirect;
	PFN_vkCmdPipelineBarrier		vk_cmd_pipeline_barrier;
	PFN_vkCmdCopyBufferToImage		vk_cmd_copy_buffer_to_image;
	PFN_vkGetBufferDeviceAddressKHR vk_get_buffer_device_address;

	PFN_vkGetAccelerationStructureBuildSizesKHR		   vk_get_acceleration_structure_build_sizes;
	PFN_vkCreateAccelerationStructureKHR			   vk_create_acceleration_structure;
	PFN_vkDestroyAccelerationStructureKHR			   vk_destroy_acceleration_structure;
	PFN_vkCmdBuildAccelerationStructuresKHR			   vk_cmd_build_acceleration_structures;
	VkPhysicalDeviceAccelerationStructurePropertiesKHR physical_device_acceleration_structure_properties;

#ifdef _DEBUG
	PFN_vkCmdBeginDebugUtilsLabelEXT vk_cmd_begin_debug_utils_label;
	PFN_vkCmdEndDebugUtilsLabelEXT	 vk_cmd_end_debug_utils_label;
#endif
} vulkanglobals_t;

vulkanglobals_t vulkan_globals;

void* Mem_Alloc(const size_t size)
{
	return SDL_calloc(1, size);
}

void* Mem_AllocNonZero(const size_t size)
{
	return SDL_malloc(size);
}

void* Mem_Realloc(void* ptr, const size_t size)
{
	return SDL_realloc(ptr, size);
}

void Mem_Free(const void* ptr)
{
	SDL_free((void*)ptr);
}

#ifdef _DEBUG

unsigned int VKAPI_PTR DebugMessageCallback(
	VkDebugUtilsMessageSeverityFlagBitsEXT message_severity, VkDebugUtilsMessageTypeFlagsEXT message_types,
	const VkDebugUtilsMessengerCallbackDataEXT* callback_data, void* user_data)
{
	SDL_Log("%s\n", callback_data->pMessage);
	return VK_FALSE;
}

#endif


class Engine {
public:
	class GL {
	public:
		Engine* engine;

		VkInstance				vulkan_instance;
		VkPhysicalDevice			vulkan_physical_device;
		VkSurfaceKHR				vulkan_surface;
		VkSurfaceCapabilitiesKHR vulkan_surface_capabilities;
		VkSwapchainKHR			vulkan_swapchain;

		uint32_t			num_swap_chain_images;
		bool			render_resources_created = false;
		uint32_t			current_cb_index;
		VkCommandPool	primary_command_pools[PCBX_NUM];
		VkCommandPool* secondary_command_pools[SCBX_NUM];
		VkCommandPool	transient_command_pool;
		VkCommandBuffer	primary_command_buffers[PCBX_NUM][DOUBLE_BUFFERED];
		VkCommandBuffer* secondary_command_buffers[SCBX_NUM][DOUBLE_BUFFERED];
		VkFence			command_buffer_fences[DOUBLE_BUFFERED];
		bool			frame_submitted[DOUBLE_BUFFERED];
		VkFramebuffer	main_framebuffers[NUM_COLOR_BUFFERS];
		VkSemaphore		image_aquired_semaphores[DOUBLE_BUFFERED];
		VkSemaphore		draw_complete_semaphores[DOUBLE_BUFFERED];
		VkFramebuffer	ui_framebuffers[MAX_SWAP_CHAIN_IMAGES];
		VkImage			swapchain_images[MAX_SWAP_CHAIN_IMAGES];
		VkImageView		swapchain_images_views[MAX_SWAP_CHAIN_IMAGES];
		VkImage			depth_buffer;
		vulkan_memory_t	depth_buffer_memory;
		VkImageView		depth_buffer_view;
		vulkan_memory_t	color_buffers_memory[NUM_COLOR_BUFFERS];
		VkImageView		color_buffers_view[NUM_COLOR_BUFFERS];
		VkImage			msaa_color_buffer;
		vulkan_memory_t	msaa_color_buffer_memory;
		VkImageView		msaa_color_buffer_view;
		VkDescriptorSet	postprocess_descriptor_set;
		VkBuffer			palette_colors_buffer;
		VkBufferView		palette_buffer_view;
		VkBuffer			palette_octree_buffer;

		PFN_vkGetInstanceProcAddr					  fpGetInstanceProcAddr;
		PFN_vkGetDeviceProcAddr						  fpGetDeviceProcAddr;
		PFN_vkGetPhysicalDeviceSurfaceSupportKHR		  fpGetPhysicalDeviceSurfaceSupportKHR;
		PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR  fpGetPhysicalDeviceSurfaceCapabilitiesKHR;
		PFN_vkGetPhysicalDeviceSurfaceCapabilities2KHR fpGetPhysicalDeviceSurfaceCapabilities2KHR;
		PFN_vkGetPhysicalDeviceSurfaceFormatsKHR		  fpGetPhysicalDeviceSurfaceFormatsKHR;
		PFN_vkGetPhysicalDeviceSurfacePresentModesKHR  fpGetPhysicalDeviceSurfacePresentModesKHR;
		PFN_vkCreateSwapchainKHR						  fpCreateSwapchainKHR;
		PFN_vkDestroySwapchainKHR					  fpDestroySwapchainKHR;
		PFN_vkGetSwapchainImagesKHR					  fpGetSwapchainImagesKHR;
		PFN_vkAcquireNextImageKHR					  fpAcquireNextImageKHR;
		PFN_vkQueuePresentKHR						  fpQueuePresentKHR;
		PFN_vkEnumerateInstanceVersion				  fpEnumerateInstanceVersion;
		PFN_vkGetPhysicalDeviceFeatures2				  fpGetPhysicalDeviceFeatures2;
		PFN_vkGetPhysicalDeviceProperties2			  fpGetPhysicalDeviceProperties2;

#ifdef _DEBUG
		PFN_vkCreateDebugUtilsMessengerEXT fpCreateDebugUtilsMessengerEXT;
		PFN_vkSetDebugUtilsObjectNameEXT		  fpSetDebugUtilsObjectNameEXT;

		VkDebugUtilsMessengerEXT debug_utils_messenger;

#endif

		GL(Engine e) {
			engine = &e;
			engine->gl = this;

			// Initialize Vulkan instance

				VkResult	 err;
				uint32_t	 i;
				unsigned int sdl_extension_count;
				vulkan_globals.debug_utils = false;

				if (!SDL_Vulkan_GetInstanceExtensions(engine->vid->draw_context, &sdl_extension_count, NULL))
					SDL_Log("SDL_Vulkan_GetInstanceExtensions failed: %s", SDL_GetError());

				const char** const instance_extensions = (const char** const)Mem_Alloc(sizeof(const char*) * (sdl_extension_count + 3));
				if (!SDL_Vulkan_GetInstanceExtensions(engine->vid->draw_context, &sdl_extension_count, instance_extensions))
					SDL_Log("SDL_Vulkan_GetInstanceExtensions failed: %s", SDL_GetError());

				uint32_t instance_extension_count;
				err = vkEnumerateInstanceExtensionProperties(NULL, &instance_extension_count, NULL);

				uint32_t additionalExtensionCount = 0;

				vulkan_globals.get_surface_capabilities_2 = false;
				vulkan_globals.get_physical_device_properties_2 = false;
				if (err == VK_SUCCESS || instance_extension_count > 0)
				{
					VkExtensionProperties* extension_props = (VkExtensionProperties*)Mem_Alloc(sizeof(VkExtensionProperties) * instance_extension_count);
					err = vkEnumerateInstanceExtensionProperties(NULL, &instance_extension_count, extension_props);

					for (i = 0; i < instance_extension_count; ++i)
					{
						if (strcmp(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME, extension_props[i].extensionName) == 0)
							vulkan_globals.get_surface_capabilities_2 = true;
						if (strcmp(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME, extension_props[i].extensionName) == 0)
							vulkan_globals.get_physical_device_properties_2 = true;
#if _DEBUG
						if (strcmp(VK_EXT_DEBUG_UTILS_EXTENSION_NAME, extension_props[i].extensionName) == 0)
							vulkan_globals.debug_utils = true;
#endif
					}

					Mem_Free(extension_props);
				}

				vulkan_globals.vulkan_1_1_available = false;
				fpGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)SDL_Vulkan_GetVkGetInstanceProcAddr();
				GET_INSTANCE_PROC_ADDR(EnumerateInstanceVersion);
				if (fpEnumerateInstanceVersion)
				{
					uint32_t api_version = 0;
					fpEnumerateInstanceVersion(&api_version);
					if (api_version >= VK_MAKE_VERSION(1, 1, 0))
					{
						SDL_Log("Using Vulkan 1.1\n");
						vulkan_globals.vulkan_1_1_available = true;
					}
				}

				ZEROED_STRUCT(VkApplicationInfo, application_info);
				application_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
				application_info.pApplicationName = "Tremor";
				application_info.applicationVersion = 1;
				application_info.pEngineName = "Tremor";
				application_info.engineVersion = 1;
				application_info.apiVersion = vulkan_globals.vulkan_1_1_available ? VK_MAKE_VERSION(1, 1, 0) : VK_MAKE_VERSION(1, 0, 0);

				ZEROED_STRUCT(VkInstanceCreateInfo, instance_create_info);
				instance_create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
				instance_create_info.pApplicationInfo = &application_info;
				instance_create_info.ppEnabledExtensionNames = instance_extensions;

				if (vulkan_globals.get_surface_capabilities_2)
					instance_extensions[sdl_extension_count + additionalExtensionCount++] = VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME;
				if (vulkan_globals.get_physical_device_properties_2)
					instance_extensions[sdl_extension_count + additionalExtensionCount++] = VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;

#ifdef _DEBUG
				if (vulkan_globals.debug_utils)
					instance_extensions[sdl_extension_count + additionalExtensionCount++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;

				const char* const layer_names[] = { "VK_LAYER_KHRONOS_validation" };
				if (vulkan_globals.validation)
				{
					SDL_Log("Using VK_LAYER_KHRONOS_validation\n");
					instance_create_info.enabledLayerCount = 1;
					instance_create_info.ppEnabledLayerNames = layer_names;
				}
#endif

				instance_create_info.enabledExtensionCount = sdl_extension_count + additionalExtensionCount;

				err = vkCreateInstance(&instance_create_info, NULL, &vulkan_instance);
				if (err != VK_SUCCESS)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR, "Couldn't create Vulkan instance");

				if (!SDL_Vulkan_CreateSurface(engine->vid->draw_context, vulkan_instance, &vulkan_surface))
					SDL_LogError(SDL_LOG_PRIORITY_ERROR,"Couldn't create Vulkan surface");

				GET_INSTANCE_PROC_ADDR(GetDeviceProcAddr);
				GET_INSTANCE_PROC_ADDR(GetPhysicalDeviceSurfaceSupportKHR);
				GET_INSTANCE_PROC_ADDR(GetPhysicalDeviceSurfaceCapabilitiesKHR);
				GET_INSTANCE_PROC_ADDR(GetPhysicalDeviceSurfaceFormatsKHR);
				GET_INSTANCE_PROC_ADDR(GetPhysicalDeviceSurfacePresentModesKHR);
				GET_INSTANCE_PROC_ADDR(GetSwapchainImagesKHR);

				if (vulkan_globals.get_physical_device_properties_2)
				{
					GET_INSTANCE_PROC_ADDR(GetPhysicalDeviceProperties2);
					GET_INSTANCE_PROC_ADDR(GetPhysicalDeviceFeatures2);
				}

				if (vulkan_globals.get_surface_capabilities_2)
					GET_INSTANCE_PROC_ADDR(GetPhysicalDeviceSurfaceCapabilities2KHR);

				SDL_Log("Instance extensions:\n");
				for (i = 0; i < (sdl_extension_count + additionalExtensionCount); ++i)
					SDL_Log(" %s\n", instance_extensions[i]);
				SDL_Log("\n");

#ifdef _DEBUG
				if (vulkan_globals.validation)
				{
					SDL_Log("Creating debug report callback\n");
					GET_INSTANCE_PROC_ADDR(CreateDebugUtilsMessengerEXT);
					if (fpCreateDebugUtilsMessengerEXT)
					{
						ZEROED_STRUCT(VkDebugUtilsMessengerCreateInfoEXT, debug_utils_messenger_create_info);
						debug_utils_messenger_create_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
						debug_utils_messenger_create_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
						debug_utils_messenger_create_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
						debug_utils_messenger_create_info.pfnUserCallback = DebugMessageCallback;

						err = fpCreateDebugUtilsMessengerEXT(vulkan_instance, &debug_utils_messenger_create_info, NULL, &debug_utils_messenger);
						if (err != VK_SUCCESS)
							SDL_LogError(SDL_LOG_PRIORITY_ERROR,"Could not create debug report callback");
					}
				}
#endif

				Mem_Free((void*)instance_extensions);
			}

	};
	class VID {
	public:
		Engine* engine;
		SDL_Window* draw_context;
		VID(Engine e) {
			engine = &e;
			engine->vid = this;

			// Initialize SDL
			if (SDL_Init(SDL_INIT_VIDEO) < 0) {
				SDL_Log("what the bitch!!!! SDL_Error: %s\n", SDL_GetError());
			}

			// Load the Vulkan library
			SDL_Vulkan_LoadLibrary(nullptr);

			// Create a window
			draw_context = SDL_CreateWindow("Tremor Engine", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1280, 720, SDL_WINDOW_VULKAN);

			engine->gl = new GL(*engine);

			while (1) {
				SDL_Event event;
				while (SDL_PollEvent(&event)) {
					if (event.type == SDL_QUIT) {
						SDL_DestroyWindow(draw_context);
						SDL_Quit();
						return;
					}
					SDL_Delay(1); // Simulate a frame delay
				}
			}
		}
	};

	VID* vid = nullptr;
	GL* gl = nullptr;

	Engine(int ct, char* var[]) {
		vid = new VID(*this);
	}
};

int main(int argc, char* argv[]) {


	auto tremor = new Engine(argc, argv);




	return 0;
}