#include "model_editor.h"
#include "model_editor_ui.h"
#include "../main.h"
#include <algorithm>
#include <limits>
#include <filesystem>
#include <glm/gtc/type_ptr.hpp>

namespace tremor::editor {

    // =============================================================================
    // ModelEditor Implementation
    // =============================================================================

    ModelEditor::ModelEditor(VkDevice device, VkPhysicalDevice physicalDevice,
                           VkCommandPool commandPool, VkQueue graphicsQueue,
                           tremor::gfx::UIRenderer& uiRenderer, tremor::gfx::VulkanBackend& backend)
        : m_device(device), m_physicalDevice(physicalDevice),
          m_commandPool(commandPool), m_graphicsQueue(graphicsQueue),
          m_uiRenderer(uiRenderer), m_backend(backend) {
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
        
        // Render mesh preview if enabled
        if (m_showMeshPreview && m_model && m_tools && m_tools->getGizmoRenderer()) {
            renderMeshPreview(commandBuffer);
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
                        if (SDL_GetModState() & KMOD_CTRL) {
                            // Triangle selection with Ctrl key
                            bool addToSelection = (SDL_GetModState() & KMOD_SHIFT) != 0;
                            selectTriangle(mousePos, addToSelection);
                        } else {
                            // Try to select custom vertex first, then regular mesh vertex
                            // Single click = single selection, Shift+click = multi-selection
                            if (!selectCustomVertex(mousePos)) {
                                selectVertex(mousePos);
                            }
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
                    if (SDL_GetModState() & KMOD_CTRL) {
                        // Ctrl+R: Reverse winding order
                        reverseWindingOrder();
                    } else {
                        setMode(EditorMode::Rotate);
                    }
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
                case SDLK_p:
                    // Toggle mesh preview
                    setShowMeshPreview(!m_showMeshPreview);
                    Logger::get().info("Mesh preview toggled: {}", m_showMeshPreview ? "on" : "off");
                    break;
                case SDLK_w:
                    if (SDL_GetModState() & KMOD_CTRL) {
                        // Ctrl+W: Toggle wireframe
                        setWireframeMode(!m_wireframeMode);
                        Logger::get().info("Wireframe mode toggled: {}", m_wireframeMode ? "on" : "off");
                    }
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

        // Clear any existing model data before loading new asset
        m_model->clear();

        // Clear selection and update UI
        clearSelection();
        if (m_selectionChangedCallback) {
            m_selectionChangedCallback();
        }

        if (m_model->loadFromFile(filepath)) {
            m_currentFilePath = filepath;
            m_hasUnsavedChanges = false;
            
            // Upload loaded meshes to renderer
            size_t meshCount = m_model->getMeshCount();
            Logger::get().info("Model loaded successfully: {}, uploading {} meshes to renderer", filepath, meshCount);

            auto* clusteredRenderer = m_backend.getClusteredRenderer();
            if (clusteredRenderer) {
                for (size_t i = 0; i < meshCount; ++i) {
                    const auto* mesh = m_model->getMesh(i);
                    if (mesh) {
                        // Get a mutable copy of vertices for potential scaling
                        auto scaledVertices = mesh->get_vertices();
                        const auto& indices = mesh->get_indices();

                        // Check if mesh is too small and scale if needed
                        if (!scaledVertices.empty()) {
                            glm::vec3 minPos = scaledVertices[0].position;
                            glm::vec3 maxPos = scaledVertices[0].position;
                            for (const auto& vertex : scaledVertices) {
                                minPos = glm::min(minPos, vertex.position);
                                maxPos = glm::max(maxPos, vertex.position);
                            }
                            glm::vec3 center = (minPos + maxPos) * 0.5f;
                            glm::vec3 size = maxPos - minPos;

                            Logger::get().info("Original mesh {} bounds: min({:.3f}, {:.3f}, {:.3f}) max({:.3f}, {:.3f}, {:.3f})",
                                             i, minPos.x, minPos.y, minPos.z, maxPos.x, maxPos.y, maxPos.z);
                            Logger::get().info("Original mesh {} center: ({:.3f}, {:.3f}, {:.3f}) size: ({:.3f}, {:.3f}, {:.3f})",
                                             i, center.x, center.y, center.z, size.x, size.y, size.z);

                            // TEMPORARILY DISABLED: 128000x scaling correction
                            // Check if this looks like incorrectly scaled quantized coordinates
                            // Only apply scaling to very tiny meshes that appear to be quantized coordinates
                            // Editor-created meshes should already be at the correct scale
                            float maxDimension = std::max({size.x, size.y, size.z});
                            Logger::get().info("Mesh {} max dimension: {:.6f}", i, maxDimension);
                            
                            if (maxDimension < 0.01f && maxDimension > 0.0f) {  // Much stricter threshold
                                // This looks like quantized coordinates that were over-scaled during conversion
                                // Apply the quantized coordinate scale factor (128000)
                                constexpr float QUANTIZED_SCALE_CORRECTION = 1000.0f;
                                Logger::get().info("Mesh {} appears to be quantized coordinates (max dimension: {:.6f}), applying scale correction: {}x",
                                                 i, maxDimension, QUANTIZED_SCALE_CORRECTION);

                                // Apply quantized scale correction to all vertices
                                for (auto& vertex : scaledVertices) {
                                    vertex.position *= QUANTIZED_SCALE_CORRECTION;
                                }

                                // Update bounds after scaling
                                minPos *= QUANTIZED_SCALE_CORRECTION;
                                maxPos *= QUANTIZED_SCALE_CORRECTION;
                                center *= QUANTIZED_SCALE_CORRECTION;
                                size *= QUANTIZED_SCALE_CORRECTION;

                                Logger::get().info("Corrected mesh {} bounds: min({:.3f}, {:.3f}, {:.3f}) max({:.3f}, {:.3f}, {:.3f})",
                                                 i, minPos.x, minPos.y, minPos.z, maxPos.x, maxPos.y, maxPos.z);
                                Logger::get().info("Corrected mesh {} center: ({:.3f}, {:.3f}, {:.3f}) size: ({:.3f}, {:.3f}, {:.3f})",
                                                 i, center.x, center.y, center.z, size.x, size.y, size.z);

                                // Update the mesh stored in EditableModel with scaled vertices
                                auto* editableMesh = const_cast<Tremor::TaffyMesh*>(mesh);
                                editableMesh->update_vertices(scaledVertices);
                                Logger::get().info("Updated EditableModel mesh {} with scaled vertices", i);
                            }
                            
                        }

                        std::string meshName = std::filesystem::path(filepath).filename().string() + "_mesh_" + std::to_string(i);
                        uint32_t meshId = clusteredRenderer->loadMesh(scaledVertices, indices, meshName);

                        if (meshId != UINT32_MAX) {
                            Logger::get().info("Successfully uploaded mesh {} to renderer with ID: {}", i, meshId);

                            // Create a default material for the mesh using clustered renderer
                            tremor::gfx::PBRMaterial material;
                            material.baseColor = glm::vec4(0.7f, 0.7f, 0.7f, 1.0f); // Light gray
                            material.metallic = 0.0f;
                            material.roughness = 0.5f;
                            material.normalScale = 1.0f;
                            material.occlusionStrength = 1.0f;
                            material.emissiveColor = glm::vec3(0.0f);
                            material.emissiveFactor = 0.0f;

                            uint32_t materialId = clusteredRenderer->createMaterial(material);
                            Logger::get().info("Created material for mesh {} with ID: {}", i, materialId);

                            // Note: Mesh is uploaded and should be visible in editor viewport
                            Logger::get().info("Mesh uploaded to clustered renderer and material created - should be visible in editor preview");
                        } else {
                            Logger::get().error("Failed to upload mesh {} to renderer", i);
                        }
                    } else {
                        Logger::get().error("Mesh {} is null", i);
                    }
                }
            } else {
                Logger::get().error("Clustered renderer is not available");
            }

            // Import the loaded mesh vertices as custom vertices for editing
            // BUT only if this asset wasn't already saved from editor (to avoid duplicates)
            if (meshCount > 0 && !m_model->isEditorModified()) {
                Logger::get().info("Converting loaded mesh vertices to custom vertices for editing");
                m_model->importMeshVerticesAsCustom(0); // Import the first mesh
            } else if (m_model->isEditorModified()) {
                Logger::get().info("Asset was editor-modified - skipping mesh-to-custom conversion to avoid duplicates");
            }

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

        Logger::get().info("Saving model as Taffy asset: {}", savePath);

        // Save as Taffy asset format
        if (saveMeshAsTaffyAsset(savePath)) {
            m_currentFilePath = savePath;
            m_hasUnsavedChanges = false;
            Logger::get().info("Model saved successfully as Taffy asset: {}", savePath);
            return true;
        }

        Logger::get().error("Failed to save model as Taffy asset: {}", savePath);
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
            m_ui->onSelectionChanged(m_selection, m_model.get());
        }

        if (m_selectionChangedCallback) {
            m_selectionChangedCallback();
        }
    }
    
    bool ModelEditor::selectTriangle(const glm::vec2& screenPos, bool addToSelection) {
        if (!m_model) return false;
        
        // Get ray from screen position
        glm::vec3 rayOrigin, rayDirection;
        if (!screenToWorldRay(screenPos, rayOrigin, rayDirection)) {
            return false;
        }
        
        float closestT = std::numeric_limits<float>::max();
        uint32_t closestTriangleIdx = UINT32_MAX;
        uint32_t closestMeshIdx = UINT32_MAX;
        
        // Check triangles in loaded meshes
        for (uint32_t meshIdx = 0; meshIdx < m_model->getMeshCount(); ++meshIdx) {
            uint32_t triangleCount = m_model->getTriangleCount(meshIdx);
            
            for (uint32_t triIdx = 0; triIdx < triangleCount; ++triIdx) {
                glm::vec3 v0, v1, v2;
                if (m_model->getTriangle(meshIdx, triIdx, v0, v1, v2)) {
                    float t;
                    if (rayTriangleIntersect(rayOrigin, rayDirection, v0, v1, v2, t)) {
                        if (t < closestT) {
                            closestT = t;
                            closestTriangleIdx = triIdx;
                            closestMeshIdx = meshIdx;
                        }
                    }
                }
            }
        }
        
        // Check custom triangles
        const auto& customTriangles = m_model->getCustomTriangles();
        for (size_t i = 0; i < customTriangles.size(); ++i) {
            const auto& tri = customTriangles[i];
            glm::vec3 v0, v1, v2;
            if (m_model->getCustomVertexPosition(tri.vertexIds[0], v0) &&
                m_model->getCustomVertexPosition(tri.vertexIds[1], v1) &&
                m_model->getCustomVertexPosition(tri.vertexIds[2], v2)) {
                float t;
                if (rayTriangleIntersect(rayOrigin, rayDirection, v0, v1, v2, t)) {
                    if (t < closestT) {
                        closestT = t;
                        // Use high bit to indicate custom triangle
                        closestTriangleIdx = static_cast<uint32_t>(i) | 0x80000000;
                        closestMeshIdx = UINT32_MAX;
                    }
                }
            }
        }
        
        if (closestTriangleIdx != UINT32_MAX) {
            if (!addToSelection) {
                m_selection.clearTriangles();
            }
            
            // Store mesh index in high 16 bits, triangle index in low 16 bits
            uint32_t combinedIdx = (closestMeshIdx << 16) | (closestTriangleIdx & 0xFFFF);
            if (closestTriangleIdx & 0x80000000) {
                // Custom triangle - store with special marker
                combinedIdx = closestTriangleIdx;
            }
            
            if (m_selection.hasTriangle(combinedIdx)) {
                m_selection.removeTriangle(combinedIdx);
                Logger::get().info("Deselected triangle {}", combinedIdx & 0xFFFF);
            } else {
                m_selection.addTriangle(combinedIdx);
                Logger::get().info("Selected triangle {} ({} total selected)",
                                 combinedIdx & 0xFFFF, m_selection.selectedTriangles.size());
            }
            
            if (m_ui) {
                m_ui->onSelectionChanged(m_selection, m_model.get());
            }
            if (m_selectionChangedCallback) {
                m_selectionChangedCallback();
            }
            return true;
        }
        
        return false;
    }
    
    void ModelEditor::reverseWindingOrder() {
        if (!m_model || !m_selection.hasSelectedTriangles()) {
            Logger::get().warning("No triangles selected for winding order reversal");
            return;
        }
        
        for (uint32_t combinedIdx : m_selection.selectedTriangles) {
            reverseWindingOrderForTriangle(combinedIdx);
        }
        
        markModelChanged();
        Logger::get().info("Reversed winding order for {} triangles", m_selection.selectedTriangles.size());
    }
    
    void ModelEditor::reverseWindingOrderForTriangle(uint32_t triangleIdx) {
        if (!m_model) return;
        
        if (triangleIdx & 0x80000000) {
            // Custom triangle
            uint32_t idx = triangleIdx & 0x7FFFFFFF;
            auto& customTriangles = const_cast<std::vector<CustomTriangle>&>(m_model->getCustomTriangles());
            if (idx < customTriangles.size()) {
                // Swap vertex 1 and 2 to reverse winding
                std::swap(customTriangles[idx].vertexIds[1], customTriangles[idx].vertexIds[2]);
                Logger::get().info("Reversed winding order for custom triangle {}", idx);
            }
        } else {
            // Regular mesh triangle
            uint32_t meshIdx = (triangleIdx >> 16) & 0xFFFF;
            uint32_t triIdx = triangleIdx & 0xFFFF;
            m_model->reverseTriangleWinding(meshIdx, triIdx);
        }
    }
    
    bool ModelEditor::rayTriangleIntersect(const glm::vec3& rayOrigin, const glm::vec3& rayDirection,
                                          const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2,
                                          float& t) const {
        // Möller-Trumbore ray-triangle intersection algorithm
        const float EPSILON = 0.0000001f;
        
        glm::vec3 edge1 = v1 - v0;
        glm::vec3 edge2 = v2 - v0;
        glm::vec3 h = glm::cross(rayDirection, edge2);
        float a = glm::dot(edge1, h);
        
        if (a > -EPSILON && a < EPSILON) {
            return false; // Ray is parallel to triangle
        }
        
        float f = 1.0f / a;
        glm::vec3 s = rayOrigin - v0;
        float u = f * glm::dot(s, h);
        
        if (u < 0.0f || u > 1.0f) {
            return false;
        }
        
        glm::vec3 q = glm::cross(s, edge1);
        float v = f * glm::dot(rayDirection, q);
        
        if (v < 0.0f || u + v > 1.0f) {
            return false;
        }
        
        t = f * glm::dot(edge2, q);
        
        if (t > EPSILON) {
            return true;
        }
        
        return false;
    }
    
    void ModelEditor::renderMeshPreview(VkCommandBuffer commandBuffer) {
        if (!m_model) {
            Logger::get().debug("renderMeshPreview: No model available");
            return;
        }

        // Debug: Check if we have any mesh data to render
        size_t meshCount = m_model->getMeshCount();
        //Logger::get().info("renderMeshPreview: Model has {} meshes", meshCount);

        // TODO: Render solid mesh using the main rendering pipeline
        // For now, we'll render wireframe and debug info

        if (!m_tools || !m_tools->getGizmoRenderer()) {
            Logger::get().debug("No gizmo renderer available for mesh preview wireframe");
            return;
        }
        
        auto* gizmoRenderer = m_tools->getGizmoRenderer();
        const glm::mat4& viewMatrix = m_viewport->getViewMatrix();
        const glm::mat4& projMatrix = m_viewport->getProjectionMatrix();
        
        // Collect triangles for solid and wireframe rendering
        std::vector<std::pair<glm::vec3, glm::vec3>> edges;
        std::vector<glm::vec3> solidTriangleVerts;
        std::vector<uint32_t> solidTriangleIndices;
        std::vector<glm::vec3> selectedTriangleVerts;
        std::vector<uint32_t> selectedTriangleIndices;
        
        // Collect all triangles for solid rendering (unless wireframe-only mode)
        // If we have custom vertices, only render those. Otherwise render the original mesh.
        bool hasCustomVertices = !m_model->getCustomVertices().empty();

        if (!m_wireframeMode) {
            // Only render original mesh triangles if we don't have custom vertices
            if (!hasCustomVertices) {
                // Add regular mesh triangles
                for (uint32_t meshIdx = 0; meshIdx < m_model->getMeshCount(); ++meshIdx) {
                    uint32_t triangleCount = m_model->getTriangleCount(meshIdx);
                    //Logger::get().info("renderMeshPreview: Mesh {} has {} triangles", meshIdx, triangleCount);

                    for (uint32_t triIdx = 0; triIdx < triangleCount; ++triIdx) {
                        glm::vec3 v0, v1, v2;
                        if (m_model->getTriangle(meshIdx, triIdx, v0, v1, v2)) {
                            // Add triangle for solid rendering
                            uint32_t baseIdx = solidTriangleVerts.size();
                            solidTriangleVerts.push_back(v0);
                            solidTriangleVerts.push_back(v1);
                            solidTriangleVerts.push_back(v2);
                            solidTriangleIndices.push_back(baseIdx);
                            solidTriangleIndices.push_back(baseIdx + 1);
                            solidTriangleIndices.push_back(baseIdx + 2);
                        }
                    }
                }
            } else {
                //Logger::get().info("renderMeshPreview: Skipping original mesh rendering - using custom vertices only");
            }

            // Then, add custom triangles to the same solid rendering pass
            const auto& customTriangles = m_model->getCustomTriangles();
            for (size_t i = 0; i < customTriangles.size(); ++i) {
                const auto& tri = customTriangles[i];
                glm::vec3 v0, v1, v2;

                if (m_model->getCustomVertexPosition(tri.vertexIds[0], v0) &&
                    m_model->getCustomVertexPosition(tri.vertexIds[1], v1) &&
                    m_model->getCustomVertexPosition(tri.vertexIds[2], v2)) {

                    // Add to solid rendering
                    uint32_t baseIdx = solidTriangleVerts.size();
                    solidTriangleVerts.push_back(v0);
                    solidTriangleVerts.push_back(v1);
                    solidTriangleVerts.push_back(v2);
                    solidTriangleIndices.push_back(baseIdx);
                    solidTriangleIndices.push_back(baseIdx + 1);
                    solidTriangleIndices.push_back(baseIdx + 2);

                    // Check if this custom triangle is selected for overlay rendering
                    uint32_t customTriIdx = 0x80000000 | i;
                    bool isSelected = std::find(m_selection.selectedTriangles.begin(),
                                               m_selection.selectedTriangles.end(),
                                               customTriIdx) != m_selection.selectedTriangles.end();

                    if (isSelected) {
                        // Add to selected triangles for overlay (slightly offset to avoid Z-fighting)
                        glm::vec3 normal = glm::normalize(glm::cross(v1 - v0, v2 - v0));
                        float offset = 0.001f;

                        uint32_t selectedBaseIdx = selectedTriangleVerts.size();
                        selectedTriangleVerts.push_back(v0 + normal * offset);
                        selectedTriangleVerts.push_back(v1 + normal * offset);
                        selectedTriangleVerts.push_back(v2 + normal * offset);
                        selectedTriangleIndices.push_back(selectedBaseIdx);
                        selectedTriangleIndices.push_back(selectedBaseIdx + 1);
                        selectedTriangleIndices.push_back(selectedBaseIdx + 2);
                    }
                }
            }
        }
        
        // Collect wireframe edges (if wireframe mode is enabled)
        // Only render original mesh wireframe if we don't have custom vertices
        if (m_wireframeMode && !hasCustomVertices) {
            for (uint32_t meshIdx = 0; meshIdx < m_model->getMeshCount(); ++meshIdx) {
                uint32_t triangleCount = m_model->getTriangleCount(meshIdx);

                for (uint32_t triIdx = 0; triIdx < triangleCount; ++triIdx) {
                    glm::vec3 v0, v1, v2;
                    if (m_model->getTriangle(meshIdx, triIdx, v0, v1, v2)) {
                        // Add triangle edges for wireframe
                        edges.emplace_back(v0, v1);
                        edges.emplace_back(v1, v2);
                        edges.emplace_back(v2, v0);
                    }
                }
            }
        }
        
        // Show selected triangles as wireframe overlay (only for original mesh if no custom vertices)
        if (!hasCustomVertices) {
            for (uint32_t meshIdx = 0; meshIdx < m_model->getMeshCount(); ++meshIdx) {
                uint32_t triangleCount = m_model->getTriangleCount(meshIdx);

                for (uint32_t triIdx = 0; triIdx < triangleCount; ++triIdx) {
                    uint32_t combinedIdx = (meshIdx << 16) | triIdx;

                    bool isSelected = std::find(m_selection.selectedTriangles.begin(),
                                               m_selection.selectedTriangles.end(),
                                               combinedIdx) != m_selection.selectedTriangles.end();

                    if (isSelected) {
                        glm::vec3 v0, v1, v2;
                        if (m_model->getTriangle(meshIdx, triIdx, v0, v1, v2)) {
                            // Add to selected triangles for red wireframe overlay
                            uint32_t baseIdx = selectedTriangleVerts.size();
                            selectedTriangleVerts.push_back(v0);
                            selectedTriangleVerts.push_back(v1);
                            selectedTriangleVerts.push_back(v2);
                            selectedTriangleIndices.push_back(baseIdx);
                            selectedTriangleIndices.push_back(baseIdx + 1);
                            selectedTriangleIndices.push_back(baseIdx + 2);
                        }
                    }
                }
            }
        }
        
        // Add custom triangles to wireframe edges if in wireframe mode
        if (m_wireframeMode) {
            const auto& customTriangles = m_model->getCustomTriangles();
            for (size_t i = 0; i < customTriangles.size(); ++i) {
                const auto& tri = customTriangles[i];
                glm::vec3 v0, v1, v2;

                if (m_model->getCustomVertexPosition(tri.vertexIds[0], v0) &&
                    m_model->getCustomVertexPosition(tri.vertexIds[1], v1) &&
                    m_model->getCustomVertexPosition(tri.vertexIds[2], v2)) {

                    // Add to wireframe
                    edges.emplace_back(v0, v1);
                    edges.emplace_back(v1, v2);
                    edges.emplace_back(v2, v0);

                    // Check if this custom triangle is selected for overlay
                    uint32_t customTriIdx = 0x80000000 | i;
                    bool isSelected = std::find(m_selection.selectedTriangles.begin(),
                                               m_selection.selectedTriangles.end(),
                                               customTriIdx) != m_selection.selectedTriangles.end();

                    if (isSelected) {
                        // Add selected triangle edges for green overlay in wireframe mode
                        glm::vec3 normal = glm::normalize(glm::cross(v1 - v0, v2 - v0));
                        float offset = 0.001f;

                        uint32_t selectedBaseIdx = selectedTriangleVerts.size();
                        selectedTriangleVerts.push_back(v0 + normal * offset);
                        selectedTriangleVerts.push_back(v1 + normal * offset);
                        selectedTriangleVerts.push_back(v2 + normal * offset);
                        selectedTriangleIndices.push_back(selectedBaseIdx);
                        selectedTriangleIndices.push_back(selectedBaseIdx + 1);
                        selectedTriangleIndices.push_back(selectedBaseIdx + 2);
                    }
                }
            }
        }
        
        // Use high-performance indirect draw calls to render all triangles in a single call
        std::vector<tremor::editor::GizmoRenderer::TriangleDrawSet> triangleDrawSets;

        // Add solid triangles as the first draw set
        if (!solidTriangleVerts.empty()) {
            tremor::editor::GizmoRenderer::TriangleDrawSet solidSet;
            solidSet.vertices = solidTriangleVerts;
            solidSet.indices = solidTriangleIndices;
            solidSet.color = glm::vec3(0.6f, 0.7f, 0.8f); // Light blue-gray color
            solidSet.alpha = 0.8f; // Slightly transparent
            triangleDrawSets.push_back(std::move(solidSet));
        }

        // Add selected triangles as the second draw set (rendered on top)
        if (!selectedTriangleVerts.empty()) {
            tremor::editor::GizmoRenderer::TriangleDrawSet selectedSet;
            selectedSet.vertices = selectedTriangleVerts;
            selectedSet.indices = selectedTriangleIndices;
            selectedSet.color = glm::vec3(0.3f, 1.0f, 0.4f); // Light green
            selectedSet.alpha = 0.5f; // Semi-transparent
            triangleDrawSets.push_back(std::move(selectedSet));
        }

        // Single indirect draw call for all triangle sets - maximum performance!
        if (!triangleDrawSets.empty()) {
            gizmoRenderer->renderTrianglesIndirect(commandBuffer, triangleDrawSets, viewMatrix, projMatrix, m_backfaceCulling);
        }

        // Use high-performance indirect draw calls for wireframe edges to eliminate flickering
        std::vector<tremor::editor::GizmoRenderer::EdgeDrawSet> edgeDrawSets;

        // Add wireframe edges (if wireframe mode is on)
        if (!edges.empty()) {
            tremor::editor::GizmoRenderer::EdgeDrawSet wireframeSet;
            wireframeSet.edges = edges;
            wireframeSet.color = glm::vec3(0.7f, 0.7f, 0.7f); // Gray wireframe
            edgeDrawSets.push_back(std::move(wireframeSet));
        }

        // Add selected triangle edges
        if (!selectedTriangleVerts.empty()) {
            std::vector<std::pair<glm::vec3, glm::vec3>> selectedEdges;
            for (size_t i = 0; i < selectedTriangleIndices.size(); i += 3) {
                if (i + 2 < selectedTriangleIndices.size()) {
                    const glm::vec3& v0 = selectedTriangleVerts[selectedTriangleIndices[i]];
                    const glm::vec3& v1 = selectedTriangleVerts[selectedTriangleIndices[i + 1]];
                    const glm::vec3& v2 = selectedTriangleVerts[selectedTriangleIndices[i + 2]];
                    selectedEdges.emplace_back(v0, v1);
                    selectedEdges.emplace_back(v1, v2);
                    selectedEdges.emplace_back(v2, v0);
                }
            }
            if (!selectedEdges.empty()) {
                tremor::editor::GizmoRenderer::EdgeDrawSet selectedSet;
                selectedSet.edges = selectedEdges;
                selectedSet.color = glm::vec3(0.2f, 1.0f, 0.3f); // Green for selected
                edgeDrawSets.push_back(std::move(selectedSet));
            }
        }

        // Single indirect draw call for all edge sets - eliminates wireframe flickering!
        if (!edgeDrawSets.empty()) {
            gizmoRenderer->renderEdgesIndirect(commandBuffer, edgeDrawSets, viewMatrix, projMatrix);
        }
    }

    bool ModelEditor::saveMeshAsTaffyAsset(const std::string& filePath) {
        if (!m_model) {
            Logger::get().error("No model to save");
            return false;
        }

        try {
            using namespace Taffy;

            // Create new Taffy asset
            Asset asset;

            // Create geometry chunk header
            GeometryChunk geomHeader{};
            std::memset(&geomHeader, 0, sizeof(geomHeader));

            // Collect all vertices and triangles from the model
            std::vector<Vec3Q> positions;
            std::vector<uint32_t> indices;

            // Get regular mesh triangles
            for (uint32_t meshIdx = 0; meshIdx < m_model->getMeshCount(); ++meshIdx) {
                uint32_t triangleCount = m_model->getTriangleCount(meshIdx);

                for (uint32_t triIdx = 0; triIdx < triangleCount; ++triIdx) {
                    glm::vec3 v0, v1, v2;
                    if (m_model->getTriangle(meshIdx, triIdx, v0, v1, v2)) {
                        uint32_t baseIdx = positions.size();

                        // Convert to quantized coordinates (Taffy's 64-bit precision system)
                        positions.emplace_back(Vec3Q{
                            static_cast<int64_t>(v0.x * 128), // 1/128mm precision
                            static_cast<int64_t>(v0.y * 128),
                            static_cast<int64_t>(v0.z * 128)
                        });
                        positions.emplace_back(Vec3Q{
                            static_cast<int64_t>(v1.x * 128),
                            static_cast<int64_t>(v1.y * 128),
                            static_cast<int64_t>(v1.z * 128)
                        });
                        positions.emplace_back(Vec3Q{
                            static_cast<int64_t>(v2.x * 128),
                            static_cast<int64_t>(v2.y * 128),
                            static_cast<int64_t>(v2.z * 128)
                        });

                        indices.push_back(baseIdx);
                        indices.push_back(baseIdx + 1);
                        indices.push_back(baseIdx + 2);
                    }
                }
            }

            // Add custom triangles
            const auto& customTriangles = m_model->getCustomTriangles();
            for (size_t i = 0; i < customTriangles.size(); ++i) {
                const auto& tri = customTriangles[i];
                glm::vec3 v0, v1, v2;

                if (m_model->getCustomVertexPosition(tri.vertexIds[0], v0) &&
                    m_model->getCustomVertexPosition(tri.vertexIds[1], v1) &&
                    m_model->getCustomVertexPosition(tri.vertexIds[2], v2)) {

                    uint32_t baseIdx = positions.size();

                    positions.emplace_back(Vec3Q{
                        static_cast<int64_t>(v0.x * 128),
                        static_cast<int64_t>(v0.y * 128),
                        static_cast<int64_t>(v0.z * 128)
                    });
                    positions.emplace_back(Vec3Q{
                        static_cast<int64_t>(v1.x * 128),
                        static_cast<int64_t>(v1.y * 128),
                        static_cast<int64_t>(v1.z * 128)
                    });
                    positions.emplace_back(Vec3Q{
                        static_cast<int64_t>(v2.x * 128),
                        static_cast<int64_t>(v2.y * 128),
                        static_cast<int64_t>(v2.z * 128)
                    });

                    indices.push_back(baseIdx);
                    indices.push_back(baseIdx + 1);
                    indices.push_back(baseIdx + 2);
                }
            }

            if (positions.empty()) {
                Logger::get().error("No geometry to save");
                return false;
            }

            // Calculate bounding box
            Vec3Q boundsMin = positions[0];
            Vec3Q boundsMax = positions[0];
            for (const auto& pos : positions) {
                boundsMin.x = std::min(boundsMin.x, pos.x);
                boundsMin.y = std::min(boundsMin.y, pos.y);
                boundsMin.z = std::min(boundsMin.z, pos.z);
                boundsMax.x = std::max(boundsMax.x, pos.x);
                boundsMax.y = std::max(boundsMax.y, pos.y);
                boundsMax.z = std::max(boundsMax.z, pos.z);
            }

            // Fill geometry header
            geomHeader.vertex_count = static_cast<uint32_t>(positions.size());
            geomHeader.index_count = static_cast<uint32_t>(indices.size());
            geomHeader.vertex_stride = sizeof(Vec3Q); // Simple position-only format for now
            geomHeader.vertex_format = VertexFormat::Position3D;
            geomHeader.bounds_min = boundsMin;
            geomHeader.bounds_max = boundsMax;
            geomHeader.lod_distance = 1000.0f;
            geomHeader.lod_level = 0;
            geomHeader.render_mode = GeometryChunk::Traditional; // Use traditional vertex/index buffers

            // Create geometry chunk data
            std::vector<uint8_t> geomData;
            geomData.resize(sizeof(GeometryChunk) + positions.size() * sizeof(Vec3Q) + indices.size() * sizeof(uint32_t));

            // Copy header
            std::memcpy(geomData.data(), &geomHeader, sizeof(GeometryChunk));

            // Copy vertex data
            std::memcpy(geomData.data() + sizeof(GeometryChunk),
                       positions.data(), positions.size() * sizeof(Vec3Q));

            // Copy index data
            std::memcpy(geomData.data() + sizeof(GeometryChunk) + positions.size() * sizeof(Vec3Q),
                       indices.data(), indices.size() * sizeof(uint32_t));

            // Add geometry chunk to asset
            asset.add_chunk(ChunkType::GEOM, geomData, "editor_mesh_geometry");

            // Set asset features
            asset.set_feature_flags(FeatureFlags::QuantizedCoords);

            // Save the asset
            if (!asset.save_to_file(filePath)) {
                Logger::get().error("Failed to save Taffy asset to: {}", filePath);
                return false;
            }

            Logger::get().info("Successfully saved mesh as Taffy asset: {}", filePath);
            Logger::get().info("  Vertices: {}", positions.size());
            Logger::get().info("  Triangles: {}", indices.size() / 3);

            return true;

        } catch (const std::exception& e) {
            Logger::get().error("Exception while saving Taffy asset: {}", e.what());
            return false;
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
            vertices.push_back(vertex.position);
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
            // Check if Shift is held for multi-selection
            bool addToSelection = SDL_GetModState() & KMOD_SHIFT;
            
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