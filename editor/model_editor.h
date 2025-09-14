#pragma once

#include "../gfx.h"
#include "../renderer/ui_renderer.h"
#include "../renderer/taffy_integration.h"
#include "../renderer/taffy_mesh.h"
#include "grid_renderer.h"
#include "gizmo_renderer.h"
#include "taffy.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include <vector>
#include <string>
#include <functional>
#include <algorithm>

namespace tremor::gfx {
    class UIRenderer;
}

namespace tremor::editor {

    class GizmoRenderer;
    class GridRenderer;

    // Forward declarations
    class EditorViewport;
    class ModelEditorUI;
    class EditableModel;
    class EditorTools;

    // Editor modes
    enum class EditorMode {
        Select,         // Selection mode
        Move,           // Translation gizmo
        Rotate,         // Rotation gizmo
        Scale,          // Scale gizmo
        AddVertex,      // Vertex creation mode
        CreateTriangle  // Triangle creation mode
    };

    // Selection info
    struct Selection {
        uint32_t meshId = UINT32_MAX;
        uint32_t vertexIndex = UINT32_MAX;
        uint32_t faceIndex = UINT32_MAX;
        std::vector<uint32_t> customVertexIds;  // Multiple custom vertices for multi-selection
        std::vector<uint32_t> selectedTriangles;  // Selected triangles for winding order operations
        bool hasMesh() const { return meshId != UINT32_MAX; }
        bool hasVertex() const { return vertexIndex != UINT32_MAX; }
        bool hasFace() const { return faceIndex != UINT32_MAX; }
        bool hasCustomVertices() const { return !customVertexIds.empty(); }
        bool hasSelectedTriangles() const { return !selectedTriangles.empty(); }
        bool hasCustomVertex(uint32_t id) const { 
            return std::find(customVertexIds.begin(), customVertexIds.end(), id) != customVertexIds.end();
        }
        bool hasTriangle(uint32_t triangleIdx) const {
            return std::find(selectedTriangles.begin(), selectedTriangles.end(), triangleIdx) != selectedTriangles.end();
        }
        void clear() { 
            meshId = UINT32_MAX; 
            vertexIndex = UINT32_MAX; 
            faceIndex = UINT32_MAX; 
            customVertexIds.clear();
            selectedTriangles.clear();
        }
        void clearCustomVertices() { customVertexIds.clear(); }
        void clearTriangles() { selectedTriangles.clear(); }
        void addCustomVertex(uint32_t id) {
            if (!hasCustomVertex(id)) {
                customVertexIds.push_back(id);
            }
        }
        void removeCustomVertex(uint32_t id) {
            auto it = std::find(customVertexIds.begin(), customVertexIds.end(), id);
            if (it != customVertexIds.end()) {
                customVertexIds.erase(it);
            }
        }
        void addTriangle(uint32_t triangleIdx) {
            if (!hasTriangle(triangleIdx)) {
                selectedTriangles.push_back(triangleIdx);
            }
        }
        void removeTriangle(uint32_t triangleIdx) {
            auto it = std::find(selectedTriangles.begin(), selectedTriangles.end(), triangleIdx);
            if (it != selectedTriangles.end()) {
                selectedTriangles.erase(it);
            }
        }
    };

    /**
     * Main model editor class - manages the editing session
     */
    class ModelEditor {
    public:
        ModelEditor(VkDevice device, VkPhysicalDevice physicalDevice,
                   VkCommandPool commandPool, VkQueue graphicsQueue,
                   tremor::gfx::UIRenderer& uiRenderer);
        ~ModelEditor();

        // Initialize the editor
        bool initialize(VkRenderPass renderPass, VkFormat colorFormat,
                       VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT);

        // Main update loop
        void update(float deltaTime);
        void render(VkCommandBuffer commandBuffer, const glm::mat4& projection);
        void handleInput(const SDL_Event& event);

        // File operations
        bool loadModel(const std::string& filepath);
        bool saveModel(const std::string& filepath = "");
        bool newModel();

        // Editor state
        void setMode(EditorMode mode);
        EditorMode getMode() const { return m_currentMode; }
        
        // Mesh preview
        void setShowMeshPreview(bool show) { m_showMeshPreview = show; }
        bool getShowMeshPreview() const { return m_showMeshPreview; }
        void setWireframeMode(bool wireframe) { m_wireframeMode = wireframe; }
        bool getWireframeMode() const { return m_wireframeMode; }
        void setBackfaceCulling(bool enable) { m_backfaceCulling = enable; }
        bool getBackfaceCulling() const { return m_backfaceCulling; }

        // Selection
        const Selection& getSelection() const { return m_selection; }
        void clearSelection();
        bool selectMesh(const glm::vec2& screenPos);
        bool selectVertex(const glm::vec2& screenPos);
        bool selectCustomVertex(const glm::vec2& screenPos);
        bool selectTriangle(const glm::vec2& screenPos, bool addToSelection = false);
        
        // Triangle operations
        void reverseWindingOrder();  // Reverse winding order of selected triangles
        void reverseWindingOrderForTriangle(uint32_t triangleIdx);  // Reverse specific triangle

        // Mesh creation
        void addVertexAtScreenPosition(const glm::vec2& screenPos);
        void selectVertexForTriangle(const glm::vec2& screenPos);

        // Transform operations
        void translateSelection(const glm::vec3& delta);
        void rotateSelection(const glm::vec3& axis, float angle);
        void scaleSelection(const glm::vec3& scale);

        // Viewport camera
        void setViewportSize(glm::vec2 size);
        glm::vec2 getViewportSize() const { return m_viewportSize; }
        
        // Scissor rectangle
        void setScissorSize(glm::vec2 size);
        glm::vec2 getScissorSize() const { return m_scissorSize; }

        // Editor callbacks
        void onModelChanged(std::function<void()> callback) { m_modelChangedCallback = callback; }
        void onSelectionChanged(std::function<void()> callback) { m_selectionChangedCallback = callback; }

        // Selection settings
        void setVertexSelectionRadius(float radius) { m_vertexSelectionRadius = radius; }
        float getVertexSelectionRadius() const { return m_vertexSelectionRadius; }

        // UI access
        ModelEditorUI* getUI() const { return m_ui.get(); }
        
        // Component access
        EditorViewport* getViewport() const { return m_viewport.get(); }
        EditableModel* getModel() const { return m_model.get(); }
        EditorTools* getTools() const { return m_tools.get(); }

    private:
        VkDevice m_device;
        VkPhysicalDevice m_physicalDevice;
        VkCommandPool m_commandPool;
        VkQueue m_graphicsQueue;
        tremor::gfx::UIRenderer& m_uiRenderer;

        // Core editor components
        std::unique_ptr<EditorViewport> m_viewport;
        std::unique_ptr<ModelEditorUI> m_ui;
        std::unique_ptr<EditableModel> m_model;
        std::unique_ptr<EditorTools> m_tools;

        // Editor state
        EditorMode m_currentMode = EditorMode::Select;
        Selection m_selection;
        glm::vec2 m_viewportSize = glm::vec2(1920, 1080);
        glm::vec2 m_scissorSize = glm::vec2(1920, 1080);
        bool m_showMeshPreview = true;
        bool m_wireframeMode = false;
        bool m_backfaceCulling = true;
        
        // File management
        std::string m_currentFilePath;
        bool m_hasUnsavedChanges = false;

        // Input state
        glm::vec2 m_lastMousePos;
        bool m_isDragging = false;
        bool m_cameraControlsEnabled = true;

        // Selection settings
        float m_vertexSelectionRadius = 0.5f;  // World-space vertex selection radius

        // Triangle creation state
        std::vector<uint32_t> m_selectedVerticesForTriangle;

        // Callbacks
        std::function<void()> m_modelChangedCallback;
        std::function<void()> m_selectionChangedCallback;

        // Helper methods
        void updateViewport(float deltaTime);
        void updateUI();
        void markModelChanged();
        void updateGizmoPosition(); // Update gizmo position based on current selection
        bool isViewportHovered(const glm::vec2& mousePos) const;
        glm::vec3 screenToWorld(const glm::vec2& screenPos, float depth = 0.0f) const;
        bool screenToWorldRay(const glm::vec2& screenPos, glm::vec3& rayOrigin, glm::vec3& rayDirection) const;
        bool rayTriangleIntersect(const glm::vec3& rayOrigin, const glm::vec3& rayDirection,
                                  const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2,
                                  float& t) const;  // Ray-triangle intersection test
        void renderMeshPreview(VkCommandBuffer commandBuffer);  // Render mesh preview using GizmoRenderer
    };

    /**
     * Editor viewport - handles 3D visualization and camera
     */
    class EditorViewport {
    public:
        EditorViewport(VkDevice device, VkPhysicalDevice physicalDevice,
                      VkCommandPool commandPool, VkQueue graphicsQueue);
        ~EditorViewport();

        bool initialize(VkRenderPass renderPass, VkFormat colorFormat,
                       VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT);
        void update(float deltaTime);
        void render(VkCommandBuffer commandBuffer);
        void handleInput(const SDL_Event& event);

        // Camera controls
        void setPosition(const glm::vec3& position) { m_cameraPos = position; }
        void setTarget(const glm::vec3& target) { m_cameraTarget = target; }
        glm::vec3 getPosition() const { return m_cameraPos; }
        glm::vec3 getTarget() const { return m_cameraTarget; }

        // Projection settings
        void setViewportSize(glm::vec2 size);
        void setScissorSize(glm::vec2 size);
        void setFOV(float fov) { m_fov = fov; }
        void setNearFar(float near, float far) { m_nearPlane = near; m_farPlane = far; }

        // View/projection matrices
        glm::mat4 getViewMatrix() const;
        glm::mat4 getProjectionMatrix() const;

        // Grid and gizmo rendering
        void setShowGrid(bool show) { m_showGrid = show; }
        void setShowGizmos(bool show) { m_showGizmos = show; }
        void setGridRenderingEnabled(bool enabled) { m_gridRenderingEnabled = enabled; }

    private:
        VkDevice m_device;
        VkPhysicalDevice m_physicalDevice;
        VkCommandPool m_commandPool;
        VkQueue m_graphicsQueue;

        // Rendering components
        std::unique_ptr<GridRenderer> m_gridRenderer;

        // Camera state
        glm::vec3 m_cameraPos = glm::vec3(5.0f, 5.0f, 5.0f);
        glm::vec3 m_cameraTarget = glm::vec3(0.0f);
        glm::vec3 m_cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);

        // Camera orbital controls
        float m_orbitRadius = 10.0f;
        float m_orbitTheta = 0.0f;    // Horizontal angle
        float m_orbitPhi = 45.0f;     // Vertical angle

        // Projection settings
        glm::vec2 m_viewportSize = glm::vec2(1920, 1080);
        glm::vec2 m_scissorSize = glm::vec2(1920, 1080);
        float m_fov = 45.0f;
        float m_nearPlane = 0.1f;
        float m_farPlane = 1000.0f;

        // Visual settings
        bool m_showGrid = true;
        bool m_showGizmos = true;
        bool m_gridRenderingEnabled = true;

        int64_t m_stepDuration = 0.00f;
        int64_t m_lastStepTime = -1.0f;

        // Input state
        bool m_isOrbiting = false;
        bool m_isPanning = false;
        glm::vec2 m_lastMousePos;

        // Helper methods
        void updateCameraFromOrbit();
        void renderGrid(VkCommandBuffer commandBuffer);
        void renderGizmos(VkCommandBuffer commandBuffer);
    };

    // Custom vertex for editor-created geometry
    struct CustomVertex {
        glm::vec3 position;
        glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec2 texCoord = glm::vec2(0.0f);
        uint32_t id; // Unique identifier for selection
    };

    // Custom triangle for editor-created geometry
    struct CustomTriangle {
        uint32_t vertexIds[3];
        uint32_t id; // Unique identifier
    };

    /**
     * Wrapper around Taffy assets for editing operations
     */
    class EditableModel {
    public:
        EditableModel();
        ~EditableModel();

        // Load/save operations
        bool loadFromFile(const std::string& filepath);
        bool saveToFile(const std::string& filepath);
        void clear();

        // Model info
        size_t getMeshCount() const { return m_meshes.size(); }
        const Tremor::TaffyMesh* getMesh(size_t index) const;
        uint32_t getMeshRenderId(size_t index) const;

        // Vertex operations
        bool getVertexPosition(uint32_t meshIndex, uint32_t vertexIndex, glm::vec3& position) const;
        bool setVertexPosition(uint32_t meshIndex, uint32_t vertexIndex, const glm::vec3& position);
        
        // Transform operations
        void transformMesh(uint32_t meshIndex, const glm::mat4& transform);
        void transformVertices(uint32_t meshIndex, const std::vector<uint32_t>& vertexIndices, 
                              const glm::mat4& transform);
        
        // Triangle operations
        bool getTriangle(uint32_t meshIndex, uint32_t triangleIndex, 
                        glm::vec3& v0, glm::vec3& v1, glm::vec3& v2) const;
        bool reverseTriangleWinding(uint32_t meshIndex, uint32_t triangleIndex);
        uint32_t getTriangleCount(uint32_t meshIndex) const;

        // Custom mesh creation
        uint32_t addCustomVertex(const glm::vec3& position);
        bool removeCustomVertex(uint32_t vertexId);
        uint32_t addCustomTriangle(uint32_t vertexId1, uint32_t vertexId2, uint32_t vertexId3);
        bool removeCustomTriangle(uint32_t triangleId);
        
        // Custom mesh queries
        const std::vector<CustomVertex>& getCustomVertices() const { return m_customVertices; }
        const std::vector<CustomTriangle>& getCustomTriangles() const { return m_customTriangles; }
        bool getCustomVertexPosition(uint32_t vertexId, glm::vec3& position) const;
        bool updateCustomVertexPosition(uint32_t vertexId, const glm::vec3& newPosition);
        void transformCustomVertices(const std::vector<uint32_t>& vertexIds, const glm::mat4& transform);
        uint32_t findCustomVertexAt(const glm::vec3& position, float tolerance = 0.1f) const;
        
        // Triangle validation
        bool hasDuplicateTriangle(uint32_t vertexId1, uint32_t vertexId2, uint32_t vertexId3) const;

        // Upload to renderer
        bool uploadToRenderer(tremor::gfx::VulkanClusteredRenderer& renderer);
        
        // Mesh preview rendering
        void renderMeshPreview(VkCommandBuffer commandBuffer, 
                              const glm::mat4& viewMatrix, 
                              const glm::mat4& projMatrix,
                              bool wireframe = false,
                              const std::vector<uint32_t>& selectedTriangles = {});

    private:
        std::vector<std::unique_ptr<Tremor::TaffyMesh>> m_meshes;
        std::vector<uint32_t> m_renderMeshIds;
        std::unique_ptr<Taffy::Asset> m_sourceAsset;
        bool m_isDirty = false;

        // Custom mesh data
        std::vector<CustomVertex> m_customVertices;
        std::vector<CustomTriangle> m_customTriangles;
        uint32_t m_nextVertexId = 1;
        uint32_t m_nextTriangleId = 1;

        void markDirty() { m_isDirty = true; }
        
        // Mesh preview buffers
        VkBuffer m_previewVertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory m_previewVertexMemory = VK_NULL_HANDLE;
        VkBuffer m_previewIndexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory m_previewIndexMemory = VK_NULL_HANDLE;
        uint32_t m_previewIndexCount = 0;
        
        void createPreviewBuffers(VkDevice device, VkPhysicalDevice physicalDevice,
                                 VkCommandPool commandPool, VkQueue graphicsQueue);
        void cleanupPreviewBuffers(VkDevice device);
    };

    /**
     * Editor tools for manipulation gizmos and transform operations
     */
    class EditorTools {
    public:
        EditorTools(VkDevice device, VkPhysicalDevice physicalDevice,
                   VkCommandPool commandPool, VkQueue graphicsQueue);
        ~EditorTools();

        bool initialize(VkRenderPass renderPass, VkFormat colorFormat,
                       VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT);

        void setMode(EditorMode mode) { m_currentMode = mode; }
        EditorMode getMode() const { return m_currentMode; }

        // Gizmo interaction
        bool handleMouseInput(const glm::vec2& mousePos, bool pressed, 
                             const glm::mat4& viewMatrix, const glm::mat4& projMatrix,
                             const glm::vec2& viewport);
        
        // Update gizmo position (call this when vertices move)
        void updateGizmoPosition(const glm::vec3& position);
        
        // Get current gizmo position
        glm::vec3 getGizmoPosition() const { return m_gizmoPosition; }
        
        // Gizmo rendering
        void renderGizmo(VkCommandBuffer commandBuffer, const glm::vec3& position,
                        const glm::mat4& viewMatrix, const glm::mat4& projMatrix,
                        const glm::vec2& viewport);

        // Transform calculation
        glm::vec3 calculateTranslation(const glm::vec2& mouseDelta, 
                                      const glm::mat4& viewMatrix, const glm::mat4& projMatrix);
        glm::vec3 calculateRotation(const glm::vec2& mouseDelta);
        glm::vec3 calculateScale(const glm::vec2& mouseDelta);

        // Access to gizmo renderer
        GizmoRenderer* getGizmoRenderer() const { return m_gizmoRenderer.get(); }

    private:
        VkDevice m_device;
        VkPhysicalDevice m_physicalDevice;
        VkCommandPool m_commandPool;
        VkQueue m_graphicsQueue;

        // Gizmo renderer
        std::unique_ptr<GizmoRenderer> m_gizmoRenderer;

        EditorMode m_currentMode = EditorMode::Select;
        
        // Gizmo interaction state
        bool m_isInteracting = false;
        int m_activeAxis = -1;  // 0=X, 1=Y, 2=Z, -1=none
        glm::vec2 m_interactionStart;
        glm::vec3 m_gizmoPosition;

        // Gizmo visual elements
        void renderTranslationGizmo(VkCommandBuffer commandBuffer, const glm::vec3& position,
                                   const glm::mat4& viewProjMatrix);
        void renderRotationGizmo(VkCommandBuffer commandBuffer, const glm::vec3& position,
                                const glm::mat4& viewProjMatrix);
        void renderScaleGizmo(VkCommandBuffer commandBuffer, const glm::vec3& position,
                             const glm::mat4& viewProjMatrix);

        // Hit testing
        int hitTestGizmo(const glm::vec2& mousePos, const glm::vec3& gizmoPos,
                        const glm::mat4& viewMatrix, const glm::mat4& projMatrix);
    };

} // namespace tremor::editor