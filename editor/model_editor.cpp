#include "model_editor.h"
#include "model_editor_ui.h"
#include "../main.h"
#include <algorithm>
#include <limits>
#include <glm/gtc/type_ptr.hpp>

namespace tremor::editor {

    // =============================================================================
    // ModelEditor Implementation
    // =============================================================================

    ModelEditor::ModelEditor(VkDevice device, VkPhysicalDevice physicalDevice,
                           VkCommandPool commandPool, VkQueue graphicsQueue,
                           tremor::gfx::UIRenderer& uiRenderer)
        : m_device(device), m_physicalDevice(physicalDevice),
          m_commandPool(commandPool), m_graphicsQueue(graphicsQueue),
          m_uiRenderer(uiRenderer) {
    }

    ModelEditor::~ModelEditor() = default;

    bool ModelEditor::initialize(VkRenderPass renderPass, VkFormat colorFormat,
                                 VkSampleCountFlagBits sampleCount) {
        Logger::get().info("*** STARTING ModelEditor::initialize() ***");

        // Create viewport
        Logger::get().info("*** Creating EditorViewport ***");
        m_viewport = std::make_unique<EditorViewport>(m_device, m_physicalDevice,
                                                     m_commandPool, m_graphicsQueue);
        if (!m_viewport) {
            Logger::get().error("Failed to create editor viewport");
            return false;
        }
        Logger::get().info("*** EditorViewport created successfully ***");

        Logger::get().info("*** Initializing EditorViewport ***");
        if (!m_viewport->initialize(renderPass, colorFormat, sampleCount)) {
            Logger::get().error("Failed to initialize editor viewport");
            return false;
        }
        Logger::get().info("*** EditorViewport initialized successfully ***");

        // Create UI
        Logger::get().info("*** Creating ModelEditorUI ***");
        m_ui = std::make_unique<ModelEditorUI>(m_uiRenderer, *this);
        if (!m_ui) {
            Logger::get().error("Failed to create editor UI");
            return false;
        }
        Logger::get().info("*** ModelEditorUI created successfully ***");
        Logger::get().info("*** Calling m_ui->initialize() ***");
        m_ui->initialize();
        Logger::get().info("*** m_ui->initialize() completed ***");

        // Hide UI panels initially - they will be shown when editor is enabled
        m_ui->setToolsPanelVisible(false);
        m_ui->setPropertiesPanelVisible(false);
        m_ui->setFilePanelVisible(false);

        // Create editing tools
        m_tools = std::make_unique<EditorTools>(m_device, m_physicalDevice,
                                               m_commandPool, m_graphicsQueue);
        if (!m_tools) {
            Logger::get().error("Failed to create editor tools");
            return false;
        }

        if (!m_tools->initialize(renderPass, colorFormat, sampleCount)) {
            Logger::get().error("Failed to initialize editor tools");
            return false;
        }

        // Create empty model
        m_model = std::make_unique<EditableModel>();
        if (!m_model) {
            Logger::get().error("Failed to create editable model");
            return false;
        }

        Logger::get().info("Model Editor initialized successfully");
        return true;
    }

    void ModelEditor::update(float deltaTime) {
        if (m_viewport) {
            m_viewport->update(deltaTime);
        }

        if (m_ui) {
            m_ui->update();
        }

        updateViewport(deltaTime);
        updateUI();
    }

    void ModelEditor::render(VkCommandBuffer commandBuffer, const glm::mat4& projection) {
        if (m_viewport) {
            m_viewport->render(commandBuffer);
        }

        // Render gizmos if we have a selection
        if (m_tools && (m_currentMode == EditorMode::Move || m_currentMode == EditorMode::Rotate || m_currentMode == EditorMode::Scale)) {
            bool hasSelection = m_selection.hasCustomVertices() || m_selection.hasMesh();
            
            if (hasSelection) {
                // Use the stored gizmo position from EditorTools (updated by updateGizmoPosition)
                glm::vec3 gizmoPos = m_tools->getGizmoPosition();
                
                m_tools->renderGizmo(commandBuffer, gizmoPos, 
                                   m_viewport->getViewMatrix(), 
                                   m_viewport->getProjectionMatrix(),
                                   m_viewportSize);
            }
        }
        
        // Debug: Always render mouse ray visualization
        if (m_tools && m_tools->getGizmoRenderer()) {
            m_tools->getGizmoRenderer()->renderMouseRayDebug(commandBuffer, m_lastMousePos,
                                                            m_viewport->getViewMatrix(),
                                                            m_viewport->getProjectionMatrix(),
                                                            m_viewportSize);
        }

        // Render vertex markers for custom vertices
        if (m_model && m_tools) {
            const auto& customVertices = m_model->getCustomVertices();
            if (!customVertices.empty()) {
                // Extract positions from custom vertices
                std::vector<glm::vec3> positions;
                std::vector<glm::vec3> selectedPositions;
                positions.reserve(customVertices.size());
                
                for (const auto& vertex : customVertices) {
                    // Check if vertex is selected for triangle creation
                    bool isSelectedForTriangle = std::find(m_selectedVerticesForTriangle.begin(), 
                                                          m_selectedVerticesForTriangle.end(), 
                                                          vertex.id) != m_selectedVerticesForTriangle.end();
                    
                    // Check if vertex is selected for transform operations (move/rotate/scale)
                    bool isSelectedForTransform = m_selection.hasCustomVertex(vertex.id);
                    
                    bool isSelected = isSelectedForTriangle || isSelectedForTransform;
                    
                    if (isSelected) {
                        selectedPositions.push_back(vertex.position);
                    } else {
                        positions.push_back(vertex.position);
                    }
                }

                // Render normal vertex markers in yellow
                if (!positions.empty()) {
                    m_tools->getGizmoRenderer()->renderVertexMarkers(
                        commandBuffer, positions,
                        m_viewport->getViewMatrix(), 
                        m_viewport->getProjectionMatrix(),
                        glm::vec3(1.0f, 1.0f, 0.0f), // Yellow color
                        0.5f // Larger size for better visibility
                    );
                }

                // Render selected vertex markers in red using separate buffer
                if (!selectedPositions.empty()) {
                    m_tools->getGizmoRenderer()->renderSelectedVertexMarkers(
                        commandBuffer, selectedPositions,
                        m_viewport->getViewMatrix(), 
                        m_viewport->getProjectionMatrix(),
                        glm::vec3(1.0f, 0.3f, 0.3f), // Red color for selected
                        0.6f // Larger size for better visibility of selected vertices
                    );
                }

                // Render triangle edges
                const auto& customTriangles = m_model->getCustomTriangles();
                if (!customTriangles.empty()) {
                    std::vector<std::pair<glm::vec3, glm::vec3>> edges;
                    
                    for (const auto& triangle : customTriangles) {
                        // Get vertex positions for this triangle
                        glm::vec3 v1, v2, v3;
                        if (m_model->getCustomVertexPosition(triangle.vertexIds[0], v1) &&
                            m_model->getCustomVertexPosition(triangle.vertexIds[1], v2) &&
                            m_model->getCustomVertexPosition(triangle.vertexIds[2], v3)) {
                            
                            // Add three edges of the triangle
                            edges.emplace_back(v1, v2);
                            edges.emplace_back(v2, v3);
                            edges.emplace_back(v3, v1);
                        }
                    }

                    if (!edges.empty()) {
                        m_tools->getGizmoRenderer()->renderTriangleEdges(
                            commandBuffer, edges,
                            m_viewport->getViewMatrix(), 
                            m_viewport->getProjectionMatrix(),
                            glm::vec3(0.0f, 1.0f, 0.5f) // Cyan color for triangle edges
                        );
                    }
                }
            }
        }

        // UI rendering is now handled by main VulkanBackend UIRenderer - skip this section
        if (m_ui) {
            Logger::get().debug("*** ModelEditor: UI elements added to main UIRenderer - skipping local UI render ***");
            m_ui->render(); // This just updates UI state, doesn't actually render
        }
    }

    void ModelEditor::handleInput(const SDL_Event& event) {
        if (m_viewport) {
            m_viewport->handleInput(event);
        }

        // Handle editor-specific input
        if (event.type == SDL_MOUSEBUTTONDOWN) {
            if (event.button.button == SDL_BUTTON_LEFT) {
                glm::vec2 mousePos(event.button.x, event.button.y);
                Logger::get().info("Mouse click at ({:.1f}, {:.1f})", mousePos.x, mousePos.y);
                
                if (isViewportHovered(mousePos)) {
                    Logger::get().info("Click is in viewport - mode: {}, has selection: {}", 
                                     (int)m_currentMode, (m_selection.hasMesh() || m_selection.hasCustomVertices()));
                    if (m_currentMode == EditorMode::Select) {
                        // Selection
                        if (SDL_GetModState() & KMOD_SHIFT) {
                            // Try to select custom vertex first, then regular mesh vertex
                            if (!selectCustomVertex(mousePos)) {
                                selectVertex(mousePos);
                            }
                        } else {
                            selectMesh(mousePos);
                        }
                    } else if (m_currentMode == EditorMode::AddVertex) {
                        // Add vertex at click position
                        addVertexAtScreenPosition(mousePos);
                    } else if (m_currentMode == EditorMode::CreateTriangle) {
                        // Select vertex for triangle creation
                        selectVertexForTriangle(mousePos);
                    } else {
                        // Tool interaction for gizmos
                        if (m_tools && (m_selection.hasMesh() || m_selection.hasCustomVertices())) {
                            bool hitGizmo = m_tools->handleMouseInput(mousePos, true,
                                                    m_viewport->getViewMatrix(),
                                                    m_viewport->getProjectionMatrix(),
                                                    m_viewportSize);
                            if (hitGizmo) {
                                m_isDragging = true;
                                m_lastMousePos = mousePos;
                            }
                        }
                    }
                }
            }
        }
        
        // Mouse button release - for gizmo dragging
        if (event.type == SDL_MOUSEBUTTONUP) {
            if (event.button.button == SDL_BUTTON_LEFT) {
                if (m_tools && (m_selection.hasMesh() || m_selection.hasCustomVertices())) {
                    glm::vec2 mousePos(event.button.x, event.button.y);
                    m_tools->handleMouseInput(mousePos, false,
                                            m_viewport->getViewMatrix(),
                                            m_viewport->getProjectionMatrix(),
                                            m_viewportSize);
                    m_isDragging = false;
                }
            }
        }
        
        // Mouse motion - for gizmo dragging
        if (event.type == SDL_MOUSEMOTION) {
            glm::vec2 mousePos(event.motion.x, event.motion.y);
            
            // If we're dragging a gizmo, update the transform
            if (m_isDragging && m_tools && (m_selection.hasMesh() || m_selection.hasCustomVertices())) {
                glm::vec2 mouseDelta = mousePos - m_lastMousePos;
                Logger::get().info("Mouse dragging with delta ({:.1f}, {:.1f})", mouseDelta.x, mouseDelta.y);
                
                // Calculate transform based on current mode and gizmo interaction
                glm::vec3 delta = m_tools->calculateTranslation(mouseDelta,
                                                               m_viewport->getViewMatrix(),
                                                               m_viewport->getProjectionMatrix());
                
                if (m_currentMode == EditorMode::Move) {
                    Logger::get().info("Applying translation: ({:.3f}, {:.3f}, {:.3f})", delta.x, delta.y, delta.z);
                    translateSelection(delta);
                    updateGizmoPosition(); // Update gizmo position after transform
                } else if (m_currentMode == EditorMode::Rotate) {
                    glm::vec3 rotation = m_tools->calculateRotation(mouseDelta);
                    if (glm::length(rotation) > 0.0f) {
                        Logger::get().info("Applying rotation");
                        rotateSelection(glm::normalize(rotation), glm::length(rotation));
                        updateGizmoPosition(); // Update gizmo position after transform
                    }
                } else if (m_currentMode == EditorMode::Scale) {
                    glm::vec3 scale = m_tools->calculateScale(mouseDelta);
                    Logger::get().info("Applying scale");
                    scaleSelection(scale); // scale already includes base 1.0f
                    updateGizmoPosition(); // Update gizmo position after transform
                }
            }
            
            m_lastMousePos = mousePos;
        }

        // Keyboard shortcuts
        if (event.type == SDL_KEYDOWN) {
            switch (event.key.keysym.sym) {
                case SDLK_g:
                    setMode(EditorMode::Move);
                    break;
                case SDLK_r:
                    setMode(EditorMode::Rotate);
                    break;
                case SDLK_s:
                    if (SDL_GetModState() & KMOD_CTRL) {
                        saveModel();
                    } else {
                        setMode(EditorMode::Scale);
                    }
                    break;
                case SDLK_ESCAPE:
                    setMode(EditorMode::Select);
                    clearSelection();
                    break;
                case SDLK_o:
                    if (SDL_GetModState() & KMOD_CTRL) {
                        // TODO: Open file dialog
                        Logger::get().info("Open file dialog (TODO)");
                    }
                    break;
                case SDLK_n:
                    if (SDL_GetModState() & KMOD_CTRL) {
                        newModel();
                    }
                    break;
                case SDLK_v:
                    setMode(EditorMode::AddVertex);
                    break;
                case SDLK_t:
                    setMode(EditorMode::CreateTriangle);
                    break;
            }
        }
    }

    bool ModelEditor::loadModel(const std::string& filepath) {
        if (!m_model) {
            Logger::get().error("No editable model instance");
            return false;
        }

        Logger::get().info("Loading model: {}", filepath);
        
        if (m_model->loadFromFile(filepath)) {
            m_currentFilePath = filepath;
            m_hasUnsavedChanges = false;
            
            // TODO: Upload to renderer when we have a proper renderer reference
            // For now, just mark as loaded without uploading
            Logger::get().info("Model loaded successfully: {}", filepath);
            Logger::get().warning("Model upload to renderer not implemented yet");
            markModelChanged();
            return true;
        }

        Logger::get().error("Failed to load model: {}", filepath);
        return false;
    }

    bool ModelEditor::saveModel(const std::string& filepath) {
        if (!m_model) {
            Logger::get().error("No model to save");
            return false;
        }

        std::string savePath = filepath.empty() ? m_currentFilePath : filepath;
        if (savePath.empty()) {
            Logger::get().error("No file path specified for save");
            return false;
        }

        Logger::get().info("Saving model: {}", savePath);
        
        if (m_model->saveToFile(savePath)) {
            m_currentFilePath = savePath;
            m_hasUnsavedChanges = false;
            Logger::get().info("Model saved successfully: {}", savePath);
            return true;
        }

        Logger::get().error("Failed to save model: {}", savePath);
        return false;
    }

    bool ModelEditor::newModel() {
        if (m_hasUnsavedChanges) {
            // TODO: Show save dialog
            Logger::get().warning("Unsaved changes will be lost");
        }

        if (m_model) {
            m_model->clear();
            m_currentFilePath.clear();
            m_hasUnsavedChanges = false;
            clearSelection();
            markModelChanged();
            Logger::get().info("New model created");
            return true;
        }

        return false;
    }

    void ModelEditor::setMode(EditorMode mode) {
        if (m_currentMode != mode) {
            m_currentMode = mode;
            
            if (m_tools) {
                m_tools->setMode(mode);
            }
            
            // Update gizmo position when switching to transform modes
            if (mode == EditorMode::Move || mode == EditorMode::Rotate || mode == EditorMode::Scale) {
                updateGizmoPosition();
            }

            if (m_ui) {
                m_ui->onModeChanged(mode);
            }

            const char* modeNames[] = {"Select", "Move", "Rotate", "Scale"};
            //Logger::get().info("Editor mode changed to: {}", modeNames[static_cast<int>(mode)]);
        }
    }

    void ModelEditor::clearSelection() {
        m_selection.clear();
        
        if (m_ui) {
            m_ui->onSelectionChanged(m_selection);
        }

        if (m_selectionChangedCallback) {
            m_selectionChangedCallback();
        }
    }

    bool ModelEditor::selectMesh(const glm::vec2& screenPos) {
        if (!m_model) return false;

        // TODO: Implement ray-mesh intersection testing
        // For now, just select the first mesh if any exist
        if (m_model->getMeshCount() > 0) {
            m_selection.meshId = 0;
            m_selection.vertexIndex = UINT32_MAX;
            m_selection.faceIndex = UINT32_MAX;

            if (m_ui) {
                m_ui->onSelectionChanged(m_selection);
            }

            if (m_selectionChangedCallback) {
                m_selectionChangedCallback();
            }

            Logger::get().info("Selected mesh: {}", m_selection.meshId);
            return true;
        }

        return false;
    }

    bool ModelEditor::selectVertex(const glm::vec2& screenPos) {
        if (!m_model || !m_selection.hasMesh()) return false;

        // Get mesh
        const Tremor::TaffyMesh* mesh = m_model->getMesh(m_selection.meshId);
        if (!mesh) return false;

        // Convert screen position to world ray
        glm::vec3 rayOrigin, rayDirection;
        if (!screenToWorldRay(screenPos, rayOrigin, rayDirection)) {
            return false;
        }

        // Selection parameters - broader selection for better UX
        const float selectionRadius = m_vertexSelectionRadius;  // Configurable world-space selection radius
        const float maxSelectionDistance = 1000.0f;  // Maximum ray distance
        
        uint32_t closestVertex = UINT32_MAX;
        float closestDistance = FLT_MAX;

        // Get actual vertex data from the Taffy mesh using quantized coordinates
        const auto& meshVertices = mesh->get_vertices();
        std::vector<glm::vec3> vertices;
        vertices.reserve(meshVertices.size());
        
        // Convert Vec3Q positions to float for selection calculation
        for (const auto& vertex : meshVertices) {
            vertices.push_back(vertex.position.toFloat());
        }

        // Check each vertex for selection
        for (uint32_t i = 0; i < vertices.size(); ++i) {
            const glm::vec3& vertexPos = vertices[i];
            
            // Calculate point-to-ray distance using cylindrical selection
            glm::vec3 rayToVertex = vertexPos - rayOrigin;
            float rayLength = glm::dot(rayToVertex, rayDirection);
            
            // Skip vertices behind the camera
            if (rayLength < 0.0f || rayLength > maxSelectionDistance) continue;
            
            // Find closest point on ray to vertex
            glm::vec3 closestPointOnRay = rayOrigin + rayDirection * rayLength;
            float distanceToRay = glm::length(vertexPos - closestPointOnRay);
            
            // Check if vertex is within selection cylinder
            if (distanceToRay <= selectionRadius) {
                // Calculate distance from camera for depth sorting
                float distanceFromCamera = glm::length(vertexPos - rayOrigin);
                
                if (distanceFromCamera < closestDistance) {
                    closestDistance = distanceFromCamera;
                    closestVertex = i;
                }
            }
        }

        // Select the closest vertex found
        if (closestVertex != UINT32_MAX) {
            m_selection.vertexIndex = closestVertex;
            m_selection.faceIndex = UINT32_MAX;

            if (m_ui) {
                m_ui->onSelectionChanged(m_selection);
            }

            if (m_selectionChangedCallback) {
                m_selectionChangedCallback();
            }

            Logger::get().info("Selected vertex: {} in mesh: {} (distance: {:.2f})", 
                             m_selection.vertexIndex, m_selection.meshId, closestDistance);
            return true;
        }

        Logger::get().info("No vertex found within selection radius at screen pos ({}, {})", 
                         screenPos.x, screenPos.y);
        return false;
    }

    bool ModelEditor::selectCustomVertex(const glm::vec2& screenPos) {
        if (!m_model || !m_viewport) {
            Logger::get().warning("Cannot select custom vertex: model or viewport not available");
            return false;
        }
        // Convert screen position to world ray
        glm::vec3 rayOrigin, rayDirection;
        if (!screenToWorldRay(screenPos, rayOrigin, rayDirection)) {
            Logger::get().error("Failed to calculate world ray from screen position");
            return false;
        }
        // Find closest custom vertex to the ray
        const auto& vertices = m_model->getCustomVertices();
        uint32_t closestVertexId = 0;
        float closestDistance = std::numeric_limits<float>::max();
        
        for (const auto& vertex : vertices) {
            // Calculate distance from ray to vertex
            glm::vec3 toVertex = vertex.position - rayOrigin;
            float projectionLength = glm::dot(toVertex, rayDirection);
            
            // Only consider vertices in front of the camera
            if (projectionLength > 0.0f) {
                glm::vec3 projectedPoint = rayOrigin + rayDirection * projectionLength;
                float distance = glm::length(vertex.position - projectedPoint);
                
                if (distance < closestDistance && distance < m_vertexSelectionRadius) {
                    closestDistance = distance;
                    closestVertexId = vertex.id;
                }
            }
        }
        
        if (closestVertexId != 0) {
            // Check if Ctrl is held for multi-selection
            bool addToSelection = SDL_GetModState() & KMOD_CTRL;
            
            if (!addToSelection) {
                // Clear existing selection if not adding
                m_selection.clearCustomVertices();
                m_selection.meshId = UINT32_MAX;
                m_selection.vertexIndex = UINT32_MAX;
                m_selection.faceIndex = UINT32_MAX;
            }
            
            // Toggle selection of this vertex
            if (m_selection.hasCustomVertex(closestVertexId)) {
                m_selection.removeCustomVertex(closestVertexId);
                Logger::get().info("Deselected custom vertex {}", closestVertexId);
            } else {
                m_selection.addCustomVertex(closestVertexId);
                Logger::get().info("Selected custom vertex {} ({} total selected)", 
                                 closestVertexId, m_selection.customVertexIds.size());
            }
            
            // Update gizmo position when selection changes
            updateGizmoPosition();
            
            if (m_ui) {
                m_ui->onSelectionChanged(m_selection);
            }
            if (m_selectionChangedCallback) {
                m_selectionChangedCallback();
            }
            return true;
        } else {
            Logger::get().info("No custom vertex found within selection radius at screen pos ({}, {})", 
                             screenPos.x, screenPos.y);
            return false;
        }
    }

    void ModelEditor::translateSelection(const glm::vec3& delta) {
        if (!m_model) return;

        Logger::get().info("Translating selection by: ({}, {}, {})", delta.x, delta.y, delta.z);
        
        // Handle custom vertex selection
        if (m_selection.hasCustomVertices()) {
            // Transform all selected custom vertices
            glm::mat4 transform = glm::translate(glm::mat4(1.0f), delta);
            m_model->transformCustomVertices(m_selection.customVertexIds, transform);
            markModelChanged();
            return;
        }
        
        // Handle mesh vertex/mesh selection
        if (!m_selection.hasMesh()) return;
        
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), delta);
        
        if (m_selection.hasVertex()) {
            // Transform single vertex
            std::vector<uint32_t> vertices = {m_selection.vertexIndex};
            m_model->transformVertices(m_selection.meshId, vertices, transform);
        } else {
            // Transform entire mesh
            m_model->transformMesh(m_selection.meshId, transform);
        }

        markModelChanged();
    }

    void ModelEditor::rotateSelection(const glm::vec3& axis, float angle) {
        if (!m_model) return;

        Logger::get().info("Rotating selection by {} degrees around axis ({}, {}, {})", 
                         glm::degrees(angle), axis.x, axis.y, axis.z);
        
        // Handle custom vertex selection
        if (m_selection.hasCustomVertices()) {
            // Calculate center of selected vertices for rotation pivot
            glm::vec3 center(0.0f);
            int count = 0;
            const auto& vertices = m_model->getCustomVertices();
            for (const auto& vertex : vertices) {
                if (m_selection.hasCustomVertex(vertex.id)) {
                    center += vertex.position;
                    count++;
                }
            }
            if (count > 0) {
                center /= static_cast<float>(count);
                // Rotate around center
                glm::mat4 transform = glm::translate(glm::mat4(1.0f), center);
                transform = glm::rotate(transform, angle, axis);
                transform = glm::translate(transform, -center);
                m_model->transformCustomVertices(m_selection.customVertexIds, transform);
                markModelChanged();
            }
            return;
        }
        
        // Handle mesh vertex/mesh selection
        if (!m_selection.hasMesh()) return;

        glm::mat4 transform = glm::rotate(glm::mat4(1.0f), angle, axis);
        
        if (m_selection.hasVertex()) {
            // Transform single vertex
            std::vector<uint32_t> vertices = {m_selection.vertexIndex};
            m_model->transformVertices(m_selection.meshId, vertices, transform);
        } else {
            // Transform entire mesh
            m_model->transformMesh(m_selection.meshId, transform);
        }

        markModelChanged();
    }

    void ModelEditor::scaleSelection(const glm::vec3& scale) {
        if (!m_model) return;

        Logger::get().info("Scaling selection by: ({}, {}, {})", scale.x, scale.y, scale.z);
        
        // Handle custom vertex selection
        if (m_selection.hasCustomVertices()) {
            // Calculate center of selected vertices for scale pivot
            glm::vec3 center(0.0f);
            int count = 0;
            const auto& vertices = m_model->getCustomVertices();
            for (const auto& vertex : vertices) {
                if (m_selection.hasCustomVertex(vertex.id)) {
                    center += vertex.position;
                    count++;
                }
            }
            if (count > 0) {
                center /= static_cast<float>(count);
                // Scale from center
                glm::mat4 transform = glm::translate(glm::mat4(1.0f), center);
                transform = glm::scale(transform, scale);
                transform = glm::translate(transform, -center);
                m_model->transformCustomVertices(m_selection.customVertexIds, transform);
                markModelChanged();
            }
            return;
        }
        
        // Handle mesh vertex/mesh selection
        if (!m_selection.hasMesh()) return;

        glm::mat4 transform = glm::scale(glm::mat4(1.0f), scale);
        
        if (m_selection.hasVertex()) {
            // Transform single vertex
            std::vector<uint32_t> vertices = {m_selection.vertexIndex};
            m_model->transformVertices(m_selection.meshId, vertices, transform);
        } else {
            // Transform entire mesh
            m_model->transformMesh(m_selection.meshId, transform);
        }

        markModelChanged();
    }

    void ModelEditor::setViewportSize(glm::vec2 size) {
        m_viewportSize = size;
        if (m_viewport) {
            m_viewport->setViewportSize(size);
        }
    }

    void ModelEditor::setScissorSize(glm::vec2 size) {
        m_scissorSize = size;
        if (m_viewport) {
            m_viewport->setScissorSize(size);
        }
    }


    void ModelEditor::updateViewport(float deltaTime) {
        // Update viewport camera based on current mode and interaction
    }

    void ModelEditor::updateUI() {
        // UI updates are handled by ModelEditorUI
    }

    void ModelEditor::markModelChanged() {
        m_hasUnsavedChanges = true;
        
        if (m_ui) {
            m_ui->onModelChanged();
        }

        if (m_modelChangedCallback) {
            m_modelChangedCallback();
        }
    }

    bool ModelEditor::isViewportHovered(const glm::vec2& mousePos) const {
        // TODO: Implement proper viewport bounds checking
        // For now, assume viewport covers most of the screen except UI panels
        return mousePos.x > 220.0f; // Leave space for UI panels
    }

    void ModelEditor::updateGizmoPosition() {
        if (!m_tools || !m_model) {
            Logger::get().info("updateGizmoPosition: tools or model is null");
            return;
        }
        
        Logger::get().info("updateGizmoPosition: called with {} selected vertices", 
                         m_selection.customVertexIds.size());
        
        // Recalculate gizmo position based on current selection
        glm::vec3 gizmoPos(0.0f);
        bool hasSelection = false;
        
        if (m_selection.hasCustomVertices()) {
            const auto& vertices = m_model->getCustomVertices();
            int count = 0;
            Logger::get().info("Total vertices in model: {}", vertices.size());
            
            for (const auto& vertex : vertices) {
                if (m_selection.hasCustomVertex(vertex.id)) {
                    Logger::get().info("Adding vertex {} at ({:.2f}, {:.2f}, {:.2f}) to gizmo calculation", 
                                     vertex.id, vertex.position.x, vertex.position.y, vertex.position.z);
                    gizmoPos += vertex.position;
                    count++;
                }
            }
            if (count > 0) {
                gizmoPos /= static_cast<float>(count);
                hasSelection = true;
                Logger::get().info("Calculated average position: ({:.2f}, {:.2f}, {:.2f}) from {} vertices", 
                                 gizmoPos.x, gizmoPos.y, gizmoPos.z, count);
            } else {
                Logger::get().info("Count is 0 - no matching vertices found!");
            }
        } else {
            Logger::get().info("No custom vertices selected");
        }
        
        if (hasSelection) {
            m_tools->updateGizmoPosition(gizmoPos);
            Logger::get().info("Updated gizmo position to ({:.2f}, {:.2f}, {:.2f})", 
                             gizmoPos.x, gizmoPos.y, gizmoPos.z);
        } else {
            Logger::get().info("No selection found for gizmo position update");
        }
    }

    glm::vec3 ModelEditor::screenToWorld(const glm::vec2& screenPos, float depth) const {
        if (!m_viewport) return glm::vec3(0.0f);

        // Convert screen coordinates to normalized device coordinates
        glm::vec2 ndc = glm::vec2(
            (2.0f * screenPos.x) / m_viewportSize.x - 1.0f,
            1.0f - (2.0f * screenPos.y) / m_viewportSize.y
        );

        // Create point in clip space
        glm::vec4 clipPos(ndc.x, ndc.y, depth, 1.0f);

        // Transform to world space
        glm::mat4 invViewProj = glm::inverse(m_viewport->getProjectionMatrix() * m_viewport->getViewMatrix());
        glm::vec4 worldPos = invViewProj * clipPos;
        
        if (worldPos.w != 0.0f) {
            worldPos /= worldPos.w;
        }

        return glm::vec3(worldPos);
    }

    bool ModelEditor::screenToWorldRay(const glm::vec2& screenPos, glm::vec3& rayOrigin, glm::vec3& rayDirection) const {
        if (!m_viewport) return false;

        // Get camera position as ray origin
        rayOrigin = m_viewport->getPosition();

        // Convert screen position to normalized device coordinates
        // Try flipping X in NDC space instead of ray direction
        glm::vec2 ndc = glm::vec2(
            -((2.0f * screenPos.x) / m_viewportSize.x - 1.0f),  // Flip X: right(-1) to left(+1)
            -((2.0f * screenPos.y) / m_viewportSize.y - 1.0f)      // Y: bottom(-1) to top(+1)
        );

        // Create ray end point in clip space (at far plane)
        glm::vec4 rayEndClip(-ndc.x, -ndc.y, 1.0f, 1.0f);

        // Transform to world space
        glm::mat4 invViewProj = glm::inverse(m_viewport->getProjectionMatrix() * m_viewport->getViewMatrix());
        glm::vec4 rayEndWorld = invViewProj * rayEndClip;
        
        if (rayEndWorld.w != 0.0f) {
            rayEndWorld /= rayEndWorld.w;
        }

        // Calculate ray direction
        rayDirection = glm::normalize(glm::vec3(rayEndWorld) - rayOrigin);
        
        return true;
    }

    void ModelEditor::addVertexAtScreenPosition(const glm::vec2& screenPos) {
        if (!m_model || !m_viewport) {
            Logger::get().warning("Cannot add vertex: model or viewport not available");
            return;
        }

        // Convert screen position to world ray
        glm::vec3 rayOrigin, rayDirection;
        if (!screenToWorldRay(screenPos, rayOrigin, rayDirection)) {
            Logger::get().error("Failed to calculate world ray from screen position");
            return;
        }

        // Place vertex on ground plane (Y = 0) using ray-plane intersection
        glm::vec3 worldPos;
        if (rayDirection.y != 0.0f) {
            // Intersect ray with ground plane (Y = 0)
            float t = -rayOrigin.y / rayDirection.y;
            if (t > 0.0f) {
                worldPos = rayOrigin + rayDirection * t;
            } else {
                // Fallback: place at fixed distance if ray doesn't hit ground plane
                float placeDistance = 10.0f;
                worldPos = rayOrigin + rayDirection * placeDistance;
            }
        } else {
            // Ray is parallel to ground plane, place at fixed distance
            float placeDistance = 10.0f;
            worldPos = rayOrigin + rayDirection * placeDistance;
        }

        // Also log NDC coordinates for debugging
        glm::vec2 ndc_debug = glm::vec2(
            (2.0f * screenPos.x) / m_viewportSize.x - 1.0f,
            1.0f - (2.0f * screenPos.y) / m_viewportSize.y
        );

        // Debug logging
        Logger::get().info("Vertex placement debug:");
        Logger::get().info("  Screen pos: ({:.0f}, {:.0f})", screenPos.x, screenPos.y);
        Logger::get().info("  Viewport size: ({:.0f}, {:.0f})", m_viewportSize.x, m_viewportSize.y);
        Logger::get().info("  NDC: ({:.2f}, {:.2f})", ndc_debug.x, ndc_debug.y);
        Logger::get().info("  Ray origin (camera): ({:.2f}, {:.2f}, {:.2f})", rayOrigin.x, rayOrigin.y, rayOrigin.z);
        Logger::get().info("  Ray direction: ({:.2f}, {:.2f}, {:.2f})", rayDirection.x, rayDirection.y, rayDirection.z);
        Logger::get().info("  Camera target: ({:.2f}, {:.2f}, {:.2f})", m_viewport->getTarget().x, m_viewport->getTarget().y, m_viewport->getTarget().z);

        // Add vertex to the custom mesh
        uint32_t vertexId = m_model->addCustomVertex(worldPos);
        
        if (vertexId != 0) {
            Logger::get().info("Added vertex {} at world position ({:.2f}, {:.2f}, {:.2f})", 
                              vertexId, worldPos.x, worldPos.y, worldPos.z);
            
            // Trigger model changed callback
            markModelChanged();
        } else {
            Logger::get().error("Failed to add vertex at screen position ({:.0f}, {:.0f})", screenPos.x, screenPos.y);
        }
    }

    void ModelEditor::selectVertexForTriangle(const glm::vec2& screenPos) {
        if (!m_model || !m_viewport) {
            Logger::get().warning("Cannot select vertex: model or viewport not available");
            return;
        }

        // Convert screen position to world ray
        glm::vec3 rayOrigin, rayDirection;
        if (!screenToWorldRay(screenPos, rayOrigin, rayDirection)) {
            Logger::get().error("Failed to calculate world ray from screen position");
            return;
        }

        // Find closest custom vertex to the ray
        const auto& vertices = m_model->getCustomVertices();
        uint32_t closestVertexId = 0;
        float closestDistance = std::numeric_limits<float>::max();

        for (const auto& vertex : vertices) {
            // Calculate distance from ray to vertex
            glm::vec3 toVertex = vertex.position - rayOrigin;
            float projectionLength = glm::dot(toVertex, rayDirection);
            
            // Only consider vertices in front of the camera
            if (projectionLength > 0.0f) {
                glm::vec3 projectedPoint = rayOrigin + rayDirection * projectionLength;
                float distance = glm::length(vertex.position - projectedPoint);
                
                if (distance < closestDistance && distance < m_vertexSelectionRadius) {
                    closestDistance = distance;
                    closestVertexId = vertex.id;
                }
            }
        }

        if (closestVertexId != 0) {
            // Check if vertex is already selected
            auto it = std::find(m_selectedVerticesForTriangle.begin(), m_selectedVerticesForTriangle.end(), closestVertexId);
            if (it != m_selectedVerticesForTriangle.end()) {
                // Deselect vertex
                m_selectedVerticesForTriangle.erase(it);
                Logger::get().info("Deselected vertex {} for triangle creation", closestVertexId);
            } else {
                // Select vertex
                m_selectedVerticesForTriangle.push_back(closestVertexId);
                Logger::get().info("Selected vertex {} for triangle creation ({}/3)", 
                                  closestVertexId, m_selectedVerticesForTriangle.size());

                // If we have 3 vertices, create a triangle
                if (m_selectedVerticesForTriangle.size() == 3) {
                    uint32_t triangleId = m_model->addCustomTriangle(
                        m_selectedVerticesForTriangle[0],
                        m_selectedVerticesForTriangle[1],
                        m_selectedVerticesForTriangle[2]);

                    if (triangleId != 0) {
                        Logger::get().info("Created triangle {} from vertices ({}, {}, {})", 
                                          triangleId, 
                                          m_selectedVerticesForTriangle[0],
                                          m_selectedVerticesForTriangle[1],
                                          m_selectedVerticesForTriangle[2]);
                        markModelChanged();
                    }

                    // Clear selection for next triangle
                    m_selectedVerticesForTriangle.clear();
                }
            }
        } else {
            Logger::get().info("No vertex found near click position");
        }
    }

} // namespace tremor::editor