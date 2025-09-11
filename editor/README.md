# Tremor Model Editor

A comprehensive 3D model editor for Tremor Engine that supports the Taffy asset format. The editor provides intuitive tools for loading, editing, and saving 3D models with real-time visualization.

## Features

### Core Functionality
- **3D Viewport**: Full 3D visualization with orbital camera controls
- **Model Loading/Saving**: Native Taffy (.taf) asset format support
- **Transform Tools**: Move, rotate, and scale operations with visual gizmos
- **Selection System**: Mesh and vertex-level selection
- **Real-time Grid**: 3D reference grid for alignment
- **Quantized Coordinates**: Planetary-scale precision using Tremor's coordinate system

### User Interface
- **Tools Panel**: Mode selection (Select, Move, Rotate, Scale)
- **Properties Panel**: Real-time transform values and selection info
- **File Panel**: New, Open, Save, and Save As operations with file dialogs
- **Status Display**: Current operation and file status

### Technical Features
- **Vulkan Rendering**: Hardware-accelerated 3D graphics
- **Grid Renderer**: Customizable reference grid with major/minor lines
- **Gizmo System**: Interactive 3D manipulation handles
- **File Dialogs**: Cross-platform file selection (GUI on Linux, console fallback)
- **Modular Architecture**: Clean separation of concerns

## Architecture

### Core Components

#### ModelEditor (`model_editor.h/cpp`)
Main editor class that coordinates all functionality:
- Session management
- Input handling
- Rendering coordination
- Transform operations

#### EditorViewport (`editor_viewport.cpp`)
3D visualization and camera system:
- Orbital camera controls (Alt+Drag to orbit, Shift+Drag to pan, Wheel to zoom)
- Projection matrix management
- Grid and gizmo rendering coordination

#### EditableModel (`editable_model.cpp`)
Taffy asset wrapper for editing operations:
- Asset loading/saving
- Vertex manipulation simulation
- Transform operations
- Mesh data management

#### EditorTools (`editor_tools.cpp`)
Transform gizmo system:
- Visual gizmo rendering
- Hit testing for interaction
- Transform calculation

#### ModelEditorUI (`model_editor_ui.cpp`)
User interface panels:
- Tool selection buttons
- Properties display
- File operations
- Status updates

### Rendering Components

#### GridRenderer (`grid_renderer.h/cpp`)
Renders the 3D reference grid:
- Configurable size and spacing
- Major/minor line differentiation
- Vulkan pipeline for line rendering

#### GizmoRenderer (`gizmo_renderer.h/cpp`)
Renders interactive transform gizmos:
- Translation arrows (X=red, Y=green, Z=blue)
- Rotation circles
- Scale handles
- Screen-space sizing

### Integration

#### ModelEditorIntegration (`model_editor_integration.h/cpp`)
Integration layer with VulkanBackend:
- Lifecycle management
- Resource creation
- Input routing
- Render pass setup

#### FileDialog (`file_dialog.h/cpp`)
Cross-platform file selection:
- Native dialogs on Windows (GetOpenFileName)
- Zenity integration on Linux
- Console fallback for all platforms

## Controls

### General
- **F1**: Toggle editor on/off
- **Esc**: Select mode and clear selection

### Modes
- **G**: Move/translate mode
- **R**: Rotate mode  
- **S**: Scale mode

### File Operations
- **Ctrl+N**: New model
- **Ctrl+O**: Open model
- **Ctrl+S**: Save model

### Viewport Navigation
- **Alt+Left Drag**: Orbit camera around target
- **Shift+Middle Drag**: Pan camera target
- **Mouse Wheel**: Zoom in/out

### Selection
- **Left Click**: Select mesh
- **Shift+Left Click**: Select vertex (when supported)

## Integration Guide

### Adding to Existing Engine

1. **Include Headers**:
```cpp
#include "editor/model_editor_integration.h"
```

2. **Create Integration** (in Engine constructor):
```cpp
auto* vulkanBackend = static_cast<tremor::gfx::VulkanBackend*>(rb.get());
if (vulkanBackend) {
    m_editorIntegration = std::make_unique<tremor::editor::ModelEditorIntegration>(*vulkanBackend);
    if (!m_editorIntegration->initialize()) {
        Logger::get().warning("Model Editor failed to initialize");
    }
}
```

3. **Handle Input** (in main loop):
```cpp
while (SDL_PollEvent(&event)) {
    // Let editor handle input first
    if (m_editorIntegration) {
        m_editorIntegration->handleInput(event);
        if (m_editorIntegration->isEditorEnabled()) {
            continue; // Editor consumed the event
        }
    }
    // ... existing input handling
}
```

4. **Update and Render**:
```cpp
// Update
if (m_editorIntegration) {
    m_editorIntegration->update(deltaTime);
}

// Render
rb.get()->beginFrame();
if (m_editorIntegration) {
    m_editorIntegration->render();
}
rb.get()->endFrame();
```

### Helper Functions

For easier integration, use the helper functions in `model_editor_main_integration.cpp`:

```cpp
// Create integration
auto integration = tremor::editor::createModelEditorIntegration(vulkanBackend);

// In main loop
bool consumed = tremor::editor::handleModelEditorInput(integration.get(), event);
tremor::editor::updateModelEditor(integration.get(), deltaTime);
tremor::editor::renderModelEditor(integration.get());
```

## Configuration

### Grid Settings
```cpp
// Access through EditorViewport
viewport->setShowGrid(true);
gridRenderer->setGridSize(50.0f);        // Grid extends ±50 units
gridRenderer->setGridSpacing(1.0f);      // 1 unit between lines
gridRenderer->setMajorLineInterval(10);  // Every 10th line is major
```

### Gizmo Settings
```cpp
// Access through EditorTools
tools->setGizmoSize(1.0f);
gizmoRenderer->setAxisColors(
    glm::vec3(1.0f, 0.3f, 0.3f),  // X = Red
    glm::vec3(0.3f, 1.0f, 0.3f),  // Y = Green
    glm::vec3(0.3f, 0.3f, 1.0f)   // Z = Blue
);
```

### UI Colors
```cpp
// Defined as constants in ModelEditorUI
static constexpr uint32_t PANEL_BG_COLOR = 0x2A2A2AE0;
static constexpr uint32_t BUTTON_ACTIVE_COLOR = 0x0080FFFF;
static constexpr uint32_t TEXT_COLOR = 0xFFFFFFFF;
```

## File Format Support

### Taffy Assets (.taf)
- Full read support for geometry chunks
- Quantized coordinate preservation
- Material information (future)
- Overlay system compatibility

### Export Formats
Currently supports Taffy format only. Future expansions planned for:
- glTF 2.0 export
- FBX export (via plugin)
- OBJ export

## Dependencies

### Required
- **Vulkan**: Graphics rendering
- **SDL2**: Window management and input
- **GLM**: Mathematics library
- **Taffy**: Asset format library

### Optional
- **Zenity**: Linux GUI file dialogs
- **Windows API**: Windows file dialogs

## Build Requirements

1. **Add to CMakeLists.txt**:
```cmake
# Editor source files
editor/model_editor.cpp
editor/model_editor_ui.cpp
editor/editor_viewport.cpp
editor/editor_tools.cpp
editor/editable_model.cpp
editor/grid_renderer.cpp
editor/gizmo_renderer.cpp
editor/model_editor_integration_impl.cpp
editor/model_editor_main_integration.cpp
editor/file_dialog.cpp

# Include directory
${CMAKE_CURRENT_SOURCE_DIR}/editor
```

2. **Compiler Support**:
- C++23 compatible compiler
- Vulkan SDK
- CMake 3.16+

## Known Limitations

### Current Implementation
- **Vertex Modification**: Simulated only (logging)
- **Shader Compilation**: Placeholder implementations
- **Material Editing**: Not yet implemented
- **Undo/Redo**: Framework ready, not implemented

### Performance Notes
- Grid uses line primitives (good for wireframe)
- Gizmos use screen-space sizing (constant visual size)
- UI updates are event-driven (efficient)

## Future Enhancements

### Planned Features
- **Material Editor**: PBR material editing
- **Texture Support**: Texture loading and preview
- **Animation Tools**: Basic keyframe animation
- **Plugin System**: Custom tool extensions
- **Multi-selection**: Multiple object manipulation

### Technical Improvements
- **Real Vertex Editing**: Direct mesh modification
- **Compiled Shaders**: SPIR-V shader compilation
- **Performance Optimization**: Frustum culling, LOD
- **Advanced Gizmos**: Constrained transforms, snapping

## Troubleshooting

### Common Issues

#### "Failed to initialize Model Editor"
- Check Vulkan device support
- Verify render pass compatibility
- Ensure adequate GPU memory

#### "GUI file dialog not available"
- Linux: Install zenity (`sudo apt install zenity`)
- Falls back to console dialog automatically

#### Grid/Gizmos not visible
- Check if shaders compiled successfully
- Verify camera position (try zooming out)
- Check if objects are behind camera

### Performance Issues
- Reduce grid size for complex scenes
- Disable gizmos when not needed
- Use lower sample counts for MSAA

## Contributing

### Code Style
- Follow existing Tremor conventions
- Use quantized coordinates for precision
- Implement proper Vulkan resource cleanup
- Add logging for debugging

### Testing
- Test with various Taffy assets
- Verify cross-platform compatibility
- Check memory leaks with Valgrind
- Performance profiling recommended

---

The Tremor Model Editor provides a solid foundation for 3D asset editing within the Tremor Engine ecosystem. Its modular design allows for easy extension and customization while maintaining high performance through Vulkan acceleration.