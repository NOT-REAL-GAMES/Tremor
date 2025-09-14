#pragma once

#include "../renderer/ui_renderer.h"
#include "model_editor.h"
#include "file_dialog.h"
#include <string>
#include <vector>
#include <functional>

namespace tremor::editor {

    // Forward declaration
    class ModelEditor;

    /**
     * UI panels and controls for the model editor
     */
    class ModelEditorUI {
    public:
        ModelEditorUI(tremor::gfx::UIRenderer& uiRenderer, ModelEditor& editor);
        ~ModelEditorUI();

        void initialize();
        void update();
        void render();

        // Panel visibility
    void setToolsPanelVisible(bool visible) {
        m_showToolsPanel = visible;
        
        // Set visibility for all tools panel elements
        if (m_toolsPanel.titleLabelId != 0)
            m_uiRenderer.setElementVisible(m_toolsPanel.titleLabelId, visible);
        if (m_toolsPanel.selectButtonId != 0)
            m_uiRenderer.setElementVisible(m_toolsPanel.selectButtonId, visible);
        if (m_toolsPanel.moveButtonId != 0)
            m_uiRenderer.setElementVisible(m_toolsPanel.moveButtonId, visible);
        if (m_toolsPanel.rotateButtonId != 0)
            m_uiRenderer.setElementVisible(m_toolsPanel.rotateButtonId, visible);
        if (m_toolsPanel.scaleButtonId != 0)
            m_uiRenderer.setElementVisible(m_toolsPanel.scaleButtonId, visible);
        if (m_toolsPanel.addVertexButtonId != 0)
            m_uiRenderer.setElementVisible(m_toolsPanel.addVertexButtonId, visible);
        if (m_toolsPanel.createTriangleButtonId != 0)
            m_uiRenderer.setElementVisible(m_toolsPanel.createTriangleButtonId, visible);
        if (m_toolsPanel.selectTriangleButtonId != 0)
            m_uiRenderer.setElementVisible(m_toolsPanel.selectTriangleButtonId, visible);
        if (m_toolsPanel.reverseWindingButtonId != 0)
            m_uiRenderer.setElementVisible(m_toolsPanel.reverseWindingButtonId, visible);
        if (m_toolsPanel.togglePreviewButtonId != 0)
            m_uiRenderer.setElementVisible(m_toolsPanel.togglePreviewButtonId, visible);
        if (m_toolsPanel.toggleWireframeButtonId != 0)
            m_uiRenderer.setElementVisible(m_toolsPanel.toggleWireframeButtonId, visible);
        if (m_toolsPanel.toggleBackfaceCullingButtonId != 0)
            m_uiRenderer.setElementVisible(m_toolsPanel.toggleBackfaceCullingButtonId, visible);
    }

    void setPropertiesPanelVisible(bool visible) {
        m_showPropertiesPanel = visible;
        
        // Set visibility for all properties panel elements
        if (m_propertiesPanel.backgroundId != 0)
            m_uiRenderer.setElementVisible(m_propertiesPanel.backgroundId, visible);
        if (m_propertiesPanel.titleLabelId != 0)
            m_uiRenderer.setElementVisible(m_propertiesPanel.titleLabelId, visible);
        if (m_propertiesPanel.meshInfoLabelId != 0)
            m_uiRenderer.setElementVisible(m_propertiesPanel.meshInfoLabelId, visible);
        if (m_propertiesPanel.positionLabelId != 0)
            m_uiRenderer.setElementVisible(m_propertiesPanel.positionLabelId, visible);
        if (m_propertiesPanel.rotationLabelId != 0)
            m_uiRenderer.setElementVisible(m_propertiesPanel.rotationLabelId, visible);
        if (m_propertiesPanel.scaleLabelId != 0)
            m_uiRenderer.setElementVisible(m_propertiesPanel.scaleLabelId, visible);
        if (m_propertiesPanel.selectionRadiusLabelId != 0)
            m_uiRenderer.setElementVisible(m_propertiesPanel.selectionRadiusLabelId, visible);
        if (m_propertiesPanel.selectionRadiusButtonId != 0)
            m_uiRenderer.setElementVisible(m_propertiesPanel.selectionRadiusButtonId, visible);
    }

    void setFilePanelVisible(bool visible) {
        m_showFilePanel = visible;
        
        // Set visibility for all file panel elements
        if (m_filePanel.titleLabelId != 0)
            m_uiRenderer.setElementVisible(m_filePanel.titleLabelId, visible);
        if (m_filePanel.newButtonId != 0)
            m_uiRenderer.setElementVisible(m_filePanel.newButtonId, visible);
        if (m_filePanel.openButtonId != 0)
            m_uiRenderer.setElementVisible(m_filePanel.openButtonId, visible);
        if (m_filePanel.saveButtonId != 0)
            m_uiRenderer.setElementVisible(m_filePanel.saveButtonId, visible);
        if (m_filePanel.saveAsButtonId != 0)
            m_uiRenderer.setElementVisible(m_filePanel.saveAsButtonId, visible);
        if (m_filePanel.statusLabelId != 0)
            m_uiRenderer.setElementVisible(m_filePanel.statusLabelId, visible);
    }

        // Update UI state based on editor state
        void onModeChanged(EditorMode mode);
        void onSelectionChanged(const Selection& selection, EditableModel* model = nullptr);
        void onModelChanged();

    private:
        tremor::gfx::UIRenderer& m_uiRenderer;
        ModelEditor& m_editor;

        // Panel visibility
        bool m_showToolsPanel = true;
        bool m_showPropertiesPanel = true;
        bool m_showFilePanel = true;

        // UI element IDs
        struct ToolsPanel {
            uint32_t backgroundId = 0;
            uint32_t selectButtonId = 0;
            uint32_t moveButtonId = 0;
            uint32_t rotateButtonId = 0;
            uint32_t scaleButtonId = 0;
            uint32_t titleLabelId = 0;
            // Mesh creation tools
            uint32_t addVertexButtonId = 0;
            uint32_t createTriangleButtonId = 0;
            // Triangle selection and modification
            uint32_t selectTriangleButtonId = 0;
            uint32_t reverseWindingButtonId = 0;
            // Preview controls
            uint32_t togglePreviewButtonId = 0;
            uint32_t toggleWireframeButtonId = 0;
            uint32_t toggleBackfaceCullingButtonId = 0;
        } m_toolsPanel;

        struct PropertiesPanel {
            uint32_t backgroundId = 0;
            uint32_t titleLabelId = 0;
            uint32_t positionLabelId = 0;
            uint32_t rotationLabelId = 0;
            uint32_t scaleLabelId = 0;
            uint32_t meshInfoLabelId = 0;
            uint32_t selectionRadiusLabelId = 0;
            uint32_t selectionRadiusButtonId = 0;
        } m_propertiesPanel;

        struct FilePanel {
            uint32_t backgroundId = 0;
            uint32_t newButtonId = 0;
            uint32_t openButtonId = 0;
            uint32_t saveButtonId = 0;
            uint32_t saveAsButtonId = 0;
            uint32_t titleLabelId = 0;
            uint32_t statusLabelId = 0;
        } m_filePanel;

        // Layout constants
        static constexpr float PANEL_WIDTH = 200.0f;
        static constexpr float PANEL_SPACING = 10.0f;
        static constexpr float BUTTON_HEIGHT = 30.0f;
        static constexpr float BUTTON_SPACING = 5.0f;
        static constexpr float PANEL_PADDING = 10.0f;

        // Panel positions (static panels)
        static constexpr float TOOLS_PANEL_X = 10.0f;
        static constexpr float TOOLS_PANEL_Y = 10.0f;
        static constexpr float FILE_PANEL_X = 10.0f;
        static constexpr float FILE_PANEL_Y = 500.0f;
        
        // Properties panel dimensions (position calculated dynamically)
        static constexpr float PROPERTIES_PANEL_HEIGHT = 192.0f;

        // Colors
        static constexpr uint32_t PANEL_BG_COLOR = 0x2A2A2AE0;
        static constexpr uint32_t BUTTON_NORMAL_COLOR = 0x404040FF;
        static constexpr uint32_t BUTTON_ACTIVE_COLOR = 0x0080FFFF;
        static constexpr uint32_t BUTTON_HOVER_COLOR = 0x505050FF;
        static constexpr uint32_t TEXT_COLOR = 0xFFFFFFFF;
        static constexpr uint32_t TITLE_COLOR = 0xCCCCCCFF;

        // Panel creation methods
        void createToolsPanel();
        void createPropertiesPanel();
        void createFilePanel();

        // Button callbacks
        void onSelectModeClicked();
        void onMoveModeClicked();
        void onRotateModeClicked();
        void onScaleModeClicked();
        
        // Mesh creation callbacks
        void onAddVertexClicked();
        void onCreateTriangleClicked();
        void onSelectTriangleClicked();
        void onReverseWindingClicked();
        void onTogglePreviewClicked();
        void onToggleWireframeClicked();
        void onToggleBackfaceCullingClicked();
        void onNewModelClicked();
        void onOpenModelClicked();
        void onSaveModelClicked();
        void onSaveAsModelClicked();
        void onSelectionRadiusClicked();

        // Update methods
        void updateToolsPanel();
        void updatePropertiesPanel();
        void updateFilePanel();

        // Helper methods
        void updateButtonState(uint32_t buttonId, bool active);
        void updateLabelText(uint32_t labelId, const std::string& text);
        std::string formatVector3(const glm::vec3& vec);
        std::string getSelectionInfo(const Selection& selection);
        
        // Dynamic positioning
        glm::vec2 calculatePropertiesPanelPosition(float viewportWidth, float viewportHeight) const;
    };

} // namespace tremor::editor