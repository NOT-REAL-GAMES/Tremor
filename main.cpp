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

#include "atomic"

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <vulkan/vulkan.hpp>
using namespace std;

#define MAX_QPATH 64

#define NUM_COLOR_BUFFERS 2

#define MIPLEVELS 4

#define NUM_AMBIENTS 4
#define WORLD_PIPELINE_COUNT		16
#define MODEL_PIPELINE_COUNT		6
#define FTE_PARTICLE_PIPELINE_COUNT 16
#define MAX_BATCH_SIZE				65536
#define NUM_WORLD_CBX				6
#define NUM_ENTITIES_CBX			6

#define MAX_GLTEXTURES				4096
#define MAX_SANITY_LIGHTMAPS		256
#define MIN_NB_DESCRIPTORS_PER_TYPE 32

#define NUM_INDEX_BITS		 8
#define MAX_PENDING_TASKS	 (1u << NUM_INDEX_BITS)
#define MAX_EXECUTABLE_TASKS 256
#define MAX_DEPENDENT_TASKS	 16
#define MAX_PAYLOAD_SIZE	 128
#define WORKER_HUNK_SIZE	 (1 * 1024 * 1024)
#define WAIT_SPIN_COUNT		 100

#define MAX_SWAP_CHAIN_IMAGES 8
#define DOUBLE_BUFFERED 2

#define NUM_STAGING_BUFFERS 2

#define THREAD_LOCAL  __declspec (thread)

#define FAN_INDEX_BUFFER_SIZE 126

#define MAX_MAP_HULLS 4

#define MAXLIGHTMAPS 4
#define MAX_DLIGHTS 64

#define MESH_HEAP_SIZE_MB	1024
#define MESH_HEAP_PAGE_SIZE 4096
#define MESH_HEAP_NAME		"Mesh heap"

#define TEXTURE_HEAP_MEMORY_SIZE_MB 1024
#define TEXTURE_HEAP_PAGE_SIZE		16384



#define NUM_SMALL_ALLOC_SIZES  6 // 64 bit mask
#define NUM_BLOCK_SIZE_CLASSES 8
#define MAX_PAGES			   (UINT64_MAX - 1)
#define INVALID_PAGE_INDEX	   UINT64_MAX

#define VERTEXSIZE 7

#define VA_NUM_BUFFS 4
#if (MAX_OSPATH >= 1024)
#define VA_BUFFERLEN MAX_OSPATH
#else
#define VA_BUFFERLEN 1024
#endif

#define COMMA ,
#define NO_COMMA

#define GENERIC_INT_TYPES(x, separator) \
	x(int, i) separator \
	x(unsigned int, u) separator \
	x(long, l) separator \
	x(unsigned long, ul) separator \
	x(long long, ll) separator \
	x(unsigned long long, ull)

#define IMPL_GENERIC_INT_FUNCS(type, suffix) \
static inline type q_align_##suffix (type size, type alignment) \
{ \
	return ((size & (alignment - 1)) == 0) ? size : (size + alignment - (size & (alignment - 1))); \
}

GENERIC_INT_TYPES(IMPL_GENERIC_INT_FUNCS, NO_COMMA)

#define SELECT_ALIGN(type, suffix) type: q_align_##suffix
#define q_align(size, alignment) _Generic((size) + (alignment), \
	GENERIC_INT_TYPES (SELECT_ALIGN, COMMA))(size, alignment)

typedef uint64_t task_handle_t;
typedef void (*task_func_t) (void*);
typedef void (*task_indexed_func_t) (int, void*);

typedef enum
{
	VULKAN_MEMORY_TYPE_DEVICE,
	VULKAN_MEMORY_TYPE_HOST,
	VULKAN_MEMORY_TYPE_NONE,
} vulkan_memory_type_t;


typedef struct vulkan_memory_s
{
	VkDeviceMemory		 handle;
	size_t				 size;
	vulkan_memory_type_t type;
} vulkan_memory_t;

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

typedef uint16_t page_index_t;

typedef struct glheappagehdr_s
{
	page_index_t size_in_pages;
	page_index_t prev_block_page_index;
} glheappagehdr_t;


static const int SECONDARY_CB_MULTIPLICITY[SCBX_NUM] = {
	NUM_WORLD_CBX,	  // SCBX_WORLD,
	NUM_ENTITIES_CBX, // SCBX_ENTITIES,
	1,				  // SCBX_SKY,
	1,				  // SCBX_ALPHA_ENTITIES_ACROSS_WATER,
	1,				  // SCBX_WATER,
	1,				  // SCBX_ALPHA_ENTITIES,
	1,				  // SCBX_PARTICLES,
	1,				  // SCBX_VIEW_MODEL,
	1,				  // SCBX_GUI,
	1,				  // SCBX_POST_PROCESS,
};

#define INITIAL_DYNAMIC_VERTEX_BUFFER_SIZE_KB  256
#define INITIAL_DYNAMIC_INDEX_BUFFER_SIZE_KB   1024
#define INITIAL_DYNAMIC_UNIFORM_BUFFER_SIZE_KB 256
#define NUM_DYNAMIC_BUFFERS					   2
#define GARBAGE_FRAME_COUNT					   3
#define MAX_UNIFORM_ALLOC					   2048

#define INVALID_TASK_HANDLE UINT64_MAX
#define TASKS_MAX_WORKERS	32

#define INITIAL_STAGING_BUFFER_SIZE_KB 16384

#define countof(x) (sizeof (x) / sizeof ((x)[0]))

#define ZEROED_STRUCT(type, name) \
	type name;                    \
	memset (&name, 0, sizeof (type));
#define ZEROED_STRUCT_ARRAY(type, name, count) \
	type name[count];                          \
	memset (name, 0, sizeof (type) * count);


#define REQUIRED_COLOR_BUFFER_FEATURES                                                                                             \
	(VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | \
	 VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)


#define GET_INSTANCE_PROC_ADDR(entrypoint)                                                              \
	{                                                                                                   \
		engine->gl->fp##entrypoint = (PFN_vk##entrypoint)engine->gl->fpGetInstanceProcAddr (engine->gl->vulkan_instance, "vk" #entrypoint); \
		if (engine->gl->fp##entrypoint == NULL)                                                                     \
			SDL_LogError (SDL_LOG_PRIORITY_ERROR,"vkGetInstanceProcAddr failed to find vk" #entrypoint);                          \
	}

#define CHAIN_PNEXT(next_ptr, chained) \
	{                                  \
		*next_ptr = &chained;          \
		next_ptr = &chained.pNext;     \
	}

#define GET_GLOBAL_INSTANCE_PROC_ADDR(_var, entrypoint)                                               \
	{                                                                                                 \
		vulkan_globals._var = (PFN_##entrypoint)engine->gl->fpGetInstanceProcAddr (engine->gl->vulkan_instance, #entrypoint); \
		if (vulkan_globals._var == NULL)                                                              \
			SDL_LogError (SDL_LOG_PRIORITY_ERROR,"vkGetInstanceProcAddr failed to find " #entrypoint);                          \
	}

#define GET_DEVICE_PROC_ADDR(entrypoint)                                                                    \
	{                                                                                                       \
		engine->gl->fp##entrypoint = (PFN_vk##entrypoint)engine->gl->fpGetDeviceProcAddr (vulkan_globals.device, "vk" #entrypoint); \
		if (engine->gl->fp##entrypoint == NULL)                                                                         \
			SDL_LogError (SDL_LOG_PRIORITY_ERROR,"vkGetDeviceProcAddr failed to find vk" #entrypoint);                                \
	}

#define GET_GLOBAL_DEVICE_PROC_ADDR(_var, entrypoint)                                                     \
	{                                                                                                     \
		vulkan_globals._var = (PFN_##entrypoint)engine->gl->fpGetDeviceProcAddr (vulkan_globals.device, #entrypoint); \
		if (vulkan_globals._var == NULL)                                                                  \
			SDL_LogError (SDL_LOG_PRIORITY_ERROR,"vkGetDeviceProcAddr failed to find " #entrypoint);                                \
	}

typedef struct glheapsmallalloclinks_s
{
	page_index_t prev_small_alloc_page;
	page_index_t next_small_alloc_page;
} glheapsmallalloclinks_t;


typedef struct glheapsegment_s
{
	vulkan_memory_t			 memory;
	glheappagehdr_t* page_hdrs;
	glheapsmallalloclinks_t* small_alloc_links;
	uint64_t* small_alloc_masks;
	uint64_t* free_blocks_bitfields[NUM_BLOCK_SIZE_CLASSES];
	uint64_t* free_blocks_skip_bitfields[NUM_BLOCK_SIZE_CLASSES];
	page_index_t			 small_alloc_free_list_heads[NUM_SMALL_ALLOC_SIZES];
	page_index_t			 num_pages_allocated;
} glheapsegment_t;

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

typedef float vec_t;
typedef vec_t vec2_t[2];
typedef vec_t vec3_t[3];
typedef vec_t vec4_t[4];
typedef vec_t vec5_t[5];
typedef int	  fixed4_t;
typedef int	  fixed8_t;
typedef int	  fixed16_t;

typedef uintptr_t src_offset_t;

typedef enum
{
	chain_world,
	chain_model_0,
	chain_model_1,
	chain_model_2,
	chain_model_3,
	chain_model_4,
	chain_model_5,
	chain_alpha_model_across_water,
	chain_alpha_model,
	chain_num,
} texchain_t;

typedef struct texture_s
{
	char				name[16];
	unsigned			width, height;
	unsigned			shift;					  // Q64
	char				source_file[MAX_QPATH];	  // relative filepath to data source, or "" if source is in memory
	src_offset_t		source_offset;			  // offset from start of BSP file for BSP textures
	struct gltexture_s* gltexture;				  // johnfitz -- pointer to gltexture
	struct gltexture_s* fullbright;				  // johnfitz -- fullbright mask texture
	struct gltexture_s* warpimage;				  // johnfitz -- for water animation
	atomic_uint32_t		update_warp;			  // johnfitz -- update warp this frame
	struct msurface_s* texturechains[chain_num]; // for texture chains
	uint32_t			chain_size[chain_num];	  // for texture chains
	int					anim_total;				  // total tenths in sequence ( 0 = no)
	int					anim_min, anim_max;		  // time for this frame min <=time< max
	struct texture_s* anim_next;				  // in the animation sequence
	struct texture_s* alternate_anims;		  // bmodels in frmae 1 use these
	unsigned			offsets[MIPLEVELS];		  // four mip maps stored
	bool			palette;
} texture_t;

typedef struct
{
	float mins[3], maxs[3];
	float origin[3];
	int	  headnode[MAX_MAP_HULLS];
	int	  visleafs; // not including the solid leaf 0
	int	  firstface, numfaces;
} dmodel_t;

typedef struct mplane_s
{
	vec3_t normal;
	float  dist;
	byte   type;	 // for texture axis selection and fast side tests
	byte   signbits; // signx + signy<<1 + signz<<1
	byte   pad[2];
} mplane_t;

typedef struct efrag_s
{
	struct efrag_s* leafnext;
	struct entity_s* entity;
} efrag_t;

typedef struct mleaf_s
{
	// common with node
	int	  contents;	  // wil be a negative contents number
	float minmaxs[6]; // for bounding box culling

	// leaf specific
	int		 nummarksurfaces;
	int		 combined_deps; // contains index into brush_deps_data[] with used warp and lightmap textures
	byte	 ambient_sound_level[NUM_AMBIENTS];
	byte* compressed_vis;
	int* firstmarksurface;
	efrag_t* efrags;
} mleaf_t;

typedef struct
{
	vec3_t position;
} mvertex_t;

typedef struct
{
	unsigned int v[2];
	unsigned int cachededgeoffset;
} medge_t;

typedef struct mnode_s
{
	// common with leaf
	int	  contents;	  // 0, to differentiate from leafs
	float minmaxs[6]; // for bounding box culling

	// node specific
	unsigned int	firstsurface;
	unsigned int	numsurfaces;
	mplane_t* plane;
	struct mnode_s* children[2];
} mnode_t;

typedef struct
{
	float	   vecs[2][4];
	texture_t* texture;
	int		   flags;
	int		   tex_idx;
} mtexinfo_t;

typedef struct glpoly_s
{
	struct glpoly_s* next;
	int				 numverts;
	float			 verts[4][VERTEXSIZE]; // variable sized (xyz s1t1 s2t2)
} glpoly_t;

typedef struct mclipnode_s
{
	int planenum;
	int children[2]; // negative numbers are contents
} mclipnode_t;

typedef struct msurface_s
{
	int visframe; // should be drawn when node is crossed

	mplane_t* plane;
	int		  flags;

	int firstedge; // look up in model->surfedges[], negative numbers
	int numedges;  // are backwards edges

	short texturemins[2];
	short extents[2];

	int light_s, light_t; // gl lightmap coordinates

	glpoly_t* polys; // multiple if warped
	struct msurface_s* texturechains[chain_num];

	mtexinfo_t* texinfo;
	int			indirect_idx;

	int vbo_firstvert; // index of this surface's first vert in the VBO

	// lighting info
	int			 dlightframe;
	unsigned int dlightbits[(MAX_DLIGHTS + 31) >> 5];
	// int is 32 bits, need an array for MAX_DLIGHTS > 32

	int		 lightmaptexturenum;
	byte	 styles[MAXLIGHTMAPS];
	uint32_t styles_bitmap;				 // bitmap of styles used (16..64 OR-folded into bits 16..31)
	int		 cached_light[MAXLIGHTMAPS]; // values currently used in lightmap
	bool cached_dlight;				 // true if dynamic light in cache
	byte* samples;					 // [numstyles*surfsize]
} msurface_t;

typedef float soa_aabb_t[2 * 3 * 8]; // 8 AABB's in SoA form
typedef float soa_plane_t[4 * 8];	 // 8 planes in SoA form

typedef enum
{
	mod_brush,
	mod_sprite,
	mod_alias
} modtype_t;

typedef enum
{
	ST_SYNC = 0,
	ST_RAND,
	ST_FRAMETIME /*sync to when .frame changes*/
} synctype_t;

typedef struct
{
	mclipnode_t* clipnodes; // johnfitz -- was dclipnode_t
	mplane_t* planes;
	int			 firstclipnode;
	int			 lastclipnode;
	vec3_t		 clip_mins;
	vec3_t		 clip_maxs;
} hull_t;

typedef struct qmodel_s
{
	char		 name[MAX_QPATH];
	unsigned int path_id;  // path id of the game directory
	// that this model came from
	bool	 needload; // bmodels and sprites don't cache normally

	modtype_t  type;
	int		   numframes;
	synctype_t synctype;

	int flags;

#ifdef PSET_SCRIPT
	int					  emiteffect;  // spike -- this effect is emitted per-frame by entities with this model
	int					  traileffect; // spike -- this effect is used when entities move
	struct skytris_s* skytris;	   // spike -- surface-based particle emission for this model
	struct skytriblock_s* skytrimem;   // spike -- surface-based particle emission for this model (for better cache performance+less allocs)
	double				  skytime;	   // doesn't really cope with multiples. oh well...
#endif
	//
	// volume occupied by the model graphics
	//
	vec3_t mins, maxs;
	vec3_t ymins, ymaxs; // johnfitz -- bounds for entities with nonzero yaw
	vec3_t rmins, rmaxs; // johnfitz -- bounds for entities with nonzero pitch or roll
	// johnfitz -- removed float radius;

	//
	// solid volume for clipping
	//
	bool clipbox;
	vec3_t	 clipmins, clipmaxs;

	//
	// brush model
	//
	int firstmodelsurface, nummodelsurfaces;

	int		  numsubmodels;
	dmodel_t* submodels;

	int		  numplanes;
	mplane_t* planes;

	int		 numleafs; // number of visible leafs, not counting 0
	mleaf_t* leafs;

	int		   numvertexes;
	mvertex_t* vertexes;

	int		 numedges;
	medge_t* edges;

	int		 numnodes;
	mnode_t* nodes;

	int			numtexinfo;
	mtexinfo_t* texinfo;

	int			numsurfaces;
	msurface_t* surfaces;

	int	 numsurfedges;
	int* surfedges;

	int			 numclipnodes;
	mclipnode_t* clipnodes; // johnfitz -- was dclipnode_t

	int	 nummarksurfaces;
	int* marksurfaces;

	soa_aabb_t* soa_leafbounds;
	byte* surfvis;
	soa_plane_t* soa_surfplanes;

	hull_t hulls[MAX_MAP_HULLS];

	int			numtextures;
	texture_t** textures;

	byte* visdata;
	byte* lightdata;
	char* entities;

	bool viswarn;	 // for Mod_DecompressVis()
	bool bogus_tree; // BSP node tree doesn't visit nummodelsurfaces surfaces

	int bspversion;
	int contentstransparent; // spike -- added this so we can disable glitchy wateralpha where its not supported.

	int combined_deps; // contains index into brush_deps_data[] with used warp and lightmap textures
	int used_specials; // contains SURF_DRAWSKY, SURF_DRAWTURB, SURF_DRAWWATER, SURF_DRAWLAVA, SURF_DRAWSLIME, SURF_DRAWTELE flags if used by any surf

	int* water_surfs; // worldmodel only: list of surface indices with SURF_DRAWTURB flag of transparent types
	int	 used_water_surfs;
	int	 water_surfs_specials; // which surfaces are in water_surfs (SURF_DRAWWATER, SURF_DRAWLAVA, SURF_DRAWSLIME, SURF_DRAWTELE) to track transparency changes

	//
	// additional model data
	//
	byte* extradata[2]; // only access through Mod_Extradata

	bool md5_prio; // if true, the MD5 model has at least as much path priority as the MDL model

	// Ray tracing
	VkAccelerationStructureKHR blas;
	VkBuffer				   blas_buffer;
	VkDeviceAddress			   blas_address;
} qmodel_t;

typedef enum
{
	TEXPREF_NONE = 0x0000,
	TEXPREF_MIPMAP = 0x0001,   // generate mipmaps
	// TEXPREF_NEAREST and TEXPREF_LINEAR aren't supposed to be ORed with TEX_MIPMAP
	TEXPREF_LINEAR = 0x0002,   // force linear
	TEXPREF_NEAREST = 0x0004,   // force nearest
	TEXPREF_ALPHA = 0x0008,   // allow alpha
	TEXPREF_PAD = 0x0010,   // allow padding
	TEXPREF_PERSIST = 0x0020,   // never free
	TEXPREF_OVERWRITE = 0x0040,   // overwrite existing same-name texture
	TEXPREF_NOPICMIP = 0x0080,   // always load full-sized
	TEXPREF_FULLBRIGHT = 0x0100,   // use fullbright mask palette
	TEXPREF_NOBRIGHT = 0x0200,   // use nobright mask palette
	TEXPREF_CONCHARS = 0x0400,   // use conchars palette
	TEXPREF_WARPIMAGE = 0x0800,   // resize this texture when warpimagesize changes
	TEXPREF_PREMULTIPLY = 0x1000,   // rgb = rgb*a; a=a; 	
} textureflags_t;

typedef enum
{
	ALLOC_TYPE_NONE,
	ALLOC_TYPE_PAGES,
	ALLOC_TYPE_DEDICATED,
	ALLOC_TYPE_SMALL_ALLOC,
} alloc_type_t;

typedef struct glheapallocation_s
{
	union
	{
		glheapsegment_t* segment;
		vulkan_memory_t* memory;
	};
	VkDeviceSize size;
	VkDeviceSize offset;
	alloc_type_t alloc_type;
#ifndef NDEBUG
	uint32_t small_alloc_slot;
	uint32_t small_alloc_size;
#endif
} glheapallocation_t;


typedef struct gltexture_s
{
	// managed by texture manager
	struct gltexture_s* next;
	qmodel_t* owner;
	// managed by image loading
	char				name[64];
	unsigned int		path_id; // path id of the game directory
	// that owner came from, if owner != NULL, else 0
	unsigned int		width;	 // size of image as it exists in opengl
	unsigned int		height;	 // size of image as it exists in opengl
	textureflags_t		flags;
	char				source_file[MAX_QPATH]; // relative filepath to data source, or "" if source is in memory
	src_offset_t		source_offset;			// byte offset into file, or memory address
	enum srcformat		source_format;			// format of pixel data (indexed, lightmap, or rgba)
	unsigned int		source_width;			// size of image in source data
	unsigned int		source_height;			// size of image in source data
	unsigned short		source_crc;				// generated by source data before modifications
	signed char			shirt;					// 0-13 shirt color, or -1 if never colormapped
	signed char			pants;					// 0-13 pants color, or -1 if never colormapped
	// used for rendering
	VkImage				image;
	VkImageView			image_view;
	VkImageView			target_image_view;
	glheapallocation_t* allocation;
	VkDescriptorSet		descriptor_set;
	VkFramebuffer		frame_buffer;
	VkDescriptorSet		storage_descriptor_set;
} gltexture_t;

typedef struct
{
	VkBuffer		buffer;
	VkCommandBuffer command_buffer;
	VkFence			fence;
	int				current_offset;
	bool			submitted;
	unsigned char* data;
} stagingbuffer_t;

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


typedef enum
{
	CVAR_NONE = 0,
	CVAR_ARCHIVE = (1U << 0),	 // if set, causes it to be saved to config
	CVAR_NOTIFY = (1U << 1),	 // changes will be broadcasted to all players (q1)
	CVAR_SERVERINFO = (1U << 2),   // added to serverinfo will be sent to clients (q1/net_dgrm.c and qwsv)
	CVAR_USERINFO = (1U << 3),	 // added to userinfo, will be sent to server (qwcl)
	CVAR_CHANGED = (1U << 4),
	CVAR_ROM = (1U << 6),
	CVAR_LOCKED = (1U << 8),	 // locked temporarily
	CVAR_REGISTERED = (1U << 10),  // the var is added to the list of variables
	CVAR_CALLBACK = (1U << 16),	 // var has a callback
	CVAR_USERDEFINED = (1U << 17),  // cvar was created by the user/mod, and needs to be saved a bit differently.
	CVAR_AUTOCVAR = (1U << 18),	 // cvar changes need to feed back to qc global changes.
	CVAR_SETA = (1U << 19)   // cvar will be saved with seta.
} cvarflags_t;

typedef void (*cvarcallback_t) (struct cvar_s*);

typedef struct cvar_s
{
	const char* name;
	const char* string;
	cvarflags_t	   flags;
	float		   value;
	const char* default_string; // johnfitz -- remember defaults for reset function
	cvarcallback_t callback;
	struct cvar_s* next;
} cvar_t;



typedef struct glheapstats_s
{
	uint32_t num_segments;
	uint32_t num_allocations;
	uint32_t num_small_allocations;
	uint32_t num_block_allocations;
	uint32_t num_dedicated_allocations;
	uint32_t num_blocks_used;
	uint32_t num_blocks_free;
	uint32_t num_pages_allocated;
	uint32_t num_pages_free;
	uint64_t num_bytes_allocated;
	uint64_t num_bytes_free;
	uint64_t num_bytes_wasted;
} glheapstats_t;

typedef struct glheap_s
{
	const char* name;
	VkDeviceSize		 segment_size;
	uint32_t			 page_size;
	uint32_t			 page_size_shift;
	uint32_t			 min_small_alloc_size;
	uint32_t			 small_alloc_shift;
	uint32_t			 memory_type_index;
	vulkan_memory_type_t memory_type;
	uint32_t			 num_segments;
	page_index_t		 num_pages_per_segment;
	page_index_t		 num_masks_per_segment;
	glheapsegment_t** segments;
	uint64_t			 dedicated_alloc_bytes;
	glheapstats_t		 stats;
} glheap_t;


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

enum
{
	DRIVER_ID_AMD_PROPRIETARY = 1,
	DRIVER_ID_AMD_OPEN_SOURCE = 2,
	DRIVER_ID_MESA_RADV = 3,
	DRIVER_ID_NVIDIA_PROPRIETARY = 4,
	DRIVER_ID_INTEL_PROPRIETARY_WINDOWS = 5,
	DRIVER_ID_INTEL_OPEN_SOURCE_MESA = 6,
	DRIVER_ID_IMAGINATION_PROPRIETARY = 7,
	DRIVER_ID_QUALCOMM_PROPRIETARY = 8,
	DRIVER_ID_ARM_PROPRIETARY = 9,
	DRIVER_ID_GOOGLE_SWIFTSHADER = 10,
	DRIVER_ID_GGP_PROPRIETARY = 11,
	DRIVER_ID_BROADCOM_PROPRIETARY = 12,
	DRIVER_ID_MESA_LLVMPIPE = 13,
	DRIVER_ID_MOLTENVK = 14,
	DRIVER_ID_COREAVI_PROPRIETARY = 15,
	DRIVER_ID_JUICE_PROPRIETARY = 16,
	DRIVER_ID_VERISILICON_PROPRIETARY = 17,
	DRIVER_ID_MESA_TURNIP = 18,
	DRIVER_ID_MESA_V3DV = 19,
	DRIVER_ID_MESA_PANVK = 20,
	DRIVER_ID_SAMSUNG_PROPRIETARY = 21,
	DRIVER_ID_MESA_VENUS = 22,
};

vulkanglobals_t vulkan_globals;

/*
===============
GetDeviceVendorFromDriverProperties
===============
*/
static const char* GetDeviceVendorFromDriverProperties(VkPhysicalDeviceDriverProperties* driver_properties)
{
	switch ((int)driver_properties->driverID)
	{
	case DRIVER_ID_AMD_PROPRIETARY:
	case DRIVER_ID_AMD_OPEN_SOURCE:
	case DRIVER_ID_MESA_RADV:
		return "AMD";
	case DRIVER_ID_NVIDIA_PROPRIETARY:
		return "NVIDIA";
	case DRIVER_ID_INTEL_PROPRIETARY_WINDOWS:
	case DRIVER_ID_INTEL_OPEN_SOURCE_MESA:
		return "Intel";
	case DRIVER_ID_IMAGINATION_PROPRIETARY:
		return "ImgTec";
	case DRIVER_ID_QUALCOMM_PROPRIETARY:
	case DRIVER_ID_MESA_TURNIP:
		return "Qualcomm";
	case DRIVER_ID_ARM_PROPRIETARY:
	case DRIVER_ID_MESA_PANVK:
		return "ARM";
	case DRIVER_ID_GOOGLE_SWIFTSHADER:
	case DRIVER_ID_GGP_PROPRIETARY:
		return "Google";
	case DRIVER_ID_BROADCOM_PROPRIETARY:
		return "Broadcom";
	case DRIVER_ID_MESA_V3DV:
		return "Raspberry Pi";
	case DRIVER_ID_MESA_LLVMPIPE:
	case DRIVER_ID_MESA_VENUS:
		return "MESA";
	case DRIVER_ID_MOLTENVK:
		return "MoltenVK";
	case DRIVER_ID_SAMSUNG_PROPRIETARY:
		return "Samsung";
	default:
		return NULL;
	}
}

static const char* GetDeviceVendorFromDeviceProperties(void)
{
	switch (vulkan_globals.device_properties.vendorID)
	{
	case 0x8086:
		return "Intel";
	case 0x10DE:
		return "NVIDIA";
	case 0x1002:
		return "AMD";
	case 0x1010:
		return "ImgTec";
	case 0x13B5:
		return "ARM";
	case 0x5143:
		return "Qualcomm";
	}

	return NULL;
}


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

float CLAMP(float* value, float min, float max)
{
	if (*value < min)
		*value = min;
	else if (*value > max)
		*value = max;
	return *value;
}

static char* get_va_buffer(void)
{
	static THREAD_LOCAL char va_buffers[VA_NUM_BUFFS][VA_BUFFERLEN];
	static THREAD_LOCAL int	 buffer_idx = 0;
	buffer_idx = (buffer_idx + 1) & (VA_NUM_BUFFS - 1);
	return va_buffers[buffer_idx];
}

char* va(const char* format, ...)
{
	va_list argptr;
	char* va_buf;

	va_buf = get_va_buffer();
	va_start(argptr, format);
	_vsnprintf_s(va_buf, VA_BUFFERLEN, sizeof(va_buf), format, argptr);
	va_end(argptr);

	return va_buf;
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

typedef struct
{
	VkBuffer		buffer;
	uint32_t		current_offset;
	unsigned char* data;
	VkDeviceAddress device_address;
} dynbuffer_t;



class Engine {
public:

	static int			numgltextures;
	static gltexture_t* active_gltextures, * free_gltextures;
	gltexture_t* notexture, * nulltexture, * whitetexture, * greytexture, * greylightmap, * bluenoisetexture;


	cvar_t r_lodbias = { "r_lodbias", "1", CVAR_ARCHIVE };
	cvar_t gl_lodbias = { "gl_lodbias", "0", CVAR_ARCHIVE };
	cvar_t r_scale = { "r_scale", "1", CVAR_ARCHIVE };
	cvar_t vid_fullscreen = { "vid_fullscreen", "0", CVAR_ARCHIVE }; // QuakeSpasm, was "1"
	cvar_t vid_width = { "vid_width", "1280", CVAR_ARCHIVE };		  // QuakeSpasm, was 640
	cvar_t vid_height = { "vid_height", "720", CVAR_ARCHIVE };		  // QuakeSpasm, was 480
	cvar_t vid_refreshrate = { "vid_refreshrate", "60", CVAR_ARCHIVE };
	cvar_t vid_vsync = { "vid_vsync", "0", CVAR_ARCHIVE };
	cvar_t vid_desktopfullscreen = { "vid_desktopfullscreen", "0", CVAR_ARCHIVE }; // QuakeSpasm
	cvar_t vid_borderless = { "vid_borderless", "0", CVAR_ARCHIVE };				// QuakeSpasm
	cvar_t		  vid_palettize = { "vid_palettize", "0", CVAR_ARCHIVE };
	cvar_t		  vid_filter = { "vid_filter", "0", CVAR_ARCHIVE };
	cvar_t		  vid_anisotropic = { "vid_anisotropic", "0", CVAR_ARCHIVE };
	cvar_t		  vid_fsaa = { "vid_fsaa", "0", CVAR_ARCHIVE };
	cvar_t		  vid_fsaamode = { "vid_fsaamode", "0", CVAR_ARCHIVE };
	cvar_t		  vid_gamma = { "gamma", "0.9", CVAR_ARCHIVE };		// johnfitz -- moved here from view.c
	cvar_t		  vid_contrast = { "contrast", "1.4", CVAR_ARCHIVE }; // QuakeSpasm, MarkV
	cvar_t		  r_usesops = { "r_usesops", "1", CVAR_ARCHIVE };		// johnfitz
#if defined(_DEBUG)
	cvar_t r_raydebug = { "r_raydebug", "0", CVAR_NONE };
#endif


	static inline int FindLastBitNonZero(const uint32_t mask)
	{
		unsigned long result;
		_BitScanReverse(&result, mask);
		return result;
	}

	static inline uint32_t Q_log2(uint32_t val)
	{
		assert(val > 0);
		return FindLastBitNonZero(val);
	}

	static inline uint32_t Q_nextPow2(uint32_t val)
	{
		uint32_t result = 1;
		if (val > 1)
			result = 1 << (FindLastBitNonZero(val - 1) + 1);
		return result;
	}

	void R_SubmitStagingBuffer(int index)
	{
		while (gl->sbuf->num_stagings_in_flight > 0)
			SDL_CondWait(gl->sbuf->staging_cond, gl->sbuf->staging_mutex);

		ZEROED_STRUCT(VkMemoryBarrier, memory_barrier);
		memory_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		memory_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		memory_barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
		vkCmdPipelineBarrier(
			gl->sbuf->staging_buffers[index].command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &memory_barrier, 0, NULL, 0, NULL);

		vkEndCommandBuffer(gl->sbuf->staging_buffers[index].command_buffer);

		ZEROED_STRUCT(VkMappedMemoryRange, range);
		range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
		range.memory = gl->sbuf->staging_memory.handle;
		range.size = VK_WHOLE_SIZE;
		vkFlushMappedMemoryRanges(vulkan_globals.device, 1, &range);

		ZEROED_STRUCT(VkSubmitInfo, submit_info);
		submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit_info.commandBufferCount = 1;
		submit_info.pCommandBuffers = &gl->sbuf->staging_buffers[index].command_buffer;

		vkQueueSubmit(vulkan_globals.queue, 1, &submit_info, gl->sbuf->staging_buffers[index].fence);

		gl->sbuf->staging_buffers[index].submitted = true;
		gl->sbuf->current_staging_buffer = (gl->sbuf->current_staging_buffer + 1) % NUM_STAGING_BUFFERS;
	}


	void R_SubmitStagingBuffers(void)
	{
		SDL_LockMutex(gl->staging_mutex);

		while (gl->sbuf->num_stagings_in_flight > 0)
			SDL_CondWait(gl->sbuf->staging_cond, gl->sbuf->staging_mutex);

		int i;
		for (i = 0; i < NUM_STAGING_BUFFERS; ++i)
		{
			if (!gl->sbuf->staging_buffers[i].submitted && gl->sbuf->staging_buffers[i].current_offset > 0)
				R_SubmitStagingBuffer(i);
		}

		SDL_UnlockMutex(gl->sbuf->staging_mutex);
	}

	static void R_FlushStagingCommandBuffer(stagingbuffer_t* staging_buffer)
	{
		VkResult err;

		if (!staging_buffer->submitted)
			return;

		err = vkWaitForFences(vulkan_globals.device, 1, &staging_buffer->fence, VK_TRUE, UINT64_MAX);
		if (err != VK_SUCCESS)
			SDL_LogError(SDL_LOG_PRIORITY_ERROR,"vkWaitForFences failed");
		
		err = vkResetFences(vulkan_globals.device, 1, &staging_buffer->fence);
		if (err != VK_SUCCESS)
			SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkResetFences failed");

		staging_buffer->current_offset = 0;
		staging_buffer->submitted = false;

		ZEROED_STRUCT(VkCommandBufferBeginInfo, command_buffer_begin_info);
		command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		command_buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

		err = vkBeginCommandBuffer(staging_buffer->command_buffer, &command_buffer_begin_info);
		if (err != VK_SUCCESS)
			SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkBeginCommandBuffer failed");
	}

	void R_FreeVulkanMemory(vulkan_memory_t* memory, atomic_uint32_t* num_allocations)
	{
		if (memory->type == VULKAN_MEMORY_TYPE_DEVICE)
			(gl->total_device_vulkan_allocation_size-= memory->size);
		else if (memory->type == VULKAN_MEMORY_TYPE_HOST)
			(gl->total_host_vulkan_allocation_size-= memory->size);
		if (memory->type != VULKAN_MEMORY_TYPE_NONE)
		{
			vkFreeMemory(vulkan_globals.device, memory->handle, NULL);
			if (num_allocations)
				(num_allocations--);
		}
		memory->handle = VK_NULL_HANDLE;
		memory->size = 0;
	}

	void R_DestroyStagingBuffers(void)
	{
		int i;

		R_FreeVulkanMemory(&gl->sbuf->staging_memory, NULL);
		for (i = 0; i < NUM_STAGING_BUFFERS; ++i)
		{
			vkDestroyBuffer(vulkan_globals.device, gl->sbuf->staging_buffers[i].buffer, NULL);
		}
	}

	void R_CreateStagingBuffers()
	{
		int		 i;
		VkResult err;

		ZEROED_STRUCT(VkBufferCreateInfo, buffer_create_info);
		buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		buffer_create_info.size = vulkan_globals.staging_buffer_size;
		buffer_create_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

		for (i = 0; i < NUM_STAGING_BUFFERS; ++i)
		{
			gl->sbuf->staging_buffers[i].current_offset = 0;
			gl->sbuf->staging_buffers[i].submitted = false;

			err = vkCreateBuffer(vulkan_globals.device, &buffer_create_info, NULL, &gl->sbuf->staging_buffers[i].buffer);
			if (err != VK_SUCCESS)
				SDL_LogError(SDL_LOG_PRIORITY_ERROR,"vkCreateBuffer failed");

			gl->SetObjectName((uint64_t)gl->sbuf->staging_buffers[i].buffer, VK_OBJECT_TYPE_BUFFER, "Staging Buffer");
		}

		VkMemoryRequirements memory_requirements;
		vkGetBufferMemoryRequirements(vulkan_globals.device, gl->sbuf->staging_buffers[0].buffer, &memory_requirements);
		const size_t aligned_size = (memory_requirements.size + memory_requirements.alignment);

		ZEROED_STRUCT(VkMemoryAllocateInfo, memory_allocate_info);
		memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		memory_allocate_info.allocationSize = NUM_STAGING_BUFFERS * aligned_size;
		memory_allocate_info.memoryTypeIndex =
			gl->MemoryTypeFromProperties(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT);

		R_AllocateVulkanMemory(&gl->sbuf->staging_memory, &memory_allocate_info, VULKAN_MEMORY_TYPE_HOST, &gl->num_vulkan_misc_allocations);
		gl->SetObjectName((uint64_t)gl->sbuf->staging_memory.handle, VK_OBJECT_TYPE_DEVICE_MEMORY, "Staging Buffers");

		for (i = 0; i < NUM_STAGING_BUFFERS; ++i)
		{
			err = vkBindBufferMemory(vulkan_globals.device, gl->sbuf->staging_buffers[i].buffer, gl->sbuf->staging_memory.handle, i * aligned_size);
			if (err != VK_SUCCESS)
				SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkBindBufferMemory failed");
		}

		void* data;
		err = vkMapMemory(vulkan_globals.device, gl->sbuf->staging_memory.handle, 0, NUM_STAGING_BUFFERS * aligned_size, 0, &data);
		if (err != VK_SUCCESS)
			SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkMapMemory failed");

		for (i = 0; i < NUM_STAGING_BUFFERS; ++i)
			gl->sbuf->staging_buffers[i].data = (unsigned char*)data + (i * aligned_size);
	}



	byte* R_StagingAllocate(int size, int alignment, VkCommandBuffer* command_buffer, VkBuffer* buffer, int* buffer_offset)
	{
		SDL_LockMutex(gl->staging_mutex);

		while (gl->num_stagings_in_flight > 0)
			SDL_CondWait(gl->staging_cond, gl->staging_mutex);

		vulkan_globals.device_idle = false;

		if (size > vulkan_globals.staging_buffer_size)
		{
			R_SubmitStagingBuffers();

			for (int i = 0; i < NUM_STAGING_BUFFERS; ++i)
				R_FlushStagingCommandBuffer(&gl->sbuf->staging_buffers[i]);

			vulkan_globals.staging_buffer_size = size;

			R_DestroyStagingBuffers();
			R_CreateStagingBuffers();
		}

		stagingbuffer_t* staging_buffer = &gl->sbuf->staging_buffers[gl->sbuf->current_staging_buffer];
		assert(alignment == Q_nextPow2(alignment));
		staging_buffer->current_offset = (staging_buffer->current_offset + alignment);

		if ((staging_buffer->current_offset + size) >= vulkan_globals.staging_buffer_size && !staging_buffer->submitted)
			R_SubmitStagingBuffer(gl->sbuf->current_staging_buffer);

		staging_buffer = & gl->sbuf->staging_buffers[gl->sbuf->current_staging_buffer];
		R_FlushStagingCommandBuffer(staging_buffer);

		if (command_buffer)
			*command_buffer = staging_buffer->command_buffer;
		if (buffer)
			*buffer = staging_buffer->buffer;
		if (buffer_offset)
			*buffer_offset = staging_buffer->current_offset;

		unsigned char* data = staging_buffer->data + staging_buffer->current_offset;
		staging_buffer->current_offset += size;
		gl->sbuf->num_stagings_in_flight += 1;

		return (byte*)data;
	}

	void R_StagingBeginCopy(void)
	{
		SDL_UnlockMutex(gl->staging_mutex);
	}

	void R_StagingEndCopy(void)
	{
		SDL_LockMutex(gl->staging_mutex);
		gl->num_stagings_in_flight -= 1;
		SDL_CondBroadcast(gl->staging_cond);
		SDL_UnlockMutex(gl->staging_mutex);
	}


	void R_InitFanIndexBuffer()
	{
		VkResult	   err;
		VkDeviceMemory memory;
		const int	   bufferSize = sizeof(uint16_t) * FAN_INDEX_BUFFER_SIZE;

		ZEROED_STRUCT(VkBufferCreateInfo, buffer_create_info);
		buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		buffer_create_info.size = bufferSize;
		buffer_create_info.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

		err = vkCreateBuffer(vulkan_globals.device, &buffer_create_info, NULL, &vulkan_globals.fan_index_buffer);
		if (err != VK_SUCCESS)
			SDL_LogError(SDL_LOG_PRIORITY_ERROR,"vkCreateBuffer failed");

		gl->SetObjectName((uint64_t)vulkan_globals.fan_index_buffer, VK_OBJECT_TYPE_BUFFER, "Quad Index Buffer");

		VkMemoryRequirements memory_requirements;
		vkGetBufferMemoryRequirements(vulkan_globals.device, vulkan_globals.fan_index_buffer, &memory_requirements);

		ZEROED_STRUCT(VkMemoryAllocateInfo, memory_allocate_info);
		memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		memory_allocate_info.allocationSize = memory_requirements.size;
		memory_allocate_info.memoryTypeIndex = gl->MemoryTypeFromProperties(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);

		(gl->num_vulkan_dynbuf_allocations++);
		(gl->total_device_vulkan_allocation_size += memory_requirements.size);
		err = vkAllocateMemory(vulkan_globals.device, &memory_allocate_info, NULL, &memory);
		if (err != VK_SUCCESS)
			SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkAllocateMemory failed");

		err = vkBindBufferMemory(vulkan_globals.device, vulkan_globals.fan_index_buffer, memory, 0);
		if (err != VK_SUCCESS)
			SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkBindBufferMemory failed");

		{
			VkBuffer		staging_buffer;
			VkCommandBuffer command_buffer;
			int				staging_offset;
			int				current_index = 0;
			int				i;
			uint16_t* staging_mem = (uint16_t*)R_StagingAllocate(bufferSize, 1, &command_buffer, &staging_buffer, &staging_offset);

			VkBufferCopy region;
			region.srcOffset = staging_offset;
			region.dstOffset = 0;
			region.size = bufferSize;
			vkCmdCopyBuffer(command_buffer, staging_buffer, vulkan_globals.fan_index_buffer, 1, &region);

			R_StagingBeginCopy();
			for (i = 0; i < FAN_INDEX_BUFFER_SIZE / 3; ++i)
			{
				staging_mem[current_index++] = 0;
				staging_mem[current_index++] = 1 + i;
				staging_mem[current_index++] = 2 + i;
			}
			R_StagingEndCopy();
		}
	}

	VkDescriptorSet R_AllocateDescriptorSet(vulkan_desc_set_layout_t* layout)
	{
		ZEROED_STRUCT(VkDescriptorSetAllocateInfo, descriptor_set_allocate_info);
		descriptor_set_allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		descriptor_set_allocate_info.descriptorPool = vulkan_globals.descriptor_pool;
		descriptor_set_allocate_info.descriptorSetCount = 1;
		descriptor_set_allocate_info.pSetLayouts = &layout->handle;

		VkDescriptorSet handle;
		vkAllocateDescriptorSets(vulkan_globals.device, &descriptor_set_allocate_info, &handle);

		(gl->num_vulkan_combined_image_samplers += layout->num_combined_image_samplers);
		(gl->num_vulkan_ubos_dynamic += layout->num_ubos_dynamic);
		(gl->num_vulkan_ubos += layout->num_ubos);
		(gl->num_vulkan_storage_buffers += layout->num_storage_buffers);
		(gl->num_vulkan_input_attachments+= layout->num_input_attachments);
		(gl->num_vulkan_storage_images+= layout->num_storage_images);
		(gl->num_vulkan_sampled_images+= layout->num_sampled_images);
		(gl->num_acceleration_structures+= layout->num_acceleration_structures);

		return handle;
	}

	void R_InitSamplers()
	{
		gl->WaitForDeviceIdle();
		SDL_Log("Initializing samplers\n");

		VkResult err;

		if (vulkan_globals.point_sampler == VK_NULL_HANDLE)
		{
			ZEROED_STRUCT(VkSamplerCreateInfo, sampler_create_info);
			sampler_create_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
			sampler_create_info.magFilter = VK_FILTER_NEAREST;
			sampler_create_info.minFilter = VK_FILTER_NEAREST;
			sampler_create_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
			sampler_create_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			sampler_create_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			sampler_create_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			sampler_create_info.mipLodBias = 0.0f;
			sampler_create_info.maxAnisotropy = 1.0f;
			sampler_create_info.minLod = 0;
			sampler_create_info.maxLod = FLT_MAX;

			err = vkCreateSampler(vulkan_globals.device, &sampler_create_info, NULL, &vulkan_globals.point_sampler);
			if (err != VK_SUCCESS)
				SDL_LogError(SDL_LOG_PRIORITY_ERROR,"vkCreateSampler failed");

			gl->SetObjectName((uint64_t)vulkan_globals.point_sampler, VK_OBJECT_TYPE_SAMPLER, "point");

			sampler_create_info.anisotropyEnable = VK_TRUE;
			sampler_create_info.maxAnisotropy = vulkan_globals.device_properties.limits.maxSamplerAnisotropy;
			err = vkCreateSampler(vulkan_globals.device, &sampler_create_info, NULL, &vulkan_globals.point_aniso_sampler);
			if (err != VK_SUCCESS)
				SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreateSampler failed");

			gl->SetObjectName((uint64_t)vulkan_globals.point_aniso_sampler, VK_OBJECT_TYPE_SAMPLER, "point_aniso");

			sampler_create_info.magFilter = VK_FILTER_LINEAR;
			sampler_create_info.minFilter = VK_FILTER_LINEAR;
			sampler_create_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
			sampler_create_info.anisotropyEnable = VK_FALSE;
			sampler_create_info.maxAnisotropy = 1.0f;

			err = vkCreateSampler(vulkan_globals.device, &sampler_create_info, NULL, &vulkan_globals.linear_sampler);
			if (err != VK_SUCCESS)
				SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreateSampler failed");

			gl->SetObjectName((uint64_t)vulkan_globals.linear_sampler, VK_OBJECT_TYPE_SAMPLER, "linear");

			sampler_create_info.anisotropyEnable = VK_TRUE;
			sampler_create_info.maxAnisotropy = vulkan_globals.device_properties.limits.maxSamplerAnisotropy;
			err = vkCreateSampler(vulkan_globals.device, &sampler_create_info, NULL, &vulkan_globals.linear_aniso_sampler);
			if (err != VK_SUCCESS)
				SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreateSampler failed");

			gl->SetObjectName((uint64_t)vulkan_globals.linear_aniso_sampler, VK_OBJECT_TYPE_SAMPLER, "linear_aniso");
		}

		if (vulkan_globals.point_sampler_lod_bias != VK_NULL_HANDLE)
		{
			vkDestroySampler(vulkan_globals.device, vulkan_globals.point_sampler_lod_bias, NULL);
			vkDestroySampler(vulkan_globals.device, vulkan_globals.point_aniso_sampler_lod_bias, NULL);
			vkDestroySampler(vulkan_globals.device, vulkan_globals.linear_sampler_lod_bias, NULL);
			vkDestroySampler(vulkan_globals.device, vulkan_globals.linear_aniso_sampler_lod_bias, NULL);
		}

		{
			float lod_bias = 0.0f;
			if (r_lodbias.value)
			{
				if (vulkan_globals.supersampling)
				{
					switch (vulkan_globals.sample_count)
					{
					case VK_SAMPLE_COUNT_2_BIT:
						lod_bias -= 0.5f;
						break;
					case VK_SAMPLE_COUNT_4_BIT:
						lod_bias -= 1.0f;
						break;
					case VK_SAMPLE_COUNT_8_BIT:
						lod_bias -= 1.5f;
						break;
					case VK_SAMPLE_COUNT_16_BIT:
						lod_bias -= 2.0f;
						break;
					default: /* silences gcc's -Wswitch */
						break;
					}
				}

				if (r_scale.value >= 8)
					lod_bias += 3.0f;
				else if (r_scale.value >= 4)
					lod_bias += 2.0f;
				else if (r_scale.value >= 2)
					lod_bias += 1.0f;
			}

			lod_bias += gl_lodbias.value;

			SDL_Log("Texture lod bias: %f\n", lod_bias);

			ZEROED_STRUCT(VkSamplerCreateInfo, sampler_create_info);
			sampler_create_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
			sampler_create_info.magFilter = VK_FILTER_NEAREST;
			sampler_create_info.minFilter = VK_FILTER_NEAREST;
			sampler_create_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
			sampler_create_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			sampler_create_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			sampler_create_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			sampler_create_info.mipLodBias = lod_bias;
			sampler_create_info.maxAnisotropy = 1.0f;
			sampler_create_info.minLod = 0;
			sampler_create_info.maxLod = FLT_MAX;

			err = vkCreateSampler(vulkan_globals.device, &sampler_create_info, NULL, &vulkan_globals.point_sampler_lod_bias);
			if (err != VK_SUCCESS)
				SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreateSampler failed");

			gl->SetObjectName((uint64_t)vulkan_globals.point_sampler_lod_bias, VK_OBJECT_TYPE_SAMPLER, "point_lod_bias");

			sampler_create_info.anisotropyEnable = VK_TRUE;
			sampler_create_info.maxAnisotropy = vulkan_globals.device_properties.limits.maxSamplerAnisotropy;
			err = vkCreateSampler(vulkan_globals.device, &sampler_create_info, NULL, &vulkan_globals.point_aniso_sampler_lod_bias);
			if (err != VK_SUCCESS)
				SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreateSampler failed");

			gl->SetObjectName((uint64_t)vulkan_globals.point_aniso_sampler_lod_bias, VK_OBJECT_TYPE_SAMPLER, "point_aniso_lod_bias");

			sampler_create_info.magFilter = VK_FILTER_LINEAR;
			sampler_create_info.minFilter = VK_FILTER_LINEAR;
			sampler_create_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
			sampler_create_info.anisotropyEnable = VK_FALSE;
			sampler_create_info.maxAnisotropy = 1.0f;

			err = vkCreateSampler(vulkan_globals.device, &sampler_create_info, NULL, &vulkan_globals.linear_sampler_lod_bias);
			if (err != VK_SUCCESS)
				SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreateSampler failed");

			gl->SetObjectName((uint64_t)vulkan_globals.linear_sampler_lod_bias, VK_OBJECT_TYPE_SAMPLER, "linear_lod_bias");

			sampler_create_info.anisotropyEnable = VK_TRUE;
			sampler_create_info.maxAnisotropy = vulkan_globals.device_properties.limits.maxSamplerAnisotropy;
			err = vkCreateSampler(vulkan_globals.device, &sampler_create_info, NULL, &vulkan_globals.linear_aniso_sampler_lod_bias);
			if (err != VK_SUCCESS)
				SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreateSampler failed");

			gl->SetObjectName((uint64_t)vulkan_globals.linear_aniso_sampler_lod_bias, VK_OBJECT_TYPE_SAMPLER, "linear_aniso_lod_bias");
		}

		TexMgr_UpdateTextureDescriptorSets();
	}

	void TexMgr_SetFilterModes(gltexture_t* glt)
	{
		ZEROED_STRUCT(VkDescriptorImageInfo, image_info);
		image_info.imageView = glt->image_view;
		image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		bool enable_anisotropy = vid_anisotropic.value && !(glt->flags & TEXPREF_NOPICMIP);

		VkSampler point_sampler = enable_anisotropy ? vulkan_globals.point_aniso_sampler_lod_bias : vulkan_globals.point_sampler_lod_bias;
		VkSampler linear_sampler = enable_anisotropy ? vulkan_globals.linear_aniso_sampler_lod_bias : vulkan_globals.linear_sampler_lod_bias;

		if (glt->flags & TEXPREF_NEAREST)
			image_info.sampler = point_sampler;
		else if (glt->flags & TEXPREF_LINEAR)
			image_info.sampler = linear_sampler;
		else
			image_info.sampler = (vid_filter.value == 1) ? point_sampler : linear_sampler;

		ZEROED_STRUCT(VkWriteDescriptorSet, texture_write);
		texture_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		texture_write.dstSet = glt->descriptor_set;
		texture_write.dstBinding = 0;
		texture_write.dstArrayElement = 0;
		texture_write.descriptorCount = 1;
		texture_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		texture_write.pImageInfo = &image_info;

		vkUpdateDescriptorSets(vulkan_globals.device, 1, &texture_write, 0, NULL);
	}


	void TexMgr_UpdateTextureDescriptorSets(void)
	{
		gltexture_t* glt;

		for (glt = active_gltextures; glt; glt = glt->next)
			TexMgr_SetFilterModes(glt);
	}

	void R_InitDynamicUniformBuffers(void)
	{
		R_InitDynamicBuffers(
			gl->dyn_uniform_buffers, &gl->dyn_uniform_buffer_memory, &gl->current_dyn_uniform_buffer_size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, false, "uniform buffer");

		ZEROED_STRUCT(VkDescriptorSetAllocateInfo, descriptor_set_allocate_info);
		descriptor_set_allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		descriptor_set_allocate_info.descriptorPool = vulkan_globals.descriptor_pool;
		descriptor_set_allocate_info.descriptorSetCount = 1;
		descriptor_set_allocate_info.pSetLayouts = &vulkan_globals.ubo_set_layout.handle;

		gl->ubo_descriptor_sets[0] = R_AllocateDescriptorSet(&vulkan_globals.ubo_set_layout);
		gl->ubo_descriptor_sets[1] = R_AllocateDescriptorSet(&vulkan_globals.ubo_set_layout);

		ZEROED_STRUCT(VkDescriptorBufferInfo, buffer_info);
		buffer_info.offset = 0;
		buffer_info.range = MAX_UNIFORM_ALLOC;

		ZEROED_STRUCT(VkWriteDescriptorSet, ubo_write);
		ubo_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		ubo_write.dstBinding = 0;
		ubo_write.dstArrayElement = 0;
		ubo_write.descriptorCount = 1;
		ubo_write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
		ubo_write.pBufferInfo = &buffer_info;

		for (int i = 0; i < NUM_DYNAMIC_BUFFERS; ++i)
		{
			buffer_info.buffer = gl->dyn_uniform_buffers[i].buffer;
			ubo_write.dstSet = gl->ubo_descriptor_sets[i];
			vkUpdateDescriptorSets(vulkan_globals.device, 1, &ubo_write, 0, NULL);
		}
	}


	void R_InitDynamicIndexBuffers(void)
	{
		R_InitDynamicBuffers(gl->dyn_index_buffers, &gl->dyn_index_buffer_memory, &gl->current_dyn_index_buffer_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, false, "index buffer");
	}


	void R_InitDynamicVertexBuffers(void)
	{
		R_InitDynamicBuffers(
			gl->dyn_vertex_buffers, &gl->dyn_vertex_buffer_memory, &gl->current_dyn_vertex_buffer_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, false, "vertex buffer");
	}

	void R_AllocateVulkanMemory(vulkan_memory_t* memory, VkMemoryAllocateInfo* memory_allocate_info, vulkan_memory_type_t type, atomic_uint32_t* num_allocations)
	{
		memory->type = type;
		if (memory->type != VULKAN_MEMORY_TYPE_NONE)
		{
			VkResult err = vkAllocateMemory(vulkan_globals.device, memory_allocate_info, NULL, &memory->handle);
			if (err != VK_SUCCESS)
				SDL_LogError(SDL_LOG_PRIORITY_ERROR,"vkAllocateMemory failed");
			if (num_allocations)
				(num_allocations++);
		}
		memory->size = memory_allocate_info->allocationSize;
		if (memory->type == VULKAN_MEMORY_TYPE_DEVICE)
			gl->total_device_vulkan_allocation_size += memory->size;
		else if (memory->type == VULKAN_MEMORY_TYPE_HOST)
			gl->total_host_vulkan_allocation_size += memory->size;
	}

	void R_InitDynamicBuffers(
		dynbuffer_t* buffers, vulkan_memory_t* memory, uint32_t* current_size, VkBufferUsageFlags usage_flags, bool get_device_address, const char* name)
	{
		int i;

		SDL_Log("Reallocating dynamic %ss (%u KB)\n", name, *current_size / 1024);

		VkResult err;

		if (get_device_address)
			usage_flags |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT_KHR;

		ZEROED_STRUCT(VkBufferCreateInfo, buffer_create_info);
		buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		buffer_create_info.size = *current_size;
		buffer_create_info.usage = usage_flags;

		for (i = 0; i < NUM_DYNAMIC_BUFFERS; ++i)
		{
			buffers[i].current_offset = 0;

			err = vkCreateBuffer(vulkan_globals.device, &buffer_create_info, NULL, &buffers[i].buffer);
			if (err != VK_SUCCESS)
				SDL_LogError(SDL_LOG_PRIORITY_ERROR,"vkCreateBuffer failed");

			gl->SetObjectName((uint64_t)buffers[i].buffer, VK_OBJECT_TYPE_BUFFER, name);
		}

		VkMemoryRequirements memory_requirements;
		vkGetBufferMemoryRequirements(vulkan_globals.device, buffers[0].buffer, &memory_requirements);

		const size_t aligned_size = memory_requirements.size + memory_requirements.alignment;

		ZEROED_STRUCT(VkMemoryAllocateFlagsInfo, memory_allocate_flags_info);
		if (get_device_address)
		{
			memory_allocate_flags_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO_KHR;
			memory_allocate_flags_info.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
		}

		ZEROED_STRUCT(VkMemoryAllocateInfo, memory_allocate_info);
		memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		memory_allocate_info.pNext = get_device_address ? &memory_allocate_flags_info : NULL;
		memory_allocate_info.allocationSize = NUM_DYNAMIC_BUFFERS * aligned_size;
		memory_allocate_info.memoryTypeIndex =
			gl->MemoryTypeFromProperties(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT);

		R_AllocateVulkanMemory(memory, &memory_allocate_info, VULKAN_MEMORY_TYPE_HOST, &gl->num_vulkan_dynbuf_allocations);
		gl->SetObjectName((uint64_t)memory->handle, VK_OBJECT_TYPE_DEVICE_MEMORY, name);

		for (i = 0; i < NUM_DYNAMIC_BUFFERS; ++i)
		{
			err = vkBindBufferMemory(vulkan_globals.device, buffers[i].buffer, memory->handle, i * aligned_size);
			if (err != VK_SUCCESS)
				SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkBindBufferMemory failed");
		}

		void* data;
		err = vkMapMemory(vulkan_globals.device, memory->handle, 0, NUM_DYNAMIC_BUFFERS * aligned_size, 0, &data);
		if (err != VK_SUCCESS)
			SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkMapMemory failed");

		for (i = 0; i < NUM_DYNAMIC_BUFFERS; ++i)
		{
			buffers[i].data = (unsigned char*)data + (i * aligned_size);

			if (get_device_address)
			{
				VkBufferDeviceAddressInfoKHR buffer_device_address_info;
				memset(&buffer_device_address_info, 0, sizeof(buffer_device_address_info));
				buffer_device_address_info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO_KHR;
				buffer_device_address_info.buffer = buffers[i].buffer;
				VkDeviceAddress device_address = vulkan_globals.vk_get_buffer_device_address(vulkan_globals.device, &buffer_device_address_info);
				buffers[i].device_address = device_address;
			}
		}
	}


	class GL {
	public:
		Engine* engine;

		void SynchronizeEndRenderingTask(void)
		{
			if (prev_end_rendering_task != INVALID_TASK_HANDLE)
			{
				//Task_Join(prev_end_rendering_task, SDL_MUTEX_MAXWAIT);
				prev_end_rendering_task = INVALID_TASK_HANDLE;
			}
		}

		void WaitForDeviceIdle(void)
		{
			//assert(!Tasks_IsWorker());
			SynchronizeEndRenderingTask();
			if (!vulkan_globals.device_idle)
			{
				engine->R_SubmitStagingBuffers();
				vkDeviceWaitIdle(vulkan_globals.device);
			}

			vulkan_globals.device_idle = true;
		}

		glheap_t* HeapCreate(VkDeviceSize segment_size, uint32_t page_size, uint32_t memory_type_index, vulkan_memory_type_t memory_type, const char* heap_name)
		{
			assert(Q_nextPow2(page_size) == page_size);
			assert(page_size >= (1 << (NUM_SMALL_ALLOC_SIZES + 1)));
			assert(segment_size >= page_size);
			assert((segment_size % page_size) == 0);
			assert((segment_size / page_size) <= MAX_PAGES);
			glheap_t* heap = (glheap_t*)Mem_Alloc(sizeof(glheap_t));
			heap->segment_size = segment_size;
			heap->num_pages_per_segment = segment_size / page_size;
			heap->num_masks_per_segment = (heap->num_pages_per_segment + 63) / 64;
			heap->page_size = page_size;
			heap->min_small_alloc_size = heap->page_size / 64;
			heap->page_size_shift = Q_log2(page_size);
			heap->small_alloc_shift = Q_log2(page_size / (1 << NUM_SMALL_ALLOC_SIZES));
			heap->memory_type_index = memory_type_index;
			heap->memory_type = memory_type;
			heap->name = heap_name;
			return heap;
		}

		void SetObjectName(uint64_t object, VkObjectType object_type, const char* name)
		{
#ifdef _DEBUG
			if (fpSetDebugUtilsObjectNameEXT && name)
			{
				ZEROED_STRUCT(VkDebugUtilsObjectNameInfoEXT, nameInfo);
				nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
				nameInfo.objectType = object_type;
				nameInfo.objectHandle = object;
				nameInfo.pObjectName = name;
				fpSetDebugUtilsObjectNameEXT(vulkan_globals.device, &nameInfo);
			};
#endif
		}


		task_handle_t prev_end_rendering_task = INVALID_TASK_HANDLE;

		uint32_t		   current_dyn_vertex_buffer_size = INITIAL_DYNAMIC_VERTEX_BUFFER_SIZE_KB * 1024;
		uint32_t		   current_dyn_index_buffer_size = INITIAL_DYNAMIC_INDEX_BUFFER_SIZE_KB * 1024;
		uint32_t		   current_dyn_uniform_buffer_size = INITIAL_DYNAMIC_UNIFORM_BUFFER_SIZE_KB * 1024;
		uint32_t		   current_dyn_storage_buffer_size = 0; // Only used for RT so allocate lazily
		vulkan_memory_t dyn_vertex_buffer_memory;
		vulkan_memory_t dyn_index_buffer_memory;
		vulkan_memory_t dyn_uniform_buffer_memory;
		vulkan_memory_t dyn_storage_buffer_memory;
		vulkan_memory_t lights_buffer_memory;
		dynbuffer_t	   dyn_vertex_buffers[NUM_DYNAMIC_BUFFERS];
		dynbuffer_t	   dyn_index_buffers[NUM_DYNAMIC_BUFFERS];
		dynbuffer_t	   dyn_uniform_buffers[NUM_DYNAMIC_BUFFERS];
		dynbuffer_t	   dyn_storage_buffers[NUM_DYNAMIC_BUFFERS];
		int			   current_dyn_buffer_index = 0;
		VkDescriptorSet ubo_descriptor_sets[2];

		int				current_garbage_index = 0;
		int				num_device_memory_garbage[GARBAGE_FRAME_COUNT];
		int				num_buffer_garbage[GARBAGE_FRAME_COUNT];
		int				num_desc_set_garbage[GARBAGE_FRAME_COUNT];
		vulkan_memory_t* device_memory_garbage[GARBAGE_FRAME_COUNT];
		VkDescriptorSet* descriptor_set_garbage[GARBAGE_FRAME_COUNT];
		VkBuffer* buffer_garbage[GARBAGE_FRAME_COUNT];


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

		VkCommandPool   staging_command_pool;
		vulkan_memory_t staging_memory;
		int			   current_staging_buffer = 0;
		int			   num_stagings_in_flight = 0;
		SDL_mutex* staging_mutex;
		SDL_cond* staging_cond;

		atomic_uint32_t num_vulkan_tex_allocations;
		atomic_uint32_t num_vulkan_bmodel_allocations;
		atomic_uint32_t num_vulkan_mesh_allocations;
		atomic_uint32_t num_vulkan_misc_allocations;
		atomic_uint32_t num_vulkan_dynbuf_allocations;
		atomic_uint32_t num_vulkan_combined_image_samplers;
		atomic_uint32_t num_vulkan_ubos_dynamic;
		atomic_uint32_t num_vulkan_ubos;
		atomic_uint32_t num_vulkan_storage_buffers;
		atomic_uint32_t num_vulkan_input_attachments;
		atomic_uint32_t num_vulkan_storage_images;
		atomic_uint32_t num_vulkan_sampled_images;
		atomic_uint32_t num_acceleration_structures;
		atomic_uint64_t total_device_vulkan_allocation_size;
		atomic_uint64_t total_host_vulkan_allocation_size;

		bool use_simd;

		SDL_mutex* vertex_allocate_mutex;
		SDL_mutex* index_allocate_mutex;
		SDL_mutex* uniform_allocate_mutex;
		SDL_mutex* storage_allocate_mutex;
		SDL_mutex* garbage_mutex;


#ifdef _DEBUG
		PFN_vkCreateDebugUtilsMessengerEXT fpCreateDebugUtilsMessengerEXT;
		PFN_vkSetDebugUtilsObjectNameEXT		  fpSetDebugUtilsObjectNameEXT;

		VkDebugUtilsMessengerEXT debug_utils_messenger;

#endif

		int MemoryTypeFromProperties(uint32_t type_bits, VkFlags requirements_mask, VkFlags preferred_mask)
		{
			uint32_t current_type_bits = type_bits;
			uint32_t i;

			for (i = 0; i < VK_MAX_MEMORY_TYPES; i++)
			{
				if ((current_type_bits & 1) == 1)
				{
					if ((vulkan_globals.memory_properties.memoryTypes[i].propertyFlags & (requirements_mask | preferred_mask)) == (requirements_mask | preferred_mask))
						return i;
				}
				current_type_bits >>= 1;
			}

			current_type_bits = type_bits;
			for (i = 0; i < VK_MAX_MEMORY_TYPES; i++)
			{
				if ((current_type_bits & 1) == 1)
				{
					if ((vulkan_globals.memory_properties.memoryTypes[i].propertyFlags & requirements_mask) == requirements_mask)
						return i;
				}
				current_type_bits >>= 1;
			}

			SDL_LogError(SDL_LOG_PRIORITY_ERROR,"Could not find memory type");
			return 0;
		}


		class Instance {
		public:
			Engine* engine;


			Instance(Engine e) {
				engine = &e;
				engine->gl->instance = this;

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
				engine->gl->fpGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)SDL_Vulkan_GetVkGetInstanceProcAddr();
				GET_INSTANCE_PROC_ADDR(EnumerateInstanceVersion);
				if (engine->gl->fpEnumerateInstanceVersion)
				{
					uint32_t api_version = 0;
					engine->gl->fpEnumerateInstanceVersion(&api_version);
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

				err = vkCreateInstance(&instance_create_info, NULL, &engine->gl->vulkan_instance);
				if (err != VK_SUCCESS)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR, "Couldn't create Vulkan instance");

				if (!SDL_Vulkan_CreateSurface(engine->vid->draw_context, engine->gl->vulkan_instance, &engine->gl->vulkan_surface))
					SDL_LogError(SDL_LOG_PRIORITY_ERROR, "Couldn't create Vulkan surface");

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
					if (engine->gl->fpCreateDebugUtilsMessengerEXT)
					{
						ZEROED_STRUCT(VkDebugUtilsMessengerCreateInfoEXT, debug_utils_messenger_create_info);
						debug_utils_messenger_create_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
						debug_utils_messenger_create_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
						debug_utils_messenger_create_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
						debug_utils_messenger_create_info.pfnUserCallback = DebugMessageCallback;

						err = engine->gl->fpCreateDebugUtilsMessengerEXT(engine->gl->vulkan_instance, &debug_utils_messenger_create_info, NULL, &engine->gl->debug_utils_messenger);
						if (err != VK_SUCCESS)
							SDL_LogError(SDL_LOG_PRIORITY_ERROR, "Could not create debug report callback");
					}
				}
#endif

				Mem_Free((void*)instance_extensions);
			}

		};
		class CommandBuffers {
		public:
			Engine* engine;
			CommandBuffers(Engine e) {
				engine = &e;
				engine->gl->cbuf = this;

				SDL_Log("Creating command buffers\n");

				VkResult err;

				{
					ZEROED_STRUCT(VkCommandPoolCreateInfo, command_pool_create_info);
					command_pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
					command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
					command_pool_create_info.queueFamilyIndex = vulkan_globals.gfx_queue_family_index;
					err = vkCreateCommandPool(vulkan_globals.device, &command_pool_create_info, NULL, &engine->gl->transient_command_pool);
					if (err != VK_SUCCESS)
						SDL_LogError(SDL_LOG_PRIORITY_ERROR,"vkCreateCommandPool failed");
				}

				ZEROED_STRUCT(VkCommandPoolCreateInfo, command_pool_create_info);
				command_pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
				command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
				command_pool_create_info.queueFamilyIndex = vulkan_globals.gfx_queue_family_index;

				for (int pcbx_index = 0; pcbx_index < PCBX_NUM; ++pcbx_index)
				{
					err = vkCreateCommandPool(vulkan_globals.device, &command_pool_create_info, NULL, &engine->gl->primary_command_pools[pcbx_index]);
					if (err != VK_SUCCESS)
						SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreateCommandPool failed");

					ZEROED_STRUCT(VkCommandBufferAllocateInfo, command_buffer_allocate_info);
					command_buffer_allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
					command_buffer_allocate_info.commandPool = engine->gl->primary_command_pools[pcbx_index];
					command_buffer_allocate_info.commandBufferCount = DOUBLE_BUFFERED;

					err = vkAllocateCommandBuffers(vulkan_globals.device, &command_buffer_allocate_info, engine->gl->primary_command_buffers[pcbx_index]);
					if (err != VK_SUCCESS)
						SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkAllocateCommandBuffers failed");
					for (int i = 0; i < DOUBLE_BUFFERED; ++i)
						engine->gl->SetObjectName(
							(uint64_t)(uintptr_t)engine->gl->primary_command_buffers[pcbx_index][i], VK_OBJECT_TYPE_COMMAND_BUFFER, va("PCBX index: %d cb_index: %d", pcbx_index, i));
				}

				for (int scbx_index = 0; scbx_index < SCBX_NUM; ++scbx_index)
				{
					const int multiplicity = SECONDARY_CB_MULTIPLICITY[scbx_index];
					vulkan_globals.secondary_cb_contexts[scbx_index] = (cb_context_t*) Mem_Alloc(multiplicity * sizeof(cb_context_t));
					engine->gl->secondary_command_pools[scbx_index] = (VkCommandPool*)Mem_Alloc(multiplicity * sizeof(VkCommandPool));
					for (int i = 0; i < DOUBLE_BUFFERED; ++i)
						engine->gl->secondary_command_buffers[scbx_index][i] = (VkCommandBuffer*)Mem_Alloc(multiplicity * sizeof(VkCommandBuffer));
					for (int i = 0; i < multiplicity; ++i)
					{
						err = vkCreateCommandPool(vulkan_globals.device, &command_pool_create_info, NULL, &engine->gl->secondary_command_pools[scbx_index][i]);
						if (err != VK_SUCCESS)
							SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreateCommandPool failed");

						ZEROED_STRUCT(VkCommandBufferAllocateInfo, command_buffer_allocate_info);
						command_buffer_allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
						command_buffer_allocate_info.commandPool = engine->gl->secondary_command_pools[scbx_index][i];
						command_buffer_allocate_info.commandBufferCount = DOUBLE_BUFFERED;
						command_buffer_allocate_info.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;

						VkCommandBuffer command_buffers[DOUBLE_BUFFERED];
						err = vkAllocateCommandBuffers(vulkan_globals.device, &command_buffer_allocate_info, command_buffers);
						if (err != VK_SUCCESS)
							SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkAllocateCommandBuffers failed");
						for (int j = 0; j < DOUBLE_BUFFERED; ++j)
						{
							engine->gl->secondary_command_buffers[scbx_index][j][i] = command_buffers[j];
							engine->gl->SetObjectName(
								(uint64_t)(uintptr_t)command_buffers[j], VK_OBJECT_TYPE_COMMAND_BUFFER, va("SCBX index: %d sub_index: %d cb_index: %d", scbx_index, i, j));
						}
					}
				}

				ZEROED_STRUCT(VkFenceCreateInfo, fence_create_info);
				fence_create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

				for (int i = 0; i < DOUBLE_BUFFERED; ++i)
				{
					err = vkCreateFence(vulkan_globals.device, &fence_create_info, NULL, &engine->gl->command_buffer_fences[i]);
					if (err != VK_SUCCESS)
						SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreateFence failed");

					ZEROED_STRUCT(VkSemaphoreCreateInfo, semaphore_create_info);
					semaphore_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
					err = vkCreateSemaphore(vulkan_globals.device, &semaphore_create_info, NULL, &engine->gl->draw_complete_semaphores[i]);
				}

				
			}
		};
		class Device {
		public:
			Engine* engine;
			Device(Engine e) {
				engine = &e;
				engine->gl->device = this;

				VkResult err;
				uint32_t i;
				int		 arg_index;
				int		 device_index = 0;

				bool subgroup_size_control = false;

				uint32_t physical_device_count;
				err = vkEnumeratePhysicalDevices(engine->gl->vulkan_instance, &physical_device_count, NULL);
				if (err != VK_SUCCESS || physical_device_count == 0)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR,"Couldn't find any Vulkan devices");

				arg_index = engine->com->CheckParm("-device");
				if (arg_index && (arg_index < (engine->com->argc - 1)))
				{
					const char* device_num = engine->com->argv[arg_index + 1];
					device_index = CLAMP(0, atoi(device_num) - 1, (int)physical_device_count - 1);
				}

				VkPhysicalDevice* physical_devices = (VkPhysicalDevice*)Mem_Alloc(sizeof(VkPhysicalDevice) * physical_device_count);
				vkEnumeratePhysicalDevices(engine->gl->vulkan_instance, &physical_device_count, physical_devices);
				if (!arg_index)
				{
					// If no device was specified by command line pick first discrete GPU
					for (i = 0; i < physical_device_count; ++i)
					{
						VkPhysicalDeviceProperties device_properties;
						vkGetPhysicalDeviceProperties(physical_devices[i], &device_properties);
						if (device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
						{
							device_index = (int)i;
							break;
						}
					}
				}
				engine->gl->vulkan_physical_device = physical_devices[device_index];
				Mem_Free(physical_devices);

				bool found_swapchain_extension = false;
				vulkan_globals.dedicated_allocation = false;
				vulkan_globals.full_screen_exclusive = false;
				vulkan_globals.swap_chain_full_screen_acquired = false;
				vulkan_globals.screen_effects_sops = false;
				vulkan_globals.ray_query = false;

				vkGetPhysicalDeviceMemoryProperties(engine->gl->vulkan_physical_device, &vulkan_globals.memory_properties);
				vkGetPhysicalDeviceProperties(engine->gl->vulkan_physical_device, &vulkan_globals.device_properties);

				bool driver_properties_available = false;
				uint32_t device_extension_count;
				err = vkEnumerateDeviceExtensionProperties(engine->gl->vulkan_physical_device, NULL, &device_extension_count, NULL);

				if (err == VK_SUCCESS || device_extension_count > 0)
				{
					VkExtensionProperties* device_extensions = (VkExtensionProperties*)Mem_Alloc(sizeof(VkExtensionProperties) * device_extension_count);
					err = vkEnumerateDeviceExtensionProperties(engine->gl->vulkan_physical_device, NULL, &device_extension_count, device_extensions);

					for (i = 0; i < device_extension_count; ++i)
					{
						if (strcmp(VK_KHR_SWAPCHAIN_EXTENSION_NAME, device_extensions[i].extensionName) == 0)
							found_swapchain_extension = true;
						if (strcmp(VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME, device_extensions[i].extensionName) == 0)
							vulkan_globals.dedicated_allocation = true;
						if (vulkan_globals.get_physical_device_properties_2 && strcmp(VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME, device_extensions[i].extensionName) == 0)
							driver_properties_available = true;
						if (strcmp(VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME, device_extensions[i].extensionName) == 0)
							subgroup_size_control = true;
#if defined(VK_EXT_full_screen_exclusive)
						if (strcmp(VK_EXT_FULL_SCREEN_EXCLUSIVE_EXTENSION_NAME, device_extensions[i].extensionName) == 0)
							vulkan_globals.full_screen_exclusive = true;
#endif
						if (strcmp(VK_KHR_RAY_QUERY_EXTENSION_NAME, device_extensions[i].extensionName) == 0)
							vulkan_globals.ray_query = true;
					}

					Mem_Free(device_extensions);
				}

				const char* vendor = NULL;
				ZEROED_STRUCT(VkPhysicalDeviceDriverProperties, driver_properties);
				if (driver_properties_available)
				{
					driver_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;

					ZEROED_STRUCT(VkPhysicalDeviceProperties2, physical_device_properties_2);
					physical_device_properties_2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
					physical_device_properties_2.pNext = &driver_properties;
					engine->gl->fpGetPhysicalDeviceProperties2(engine->gl->vulkan_physical_device, &physical_device_properties_2);

					vendor = GetDeviceVendorFromDriverProperties(&driver_properties);
				}

				if (!vendor)
					vendor = GetDeviceVendorFromDeviceProperties();

				if (vendor)
					SDL_Log("Vendor: %s\n", vendor);
				else
					SDL_Log("Vendor: Unknown (0x%x)\n", vulkan_globals.device_properties.vendorID);

				SDL_Log("Device: %s\n", vulkan_globals.device_properties.deviceName);

				if (driver_properties_available)
					SDL_Log("Driver: %s %s\n", driver_properties.driverName, driver_properties.driverInfo);

				if (!found_swapchain_extension)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR,"Couldn't find %s extension", VK_KHR_SWAPCHAIN_EXTENSION_NAME);

				bool found_graphics_queue = false;

				uint32_t vulkan_queue_count;
				vkGetPhysicalDeviceQueueFamilyProperties(engine->gl->vulkan_physical_device, &vulkan_queue_count, NULL);
				if (vulkan_queue_count == 0)
				{
					SDL_LogError(SDL_LOG_PRIORITY_ERROR,"Couldn't find any Vulkan queues");
				}

				VkQueueFamilyProperties* queue_family_properties = (VkQueueFamilyProperties*)Mem_Alloc(vulkan_queue_count * sizeof(VkQueueFamilyProperties));
				vkGetPhysicalDeviceQueueFamilyProperties(engine->gl->vulkan_physical_device, &vulkan_queue_count, queue_family_properties);

				// Iterate over each queue to learn whether it supports presenting:
				VkBool32* queue_supports_present = (VkBool32*)Mem_Alloc(vulkan_queue_count * sizeof(VkBool32));
				for (i = 0; i < vulkan_queue_count; ++i)
					engine->gl->fpGetPhysicalDeviceSurfaceSupportKHR(engine->gl->vulkan_physical_device, i, engine->gl->vulkan_surface, &queue_supports_present[i]);

				for (i = 0; i < vulkan_queue_count; ++i)
				{
					if (((queue_family_properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) && queue_supports_present[i])
					{
						found_graphics_queue = true;
						vulkan_globals.gfx_queue_family_index = i;
						break;
					}
				}

				Mem_Free(queue_supports_present);
				Mem_Free(queue_family_properties);

				if (!found_graphics_queue)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR,"Couldn't find graphics queue");

				float queue_priorities[] = { 0.0 };
				ZEROED_STRUCT(VkDeviceQueueCreateInfo, queue_create_info);
				queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
				queue_create_info.queueFamilyIndex = vulkan_globals.gfx_queue_family_index;
				queue_create_info.queueCount = 1;
				queue_create_info.pQueuePriorities = queue_priorities;

				ZEROED_STRUCT(VkPhysicalDeviceSubgroupProperties, physical_device_subgroup_properties);
				ZEROED_STRUCT(VkPhysicalDeviceSubgroupSizeControlPropertiesEXT, physical_device_subgroup_size_control_properties);
				ZEROED_STRUCT(VkPhysicalDeviceSubgroupSizeControlFeaturesEXT, subgroup_size_control_features);
				ZEROED_STRUCT(VkPhysicalDeviceBufferDeviceAddressFeaturesKHR, buffer_device_address_features);
				ZEROED_STRUCT(VkPhysicalDeviceAccelerationStructureFeaturesKHR, acceleration_structure_features);
				ZEROED_STRUCT(VkPhysicalDeviceRayQueryFeaturesKHR, ray_query_features);
				memset(&vulkan_globals.physical_device_acceleration_structure_properties, 0, sizeof(vulkan_globals.physical_device_acceleration_structure_properties));
				if (vulkan_globals.vulkan_1_1_available)
				{
					ZEROED_STRUCT(VkPhysicalDeviceProperties2KHR, physical_device_properties_2);
					physical_device_properties_2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
					void** device_properties_next = &physical_device_properties_2.pNext;

					if (subgroup_size_control)
					{
						physical_device_subgroup_size_control_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_PROPERTIES_EXT;
						CHAIN_PNEXT(device_properties_next, physical_device_subgroup_size_control_properties);
						physical_device_subgroup_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES;
						CHAIN_PNEXT(device_properties_next, physical_device_subgroup_properties);
					}
					if (vulkan_globals.ray_query)
					{
						vulkan_globals.physical_device_acceleration_structure_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
						CHAIN_PNEXT(device_properties_next, vulkan_globals.physical_device_acceleration_structure_properties);
					}

					engine->gl->fpGetPhysicalDeviceProperties2(engine->gl->vulkan_physical_device, &physical_device_properties_2);

					ZEROED_STRUCT(VkPhysicalDeviceFeatures2, physical_device_features_2);
					physical_device_features_2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
					void** device_features_next = &physical_device_features_2.pNext;

					if (subgroup_size_control)
					{
						subgroup_size_control_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES_EXT;
						CHAIN_PNEXT(device_features_next, subgroup_size_control_features);
					}
					if (vulkan_globals.ray_query)
					{
						buffer_device_address_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_KHR;
						CHAIN_PNEXT(device_features_next, buffer_device_address_features);
						acceleration_structure_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
						CHAIN_PNEXT(device_features_next, acceleration_structure_features);
						ray_query_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
						CHAIN_PNEXT(device_features_next, ray_query_features);
					}

					engine->gl->fpGetPhysicalDeviceFeatures2(engine->gl->vulkan_physical_device, &physical_device_features_2);
					vulkan_globals.device_features = physical_device_features_2.features;
				}
				else
					vkGetPhysicalDeviceFeatures(engine->gl->vulkan_physical_device, &vulkan_globals.device_features);

#ifdef __APPLE__ // MoltenVK lies about this
				vulkan_globals.device_features.sampleRateShading = false;
#endif

				vulkan_globals.screen_effects_sops =
					vulkan_globals.vulkan_1_1_available && subgroup_size_control && subgroup_size_control_features.subgroupSizeControl &&
					subgroup_size_control_features.computeFullSubgroups && ((physical_device_subgroup_properties.supportedStages & VK_SHADER_STAGE_COMPUTE_BIT) != 0) &&
					((physical_device_subgroup_properties.supportedOperations & VK_SUBGROUP_FEATURE_SHUFFLE_BIT) != 0)
					// Shader only supports subgroup sizes from 4 to 64. 128 can't be supported because Vulkan spec states that workgroup size
					// in x dimension must be a multiple of the subgroup size for VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT_EXT.
					&& (physical_device_subgroup_size_control_properties.minSubgroupSize >= 4) && (physical_device_subgroup_size_control_properties.maxSubgroupSize <= 64);
				if (vulkan_globals.screen_effects_sops)
					SDL_Log("Using subgroup operations\n");

				vulkan_globals.ray_query = vulkan_globals.ray_query && acceleration_structure_features.accelerationStructure && ray_query_features.rayQuery &&
					buffer_device_address_features.bufferDeviceAddress;
				if (vulkan_globals.ray_query)
					SDL_Log("Using ray queries\n");

				const char* device_extensions[32] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
				uint32_t	numEnabledExtensions = 1;
				if (vulkan_globals.dedicated_allocation)
				{
					device_extensions[numEnabledExtensions++] = VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME;
					device_extensions[numEnabledExtensions++] = VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME;
				}
				if (vulkan_globals.screen_effects_sops)
					device_extensions[numEnabledExtensions++] = VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME;
#if defined(VK_EXT_full_screen_exclusive)
				if (vulkan_globals.full_screen_exclusive)
					device_extensions[numEnabledExtensions++] = VK_EXT_FULL_SCREEN_EXCLUSIVE_EXTENSION_NAME;
#endif
				if (vulkan_globals.ray_query)
				{
					device_extensions[numEnabledExtensions++] = VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME;
					device_extensions[numEnabledExtensions++] = VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME;
					device_extensions[numEnabledExtensions++] = VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME;
					device_extensions[numEnabledExtensions++] = VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME;
					device_extensions[numEnabledExtensions++] = VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME;
					device_extensions[numEnabledExtensions++] = VK_KHR_SPIRV_1_4_EXTENSION_NAME;
					device_extensions[numEnabledExtensions++] = VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME;
					device_extensions[numEnabledExtensions++] = VK_KHR_RAY_QUERY_EXTENSION_NAME;
				}

				const VkBool32 extended_format_support = vulkan_globals.device_features.shaderStorageImageExtendedFormats;
				const VkBool32 sampler_anisotropic = vulkan_globals.device_features.samplerAnisotropy;

				ZEROED_STRUCT(VkPhysicalDeviceFeatures, device_features);
				device_features.shaderStorageImageExtendedFormats = extended_format_support;
				device_features.samplerAnisotropy = sampler_anisotropic;
				device_features.sampleRateShading = vulkan_globals.device_features.sampleRateShading;
				device_features.fillModeNonSolid = vulkan_globals.device_features.fillModeNonSolid;
				device_features.multiDrawIndirect = vulkan_globals.device_features.multiDrawIndirect;

				vulkan_globals.non_solid_fill = (device_features.fillModeNonSolid == VK_TRUE) ? true : false;
				vulkan_globals.multi_draw_indirect = (device_features.multiDrawIndirect == VK_TRUE) ? true : false;

				ZEROED_STRUCT(VkDeviceCreateInfo, device_create_info);
				device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
				void** device_create_info_next = (void**)&device_create_info.pNext;
				if (vulkan_globals.screen_effects_sops)
					CHAIN_PNEXT(device_create_info_next, subgroup_size_control_features);
				if (vulkan_globals.ray_query)
				{
					CHAIN_PNEXT(device_create_info_next, buffer_device_address_features);
					CHAIN_PNEXT(device_create_info_next, acceleration_structure_features);
					CHAIN_PNEXT(device_create_info_next, ray_query_features);
				}
				device_create_info.queueCreateInfoCount = 1;
				device_create_info.pQueueCreateInfos = &queue_create_info;
				device_create_info.enabledExtensionCount = numEnabledExtensions;
				device_create_info.ppEnabledExtensionNames = device_extensions;
				device_create_info.pEnabledFeatures = &device_features;

				err = vkCreateDevice(engine->gl->vulkan_physical_device, &device_create_info, NULL, &vulkan_globals.device);
				if (err != VK_SUCCESS)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR,"Couldn't create Vulkan device");

				GET_DEVICE_PROC_ADDR(CreateSwapchainKHR);
				GET_DEVICE_PROC_ADDR(DestroySwapchainKHR);
				GET_DEVICE_PROC_ADDR(GetSwapchainImagesKHR);
				GET_DEVICE_PROC_ADDR(AcquireNextImageKHR);
				GET_DEVICE_PROC_ADDR(QueuePresentKHR);

				SDL_Log("Device extensions:\n");
				for (i = 0; i < numEnabledExtensions; ++i)
					SDL_Log(" %s\n", device_extensions[i]);

#if defined(VK_EXT_full_screen_exclusive)
				if (vulkan_globals.full_screen_exclusive)
				{
					GET_DEVICE_PROC_ADDR(AcquireFullScreenExclusiveModeEXT);
					GET_DEVICE_PROC_ADDR(ReleaseFullScreenExclusiveModeEXT);
				}
#endif
				if (vulkan_globals.ray_query)
				{
					GET_GLOBAL_DEVICE_PROC_ADDR(vk_get_buffer_device_address, vkGetBufferDeviceAddressKHR);
					GET_GLOBAL_DEVICE_PROC_ADDR(vk_get_acceleration_structure_build_sizes, vkGetAccelerationStructureBuildSizesKHR);
					GET_GLOBAL_DEVICE_PROC_ADDR(vk_create_acceleration_structure, vkCreateAccelerationStructureKHR);
					GET_GLOBAL_DEVICE_PROC_ADDR(vk_destroy_acceleration_structure, vkDestroyAccelerationStructureKHR);
					GET_GLOBAL_DEVICE_PROC_ADDR(vk_cmd_build_acceleration_structures, vkCmdBuildAccelerationStructuresKHR);
				}
#ifdef _DEBUG
				if (vulkan_globals.debug_utils)
				{
					GET_INSTANCE_PROC_ADDR(SetDebugUtilsObjectNameEXT);
					GET_GLOBAL_INSTANCE_PROC_ADDR(vk_cmd_begin_debug_utils_label, vkCmdBeginDebugUtilsLabelEXT);
					GET_GLOBAL_INSTANCE_PROC_ADDR(vk_cmd_end_debug_utils_label, vkCmdEndDebugUtilsLabelEXT);
				}
#endif

				vkGetDeviceQueue(vulkan_globals.device, vulkan_globals.gfx_queue_family_index, 0, &vulkan_globals.queue);

				VkFormatProperties format_properties;

				// Find color buffer format
				vulkan_globals.color_format = VK_FORMAT_R8G8B8A8_UNORM;

				if (extended_format_support == VK_TRUE)
				{
					vkGetPhysicalDeviceFormatProperties(engine->gl->vulkan_physical_device, VK_FORMAT_A2B10G10R10_UNORM_PACK32, &format_properties);
					bool a2_b10_g10_r10_support = (format_properties.optimalTilingFeatures & REQUIRED_COLOR_BUFFER_FEATURES) == REQUIRED_COLOR_BUFFER_FEATURES;

					if (a2_b10_g10_r10_support)
					{
						SDL_Log("Using A2B10G10R10 color buffer format\n");
						vulkan_globals.color_format = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
					}
				}

				// Find depth format
				vkGetPhysicalDeviceFormatProperties(engine->gl->vulkan_physical_device, VK_FORMAT_D24_UNORM_S8_UINT, &format_properties);
				bool x8_d24_support = (format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0;
				vkGetPhysicalDeviceFormatProperties(engine->gl->vulkan_physical_device, VK_FORMAT_D32_SFLOAT_S8_UINT, &format_properties);
				bool d32_support = (format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0;

				vulkan_globals.depth_format = VK_FORMAT_UNDEFINED;
				if (d32_support)
				{
					SDL_Log("Using D32_S8 depth buffer format\n");
					vulkan_globals.depth_format = VK_FORMAT_D32_SFLOAT_S8_UINT;
				}
				else if (x8_d24_support)
				{
					SDL_Log("Using D24_S8 depth buffer format\n");
					vulkan_globals.depth_format = VK_FORMAT_D24_UNORM_S8_UINT;
				}
				else
				{
					// This cannot happen with a compliant Vulkan driver. The spec requires support for one of the formats.
					SDL_LogError(SDL_LOG_PRIORITY_ERROR,"Cannot find VK_FORMAT_D24_UNORM_S8_UINT or VK_FORMAT_D32_SFLOAT_S8_UINT depth buffer format");
				}

				SDL_Log("\n");

				GET_GLOBAL_DEVICE_PROC_ADDR(vk_cmd_bind_pipeline, vkCmdBindPipeline);
				GET_GLOBAL_DEVICE_PROC_ADDR(vk_cmd_push_constants, vkCmdPushConstants);
				GET_GLOBAL_DEVICE_PROC_ADDR(vk_cmd_bind_descriptor_sets, vkCmdBindDescriptorSets);
				GET_GLOBAL_DEVICE_PROC_ADDR(vk_cmd_bind_index_buffer, vkCmdBindIndexBuffer);
				GET_GLOBAL_DEVICE_PROC_ADDR(vk_cmd_bind_vertex_buffers, vkCmdBindVertexBuffers);
				GET_GLOBAL_DEVICE_PROC_ADDR(vk_cmd_draw, vkCmdDraw);
				GET_GLOBAL_DEVICE_PROC_ADDR(vk_cmd_draw_indexed, vkCmdDrawIndexed);
				GET_GLOBAL_DEVICE_PROC_ADDR(vk_cmd_draw_indexed_indirect, vkCmdDrawIndexedIndirect);
				GET_GLOBAL_DEVICE_PROC_ADDR(vk_cmd_pipeline_barrier, vkCmdPipelineBarrier);
				GET_GLOBAL_DEVICE_PROC_ADDR(vk_cmd_copy_buffer_to_image, vkCmdCopyBufferToImage);

			}
		};
		class StagingBuffers {
		public:
			Engine* engine;

			VkCommandPool   staging_command_pool;
			vulkan_memory_t staging_memory;
			stagingbuffer_t staging_buffers[NUM_STAGING_BUFFERS];
			int			   current_staging_buffer = 0;
			int			   num_stagings_in_flight = 0;
			SDL_mutex* staging_mutex;
			SDL_cond* staging_cond;

			void R_CreateStagingBuffers()
			{
				int		 i;
				VkResult err;

				ZEROED_STRUCT(VkBufferCreateInfo, buffer_create_info);
				buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
				buffer_create_info.size = vulkan_globals.staging_buffer_size;
				buffer_create_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

				for (i = 0; i < NUM_STAGING_BUFFERS; ++i)
				{
					staging_buffers[i].current_offset = 0;
					staging_buffers[i].submitted = false;

					err = vkCreateBuffer(vulkan_globals.device, &buffer_create_info, NULL, &staging_buffers[i].buffer);
					if (err != VK_SUCCESS)
						SDL_LogError(SDL_LOG_PRIORITY_ERROR,"vkCreateBuffer failed");

					engine->gl->SetObjectName((uint64_t)staging_buffers[i].buffer, VK_OBJECT_TYPE_BUFFER, "Staging Buffer");
				}

				VkMemoryRequirements memory_requirements;
				vkGetBufferMemoryRequirements(vulkan_globals.device, staging_buffers[0].buffer, &memory_requirements);
				const size_t aligned_size = (memory_requirements.size + memory_requirements.alignment);

				ZEROED_STRUCT(VkMemoryAllocateInfo, memory_allocate_info);
				memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
				memory_allocate_info.allocationSize = NUM_STAGING_BUFFERS * aligned_size;
				memory_allocate_info.memoryTypeIndex =
					engine->gl->MemoryTypeFromProperties(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT);

				engine->R_AllocateVulkanMemory(&engine->gl->staging_memory, &memory_allocate_info, VULKAN_MEMORY_TYPE_HOST, &engine->gl->num_vulkan_misc_allocations);
				engine->gl->SetObjectName((uint64_t)engine->gl->staging_memory.handle, VK_OBJECT_TYPE_DEVICE_MEMORY, "Staging Buffers");

				for (i = 0; i < NUM_STAGING_BUFFERS; ++i)
				{
					err = vkBindBufferMemory(vulkan_globals.device, staging_buffers[i].buffer, engine->gl->staging_memory.handle, i * aligned_size);
					if (err != VK_SUCCESS)
						SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkBindBufferMemory failed");
				}

				void* data;
				err = vkMapMemory(vulkan_globals.device, engine->gl->staging_memory.handle, 0, NUM_STAGING_BUFFERS * aligned_size, 0, &data);
				if (err != VK_SUCCESS)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkMapMemory failed");

				for (i = 0; i < NUM_STAGING_BUFFERS; ++i)
					staging_buffers[i].data = (unsigned char*)data + (i * aligned_size);
			}


			StagingBuffers(Engine e) {
				engine = &e;
				engine->gl->sbuf = this;

				int		 i;
				VkResult err;

				SDL_Log("Initializing staging\n");

				R_CreateStagingBuffers();

				ZEROED_STRUCT(VkCommandPoolCreateInfo, command_pool_create_info);
				command_pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
				command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
				command_pool_create_info.queueFamilyIndex = vulkan_globals.gfx_queue_family_index;

				err = vkCreateCommandPool(vulkan_globals.device, &command_pool_create_info, NULL, &engine->gl->staging_command_pool);
				if (err != VK_SUCCESS)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreateCommandPool failed");

				ZEROED_STRUCT(VkCommandBufferAllocateInfo, command_buffer_allocate_info);
				command_buffer_allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
				command_buffer_allocate_info.commandPool = engine->gl->staging_command_pool;
				command_buffer_allocate_info.commandBufferCount = NUM_STAGING_BUFFERS;

				VkCommandBuffer command_buffers[NUM_STAGING_BUFFERS];
				err = vkAllocateCommandBuffers(vulkan_globals.device, &command_buffer_allocate_info, command_buffers);
				if (err != VK_SUCCESS)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkAllocateCommandBuffers failed");

				ZEROED_STRUCT(VkFenceCreateInfo, fence_create_info);
				fence_create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

				ZEROED_STRUCT(VkCommandBufferBeginInfo, command_buffer_begin_info);
				command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
				command_buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

				for (i = 0; i < NUM_STAGING_BUFFERS; ++i)
				{
					err = vkCreateFence(vulkan_globals.device, &fence_create_info, NULL, &staging_buffers[i].fence);
					if (err != VK_SUCCESS)
						SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreateFence failed");

					staging_buffers[i].command_buffer = command_buffers[i];

					err = vkBeginCommandBuffer(staging_buffers[i].command_buffer, &command_buffer_begin_info);
					if (err != VK_SUCCESS)
						SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkBeginCommandBuffer failed");
				}

				engine->gl->vertex_allocate_mutex = SDL_CreateMutex();
				engine->gl->index_allocate_mutex = SDL_CreateMutex();
				engine->gl->uniform_allocate_mutex = SDL_CreateMutex();
				engine->gl->storage_allocate_mutex = SDL_CreateMutex();
				engine->gl->garbage_mutex = SDL_CreateMutex();
				engine->gl->staging_mutex = SDL_CreateMutex();
				engine->gl->staging_cond = SDL_CreateCond();

			}
		};
		class DSLayouts {
		public:
			Engine* engine;
			DSLayouts(Engine e) {
				engine = &e;

				SDL_Log("Creating descriptor set layouts\n");

				VkResult err;
				ZEROED_STRUCT(VkDescriptorSetLayoutCreateInfo, descriptor_set_layout_create_info);
				descriptor_set_layout_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;

				{
					ZEROED_STRUCT(VkDescriptorSetLayoutBinding, single_texture_layout_binding);
					single_texture_layout_binding.binding = 0;
					single_texture_layout_binding.descriptorCount = 1;
					single_texture_layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
					single_texture_layout_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;

					descriptor_set_layout_create_info.bindingCount = 1;
					descriptor_set_layout_create_info.pBindings = &single_texture_layout_binding;

					memset(&vulkan_globals.single_texture_set_layout, 0, sizeof(vulkan_globals.single_texture_set_layout));
					vulkan_globals.single_texture_set_layout.num_combined_image_samplers = 1;

					err = vkCreateDescriptorSetLayout(vulkan_globals.device, &descriptor_set_layout_create_info, NULL, &vulkan_globals.single_texture_set_layout.handle);
					if (err != VK_SUCCESS)
						SDL_LogError(SDL_LOG_PRIORITY_ERROR,"vkCreateDescriptorSetLayout failed");
					engine->gl->SetObjectName((uint64_t)vulkan_globals.single_texture_set_layout.handle, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, "single texture");
				}

				{
					ZEROED_STRUCT(VkDescriptorSetLayoutBinding, ubo_layout_bindings);
					ubo_layout_bindings.binding = 0;
					ubo_layout_bindings.descriptorCount = 1;
					ubo_layout_bindings.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
					ubo_layout_bindings.stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS;

					descriptor_set_layout_create_info.bindingCount = 1;
					descriptor_set_layout_create_info.pBindings = &ubo_layout_bindings;

					memset(&vulkan_globals.ubo_set_layout, 0, sizeof(vulkan_globals.ubo_set_layout));
					vulkan_globals.ubo_set_layout.num_ubos_dynamic = 1;

					err = vkCreateDescriptorSetLayout(vulkan_globals.device, &descriptor_set_layout_create_info, NULL, &vulkan_globals.ubo_set_layout.handle);
					if (err != VK_SUCCESS)
						SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreateDescriptorSetLayout failed");
					engine->gl->SetObjectName((uint64_t)vulkan_globals.ubo_set_layout.handle, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, "single dynamic UBO");
				}

				{
					ZEROED_STRUCT(VkDescriptorSetLayoutBinding, joints_buffer_layout_bindings);
					joints_buffer_layout_bindings.binding = 0;
					joints_buffer_layout_bindings.descriptorCount = 1;
					joints_buffer_layout_bindings.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
					joints_buffer_layout_bindings.stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS;

					descriptor_set_layout_create_info.bindingCount = 1;
					descriptor_set_layout_create_info.pBindings = &joints_buffer_layout_bindings;

					memset(&vulkan_globals.joints_buffer_set_layout, 0, sizeof(vulkan_globals.joints_buffer_set_layout));
					vulkan_globals.joints_buffer_set_layout.num_storage_buffers = 1;

					err = vkCreateDescriptorSetLayout(vulkan_globals.device, &descriptor_set_layout_create_info, NULL, &vulkan_globals.joints_buffer_set_layout.handle);
					if (err != VK_SUCCESS)
						SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreateDescriptorSetLayout failed");
					engine->gl->SetObjectName((uint64_t)vulkan_globals.joints_buffer_set_layout.handle, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, "joints buffer");
				}

				{
					ZEROED_STRUCT(VkDescriptorSetLayoutBinding, input_attachment_layout_bindings);
					input_attachment_layout_bindings.binding = 0;
					input_attachment_layout_bindings.descriptorCount = 1;
					input_attachment_layout_bindings.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
					input_attachment_layout_bindings.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

					descriptor_set_layout_create_info.bindingCount = 1;
					descriptor_set_layout_create_info.pBindings = &input_attachment_layout_bindings;

					memset(&vulkan_globals.input_attachment_set_layout, 0, sizeof(vulkan_globals.input_attachment_set_layout));
					vulkan_globals.input_attachment_set_layout.num_input_attachments = 1;

					err = vkCreateDescriptorSetLayout(vulkan_globals.device, &descriptor_set_layout_create_info, NULL, &vulkan_globals.input_attachment_set_layout.handle);
					if (err != VK_SUCCESS)
						SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreateDescriptorSetLayout failed");
					engine->gl->SetObjectName((uint64_t)vulkan_globals.input_attachment_set_layout.handle, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, "input attachment");
				}

				{
					ZEROED_STRUCT_ARRAY(VkDescriptorSetLayoutBinding, screen_effects_layout_bindings, 5);
					screen_effects_layout_bindings[0].binding = 0;
					screen_effects_layout_bindings[0].descriptorCount = 1;
					screen_effects_layout_bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
					screen_effects_layout_bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
					screen_effects_layout_bindings[1].binding = 1;
					screen_effects_layout_bindings[1].descriptorCount = 1;
					screen_effects_layout_bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
					screen_effects_layout_bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
					screen_effects_layout_bindings[2].binding = 2;
					screen_effects_layout_bindings[2].descriptorCount = 1;
					screen_effects_layout_bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
					screen_effects_layout_bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
					screen_effects_layout_bindings[3].binding = 3;
					screen_effects_layout_bindings[3].descriptorCount = 1;
					screen_effects_layout_bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
					screen_effects_layout_bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
					screen_effects_layout_bindings[4].binding = 4;
					screen_effects_layout_bindings[4].descriptorCount = 1;
					screen_effects_layout_bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
					screen_effects_layout_bindings[4].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

					descriptor_set_layout_create_info.bindingCount = countof(screen_effects_layout_bindings);
					descriptor_set_layout_create_info.pBindings = screen_effects_layout_bindings;

					memset(&vulkan_globals.screen_effects_set_layout, 0, sizeof(vulkan_globals.screen_effects_set_layout));
					vulkan_globals.screen_effects_set_layout.num_combined_image_samplers = 2;
					vulkan_globals.screen_effects_set_layout.num_storage_images = 1;

					err = vkCreateDescriptorSetLayout(vulkan_globals.device, &descriptor_set_layout_create_info, NULL, &vulkan_globals.screen_effects_set_layout.handle);
					if (err != VK_SUCCESS)
						SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreateDescriptorSetLayout failed");
					engine->gl->SetObjectName((uint64_t)vulkan_globals.screen_effects_set_layout.handle, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, "screen effects");
				}

				{
					ZEROED_STRUCT(VkDescriptorSetLayoutBinding, single_texture_cs_write_layout_binding);
					single_texture_cs_write_layout_binding.binding = 0;
					single_texture_cs_write_layout_binding.descriptorCount = 1;
					single_texture_cs_write_layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
					single_texture_cs_write_layout_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

					descriptor_set_layout_create_info.bindingCount = 1;
					descriptor_set_layout_create_info.pBindings = &single_texture_cs_write_layout_binding;

					memset(&vulkan_globals.single_texture_cs_write_set_layout, 0, sizeof(vulkan_globals.single_texture_cs_write_set_layout));
					vulkan_globals.single_texture_cs_write_set_layout.num_storage_images = 1;

					err = vkCreateDescriptorSetLayout(
						vulkan_globals.device, &descriptor_set_layout_create_info, NULL, &vulkan_globals.single_texture_cs_write_set_layout.handle);
					if (err != VK_SUCCESS)
						SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreateDescriptorSetLayout failed");
					engine->gl->SetObjectName((uint64_t)vulkan_globals.single_texture_cs_write_set_layout.handle, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, "single storage image");
				}

				{
					int num_descriptors = 0;
					ZEROED_STRUCT_ARRAY(VkDescriptorSetLayoutBinding, lightmap_compute_layout_bindings, 9);
					lightmap_compute_layout_bindings[0].binding = num_descriptors++;
					lightmap_compute_layout_bindings[0].descriptorCount = 1;
					lightmap_compute_layout_bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
					lightmap_compute_layout_bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
					lightmap_compute_layout_bindings[1].binding = num_descriptors++;
					lightmap_compute_layout_bindings[1].descriptorCount = 1;
					lightmap_compute_layout_bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
					lightmap_compute_layout_bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
					lightmap_compute_layout_bindings[2].binding = num_descriptors++;
					lightmap_compute_layout_bindings[2].descriptorCount = MAXLIGHTMAPS * 3 / 4;
					lightmap_compute_layout_bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
					lightmap_compute_layout_bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
					lightmap_compute_layout_bindings[3].binding = num_descriptors++;
					lightmap_compute_layout_bindings[3].descriptorCount = 1;
					lightmap_compute_layout_bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
					lightmap_compute_layout_bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
					lightmap_compute_layout_bindings[4].binding = num_descriptors++;
					lightmap_compute_layout_bindings[4].descriptorCount = 1;
					lightmap_compute_layout_bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
					lightmap_compute_layout_bindings[4].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
					lightmap_compute_layout_bindings[5].binding = num_descriptors++;
					lightmap_compute_layout_bindings[5].descriptorCount = 1;
					lightmap_compute_layout_bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
					lightmap_compute_layout_bindings[5].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
					lightmap_compute_layout_bindings[6].binding = num_descriptors++;
					lightmap_compute_layout_bindings[6].descriptorCount = 1;
					lightmap_compute_layout_bindings[6].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
					lightmap_compute_layout_bindings[6].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
					lightmap_compute_layout_bindings[7].binding = num_descriptors++;
					lightmap_compute_layout_bindings[7].descriptorCount = 1;
					lightmap_compute_layout_bindings[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
					lightmap_compute_layout_bindings[7].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

					descriptor_set_layout_create_info.bindingCount = num_descriptors;
					descriptor_set_layout_create_info.pBindings = lightmap_compute_layout_bindings;

					memset(&vulkan_globals.lightmap_compute_set_layout, 0, sizeof(vulkan_globals.lightmap_compute_set_layout));
					vulkan_globals.lightmap_compute_set_layout.num_storage_images = 1;
					vulkan_globals.lightmap_compute_set_layout.num_sampled_images = 1 + MAXLIGHTMAPS * 3 / 4;
					vulkan_globals.lightmap_compute_set_layout.num_storage_buffers = 3;
					vulkan_globals.lightmap_compute_set_layout.num_ubos_dynamic = 2;

					err = vkCreateDescriptorSetLayout(vulkan_globals.device, &descriptor_set_layout_create_info, NULL, &vulkan_globals.lightmap_compute_set_layout.handle);
					if (err != VK_SUCCESS)
						SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreateDescriptorSetLayout failed");
					engine->gl->SetObjectName((uint64_t)vulkan_globals.lightmap_compute_set_layout.handle, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, "lightmap compute");

					if (vulkan_globals.ray_query)
					{
						lightmap_compute_layout_bindings[8].binding = num_descriptors++;
						lightmap_compute_layout_bindings[8].descriptorCount = 1;
						lightmap_compute_layout_bindings[8].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
						lightmap_compute_layout_bindings[8].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
						descriptor_set_layout_create_info.bindingCount = num_descriptors;

						vulkan_globals.lightmap_compute_rt_set_layout.num_storage_images = 1;
						vulkan_globals.lightmap_compute_rt_set_layout.num_sampled_images = 1 + MAXLIGHTMAPS * 3 / 4;
						vulkan_globals.lightmap_compute_rt_set_layout.num_storage_buffers = 3;
						vulkan_globals.lightmap_compute_rt_set_layout.num_ubos_dynamic = 2;
						vulkan_globals.lightmap_compute_rt_set_layout.num_acceleration_structures = 1;

						err = vkCreateDescriptorSetLayout(
							vulkan_globals.device, &descriptor_set_layout_create_info, NULL, &vulkan_globals.lightmap_compute_rt_set_layout.handle);
						if (err != VK_SUCCESS)
							SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreateDescriptorSetLayout failed");
						engine->gl->SetObjectName((uint64_t)vulkan_globals.lightmap_compute_rt_set_layout.handle, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, "lightmap compute rt");
					}
				}

				{
					ZEROED_STRUCT_ARRAY(VkDescriptorSetLayoutBinding, indirect_compute_layout_bindings, 4);
					indirect_compute_layout_bindings[0].binding = 0;
					indirect_compute_layout_bindings[0].descriptorCount = 1;
					indirect_compute_layout_bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
					indirect_compute_layout_bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
					indirect_compute_layout_bindings[1].binding = 1;
					indirect_compute_layout_bindings[1].descriptorCount = 1;
					indirect_compute_layout_bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
					indirect_compute_layout_bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
					indirect_compute_layout_bindings[2].binding = 2;
					indirect_compute_layout_bindings[2].descriptorCount = 1;
					indirect_compute_layout_bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
					indirect_compute_layout_bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
					indirect_compute_layout_bindings[3].binding = 3;
					indirect_compute_layout_bindings[3].descriptorCount = 1;
					indirect_compute_layout_bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
					indirect_compute_layout_bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

					descriptor_set_layout_create_info.bindingCount = countof(indirect_compute_layout_bindings);
					descriptor_set_layout_create_info.pBindings = indirect_compute_layout_bindings;

					memset(&vulkan_globals.indirect_compute_set_layout, 0, sizeof(vulkan_globals.indirect_compute_set_layout));
					vulkan_globals.indirect_compute_set_layout.num_storage_buffers = 4;

					err = vkCreateDescriptorSetLayout(vulkan_globals.device, &descriptor_set_layout_create_info, NULL, &vulkan_globals.indirect_compute_set_layout.handle);
					if (err != VK_SUCCESS)
						SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreateDescriptorSetLayout failed");
					engine->gl->SetObjectName((uint64_t)vulkan_globals.indirect_compute_set_layout.handle, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, "indirect compute");
				}

#if defined(_DEBUG)
				if (vulkan_globals.ray_query)
				{
					ZEROED_STRUCT_ARRAY(VkDescriptorSetLayoutBinding, ray_debug_layout_bindings, 2);
					ray_debug_layout_bindings[0].binding = 0;
					ray_debug_layout_bindings[0].descriptorCount = 1;
					ray_debug_layout_bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
					ray_debug_layout_bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
					ray_debug_layout_bindings[1].binding = 1;
					ray_debug_layout_bindings[1].descriptorCount = 1;
					ray_debug_layout_bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
					ray_debug_layout_bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

					descriptor_set_layout_create_info.bindingCount = countof(ray_debug_layout_bindings);
					descriptor_set_layout_create_info.pBindings = ray_debug_layout_bindings;

					memset(&vulkan_globals.ray_debug_set_layout, 0, sizeof(vulkan_globals.ray_debug_set_layout));
					vulkan_globals.ray_debug_set_layout.num_storage_images = 1;

					err = vkCreateDescriptorSetLayout(vulkan_globals.device, &descriptor_set_layout_create_info, NULL, &vulkan_globals.ray_debug_set_layout.handle);
					if (err != VK_SUCCESS)
						SDL_LogError(SDL_LOG_PRIORITY_ERROR, "vkCreateDescriptorSetLayout failed");
					engine->gl->SetObjectName((uint64_t)vulkan_globals.screen_effects_set_layout.handle, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, "ray debug");
				}
#endif

			}
		};
		class DescPool {
		public:
			Engine* engine;
			DescPool(Engine e) {
				engine = &e;
				engine->gl->desc_pool = this;

				ZEROED_STRUCT_ARRAY(VkDescriptorPoolSize, pool_sizes, 9);
				pool_sizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				pool_sizes[0].descriptorCount = MIN_NB_DESCRIPTORS_PER_TYPE + (MAX_SANITY_LIGHTMAPS * 2) + (MAX_GLTEXTURES + 1);
				pool_sizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
				pool_sizes[1].descriptorCount = MIN_NB_DESCRIPTORS_PER_TYPE + MAX_GLTEXTURES + MAX_SANITY_LIGHTMAPS;
				pool_sizes[2].type = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
				pool_sizes[2].descriptorCount = MIN_NB_DESCRIPTORS_PER_TYPE;
				pool_sizes[3].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
				pool_sizes[3].descriptorCount = MIN_NB_DESCRIPTORS_PER_TYPE;
				pool_sizes[4].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				pool_sizes[4].descriptorCount = MIN_NB_DESCRIPTORS_PER_TYPE + MAX_SANITY_LIGHTMAPS * 2;
				pool_sizes[5].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
				pool_sizes[5].descriptorCount = MIN_NB_DESCRIPTORS_PER_TYPE + (MAX_SANITY_LIGHTMAPS * 2);
				pool_sizes[6].type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
				pool_sizes[6].descriptorCount = MIN_NB_DESCRIPTORS_PER_TYPE;
				pool_sizes[7].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
				pool_sizes[7].descriptorCount = MIN_NB_DESCRIPTORS_PER_TYPE + (1 + MAXLIGHTMAPS * 3 / 4) * MAX_SANITY_LIGHTMAPS;
				int num_sizes = 8;
				if (vulkan_globals.ray_query)
				{
					pool_sizes[8].type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
					pool_sizes[8].descriptorCount = MIN_NB_DESCRIPTORS_PER_TYPE + MAX_SANITY_LIGHTMAPS;
					num_sizes = 9;
				}

				ZEROED_STRUCT(VkDescriptorPoolCreateInfo, descriptor_pool_create_info);
				descriptor_pool_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
				descriptor_pool_create_info.maxSets = MAX_GLTEXTURES + MAX_SANITY_LIGHTMAPS + 128;
				descriptor_pool_create_info.poolSizeCount = num_sizes;
				descriptor_pool_create_info.pPoolSizes = pool_sizes;
				descriptor_pool_create_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

				vkCreateDescriptorPool(vulkan_globals.device, &descriptor_pool_create_info, NULL, &vulkan_globals.descriptor_pool);
			}

		};
		class GPUBuffers {
		public:
			Engine* engine;
			GPUBuffers(Engine e) {
				engine = &e;
				engine->gl->gpu_buffers = this;
				SDL_Log("Creating GPU buffers\n");
				engine->R_InitDynamicVertexBuffers();
				engine->R_InitDynamicIndexBuffers();
				engine->R_InitDynamicUniformBuffers();
				engine->R_InitFanIndexBuffer();

			}
		};
		class MeshHeap {
		public:
			glheap_t* heap;
			Engine* engine;
			MeshHeap(Engine e) {
				engine = &e;
				engine->gl->mesh_heap = this;
				SDL_Log("Creating mesh heap\n");

				// Allocate index buffer & upload to GPU
				ZEROED_STRUCT(VkBufferCreateInfo, buffer_create_info);
				buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
				buffer_create_info.size = 16;
				buffer_create_info.usage =
					VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
				VkBuffer dummy_buffer;
				VkResult err = vkCreateBuffer(vulkan_globals.device, &buffer_create_info, NULL, &dummy_buffer);
				if (err != VK_SUCCESS)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR,"vkCreateBuffer failed");

				VkMemoryRequirements memory_requirements;
				vkGetBufferMemoryRequirements(vulkan_globals.device, dummy_buffer, &memory_requirements);

				const uint32_t memory_type_index = engine->gl->MemoryTypeFromProperties(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
				VkDeviceSize   heap_size = MESH_HEAP_SIZE_MB * (VkDeviceSize)1024 * (VkDeviceSize)1024;
				heap = engine->gl->HeapCreate(heap_size, MESH_HEAP_PAGE_SIZE, memory_type_index, VULKAN_MEMORY_TYPE_DEVICE, MESH_HEAP_NAME);

				vkDestroyBuffer(vulkan_globals.device, dummy_buffer, NULL);

			}
		};
		class TexHeap {
		public:
			Engine* engine;
			glheap_t* heap;
			TexHeap(Engine e) {
				engine = &e;
				engine->gl->tex_heap = this;
				SDL_Log("Creating texture heap\n");
				ZEROED_STRUCT(VkImageCreateInfo, image_create_info);
				image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
				image_create_info.imageType = VK_IMAGE_TYPE_2D;
				image_create_info.format = VK_FORMAT_R8G8B8A8_UNORM;
				image_create_info.extent.width = 1;
				image_create_info.extent.height = 1;
				image_create_info.extent.depth = 1;
				image_create_info.mipLevels = 1;
				image_create_info.arrayLayers = 1;
				image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
				image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
				image_create_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
					VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
				image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
				image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

				VkImage	 dummy_image;
				VkResult err = vkCreateImage(vulkan_globals.device, &image_create_info, NULL, &dummy_image);
				if (err != VK_SUCCESS)
					SDL_LogError(SDL_LOG_PRIORITY_ERROR,"vkCreateImage failed");

				VkMemoryRequirements memory_requirements;
				vkGetImageMemoryRequirements(vulkan_globals.device, dummy_image, &memory_requirements);
				const uint32_t memory_type_index = engine->gl->MemoryTypeFromProperties(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);

				const VkDeviceSize heap_memory_size = TEXTURE_HEAP_MEMORY_SIZE_MB * (VkDeviceSize)1024 * (VkDeviceSize)1024;
				heap = engine->gl->HeapCreate(heap_memory_size, TEXTURE_HEAP_PAGE_SIZE, memory_type_index, VULKAN_MEMORY_TYPE_DEVICE, "Texture Heap");

				vkDestroyImage(vulkan_globals.device, dummy_image, NULL);

			}
		};

		Instance* instance = nullptr;
		Device* device = nullptr;
		CommandBuffers* cbuf = nullptr;
		StagingBuffers* sbuf = nullptr;
		DSLayouts* dslayouts = nullptr;
		DescPool* desc_pool = nullptr;
		GPUBuffers* gpu_buffers = nullptr;
		MeshHeap* mesh_heap = nullptr;
		TexHeap* tex_heap = nullptr;

		GL(Engine e) {
			engine = &e;
			engine->gl = this;

			instance = new Instance(*engine);
			device = new Device(*engine);
			cbuf = new CommandBuffers(*engine);
			vulkan_globals.staging_buffer_size = INITIAL_STAGING_BUFFER_SIZE_KB * 1024;
			sbuf = new StagingBuffers(*engine);
			dslayouts = new DSLayouts(*engine);
			desc_pool = new DescPool(*engine);
			gpu_buffers = new GPUBuffers(*engine);
			mesh_heap = new MeshHeap(*engine);
			tex_heap = new TexHeap(*engine);
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

	class COM {
	public:
		int argc; char** argv;
		COM(int c, char* v[]) {
			argc = c;
			argv = v;
		}

		int CheckParmNext(int last, const char* parm)
		{
			int i;

			for (i = last + 1; i < argc; i++)
			{
				if (!argv[i])
					continue; // NEXTSTEP sometimes clears appkit vars.
				if (!strcmp(parm, argv[i]))
					return i;
			}

			return 0;
		}
		int CheckParm(const char* parm)
		{
			return CheckParmNext(0, parm);
		}
	};

	VID* vid = nullptr;
	GL* gl = nullptr;
	COM* com = nullptr;

	Engine(int ct, char* var[]) {
		com = new COM(ct, var);
		vid = new VID(*this);
	}
};

int main(int argc, char* argv[]) {


	auto tremor = new Engine(argc, argv);




	return 0;
}