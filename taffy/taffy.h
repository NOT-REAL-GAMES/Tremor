/**
 * Taffy: The Web 3.0 Interactive Asset Format
 * Version 0.1 - Foundation Implementation
 *
 * "Real-Time First, Universal Second, Intelligent Third"
 *
 * This header defines the core Taffy format that will evolve from
 * basic geometry loading to AI-native interactive experiences.
 */

#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <unordered_map>
#include <memory>
#include <optional>

 // Assume Tremor's quantized coordinate system exists
#include "../quan.h"  // Vec3Q, etc.
#include "../quan.h"

namespace Taffy {

    // =============================================================================
    // VERSION & MAGIC CONSTANTS
    // =============================================================================

    constexpr uint32_t TAFFY_MAGIC = 0x21464154;  // "TAF!" in little-endian
    constexpr uint16_t VERSION_MAJOR = 0;
    constexpr uint16_t VERSION_MINOR = 1;
    constexpr uint16_t VERSION_PATCH = 0;

    // =============================================================================
    // FEATURE FLAGS - Extensible for future capabilities
    // =============================================================================

    enum class FeatureFlags : uint64_t {
        None = 0,
        QuantizedCoords = 1 << 0,   // Using 64-bit quantized coordinates
        RealTimeFracture = 1 << 1,   // Voronoi fracturing support
        EmbeddedScripts = 1 << 2,   // Q3VM/Lua integration
        ParticleSystems = 1 << 3,   // Self-contained particle effects
        NarrativeContent = 1 << 4,   // Ink-style dialogue trees
        SVGUserInterface = 1 << 5,   // Programmable SVG UI
        DependencySystem = 1 << 6,   // Modular asset composition

        // Future AI features (v2.0+)
        LocalAIModels = 1 << 32,  // NPU-powered AI
        DualQueryProcessing = 1 << 33,  // Context + Content AI
        PsychologicalModeling = 1 << 34,  // Therapeutic accuracy
        AdaptiveBehavior = 1 << 35,  // Learning NPCs

        // Reserved for future use
        Reserved = 0xFFFF000000000000ULL
    };

    inline FeatureFlags operator|(FeatureFlags a, FeatureFlags b) {
        return static_cast<FeatureFlags>(static_cast<uint64_t>(a) | static_cast<uint64_t>(b));
    }

    inline bool has_feature(FeatureFlags flags, FeatureFlags feature) {
        return (static_cast<uint64_t>(flags) & static_cast<uint64_t>(feature)) != 0;
    }

    // =============================================================================
    // CHUNK TYPES - Extensible chunk system
    // =============================================================================

    enum class ChunkType : uint32_t {
        // Core Geometry & Rendering
        GEOM = 0x4D4F4547,  // 'GEOM' - Mesh geometry
        GLOD = 0x444F4C47,  // 'GLOD' - LOD chains  
        MTRL = 0x4C52544D,  // 'MTRL' - PBR materials
        SHDR = 0x52444853,  // 'SHDR' - SPIR-V shaders
        TXTR = 0x52545854,  // 'TXTR' - Texture data
        ANIM = 0x4D494E41,  // 'ANIM' - Skeletal animation

        // Intelligence & Behavior (v0.3+)
        SCPT = 0x54504353,  // 'SCPT' - Q3VM + Lua scripts
        NARR = 0x5252414E,  // 'NARR' - Ink narrative trees
        CHAR = 0x52414843,  // 'CHAR' - Character personalities
        QUES = 0x53455551,  // 'QUES' - Quest integration
        PROP = 0x504F5250,  // 'PROP' - Property system

        // Physics & Effects (v0.4+)
        FRAC = 0x43415246,  // 'FRAC' - Fracturing patterns
        PART = 0x54524150,  // 'PART' - Particle systems
        PHYS = 0x53594850,  // 'PHYS' - Physics properties
        AUDI = 0x49445541,  // 'AUDI' - Procedural audio

        // Structure & UI (v0.5+)
        SCNG = 0x474E4353,  // 'SCNG' - Scene graph
        SVGU = 0x55475653,  // 'SVGU' - SVG UI definitions
        INST = 0x54534E49,  // 'INST' - GPU instancing
        BBOX = 0x584F4242,  // 'BBOX' - Spatial bounds
        STRM = 0x4D525453,  // 'STRM' - Streaming metadata

        // System Integration (v1.0+)
        DEPS = 0x53504544,  // 'DEPS' - Dependencies
        NETW = 0x5754454E,  // 'NETW' - Multiplayer sync
        L10N = 0x4E30314C,  // 'L10N' - Localization
        PERF = 0x46524550,  // 'PERF' - Performance analytics
        COMM = 0x4D4D4F43,  // 'COMM' - Asset communication

        // AI & Psychology (v2.0+)
        AIMD = 0x444D4941,  // 'AIMD' - AI models
        PSYC = 0x43595350,  // 'PSYC' - Psychological profiles
        CTXT = 0x54585443,  // 'CTXT' - Context analysis
        LRNG = 0x474E524C,  // 'LRNG' - Learning systems
        EMRG = 0x47524D45,  // 'EMRG' - Emergent behavior

        // Sentinel
        UNKNOWN = 0x00000000
    };

    // =============================================================================
    // CORE STRUCTURES
    // =============================================================================

    /**
     * Main Taffy asset header - appears at start of every .taf file
     */
    struct Header {
        uint32_t magic;              // Always TAFFY_MAGIC
        uint16_t version_major;      // Major version
        uint16_t version_minor;      // Minor version  
        uint16_t version_patch;      // Patch version
        uint16_t _reserved;          // Future use

        FeatureFlags feature_flags;  // Capability bitmask

        uint32_t chunk_count;        // Number of chunks in directory
        uint32_t dependency_count;   // Number of dependencies (v0.6+)

        uint64_t total_size;         // Total file size in bytes

        // Quantized world bounds (requires QuantizedCoords feature)
        Vec3Q world_min;             // Minimum bounding box
        Vec3Q world_max;             // Maximum bounding box

        // Timestamps and metadata
        uint64_t created_timestamp;  // Unix timestamp
        uint64_t modified_timestamp; // Last modification

        char creator[64];            // Creator tool/engine name
        char description[128];       // Human-readable description

        uint32_t checksum;           // CRC32 of entire file content
        uint32_t _padding;           // Ensure 8-byte alignment
    };

    static_assert(sizeof(Header) % 8 == 0, "Header must be 8-byte aligned");

    /**
     * Directory entry for each chunk in the asset
     */
    struct ChunkHeader {
        ChunkType type;              // Chunk type identifier
        uint32_t size;               // Size of chunk data in bytes
        uint64_t offset;             // Offset from file start
        uint32_t checksum;           // CRC32 of chunk data
        uint32_t compression;        // Compression type (0=none, 1=LZ4, 2=ZSTD)

        // Chunk-specific metadata
        uint32_t version;            // Chunk format version
        uint32_t flags;              // Chunk-specific flags

        char name[32];               // Optional human-readable name
    };

    static_assert(sizeof(ChunkHeader) % 8 == 0, "ChunkHeader must be 8-byte aligned");

    // =============================================================================
    // CHUNK DATA STRUCTURES - Start with essentials
    // =============================================================================

    /**
     * Basic geometry chunk - the foundation
     */
    struct GeometryChunk {
        uint32_t vertex_count;
        uint32_t index_count;
        uint32_t vertex_stride;      // Size of each vertex in bytes
        uint32_t vertex_format;      // Vertex attribute bitmask

        // Quantized bounding box for this geometry
        Vec3Q bounds_min;
        Vec3Q bounds_max;

        // LOD information
        float lod_distance;          // Distance for this LOD level
        uint32_t lod_level;          // 0 = highest detail

        // Data follows this header:
        // - Vertex data: vertex_count * vertex_stride bytes
        // - Index data: index_count * sizeof(uint32_t) bytes
    };

    /**
     * Material chunk - PBR materials
     */
    struct MaterialChunk {
        uint32_t material_count;
        uint32_t _padding;

        struct Material {
            char name[64];

            // PBR parameters
            float albedo[4];         // RGBA
            float metallic;
            float roughness;
            float normal_intensity;
            float emission[3];

            // Texture indices (into TXTR chunks)
            uint32_t albedo_texture;
            uint32_t normal_texture;
            uint32_t orm_texture;    // Occlusion/Roughness/Metallic
            uint32_t emission_texture;

            uint32_t flags;          // Material flags
            uint32_t _reserved[3];
        };

        // Array of materials follows
    };

    /**
     * Dependency chunk - for modular composition (v0.6+)
     */
    struct DependencyChunk {
        uint32_t dependency_count;
        uint32_t _padding;

        struct Dependency {
            char name[128];          // "medieval_npc_framework"
            char version_spec[32];   // "^2.1.0" (semver)
            uint32_t type;           // 0=required, 1=optional
            uint32_t chunk_types;    // Which chunk types this provides
            char description[256];   // Human description
        };

        // Array of dependencies follows
    };

    // =============================================================================
    // MAIN ASSET CLASS
    // =============================================================================

    /**
     * Main Taffy asset container - loads, validates, and provides access to chunks
     */
    class Asset {
    public:
        Asset() = default;
        ~Asset() = default;

        // No copy, only move
        Asset(const Asset&) = delete;
        Asset& operator=(const Asset&) = delete;
        Asset(Asset&&) = default;
        Asset& operator=(Asset&&) = default;

        // File I/O
        bool load_from_file(const std::string& filepath);
        bool save_to_file(const std::string& filepath) const;
        bool validate() const;

        // Header access
        const Header& get_header() const { return header_; }
        bool has_feature(FeatureFlags feature) const;

        // Chunk access
        std::optional<std::vector<uint8_t>> get_chunk_data(ChunkType type) const;
        std::vector<ChunkHeader> get_chunks_of_type(ChunkType type) const;
        bool has_chunk(ChunkType type) const;

        // Typed chunk accessors (convenience)
        std::optional<GeometryChunk> get_geometry() const;
        std::optional<MaterialChunk> get_materials() const;
        std::optional<DependencyChunk> get_dependencies() const;

        // Chunk management (for asset creation)
        bool add_chunk(ChunkType type, const std::vector<uint8_t>& data,
            const std::string& name = "");
        bool remove_chunk(ChunkType type);

        // Metadata
        std::string get_creator() const;
        std::string get_description() const;
        uint64_t get_file_size() const { return header_.total_size; }

    private:
        Header header_{};
        std::vector<ChunkHeader> chunk_directory_;
        std::unordered_map<ChunkType, std::vector<uint8_t>> chunks_;

        // Internal helpers
        bool validate_header() const;
        bool validate_chunks() const;
        uint32_t calculate_checksum() const;
        void update_header_from_chunks();
    };

    // =============================================================================
    // UTILITY FUNCTIONS
    // =============================================================================

    /**
     * Get human-readable chunk type name
     */
    const char* chunk_type_to_string(ChunkType type);

    /**
     * Check if running engine supports required features
     */
    bool engine_supports_features(FeatureFlags required_features);

    /**
     * Version compatibility checking
     */
    bool is_version_compatible(uint16_t major, uint16_t minor, uint16_t patch);

} // namespace Taffy

/**
 * IMPLEMENTATION NOTES:
 *
 * This header defines the foundation for Taffy assets that will evolve:
 *
 * v0.1: Basic geometry and materials (this version)
 * v0.2: Add textures and simple animation
 * v0.3: Add Q3VM scripting integration
 * v0.4: Add fracturing and particle systems
 * v0.5: Add SVG UI and scene graphs
 * v0.6: Add dependency system for composition
 * v1.0: Complete cross-engine compatibility
 * v2.0: AI integration for Complex demo
 * v3.0: Full AI-native interactive experiences
 *
 * The chunk system is designed to be completely extensible.
 * New chunk types can be added without breaking existing assets.
 * Feature flags let engines gracefully handle unsupported capabilities.
 *
 * Start simple, evolve incrementally, change the world!
 */