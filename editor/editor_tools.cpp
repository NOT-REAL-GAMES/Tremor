#include "model_editor.h"
#include "../main.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

namespace tremor::editor {

    // =============================================================================
    // EditorTools Implementation
    // =============================================================================

    EditorTools::EditorTools(VkDevice device, VkPhysicalDevice physicalDevice,
                             VkCommandPool commandPool, VkQueue graphicsQueue)
        : m_device(device), m_physicalDevice(physicalDevice),
          m_commandPool(commandPool), m_graphicsQueue(graphicsQueue) {
    }

    EditorTools::~EditorTools() = default;

    bool EditorTools::initialize(VkRenderPass renderPass, VkFormat colorFormat,
                                 VkSampleCountFlagBits sampleCount) {
        Logger::get().info("Initializing EditorTools");

        // Create gizmo renderer
        m_gizmoRenderer = std::make_unique<GizmoRenderer>(m_device, m_physicalDevice,
                                                         m_commandPool, m_graphicsQueue);
        if (!m_gizmoRenderer->initialize(renderPass, colorFormat, sampleCount)) {
            Logger::get().error("Failed to initialize gizmo renderer");
            return false;
        }

        Logger::get().info("EditorTools initialized successfully");
        return true;
    }

    bool EditorTools::handleMouseInput(const glm::vec2& mousePos, bool pressed, 
                                      const glm::mat4& viewMatrix, const glm::mat4& projMatrix,
                                      const glm::vec2& viewport) {
        if (pressed) {
            // Start interaction
            if (m_gizmoRenderer) {
                // Only log when we actually hit something
                // Logger::get().info("Testing gizmo hit...");
                
                m_activeAxis = m_gizmoRenderer->hitTest(m_currentMode, mousePos, m_gizmoPosition, 
                                                       viewMatrix, projMatrix, viewport);
                // Hit test logging handled in GizmoRenderer
                
                if (m_activeAxis >= 0) {
                    m_isInteracting = true;
                    m_interactionStart = mousePos;
                    Logger::get().info("Started gizmo interaction on axis {}", m_activeAxis);
                    return true;
                }
            }
        } else {
            // End interaction
            if (m_isInteracting) {
                m_isInteracting = false;
                m_activeAxis = -1;
                Logger::get().info("Ended gizmo interaction");
                return true;
            }
        }
        
        return false;
    }

    void EditorTools::renderGizmo(VkCommandBuffer commandBuffer, const glm::vec3& position,
                                 const glm::mat4& viewMatrix, const glm::mat4& projMatrix,
                                 const glm::vec2& viewport) {
        // Use the stored position instead of overwriting it
        glm::vec3 renderPos = m_gizmoPosition;
        
        /*
        
        Logger::get().info("EditorTools::renderGizmo - stored position: ({:.2f}, {:.2f}, {:.2f}), passed position: ({:.2f}, {:.2f}, {:.2f})",
                         m_gizmoPosition.x, m_gizmoPosition.y, m_gizmoPosition.z,
                         position.x, position.y, position.z);
        
        */
        
        if (m_gizmoRenderer && m_currentMode != EditorMode::Select) {
            
            /*
            
            Logger::get().info("Actually rendering gizmo at: ({:.2f}, {:.2f}, {:.2f})", 
                             renderPos.x, renderPos.y, renderPos.z);
            
            */
            
            m_gizmoRenderer->renderGizmo(commandBuffer, m_currentMode, renderPos, 
                                       viewMatrix, projMatrix, m_activeAxis);
        }
    }

    glm::vec3 EditorTools::calculateTranslation(const glm::vec2& mouseDelta, 
                                               const glm::mat4& viewMatrix, const glm::mat4& projMatrix) {
        if (!m_isInteracting || m_activeAxis < 0) {
            Logger::get().info("calculateTranslation: not interacting (interacting={}, axis={})", 
                             m_isInteracting, m_activeAxis);
            return glm::vec3(0.0f);
        }

        // Convert mouse delta to world space movement along the active axis
        float sensitivity = 0.01f;
        glm::vec3 movement(0.0f);
        
        Logger::get().info("calculateTranslation: mouseDelta=({:.1f}, {:.1f}), activeAxis={}", 
                         mouseDelta.x, mouseDelta.y, m_activeAxis);
        
        switch (m_activeAxis) {
            case 0: // X axis
                movement.x = mouseDelta.x * sensitivity;
                break;
            case 1: // Y axis  
                movement.y = -mouseDelta.y * sensitivity; // Invert Y for screen coordinates
                break;
            case 2: // Z axis
                movement.z = mouseDelta.x * sensitivity; // Use X mouse movement for Z axis
                break;
        }

        Logger::get().info("calculateTranslation: resulting movement=({:.3f}, {:.3f}, {:.3f})", 
                         movement.x, movement.y, movement.z);
        return movement;
    }

    void EditorTools::updateGizmoPosition(const glm::vec3& position) {
        m_gizmoPosition = position;
    }

    glm::vec3 EditorTools::calculateRotation(const glm::vec2& mouseDelta) {
        if (!m_isInteracting || m_activeAxis < 0) {
            return glm::vec3(0.0f);
        }

        // Convert mouse delta to rotation around the active axis
        float sensitivity = 0.01f;
        glm::vec3 rotation(0.0f);
        
        switch (m_activeAxis) {
            case 0: // X axis
                rotation.x = mouseDelta.y * sensitivity;
                break;
            case 1: // Y axis
                rotation.y = mouseDelta.x * sensitivity;
                break;
            case 2: // Z axis
                rotation.z = mouseDelta.x * sensitivity;
                break;
        }

        return rotation;
    }

    glm::vec3 EditorTools::calculateScale(const glm::vec2& mouseDelta) {
        if (!m_isInteracting || m_activeAxis < 0) {
            return glm::vec3(1.0f);
        }

        // Convert mouse delta to scale factor (increased sensitivity)
        float sensitivity = 0.05f; // 5x more sensitive than before
        float scaleDelta = mouseDelta.x * sensitivity;
        glm::vec3 scale(1.0f);
        
        switch (m_activeAxis) {
            case 0: // X axis
                scale.x = 1.0f + scaleDelta;
                break;
            case 1: // Y axis
                scale.y = 1.0f + scaleDelta;
                break;
            case 2: // Z axis
                scale.z = 1.0f + scaleDelta;
                break;
            case 3: // Uniform scale (center handle)
                scale = glm::vec3(1.0f + scaleDelta);
                break;
        }

        // Prevent negative or zero scale
        scale.x = std::max(scale.x, 0.1f);
        scale.y = std::max(scale.y, 0.1f);
        scale.z = std::max(scale.z, 0.1f);

        return scale;
    }

    void EditorTools::renderTranslationGizmo(VkCommandBuffer commandBuffer, const glm::vec3& position,
                                           const glm::mat4& viewProjMatrix) {
        // TODO: Implement translation gizmo rendering
        // This would render 3 colored arrows (X=red, Y=green, Z=blue) from the position
        
        // Translation gizmo components:
        // 1. X axis arrow (red) - pointing right
        // 2. Y axis arrow (green) - pointing up  
        // 3. Z axis arrow (blue) - pointing forward
        // 4. Optional: XY, XZ, YZ plane handles for 2D movement
        
        // Rendering approach:
        // 1. Create arrow geometry (cylinder + cone)
        // 2. Transform to gizmo position
        // 3. Scale based on distance to camera (screen-space size)
        // 4. Highlight active axis if being manipulated
        
        Logger::get().debug("Rendering translation gizmo at ({}, {}, {})", 
                          position.x, position.y, position.z);
    }

    void EditorTools::renderRotationGizmo(VkCommandBuffer commandBuffer, const glm::vec3& position,
                                        const glm::mat4& viewProjMatrix) {
        // TODO: Implement rotation gizmo rendering
        // This would render 3 colored circles (X=red, Y=green, Z=blue) around the position
        
        // Rotation gizmo components:
        // 1. X axis circle (red) - rotation around X axis
        // 2. Y axis circle (green) - rotation around Y axis
        // 3. Z axis circle (blue) - rotation around Z axis
        // 4. Optional: Screen-space circle for screen-aligned rotation
        
        Logger::get().debug("Rendering rotation gizmo at ({}, {}, {})", 
                          position.x, position.y, position.z);
    }

    void EditorTools::renderScaleGizmo(VkCommandBuffer commandBuffer, const glm::vec3& position,
                                     const glm::mat4& viewProjMatrix) {
        // TODO: Implement scale gizmo rendering
        // This would render 3 colored lines with cubes at the ends, plus center cube
        
        // Scale gizmo components:
        // 1. X axis line with cube (red)
        // 2. Y axis line with cube (green)
        // 3. Z axis line with cube (blue)
        // 4. Center cube for uniform scaling
        
        Logger::get().debug("Rendering scale gizmo at ({}, {}, {})", 
                          position.x, position.y, position.z);
    }

    int EditorTools::hitTestGizmo(const glm::vec2& mousePos, const glm::vec3& gizmoPos,
                                 const glm::mat4& viewMatrix, const glm::mat4& projMatrix) {
        // TODO: Implement proper gizmo hit testing
        // This would:
        // 1. Project gizmo axis endpoints to screen space
        // 2. Test mouse position against each axis line/handle
        // 3. Return the closest axis (0=X, 1=Y, 2=Z, -1=none)
        
        // For now, return a simple test based on mouse position
        // In a real implementation, this would involve:
        // - Ray casting from mouse position
        // - Intersection testing with gizmo geometry
        // - Distance-based selection of closest element
        
        // Simple placeholder: divide screen into regions
        float screenX = mousePos.x;
        if (screenX < 200.0f) return 0; // X axis region
        if (screenX < 400.0f) return 1; // Y axis region  
        if (screenX < 600.0f) return 2; // Z axis region
        
        return -1; // No axis selected
    }

} // namespace tremor::editor