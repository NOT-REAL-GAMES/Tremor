#include "model_editor.h"
#include "../main.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

namespace tremor::editor {

    // =============================================================================
    // EditorViewport Implementation
    // =============================================================================

    EditorViewport::EditorViewport(VkDevice device, VkPhysicalDevice physicalDevice,
                                  VkCommandPool commandPool, VkQueue graphicsQueue)
        : m_device(device), m_physicalDevice(physicalDevice),
          m_commandPool(commandPool), m_graphicsQueue(graphicsQueue) {
        
        // Initialize camera to look at origin
        updateCameraFromOrbit();
        
        // Calculate initial step duration
        m_stepDuration = std::chrono::milliseconds(static_cast<int>((60.0f / m_orbitRadius) * 1000.0f)).count();
        m_lastStepTime = (float)std::chrono::steady_clock::now().time_since_epoch().count();
    }

    EditorViewport::~EditorViewport() = default;

    bool EditorViewport::initialize(VkRenderPass renderPass, VkFormat colorFormat,
                                   VkSampleCountFlagBits sampleCount) {
        Logger::get().info("Initializing EditorViewport");

        // Create grid renderer
        m_gridRenderer = std::make_unique<GridRenderer>(m_device, m_physicalDevice,
                                                       m_commandPool, m_graphicsQueue);
        if (!m_gridRenderer->initialize(renderPass, colorFormat, sampleCount)) {
            Logger::get().error("Failed to initialize grid renderer");
            return false;
        }

        Logger::get().info("EditorViewport initialized successfully");
        return true;
    }

    void EditorViewport::update(float deltaTime) {
        // Update camera position from orbital controls
        updateCameraFromOrbit();
    }

    void EditorViewport::render(VkCommandBuffer commandBuffer) {
        if (m_showGrid && m_gridRenderer && m_gridRenderingEnabled && !GridRenderer::isGlobalRenderingBlocked()) {
            VkExtent2D viewportExtent = {
                static_cast<uint32_t>(m_viewportSize.x),
                static_cast<uint32_t>(m_viewportSize.y)
            };
            VkExtent2D scissorExtent = {
                static_cast<uint32_t>(m_scissorSize.x),
                static_cast<uint32_t>(m_scissorSize.y)
            };
            m_gridRenderer->render(commandBuffer, getViewMatrix(), getProjectionMatrix(), viewportExtent, scissorExtent);
        }

        if (m_showGizmos) {
            renderGizmos(commandBuffer);
        }
    }

    void EditorViewport::handleInput(const SDL_Event& event) {
        if (event.type == SDL_MOUSEBUTTONDOWN) {
            if (event.button.button == SDL_BUTTON_MIDDLE || 
                (event.button.button == SDL_BUTTON_LEFT && (SDL_GetModState() & KMOD_ALT))) {
                m_isOrbiting = true;
                m_lastMousePos = glm::vec2(event.button.x, event.button.y);
            } else if (event.button.button == SDL_BUTTON_RIGHT || 
                      (event.button.button == SDL_BUTTON_MIDDLE && (SDL_GetModState() & KMOD_SHIFT))) {
                m_isPanning = true;
                m_lastMousePos = glm::vec2(event.button.x, event.button.y);
            }
        } else if (event.type == SDL_MOUSEBUTTONUP) {
            if (event.button.button == SDL_BUTTON_MIDDLE || event.button.button == SDL_BUTTON_LEFT) {
                m_isOrbiting = false;
            }
            if (event.button.button == SDL_BUTTON_RIGHT || event.button.button == SDL_BUTTON_MIDDLE) {
                m_isPanning = false;
            }
        } else if (event.type == SDL_MOUSEMOTION) {
            glm::vec2 currentMousePos(event.motion.x, event.motion.y);
            glm::vec2 mouseDelta = currentMousePos - m_lastMousePos;

            if (m_isOrbiting) {
                // Orbital camera controls
                float sensitivity = 0.5f;
                m_orbitTheta += mouseDelta.x * sensitivity;
                m_orbitPhi -= mouseDelta.y * sensitivity;
                
                // Clamp phi to prevent camera flip
                m_orbitPhi = std::clamp(m_orbitPhi, 1.0f, 179.0f);
                
                updateCameraFromOrbit();
            } else if (m_isPanning) {
                // Pan camera target
                float sensitivity = 0.01f;
                glm::vec3 right = glm::normalize(glm::cross(m_cameraPos - m_cameraTarget, m_cameraUp));
                glm::vec3 up = glm::normalize(glm::cross(right, m_cameraPos - m_cameraTarget));
                
                m_cameraTarget += -right * mouseDelta.x * sensitivity * m_orbitRadius * 0.1f;
                m_cameraTarget += up * mouseDelta.y * sensitivity * m_orbitRadius * 0.1f;
                
                updateCameraFromOrbit();
            }

            m_lastMousePos = currentMousePos;
        } else if (event.type == SDL_MOUSEWHEEL) {
            // Zoom in/out by adjusting orbit radius
            float zoomSensitivity = 0.1f;
            m_orbitRadius -= event.wheel.y * zoomSensitivity * m_orbitRadius;
            m_orbitRadius = std::clamp(m_orbitRadius, 0.5f, 100.0f);
            
            updateCameraFromOrbit();
        }
    }

    void EditorViewport::setViewportSize(glm::vec2 size) {
        m_viewportSize = size;
    }

    void EditorViewport::setScissorSize(glm::vec2 size) {
        m_scissorSize = size;
    }

    glm::mat4 EditorViewport::getViewMatrix() const {
        return glm::lookAt(m_cameraPos, m_cameraTarget, m_cameraUp);
    }

    glm::mat4 EditorViewport::getProjectionMatrix() const {
        float aspect = m_viewportSize.x / m_viewportSize.y;
        glm::mat4 proj = glm::perspective(glm::radians(m_fov), aspect, m_nearPlane, m_farPlane);
        
        // Flip Y axis for Vulkan coordinate system
        proj[1][1] *= -1;
        
        return proj;
    }

    void EditorViewport::updateCameraFromOrbit() {
        // Convert spherical coordinates to cartesian
        float thetaRad = glm::radians(m_orbitTheta);
        float phiRad = glm::radians(m_orbitPhi);
        
        float x = m_orbitRadius * sin(phiRad) * cos(thetaRad);
        float y = m_orbitRadius * cos(phiRad);
        float z = m_orbitRadius * sin(phiRad) * sin(thetaRad);
        
        m_cameraPos = m_cameraTarget + glm::vec3(x, y, z);
    }

    void EditorViewport::renderGrid(VkCommandBuffer commandBuffer) {
        // TODO: Implement grid rendering
        // This would typically render a 3D grid on the ground plane
        // For now, this is a placeholder
        
        // Grid rendering would involve:
        // 1. Create grid vertices (lines from -gridSize to +gridSize)
        // 2. Upload to vertex buffer
        // 3. Render with simple line shader
        // 4. Use different colors for major/minor grid lines
    }

    void EditorViewport::renderGizmos(VkCommandBuffer commandBuffer) {
        // TODO: Implement coordinate axis gizmos
        // This would render X/Y/Z axis indicators in the corner of the viewport
        
        // Gizmo rendering would involve:
        // 1. Render X axis (red line/arrow)
        // 2. Render Y axis (green line/arrow)  
        // 3. Render Z axis (blue line/arrow)
        // 4. Position in corner of viewport with fixed size
    }

} // namespace tremor::editor