#pragma once

#include "main.h"
#include "quan.h"

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>


namespace tremor::gfx {

    // Forward declarations
    class Camera;
    class ClusteredRenderer;
    class RenderCommandBuffer;
    class Format;
    struct Frustum;
    template<typename T> class Octree;
    template<typename T> class OctreeNode;

    // Constants
    const float PI = 3.14159265359f;

    // Basic vertex structure
    struct alignas(16) MeshVertex {
        Vec3Q position;
        glm::vec3 normal;
		glm::vec4 color; // COLORE
        glm::vec2 texCoord;
        glm::vec2 padding;
        //glm::vec4 tangent; // w component stores handedness
    };

    // Material structures
    struct MaterialDesc {
        glm::vec4 baseColor = glm::vec4(1.0f);
        float metallic = 0.0f;
        float roughness = 0.5f;
        float ao = 1.0f;
        float emissive = 0.0f;
        glm::vec3 emissiveColor = glm::vec3(0.0f);
        float padding = 0.0f;
    };

    struct PBRMaterial {
        glm::vec4 baseColor = glm::vec4(1.0f);
        float metallic = 0.0f;
        float roughness = 0.5f;
        float normalScale = 1.0f;
        float occlusionStrength = 1.0f;
        glm::vec3 emissiveColor = glm::vec3(0.0f);
        float emissiveFactor = 0.0f;
        int32_t albedoTexture = -1;
        int32_t normalTexture = -1;
        int32_t metallicRoughnessTexture = -1;
        int32_t occlusionTexture = -1;
        int32_t emissiveTexture = -1;
        float alphaCutoff = 0.5f;
        uint32_t flags = 0;
        float padding = 0.0f;
    };

    // Mesh information for GPU access
    struct alignas(16) MeshInfo {
        uint32_t vertexOffset = 0;
        uint32_t vertexCount = 0;
        uint32_t indexOffset = 0;
        uint32_t indexCount = 0;
        glm::vec3 boundsMin = glm::vec3(0.0f);
        float padding1 = 0.0f;
        glm::vec3 boundsMax = glm::vec3(0.0f);
        float padding2 = 0.0f;
    };

    // Bounding box structures
    struct AABBF {
        glm::vec3 min = glm::vec3(0.0f);
        glm::vec3 max = glm::vec3(0.0f);

        AABBF() = default;
        AABBF(const glm::vec3& minVal, const glm::vec3& maxVal) : min(minVal), max(maxVal) {}

        bool contains(const glm::vec3& point) const {
            return point.x >= min.x && point.x <= max.x &&
                point.y >= min.y && point.y <= max.y &&
                point.z >= min.z && point.z <= max.z;
        }

        bool intersects(const AABBF& other) const {
            return !(other.min.x > max.x || other.max.x < min.x ||
                other.min.y > max.y || other.max.y < min.y ||
                other.min.z > max.z || other.max.z < min.z);
        }

        glm::vec3 getCenter() const {
            return min + (max - min) * 0.5f;
        }

        glm::vec3 getDimensions() const {
            return max - min;
        }

        void expand(const glm::vec3& point) {
            min = glm::min(min, point);
            max = glm::max(max, point);
        }

        void expand(const AABBF& other) {
            min = glm::min(min, other.min);
            max = glm::max(max, other.max);
        }
    };

    struct alignas(16) AABBQ {
        Vec3Q min;
        Vec3Q max;

        AABBQ() = default;
        AABBQ(const Vec3Q& minVal, const Vec3Q& maxVal) : min(minVal), max(maxVal) {}

        static AABBQ fromFloat(const AABBF& aabb) {
            return AABBQ(Vec3Q::fromFloat(aabb.min), Vec3Q::fromFloat(aabb.max));
        }

        AABBF toFloat() const {
            return AABBF(min.toFloat(), max.toFloat());
        }

        bool contains(const Vec3Q& point) const {
            return point.x >= min.x && point.x <= max.x &&
                point.y >= min.y && point.y <= max.y &&
                point.z >= min.z && point.z <= max.z;
        }

        bool intersects(const AABBQ& other) const {
            return !(other.min.x > max.x || other.max.x < min.x ||
                other.min.y > max.y || other.max.y < min.y ||
                other.min.z > max.z || other.max.z < min.z);
        }

        Vec3Q getCenter() const {
            return Vec3Q(
                min.x + (max.x - min.x) / 2,
                min.y + (max.y - min.y) / 2,
                min.z + (max.z - min.z) / 2
            );
        }

        void expand(const Vec3Q& point) {
            min.x = std::min(min.x, point.x);
            min.y = std::min(min.y, point.y);
            min.z = std::min(min.z, point.z);
            max.x = std::max(max.x, point.x);
            max.y = std::max(max.y, point.y);
            max.z = std::max(max.z, point.z);
        }
    };

    // Frustum for culling
    struct Frustum {
        enum PlaneID {
            NEAR = 0, FAR, LEFT, RIGHT, TOP, BOTTOM, PLANE_COUNT
        };

        glm::vec4 planes[PLANE_COUNT];
        glm::vec3 corners[8];

        bool intersectsFrustum(const Frustum& other) const;

        bool containsPoint(const glm::vec3& point) const;
        bool containsSphere(const glm::vec3& center, float radius) const;
        bool containsAABB(const glm::vec3& minPoint, const glm::vec3& maxPoint) const;
    };

    // Renderable object structure
    struct alignas(16) RenderableObject {
        glm::mat4 transform = glm::mat4(1.0f);
        glm::mat4 prevTransform = glm::mat4(1.0f);
        uint32_t meshID = 0;
        uint32_t materialID = 0;
        uint32_t instanceID = 0;
        uint32_t flags = 1; // Visible by default
        AABBQ bounds;
    };

    // Clustering structures
    struct alignas(16) ClusterConfig {
        uint32_t xSlices = 16;
        uint32_t ySlices = 9;
        uint32_t zSlices = 24;
        float nearPlane = 0.1f;
        float farPlane = 1000.0f;
        bool logarithmicZ = true;
    };

    struct alignas(16) Cluster {
        uint32_t lightOffset = 0;
        uint32_t lightCount = 0;
        uint32_t objectOffset = 0;
        uint32_t objectCount = 0;
        uint32_t padding[4] = { 0 };
    };

    struct alignas(16) ClusterLight {
        glm::vec3 position = glm::vec3(0.0f);
        float radius = 1.0f;
        glm::vec3 color = glm::vec3(1.0f);
        float intensity = 1.0f;
        int32_t type = 0; // 0=point, 1=spot, 2=directional
        float spotAngle = 45.0f;
        float spotSoftness = 0.0f;
        float padding = 0.0f;
    };

    // Camera class
    class Camera {
    public:
        struct WorldPosition {
            glm::i64vec3 integer = glm::i64vec3(0);
            glm::vec3 fractional = glm::vec3(0.0f);

            glm::dvec3 getCombined() const {
                return glm::dvec3(integer) + glm::dvec3(fractional);
            }
        };

        Camera();
        Camera(float fovDegrees, float aspectRatio, float nearZ, float farZ);

        void lookAt(const WorldPosition& target, const glm::vec3& up);
        glm::mat4 calculateViewMatrix() const;
        // Position and orientation
        void setPosition(const WorldPosition& position);
        void setPosition(const glm::vec3& position);
        void setRotation(const glm::quat& rotation);
        void setRotation(float pitch, float yaw, float roll);

        // Camera properties
        void setFov(float fovDegrees);
        void setAspectRatio(float aspectRatio);
        void setClipPlanes(float nearZ, float farZ);

        // Movement
        void move(const glm::vec3& delta);
        void moveWorld(const glm::vec3& delta);
        void rotate(float pitchDelta, float yawDelta, float rollDelta = 0.0f);
        void lookAt(const glm::vec3& target, const glm::vec3& up = glm::vec3(0.0f, 1.0f, 0.0f));

        // Getters
        const WorldPosition& getPosition() const { return m_position; }
        glm::vec3 getLocalPosition() const;
        glm::quat getRotation() const { return m_rotation; }
        float getFovDegrees() const { return glm::degrees(m_fovRadians); }
        float getFovRadians() const { return m_fovRadians; }
        float getAspectRatio() const { return m_aspectRatio; }
        float getNearClip() const { return m_nearZ; }
        float getFarClip() const { return m_farZ; }

        // Directional vectors
        glm::vec3 getForward() const;
        glm::vec3 getRight() const;
        glm::vec3 getUp() const;

        // Matrices
        const glm::mat4& getViewMatrix() const;
        const glm::mat4& getProjectionMatrix() const;
        const glm::mat4& getViewProjectionMatrix() const;

        // Frustum extraction
        Frustum getViewFrustum();
        void extractFrustumPlanes(glm::vec4 planes[6]);

        glm::mat4 getJitteredProjectionMatrix(const glm::vec2& jitter);

        // Update
        void update(float deltaTime);

        // For Vulkan integration
        VkExtent2D extent = { 1920, 1080 };

    private:
        WorldPosition m_position;
        glm::quat m_rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        float m_fovRadians = glm::radians(60.0f);
        float m_aspectRatio = 16.0f / 9.0f;
        float m_nearZ = 0.1f;
        float m_farZ = 1000.0f;

        mutable glm::mat4 m_viewMatrix = glm::mat4(1.0f);
        mutable glm::mat4 m_projectionMatrix = glm::mat4(1.0f);
        mutable glm::mat4 m_viewProjectionMatrix = glm::mat4(1.0f);

        mutable bool m_viewDirty = true;
        mutable bool m_projDirty = true;
        mutable bool m_vpDirty = true;

        void updateViewMatrix() const;
        void updateProjectionMatrix() const;
        void updateViewProjectionMatrix() const;
        void normalizePosition();
    };

    // Octree implementation
    template<typename T>
    class OctreeNode {
    public:
        OctreeNode(const AABBQ& bounds, uint32_t depth = 0, uint32_t maxDepth = 8, uint32_t maxObjects = 16)
            : m_bounds(bounds), m_depth(depth), m_maxDepth(maxDepth), m_maxObjects(maxObjects), m_isLeaf(true) {
        }

        bool isLeaf() const { return m_isLeaf; }
        const AABBQ& getBounds() const { return m_bounds; }
        const std::vector<T>& getObjects() const { return m_objects; }
        const OctreeNode* getChild(int index) const {
            return (index >= 0 && index < 8) ? m_children[index].get() : nullptr;
        }

        void insert(const T& object, const AABBQ& objectBounds);
        bool remove(const T& object, const AABBQ& objectBounds);
        void query(const AABBQ& queryBounds, std::vector<T>& results) const;
        void query(const Frustum& frustum, std::vector<T>& results) const;
        void getAllObjects(std::vector<T>& results) const;

    private:
        AABBQ m_bounds;
        uint32_t m_depth;
        uint32_t m_maxDepth;
        uint32_t m_maxObjects;
        bool m_isLeaf;
        std::vector<T> m_objects;
        std::vector<AABBQ> m_objectBounds;
        std::array<std::unique_ptr<OctreeNode>, 8> m_children;

        void split();
        int getChildIndex(const AABBQ& objectBounds) const;
    };

    template<typename T>
    class Octree {
    public:
        Octree() = default;


        Octree(const AABBQ& bounds, uint32_t maxDepth = 8, uint32_t maxObjects = 16)
            : m_root(std::make_unique<OctreeNode<T>>(bounds, 0, maxDepth, maxObjects)) {
        }

        void insert(const T& object, const AABBQ& bounds) { m_root->insert(object, bounds); }
        bool remove(const T& object, const AABBQ& bounds) { return m_root->remove(object, bounds); }
        std::vector<T> query(const AABBQ& bounds) const;
        std::vector<T> query(const Frustum& frustum) const;
        std::vector<T> getAllObjects() const;
        const OctreeNode<T>* getRoot() const { return m_root.get(); }

    private:
        std::unique_ptr<OctreeNode<T>> m_root;
    };

    // Abstract interfaces
    class Format {
    public:
        VkFormat format;
        Format() = default;
        Format(VkFormat f) : format(f) {};
        virtual ~Format() = default;
    };

    class RenderCommandBuffer {
    public:
        virtual ~RenderCommandBuffer() = default;
    };

    // Base clustered renderer
    class ClusteredRenderer {
    public:
        ClusteredRenderer() = default;
        ClusteredRenderer(const ClusterConfig& config);
        virtual ~ClusteredRenderer() = default;

        // Pure virtual methods
        virtual bool initialize(Format colorFormat, Format depthFormat) = 0;
        virtual void shutdown() = 0;
        virtual uint32_t loadMesh(const std::vector<MeshVertex>& vertices,
            const std::vector<uint32_t>& indices,
            const std::string& name = "") = 0;
        virtual uint32_t createMaterial(const PBRMaterial& material) = 0;
        virtual void render(RenderCommandBuffer* cmdBuffer, Camera* camera) = 0;
        virtual void updateGPUBuffers() = 0;

        // Platform-agnostic interface
        void setCamera(Camera* camera) { if (camera) { m_camera = camera; } }
        void buildClusters(Camera* camera, Octree<RenderableObject>& octree);
        virtual void updateLights(const std::vector<ClusterLight>& lights){}
        uint32_t createDefaultMaterial();

        // Utilities
        std::vector<uint32_t> findClustersForBounds(const AABBF& bounds, const Camera& camera);
        glm::vec3 worldToCluster(const glm::vec3& worldPos, const Camera& camera);

        // Debug
        void enableWireframe(bool enable) { m_wireframeMode = enable; }
        void setDebugClusterVisualization(bool enable) { m_debugClusters = enable; }

        // Getters
        const std::vector<RenderableObject>& getVisibleObjects() const { return m_visibleObjects; }
        uint32_t getClusterCount() const { return m_totalClusters; }

    protected:
        ClusterConfig m_config;
        uint32_t m_totalClusters;
        bool m_wireframeMode = false;
        bool m_debugClusters = false;

        Camera* m_camera = nullptr;
        Frustum m_frustum;

        // CPU storage
        std::vector<Cluster> m_clusters;
        std::vector<RenderableObject> m_visibleObjects;
        std::vector<ClusterLight> m_lights;
        std::vector<uint32_t> m_clusterLightIndices;
        std::vector<uint32_t> m_clusterObjectIndices;

        // Mesh data
        std::vector<MeshVertex> m_allVertices;
        std::vector<uint32_t> m_allIndices;
        std::vector<MeshInfo> m_meshInfos;
        std::vector<PBRMaterial> m_materials;
        std::unordered_map<std::string, uint32_t> m_meshNameToID;

        // Platform-agnostic algorithms
        virtual void createClusterGrid() {}
        void cullOctree(const Octree<RenderableObject>& octree, const Frustum& frustum);
        void processOctreeNode(const OctreeNode<RenderableObject>* node, const Frustum& frustum);
        void assignObjectsToClusters();
        void assignLightsToClusters();

        virtual void onClustersUpdated() {}
        virtual void onLightsUpdated() {}
        virtual void onMeshDataUpdated() {}
    };

    // Utility functions
    inline AABBF transformAABB(const glm::mat4& transform, const AABBF& aabb) {
        const glm::vec3 corners[8] = {
            {aabb.min.x, aabb.min.y, aabb.min.z}, {aabb.max.x, aabb.min.y, aabb.min.z},
            {aabb.min.x, aabb.max.y, aabb.min.z}, {aabb.max.x, aabb.max.y, aabb.min.z},
            {aabb.min.x, aabb.min.y, aabb.max.z}, {aabb.max.x, aabb.min.y, aabb.max.z},
            {aabb.min.x, aabb.max.y, aabb.max.z}, {aabb.max.x, aabb.max.y, aabb.max.z}
        };

        AABBF result;
        result.min = glm::vec3(FLT_MAX);
        result.max = glm::vec3(-FLT_MAX);

        for (int i = 0; i < 8; i++) {
            glm::vec4 transformedCorner = transform * glm::vec4(corners[i], 1.0f);
            glm::vec3 transformedPos = glm::vec3(transformedCorner) / transformedCorner.w;
            result.min = glm::min(result.min, transformedPos);
            result.max = glm::max(result.max, transformedPos);
        }

        return result;
    }

    inline Camera::Camera()
        : m_fovRadians(glm::radians(60.0f))
        , m_aspectRatio(16.0f / 9.0f)
        , m_nearZ(0.1f)
        , m_farZ(1000.0f)
        , m_viewDirty(true)
        , m_projDirty(true)
        , m_vpDirty(true) {

        // Default position at origin
        m_position.integer = glm::i64vec3(0);
        m_position.fractional = glm::vec3(0.0f);

        // Default orientation
        m_rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    }

    inline Camera::Camera(float fovDegrees, float aspectRatio, float nearZ, float farZ)
        : m_fovRadians(glm::radians(fovDegrees))
        , m_aspectRatio(aspectRatio)
        , m_nearZ(nearZ)
        , m_farZ(farZ)
        , m_viewDirty(true)
        , m_projDirty(true)
        , m_vpDirty(true) {

        // Default position at origin
        m_position.integer = glm::i64vec3(0);
        m_position.fractional = glm::vec3(0.0f);

        // Default orientation
        m_rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    }

    inline void Camera::setPosition(const glm::vec3& position) {
        //Logger::get().info("Camera::setPosition: position={},{},{}",
        //    position.x, position.y, position.z);
        m_position.integer = glm::i64vec3(0);
        m_position.fractional = position;
        normalizePosition();
        m_viewDirty = true;
    }

    inline void Camera::setRotation(float pitch, float yaw, float roll) {
        // Create quaternion from Euler angles (in radians)
        glm::quat quatPitch = glm::angleAxis(glm::radians(pitch), glm::vec3(1.0f, 0.0f, 0.0f));
        glm::quat quatYaw = glm::angleAxis(glm::radians(yaw), glm::vec3(0.0f, 1.0f, 0.0f));
        glm::quat quatRoll = glm::angleAxis(glm::radians(roll), glm::vec3(0.0f, 0.0f, 1.0f));

        m_rotation = quatYaw * quatPitch * quatRoll;
        m_viewDirty = true;
    }

    inline void Camera::move(const glm::vec3& delta) {
        // Convert delta to world space
        glm::vec3 worldDelta = m_rotation * delta;

        // Add to fractional part
        m_position.fractional += worldDelta;

        // Handle potential overflow/underflow
        normalizePosition();
        m_viewDirty = true;
    }

    inline void Camera::moveWorld(const glm::vec3& delta) {
        // Add directly to fractional part
        m_position.fractional += delta;

        // Handle potential overflow/underflow
        normalizePosition();
        m_viewDirty = true;
    }

    inline void Camera::rotate(float pitchDelta, float yawDelta, float rollDelta) {
        // Create delta rotation quaternion
        glm::quat quatPitch = glm::angleAxis(glm::radians(pitchDelta), glm::vec3(1.0f, 0.0f, 0.0f));
        glm::quat quatYaw = glm::angleAxis(glm::radians(yawDelta), glm::vec3(0.0f, 1.0f, 0.0f));
        glm::quat quatRoll = glm::angleAxis(glm::radians(rollDelta), glm::vec3(0.0f, 0.0f, 1.0f));

        // Apply rotation (yaw * pitch * roll order)
        glm::quat deltaRotation = quatYaw * quatPitch * quatRoll;
        m_rotation = m_rotation * deltaRotation;

        // Normalize to prevent drift
        m_rotation = glm::normalize(m_rotation);
        m_viewDirty = true;
    }

    inline void Camera::lookAt(const glm::vec3& target, const glm::vec3& up) {
        // Get current position in local space
        glm::vec3 pos = getLocalPosition();

        // Calculate view matrix directly
        glm::mat4 view = glm::lookAt(pos, target, up);

        // Extract rotation from the view matrix
        // This is more reliable than trying to calculate the quaternion ourselves
        glm::mat3 rotMat(view);
        m_rotation = glm::quat(rotMat);

        // Note: Quaternion from view matrix might need conjugation
        // depending on how it's used later
        m_rotation = glm::conjugate(m_rotation);

        m_viewDirty = true;

        Logger::get().info("lookAt: pos={},{},{}, target={},{},{}",
            pos.x, pos.y, pos.z, target.x, target.y, target.z);
    }

    inline void Camera::lookAt(const WorldPosition& target, const glm::vec3& up) {
        // Calculate world-space vector from camera to target
        glm::dvec3 targetPos = target.getCombined();
        glm::dvec3 cameraPos = m_position.getCombined();

        // Convert to float (for local space operations)
        glm::vec3 direction = glm::normalize(glm::vec3(targetPos - cameraPos));

        // Create rotation quaternion
        m_rotation = glm::quatLookAt(direction, up);
        m_viewDirty = true;
    }

    inline glm::vec3 Camera::getLocalPosition() const {
        // Convert 64-bit position to local-space float position
        // This is a simplified version - a real implementation would
        // have to consider the reference point for local space
        glm::vec3 combinedPos;

        if (m_position.integer == glm::i64vec3(0)) {
            // If integer part is zero, use fractional directly
            combinedPos = m_position.fractional;
        }
        else {
            // Otherwise use a local-space approximation
            // (This could be more sophisticated based on a reference point)
            combinedPos = glm::vec3(
                static_cast<float>(m_position.integer.x) + m_position.fractional.x,
                static_cast<float>(m_position.integer.y) + m_position.fractional.y,
                static_cast<float>(m_position.integer.z) + m_position.fractional.z
            );
        }
        return combinedPos;
    }

    inline glm::vec3 Camera::getForward() const {
        return m_rotation * glm::vec3(0.0f, 0.0f, -1.0f);
    }

    inline glm::vec3 Camera::getRight() const {
        return m_rotation * glm::vec3(1.0f, 0.0f, 0.0f);
    }

    inline glm::vec3 Camera::getUp() const {
        return m_rotation * glm::vec3(0.0f, 1.0f, 0.0f);
    }

    const inline glm::mat4& Camera::getViewMatrix() const {
        if (m_viewDirty) {
            static_cast<Camera>(*this).updateViewMatrix();
            //Logger::get().info("View matrix updated");
        }
        return m_viewMatrix;
    }

    const inline glm::mat4& Camera::getProjectionMatrix() const {
        if (m_projDirty) {
            static_cast<Camera>(*this).updateProjectionMatrix();
        }
        return m_projectionMatrix;
    }

    const inline glm::mat4& Camera::getViewProjectionMatrix() const {
        if (m_viewDirty || m_projDirty || m_vpDirty) {
            static_cast<Camera>(*this).updateViewProjectionMatrix();
        }
        return m_viewProjectionMatrix;
    }

    inline void Camera::updateViewMatrix() const {
        m_viewMatrix = calculateViewMatrix();

        //Logger::get().info("View Matrix[3]: {}, {}, {}",
        //    m_viewMatrix[3][0], m_viewMatrix[3][1], m_viewMatrix[3][2]);



        m_viewDirty = false;
        m_vpDirty = true;
    }

    inline void Camera::updateProjectionMatrix() const {
        // Use reverse depth for better precision
        m_projectionMatrix = glm::perspectiveZO(m_fovRadians, m_aspectRatio, m_farZ, m_nearZ);

        // Fix Vulkan's coordinate system (flip Y)
        m_projectionMatrix[1][1] *= -1;

        m_projDirty = false;
        m_vpDirty = true;
    }

    inline void Camera::updateViewProjectionMatrix() const {
        if (m_viewDirty) {
            updateViewMatrix();
        }
        if (m_projDirty) {
            updateProjectionMatrix();
        }

        m_viewProjectionMatrix = m_projectionMatrix * m_viewMatrix;
        m_vpDirty = false;
    }

    inline glm::mat4 Camera::calculateViewMatrix() const{
        glm::vec3 pos = getLocalPosition();

        // Create view matrix based on position and rotation
        glm::vec3 forward = getForward();
        glm::vec3 target = pos + forward; // Look in the direction of forward vector

        return glm::lookAt(pos, target, glm::vec3(0.0f, 1.0f, 0.0f));
    }


    inline void Camera::normalizePosition() {
        // Handle overflow/underflow in fractional part
        // If any component is >= 1.0 or < 0.0, adjust integer part

        // X component
        if (m_position.fractional.x >= 1.0f) {
            int64_t increment = static_cast<int64_t>(m_position.fractional.x);
            m_position.integer.x += increment;
            m_position.fractional.x -= static_cast<float>(increment);
        }
        else if (m_position.fractional.x < 0.0f) {
            int64_t decrement = static_cast<int64_t>(-m_position.fractional.x) + 1;
            m_position.integer.x -= decrement;
            m_position.fractional.x += static_cast<float>(decrement);
        }

        // Y component
        if (m_position.fractional.y >= 1.0f) {
            int64_t increment = static_cast<int64_t>(m_position.fractional.y);
            m_position.integer.y += increment;
            m_position.fractional.y -= static_cast<float>(increment);
        }
        else if (m_position.fractional.y < 0.0f) {
            int64_t decrement = static_cast<int64_t>(-m_position.fractional.y) + 1;
            m_position.integer.y -= decrement;
            m_position.fractional.y += static_cast<float>(decrement);
        }

        // Z component
        if (m_position.fractional.z >= 1.0f) {
            int64_t increment = static_cast<int64_t>(m_position.fractional.z);
            m_position.integer.z += increment;
            m_position.fractional.z -= static_cast<float>(increment);
        }
        else if (m_position.fractional.z < 0.0f) {
            int64_t decrement = static_cast<int64_t>(-m_position.fractional.z) + 1;
            m_position.integer.z -= decrement;
            m_position.fractional.z += static_cast<float>(decrement);
        }
    }

    inline Frustum Camera::getViewFrustum() {
        Frustum frustum;
        extractFrustumPlanes(frustum.planes);

        // Calculate frustum corners
        // ... (implementation for calculating the 8 corners)

        return frustum;
    }

    inline void Camera::extractFrustumPlanes(glm::vec4 planes[6]) {
        // Get the view-projection matrix
        const glm::mat4& vp = getViewProjectionMatrix();

        // Extract the six frustum planes
        // Left plane
        planes[Frustum::LEFT].x = vp[0][3] + vp[0][0];
        planes[Frustum::LEFT].y = vp[1][3] + vp[1][0];
        planes[Frustum::LEFT].z = vp[2][3] + vp[2][0];
        planes[Frustum::LEFT].w = vp[3][3] + vp[3][0];

        // Right plane
        planes[Frustum::RIGHT].x = vp[0][3] - vp[0][0];
        planes[Frustum::RIGHT].y = vp[1][3] - vp[1][0];
        planes[Frustum::RIGHT].z = vp[2][3] - vp[2][0];
        planes[Frustum::RIGHT].w = vp[3][3] - vp[3][0];

        // Bottom plane
        planes[Frustum::BOTTOM].x = vp[0][3] + vp[0][1];
        planes[Frustum::BOTTOM].y = vp[1][3] + vp[1][1];
        planes[Frustum::BOTTOM].z = vp[2][3] + vp[2][1];
        planes[Frustum::BOTTOM].w = vp[3][3] + vp[3][1];

        // Top plane
        planes[Frustum::TOP].x = vp[0][3] - vp[0][1];
        planes[Frustum::TOP].y = vp[1][3] - vp[1][1];
        planes[Frustum::TOP].z = vp[2][3] - vp[2][1];
        planes[Frustum::TOP].w = vp[3][3] - vp[3][1];

        // Near plane
        planes[Frustum::NEAR].x = vp[0][3] + vp[0][2];
        planes[Frustum::NEAR].y = vp[1][3] + vp[1][2];
        planes[Frustum::NEAR].z = vp[2][3] + vp[2][2];
        planes[Frustum::NEAR].w = vp[3][3] + vp[3][2];

        // Far plane
        planes[Frustum::FAR].x = vp[0][3] - vp[0][2];
        planes[Frustum::FAR].y = vp[1][3] - vp[1][2];
        planes[Frustum::FAR].z = vp[2][3] - vp[2][2];
        planes[Frustum::FAR].w = vp[3][3] - vp[3][2];

        // Normalize all planes
        for (int i = 0; i < Frustum::PLANE_COUNT; i++) {
            float length = sqrtf(planes[i].x * planes[i].x +
                planes[i].y * planes[i].y +
                planes[i].z * planes[i].z);
            planes[i] /= length;
        }
    }

    inline glm::mat4 Camera::getJitteredProjectionMatrix(const glm::vec2& jitter) {
        // Create jittered projection matrix for temporal anti-aliasing
        glm::mat4 proj = glm::perspective(m_fovRadians, m_aspectRatio, m_nearZ, m_farZ);

        // Apply jitter
        proj[2][0] += jitter.x;
        proj[2][1] += jitter.y;

        // Fix Vulkan's coordinate system (flip Y)
        proj[1][1] *= -1;

        return proj;
    }

    inline void Camera::update(float deltaTime) {
        if (m_viewDirty || m_projDirty || m_vpDirty) {
            if (m_viewDirty) updateViewMatrix();
            if (m_projDirty) updateProjectionMatrix();
            if (m_vpDirty) updateViewProjectionMatrix();
        }
        //Logger::get().info("View matrix: \n {} {} {} {} \n {} {} {} {} \n {} {} {} {} \n {} {} {} {}", m_viewMatrix[0][0], m_viewMatrix[0][1], m_viewMatrix[0][2], m_viewMatrix[0][3], m_viewMatrix[1][0], m_viewMatrix[1][1], m_viewMatrix[1][2], m_viewMatrix[1][3], m_viewMatrix[2][0], m_viewMatrix[2][1], m_viewMatrix[2][2], m_viewMatrix[2][3], m_viewMatrix[3][0], m_viewMatrix[3][1], m_viewMatrix[3][2], m_viewMatrix[3][3]);

        //Logger::get().info("Projection matrix: \n {} {} {} {} \n {} {} {} {} \n {} {} {} {} \n {} {} {} {}", m_projectionMatrix[0][0], m_projectionMatrix[0][1], m_projectionMatrix[0][2], m_projectionMatrix[0][3], m_projectionMatrix[1][0], m_projectionMatrix[1][1], m_projectionMatrix[1][2], m_projectionMatrix[1][3], m_projectionMatrix[2][0], m_projectionMatrix[2][1], m_projectionMatrix[2][2], m_projectionMatrix[2][3], m_projectionMatrix[3][0], m_projectionMatrix[3][1], m_projectionMatrix[3][2], m_projectionMatrix[3][3]);
    }

    // Frustum implementation
    inline bool Frustum::containsPoint(const glm::vec3& point) const {
        for (int i = 0; i < PLANE_COUNT; i++) {
            if (planes[i].x * point.x +
                planes[i].y * point.y +
                planes[i].z * point.z +
                planes[i].w <= 0) {
                return false;
            }
        }
        return true;
    }

    inline bool Frustum::containsSphere(const glm::vec3& center, float radius) const {
        for (int i = 0; i < PLANE_COUNT; i++) {
            float distance = planes[i].x * center.x +
                planes[i].y * center.y +
                planes[i].z * center.z +
                planes[i].w;

            if (distance <= -radius) {
                return false;  // Sphere is completely outside this plane
            }
        }
        return true;  // Sphere is at least partially inside all planes
    }

    inline bool Frustum::containsAABB(const glm::vec3& min, const glm::vec3& max) const {
        // For each plane, find the positive vertex (P-vertex) and test it
        for (int i = 0; i < PLANE_COUNT; i++) {
            // Get plane normal
            glm::vec3 normal(planes[i].x, planes[i].y, planes[i].z);

            // Determine farthest corner along the normal
            glm::vec3 p;
            p.x = (normal.x > 0) ? min.x : max.x;
            p.y = (normal.y > 0) ? min.y : max.y;
            p.z = (normal.z > 0) ? min.z : max.z;

            // If this point is outside the plane, the box is outside
            if (glm::dot(normal, p) + planes[i].w > 0) {
                return false;
            }
        }

        return true;
    }


    inline bool Frustum::intersectsFrustum(const Frustum& other) const {
        // This is a simplified test for cluster vs view frustum
        // Fully implemented, you'd want to use separating axis theorem

        // First, check if any of the corners of one frustum are inside the other
        for (int i = 0; i < 8; i++) {
            if (other.containsPoint(corners[i])) {
                return true;
            }
            if (containsPoint(other.corners[i])) {
                return true;
            }
        }

        // Then check if any of the edges of one frustum intersect any of the faces of the other
        // (Implementation omitted for brevity)

        return false;
    }


} // namespace tremor::gfx

// Template implementations
#include "gfx_impl.inl"