#include "model_editor_ui.h"
#include "model_editor.h"
#include "../main.h"
#include <sstream>
#include <iomanip>
#include <filesystem>

namespace tremor::editor {

    // =============================================================================
    // ModelEditorUI Implementation
    // =============================================================================

    ModelEditorUI::ModelEditorUI(tremor::gfx::UIRenderer& uiRenderer, ModelEditor& editor)
        : m_uiRenderer(uiRenderer), m_editor(editor) {
    }

    ModelEditorUI::~ModelEditorUI() = default;

    void ModelEditorUI::initialize() {
        Logger::get().info("*** STARTING ModelEditorUI::initialize() ***");

        createToolsPanel();
        createPropertiesPanel();
        createFilePanel();

        Logger::get().info("Model Editor UI initialized successfully");
    }

    void ModelEditorUI::update() {
        updateToolsPanel();
        updatePropertiesPanel();
        updateFilePanel();
    }

    void ModelEditorUI::render() {
        // UI rendering is handled by the UIRenderer automatically
        // This method can be used for custom rendering if needed
    }

    void ModelEditorUI::onModeChanged(EditorMode mode) {
        // Update button states based on new mode
        updateButtonState(m_toolsPanel.selectButtonId, mode == EditorMode::Select);
        updateButtonState(m_toolsPanel.moveButtonId, mode == EditorMode::Move);
        updateButtonState(m_toolsPanel.rotateButtonId, mode == EditorMode::Rotate);
        updateButtonState(m_toolsPanel.scaleButtonId, mode == EditorMode::Scale);
        updateButtonState(m_toolsPanel.addVertexButtonId, mode == EditorMode::AddVertex);
        updateButtonState(m_toolsPanel.createTriangleButtonId, mode == EditorMode::CreateTriangle);
    }

    void ModelEditorUI::onSelectionChanged(const Selection& selection, EditableModel* model) {
        Logger::get().info("🎯 onSelectionChanged called - updating UI labels");
        
        // Update properties panel with selection info
        std::string selectionInfo = getSelectionInfo(selection);
        Logger::get().info("Selection info: {}", selectionInfo);
        updateLabelText(m_propertiesPanel.meshInfoLabelId, selectionInfo);
        
        // Enable/disable reverse winding button based on triangle selection
        if (m_toolsPanel.reverseWindingButtonId != 0) {
            bool hasTriangles = selection.hasSelectedTriangles();
            // TODO: Add setButtonEnabled to UIRenderer
            // m_uiRenderer.setButtonEnabled(m_toolsPanel.reverseWindingButtonId, hasTriangles);
        }

        // Calculate average position for selected vertices
        if (selection.hasCustomVertices() && model) {
            glm::vec3 averagePos(0.0f);
            int count = 0;
            
            const auto& vertices = model->getCustomVertices();
            for (const auto& vertex : vertices) {
                if (selection.hasCustomVertex(vertex.id)) {
                    averagePos += vertex.position;
                    count++;
                }
            }
            
            if (count > 0) {
                averagePos /= static_cast<float>(count);
                std::string posText = "Pos: " + formatVector3(averagePos);
                updateLabelText(m_propertiesPanel.positionLabelId, posText);
                Logger::get().info("Updated position label: {}", posText);
            } else {
                updateLabelText(m_propertiesPanel.positionLabelId, "Pos: (0.0, 0.0, 0.0)");
            }
            
            // TODO: Add rotation calculation for custom vertices if needed
            updateLabelText(m_propertiesPanel.rotationLabelId, "Rot: (0.0, 0.0, 0.0)");
            updateLabelText(m_propertiesPanel.scaleLabelId, "Scale: (1.0, 1.0, 1.0)");
            
        } else if (selection.hasMesh()) {
            // Handle mesh selections (non-custom vertices)
            updateLabelText(m_propertiesPanel.positionLabelId, "Pos: (0.0, 0.0, 0.0)");
            updateLabelText(m_propertiesPanel.rotationLabelId, "Rot: (0.0, 0.0, 0.0)");
            updateLabelText(m_propertiesPanel.scaleLabelId, "Scale: (1.0, 1.0, 1.0)");
        } else {
            updateLabelText(m_propertiesPanel.positionLabelId, "Pos: -");
            updateLabelText(m_propertiesPanel.rotationLabelId, "Rot: -");
            updateLabelText(m_propertiesPanel.scaleLabelId, "Scale: -");
        }
    }

    void ModelEditorUI::onModelChanged() {
        // Update file panel status
        updateLabelText(m_filePanel.statusLabelId, "Status: Modified *");
    }

    void ModelEditorUI::createToolsPanel() {
        Logger::get().info("Creating tools panel with UIRenderer");

        // Title
        m_toolsPanel.titleLabelId = m_uiRenderer.addLabel("Tools", 
            glm::vec2(TOOLS_PANEL_X + PANEL_PADDING, TOOLS_PANEL_Y + PANEL_PADDING), 
            TITLE_COLOR, 0.7f);

        float buttonY = TOOLS_PANEL_Y + 40.0f;

        // Tool buttons
        m_toolsPanel.selectButtonId = m_uiRenderer.addButton("Select (Esc)",
            glm::vec2(TOOLS_PANEL_X + PANEL_PADDING, buttonY),
            glm::vec2(PANEL_WIDTH - 2 * PANEL_PADDING, BUTTON_HEIGHT),
            [this]() { onSelectModeClicked(); });

        buttonY += BUTTON_HEIGHT + BUTTON_SPACING;
        m_toolsPanel.moveButtonId = m_uiRenderer.addButton("Move (G)",
            glm::vec2(TOOLS_PANEL_X + PANEL_PADDING, buttonY),
            glm::vec2(PANEL_WIDTH - 2 * PANEL_PADDING, BUTTON_HEIGHT),
            [this]() { onMoveModeClicked(); });

        buttonY += BUTTON_HEIGHT + BUTTON_SPACING;
        m_toolsPanel.rotateButtonId = m_uiRenderer.addButton("Rotate (R)",
            glm::vec2(TOOLS_PANEL_X + PANEL_PADDING, buttonY),
            glm::vec2(PANEL_WIDTH - 2 * PANEL_PADDING, BUTTON_HEIGHT),
            [this]() { onRotateModeClicked(); });

        buttonY += BUTTON_HEIGHT + BUTTON_SPACING;
        m_toolsPanel.scaleButtonId = m_uiRenderer.addButton("Scale (S)",
            glm::vec2(TOOLS_PANEL_X + PANEL_PADDING, buttonY),
            glm::vec2(PANEL_WIDTH - 2 * PANEL_PADDING, BUTTON_HEIGHT),
            [this]() { onScaleModeClicked(); });

        // Add mesh creation tools
        buttonY += BUTTON_HEIGHT + BUTTON_SPACING + 10.0f; // Extra spacing
        
        // Add Vertex button
        m_toolsPanel.addVertexButtonId = m_uiRenderer.addButton("Add Vertex (V)",
            glm::vec2(TOOLS_PANEL_X + PANEL_PADDING, buttonY),
            glm::vec2(PANEL_WIDTH - 2 * PANEL_PADDING, BUTTON_HEIGHT),
            [this]() { onAddVertexClicked(); });

        buttonY += BUTTON_HEIGHT + BUTTON_SPACING;
        m_toolsPanel.createTriangleButtonId = m_uiRenderer.addButton("Add Triangle (T)",
            glm::vec2(TOOLS_PANEL_X + PANEL_PADDING, buttonY),
            glm::vec2(PANEL_WIDTH - 2 * PANEL_PADDING, BUTTON_HEIGHT),
            [this]() { onCreateTriangleClicked(); });
        
        // Triangle selection and modification tools
        buttonY += BUTTON_HEIGHT + BUTTON_SPACING + 10.0f;
        m_toolsPanel.selectTriangleButtonId = m_uiRenderer.addButton("Select Triangle",
            glm::vec2(TOOLS_PANEL_X + PANEL_PADDING, buttonY),
            glm::vec2(PANEL_WIDTH - 2 * PANEL_PADDING, BUTTON_HEIGHT),
            [this]() { onSelectTriangleClicked(); });
        
        buttonY += BUTTON_HEIGHT + BUTTON_SPACING;
        m_toolsPanel.reverseWindingButtonId = m_uiRenderer.addButton("Reverse Winding",
            glm::vec2(TOOLS_PANEL_X + PANEL_PADDING, buttonY),
            glm::vec2(PANEL_WIDTH - 2 * PANEL_PADDING, BUTTON_HEIGHT),
            [this]() { onReverseWindingClicked(); });
        
        // Preview controls
        buttonY += BUTTON_HEIGHT + BUTTON_SPACING + 10.0f;
        m_toolsPanel.togglePreviewButtonId = m_uiRenderer.addButton("Hide Preview",
            glm::vec2(TOOLS_PANEL_X + PANEL_PADDING, buttonY),
            glm::vec2(PANEL_WIDTH - 2 * PANEL_PADDING, BUTTON_HEIGHT),
            [this]() { onTogglePreviewClicked(); });
        
        buttonY += BUTTON_HEIGHT + BUTTON_SPACING;
        m_toolsPanel.toggleWireframeButtonId = m_uiRenderer.addButton("Wireframe Mode",
            glm::vec2(TOOLS_PANEL_X + PANEL_PADDING, buttonY),
            glm::vec2(PANEL_WIDTH - 2 * PANEL_PADDING, BUTTON_HEIGHT),
            [this]() { onToggleWireframeClicked(); });
        
        buttonY += BUTTON_HEIGHT + BUTTON_SPACING;
        m_toolsPanel.toggleBackfaceCullingButtonId = m_uiRenderer.addButton("Culling ON",
            glm::vec2(TOOLS_PANEL_X + PANEL_PADDING, buttonY),
            glm::vec2(PANEL_WIDTH - 2 * PANEL_PADDING, BUTTON_HEIGHT),
            [this]() { onToggleBackfaceCullingClicked(); });
    }

    void ModelEditorUI::createPropertiesPanel() {
        Logger::get().debug("Creating properties panel");

        // Calculate dynamic position (assuming 1280x720 initially, will be updated properly later)
        glm::vec2 panelPos = calculatePropertiesPanelPosition(1280.0f, 720.0f);

        // Background rectangle
        m_propertiesPanel.backgroundId = m_uiRenderer.addRect(
            panelPos,
            glm::vec2(PANEL_WIDTH, PROPERTIES_PANEL_HEIGHT),
            PANEL_BG_COLOR,    // fillColor  
            0x404040FF,        // borderColor (dark gray)
            1.0f               // borderWidth
        );

        // Title
        m_propertiesPanel.titleLabelId = m_uiRenderer.addLabel("Properties",
            glm::vec2(panelPos.x + PANEL_PADDING, panelPos.y + PANEL_PADDING),
            TITLE_COLOR, 0.75f);

        float labelY = panelPos.y + 40.0f;
        const float labelHeight = 20.0f;
        const float labelSpacing = 5.0f;

        // Property labels
        m_propertiesPanel.meshInfoLabelId = m_uiRenderer.addLabel("Selection: None",
            glm::vec2(panelPos.x + PANEL_PADDING, labelY), TEXT_COLOR, 0.4f);

        labelY += labelHeight + labelSpacing;
        m_propertiesPanel.positionLabelId = m_uiRenderer.addLabel("Pos: -",
            glm::vec2(panelPos.x + PANEL_PADDING, labelY), TEXT_COLOR, 0.3f);

        labelY += labelHeight + labelSpacing;
        m_propertiesPanel.rotationLabelId = m_uiRenderer.addLabel("Rot: -",
            glm::vec2(panelPos.x + PANEL_PADDING, labelY), TEXT_COLOR, 0.3f);

        labelY += labelHeight + labelSpacing;
        m_propertiesPanel.scaleLabelId = m_uiRenderer.addLabel("Scale: -",
            glm::vec2(panelPos.x + PANEL_PADDING, labelY), TEXT_COLOR, 0.3f);

        // Selection radius controls
        labelY += labelHeight + labelSpacing + 10.0f; // Extra spacing
        m_propertiesPanel.selectionRadiusLabelId = m_uiRenderer.addLabel("Vertex Radius: 0.25",
            glm::vec2(panelPos.x + PANEL_PADDING, labelY), TEXT_COLOR, 0.2f);

        labelY += labelHeight + labelSpacing;
        m_propertiesPanel.selectionRadiusButtonId = m_uiRenderer.addButton("Adjust",
            glm::vec2(panelPos.x + PANEL_PADDING, labelY), glm::vec2(80, 25),
            [this]() { onSelectionRadiusClicked(); });
    }

    void ModelEditorUI::createFilePanel() {
        Logger::get().debug("Creating file panel");        

        // Title
        m_filePanel.titleLabelId = m_uiRenderer.addLabel("File",
            glm::vec2(FILE_PANEL_X + PANEL_PADDING, FILE_PANEL_Y + PANEL_PADDING),
            TITLE_COLOR, 0.4f);

        float buttonY = FILE_PANEL_Y + 40.0f;

        // File operation buttons
        m_filePanel.newButtonId = m_uiRenderer.addButton("New (Ctrl+N)",
            glm::vec2(FILE_PANEL_X + PANEL_PADDING, buttonY),
            glm::vec2(PANEL_WIDTH - 2 * PANEL_PADDING, BUTTON_HEIGHT),
            [this]() { onNewModelClicked(); });

        buttonY += BUTTON_HEIGHT + BUTTON_SPACING;
        m_filePanel.openButtonId = m_uiRenderer.addButton("Open (Ctrl+O)",
            glm::vec2(FILE_PANEL_X + PANEL_PADDING, buttonY),
            glm::vec2(PANEL_WIDTH - 2 * PANEL_PADDING, BUTTON_HEIGHT),
            [this]() { onOpenModelClicked(); });

        buttonY += BUTTON_HEIGHT + BUTTON_SPACING;
        m_filePanel.saveButtonId = m_uiRenderer.addButton("Save (Ctrl+S)",
            glm::vec2(FILE_PANEL_X + PANEL_PADDING, buttonY),
            glm::vec2(PANEL_WIDTH - 2 * PANEL_PADDING, BUTTON_HEIGHT),
            [this]() { onSaveModelClicked(); });

        buttonY += BUTTON_HEIGHT + BUTTON_SPACING;
        m_filePanel.saveAsButtonId = m_uiRenderer.addButton("Save As...",
            glm::vec2(FILE_PANEL_X + PANEL_PADDING, buttonY),
            glm::vec2(PANEL_WIDTH - 2 * PANEL_PADDING, BUTTON_HEIGHT),
            [this]() { onSaveAsModelClicked(); });

        buttonY += BUTTON_HEIGHT + BUTTON_SPACING + 10.0f;
        m_filePanel.statusLabelId = m_uiRenderer.addLabel("Status: Ready",
            glm::vec2(FILE_PANEL_X + PANEL_PADDING, buttonY), TEXT_COLOR, 0.2f);
    }

    void ModelEditorUI::onSelectModeClicked() {
        m_editor.setMode(EditorMode::Select);
    }

    void ModelEditorUI::onMoveModeClicked() {
        m_editor.setMode(EditorMode::Move);
    }

    void ModelEditorUI::onRotateModeClicked() {
        m_editor.setMode(EditorMode::Rotate);
    }

    void ModelEditorUI::onScaleModeClicked() {
        m_editor.setMode(EditorMode::Scale);
    }

    void ModelEditorUI::onAddVertexClicked() {
        Logger::get().info("Add Vertex mode activated");
        m_editor.setMode(EditorMode::AddVertex);
    }

    void ModelEditorUI::onCreateTriangleClicked() {
        Logger::get().info("Create Triangle mode activated");
        m_editor.setMode(EditorMode::CreateTriangle);
    }
    
    void ModelEditorUI::onSelectTriangleClicked() {
        Logger::get().info("Select Triangle mode activated");
        // Keep in Select mode but enable triangle selection
        m_editor.setMode(EditorMode::Select);
    }
    
    void ModelEditorUI::onReverseWindingClicked() {
        Logger::get().info("Reverse winding button clicked");
        m_editor.reverseWindingOrder();
    }
    
    void ModelEditorUI::onTogglePreviewClicked() {
        bool currentState = m_editor.getShowMeshPreview();
        m_editor.setShowMeshPreview(!currentState);
        Logger::get().info("Mesh preview toggled: {}", !currentState ? "on" : "off");
        
        // Update button text to reflect state
        std::string buttonText = !currentState ? "Hide Preview" : "Show Preview";
        m_uiRenderer.setElementText(m_toolsPanel.togglePreviewButtonId, buttonText);
    }
    
    void ModelEditorUI::onToggleWireframeClicked() {
        bool currentState = m_editor.getWireframeMode();
        m_editor.setWireframeMode(!currentState);
        Logger::get().info("Wireframe mode toggled: {}", !currentState ? "on" : "off");
        
        // Update button text to reflect state
        std::string buttonText = !currentState ? "Solid Mode" : "Wireframe Mode";
        m_uiRenderer.setElementText(m_toolsPanel.toggleWireframeButtonId, buttonText);
    }

    void ModelEditorUI::onToggleBackfaceCullingClicked() {
        bool currentState = m_editor.getBackfaceCulling();
        m_editor.setBackfaceCulling(!currentState);
        Logger::get().info("Backface culling toggled: {}", !currentState ? "on" : "off");
        
        // Update button text to reflect state
        std::string buttonText = !currentState ? "Culling ON" : "Culling OFF";
        m_uiRenderer.setElementText(m_toolsPanel.toggleBackfaceCullingButtonId, buttonText);
    }

    void ModelEditorUI::onNewModelClicked() {
        Logger::get().info("New model button clicked");
        if (m_editor.newModel()) {
            updateLabelText(m_filePanel.statusLabelId, "Status: New Model");
        }
    }

    void ModelEditorUI::onOpenModelClicked() {
        Logger::get().info("Open model button clicked");
        
        std::string filePath = FileDialog::showOpenDialog("assets/");
        if (!filePath.empty()) {
            Logger::get().info("Selected file: {}", filePath);
            if (m_editor.loadModel(filePath)) {
                std::string filename = std::filesystem::path(filePath).filename().string();
                updateLabelText(m_filePanel.statusLabelId, "Status: Loaded " + filename);
                Logger::get().info("Successfully loaded model: {}", filename);
            } else {
                updateLabelText(m_filePanel.statusLabelId, "Status: Load Failed");
                Logger::get().error("Failed to load model: {}", filePath);
            }
        } else {
            Logger::get().info("No file selected");
        }
    }

    void ModelEditorUI::onSaveModelClicked() {
        Logger::get().info("Save model button clicked");
        if (m_editor.saveModel()) {
            updateLabelText(m_filePanel.statusLabelId, "Status: Saved");
        } else {
            updateLabelText(m_filePanel.statusLabelId, "Status: Save Failed");
        }
    }

    void ModelEditorUI::onSaveAsModelClicked() {
        Logger::get().info("Save As button clicked");
        
        std::string filePath = FileDialog::showSaveDialog("assets/");
        if (!filePath.empty()) {
            // Ensure .taf extension
            if (FileDialog::getFileExtension(filePath).empty()) {
                filePath += ".taf";
            }
            
            Logger::get().info("Saving to: {}", filePath);
            if (m_editor.saveModel(filePath)) {
                std::string filename = std::filesystem::path(filePath).filename().string();
                updateLabelText(m_filePanel.statusLabelId, "Status: Saved As " + filename);
                Logger::get().info("Successfully saved model as: {}", filename);
            } else {
                updateLabelText(m_filePanel.statusLabelId, "Status: Save As Failed");
                Logger::get().error("Failed to save model as: {}", filePath);
            }
        } else {
            Logger::get().info("No save location selected");
        }
    }

    void ModelEditorUI::updateToolsPanel() {
        // Tools panel updates are event-driven, no continuous updates needed
    }

    void ModelEditorUI::updatePropertiesPanel() {
        // Properties panel updates are event-driven, no continuous updates needed
    }

    void ModelEditorUI::updateFilePanel() {
        // File panel updates are event-driven, no continuous updates needed
    }

    void ModelEditorUI::updateButtonState(uint32_t buttonId, bool active) {
        auto* element = m_uiRenderer.getElement(buttonId);
        if (element) {
            if (active) {
                element->backgroundColor = BUTTON_ACTIVE_COLOR;
                element->hoverColor = BUTTON_ACTIVE_COLOR;
            } else {
                element->backgroundColor = BUTTON_NORMAL_COLOR;
                element->hoverColor = BUTTON_HOVER_COLOR;
            }
        }
    }

    void ModelEditorUI::updateLabelText(uint32_t labelId, const std::string& text) {
        Logger::get().debug("updateLabelText called: labelId={}, text='{}'", labelId, text);
        m_uiRenderer.setElementText(labelId, text);
    }

    std::string ModelEditorUI::formatVector3(const glm::vec3& vec) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) 
            << "(" << vec.x << ", " << vec.y << ", " << vec.z << ")";
        return oss.str();
    }

    std::string ModelEditorUI::getSelectionInfo(const Selection& selection) {
        std::ostringstream oss;
        
        // Handle custom vertex selections
        if (selection.hasCustomVertices()) {
            oss << "Custom Vertices: ";
            const auto& customVertices = selection.customVertexIds;
            for (size_t i = 0; i < customVertices.size(); ++i) {
                if (i > 0) oss << ", ";
                oss << customVertices[i];
            }
            return oss.str();
        }
        
        // Handle mesh selections  
        if (selection.hasMesh()) {
            oss << "Mesh: " << selection.meshId;
            
            if (selection.hasVertex()) {
                oss << ", Vertex: " << selection.vertexIndex;
            }
            return oss.str();
        }
        
        return "Selection: None";
    }

    void ModelEditorUI::onSelectionRadiusClicked() {
        Logger::get().info("Selection radius adjust clicked");
        
        // Cycle through different radius values for easy adjustment
        float currentRadius = m_editor.getVertexSelectionRadius();
        float newRadius;
        
        if (currentRadius <= 0.1f) {
            newRadius = 0.25f;
        } else if (currentRadius <= 0.25f) {
            newRadius = 0.5f;
        } else if (currentRadius <= 0.5f) {
            newRadius = 1.0f;
        } else if (currentRadius <= 1.0f) {
            newRadius = 2.0f;
        } else {
            newRadius = 0.1f; // Reset to smallest
        }
        
        m_editor.setVertexSelectionRadius(newRadius);
        
        // Update the label
        std::ostringstream oss;
        oss << "Vertex Radius: " << std::fixed << std::setprecision(2) << newRadius;
        updateLabelText(m_propertiesPanel.selectionRadiusLabelId, oss.str());
        
        Logger::get().info("Vertex selection radius changed to: {:.2f}", newRadius);
    }

    glm::vec2 ModelEditorUI::calculatePropertiesPanelPosition(float viewportWidth, float viewportHeight) const {
        // Anchor to bottom-right with bottom-left pivot
        const float margin = 10.0f;
        
        // Calculate position so the panel appears in bottom-right corner
        float x = viewportWidth - PANEL_WIDTH - margin;  // Right edge minus width minus margin
        float y = viewportHeight - PROPERTIES_PANEL_HEIGHT - margin; // Bottom edge minus height minus margin
        
        // Ensure minimum positions to prevent going off-screen
        x = std::max(x, margin);
        y = std::max(y, margin);
        
        return glm::vec2(x, y);
    }

} // namespace tremor::editor