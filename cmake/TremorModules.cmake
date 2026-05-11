# Tremor module layout scaffold.
#
# This file does not fully split the engine into libraries yet; it establishes
# the ownership map and IDE grouping so we can migrate incrementally without a
# destabilizing "move everything at once" change.

set(TREMOR_APP_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
)

set(TREMOR_FOUNDATION_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/tremor_platform.h
    ${CMAKE_CURRENT_SOURCE_DIR}/tremor_core.h
    ${CMAKE_CURRENT_SOURCE_DIR}/tremor_graphics_platform.h
    ${CMAKE_CURRENT_SOURCE_DIR}/logger.h
    ${CMAKE_CURRENT_SOURCE_DIR}/main.h
    ${CMAKE_CURRENT_SOURCE_DIR}/mem.h
    ${CMAKE_CURRENT_SOURCE_DIR}/handle.h
    ${CMAKE_CURRENT_SOURCE_DIR}/gfx_resource_types.h
    ${CMAKE_CURRENT_SOURCE_DIR}/gfx_resource_handles.h
    ${CMAKE_CURRENT_SOURCE_DIR}/vk_resource_wrappers.h
    ${CMAKE_CURRENT_SOURCE_DIR}/res.h
    ${CMAKE_CURRENT_SOURCE_DIR}/gfx.h
    ${CMAKE_CURRENT_SOURCE_DIR}/gfx_impl.inl
    ${CMAKE_CURRENT_SOURCE_DIR}/RenderBackend.h
    ${CMAKE_CURRENT_SOURCE_DIR}/RenderBackendBase.h
)

set(TREMOR_RUNTIME_ASSET_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/gltf_importer.cpp
)

set(TREMOR_RUNTIME_ASSET_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/gltf_importer.h
)

set(TREMOR_RUNTIME_SCRIPTING_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/flecs_interpreter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui_message_center.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui_message_commands.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/script_ecs_components.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/render_interop.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/script_render_system.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/physics_interop.cpp
)

set(TREMOR_RUNTIME_SCRIPTING_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/flecs_interpreter.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ui_message_center.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ui_message_commands.h
    ${CMAKE_CURRENT_SOURCE_DIR}/script_ecs_components.h
    ${CMAKE_CURRENT_SOURCE_DIR}/render_interop.h
    ${CMAKE_CURRENT_SOURCE_DIR}/script_render_system.h
    ${CMAKE_CURRENT_SOURCE_DIR}/physics_interop.h
)

set(TREMOR_RUNTIME_GAMEPLAY_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/dmc_survivors.h
)

set(TREMOR_RUNTIME_PHYSICS_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/dmc_physics.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/jolt_physics_world.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/jolt_physics_adapter.cpp
)

set(TREMOR_RUNTIME_PHYSICS_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/dmc_physics.h
    ${CMAKE_CURRENT_SOURCE_DIR}/jolt_physics_world.h
    ${CMAKE_CURRENT_SOURCE_DIR}/jolt_physics_adapter.h
)

set(TREMOR_RUNTIME_AUDIO_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/audio/taffy_audio_processor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/audio/taffy_polyphonic_processor.cpp
)

set(TREMOR_RUNTIME_AUDIO_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/audio/taffy_audio_processor.h
    ${CMAKE_CURRENT_SOURCE_DIR}/audio/taffy_polyphonic_processor.h
)

set(TREMOR_RUNTIME_VM_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/vm.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/vm_bytecode.cpp
)

set(TREMOR_RUNTIME_VM_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/vm.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/vm_bytecode.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/vm_decoder.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/vm_execution.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/vm_memory.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/vm_syscall.hpp
)

set(TREMOR_RUNTIME_RHI_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/vk.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/volk.c
)

set(TREMOR_RUNTIME_RHI_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/vk.h
    ${CMAKE_CURRENT_SOURCE_DIR}/vk_rhi.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Runtime/TremorRHI/vk_rhi.h
    ${CMAKE_CURRENT_SOURCE_DIR}/volk.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Runtime/TremorRHI/vk_resource_wrappers.h
)

set(TREMOR_RUNTIME_RENDERER_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Runtime/TremorRenderer/taffy_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Runtime/TremorRenderer/taffy_mesh.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Runtime/TremorRenderer/sdf_text_renderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Runtime/TremorRenderer/ui_renderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Runtime/TremorRenderer/sequencer_ui.cpp
)

set(TREMOR_RUNTIME_RENDERER_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Runtime/TremorRenderer/vk_renderer_support.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Runtime/TremorRenderer/taffy_integration.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Runtime/TremorRenderer/taffy_mesh.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Runtime/TremorRenderer/sdf_text_renderer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Runtime/TremorRenderer/ui_renderer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Runtime/TremorRenderer/sequencer_ui.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Runtime/TremorRenderCore/gfx_resource_types.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Runtime/TremorRenderCore/gfx_resource_handles.h
    ${CMAKE_CURRENT_SOURCE_DIR}/vk_renderer_support.h
    ${CMAKE_CURRENT_SOURCE_DIR}/renderer/taffy_integration.h
    ${CMAKE_CURRENT_SOURCE_DIR}/renderer/taffy_mesh.h
    ${CMAKE_CURRENT_SOURCE_DIR}/renderer/sdf_text_renderer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/renderer/ui_renderer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/renderer/sequencer_ui.h
)

set(TREMOR_EDITOR_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/editor/model_editor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/editor/model_editor_ui.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/editor/editor_viewport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/editor/editor_tools.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/editor/editable_model.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/editor/grid_renderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/editor/gizmo_renderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/editor/model_editor_integration_impl.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/editor/model_editor_main_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/editor/file_dialog.cpp
)

set(TREMOR_EDITOR_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/editor/model_editor.h
    ${CMAKE_CURRENT_SOURCE_DIR}/editor/model_editor_ui.h
    ${CMAKE_CURRENT_SOURCE_DIR}/editor/model_editor_integration.h
    ${CMAKE_CURRENT_SOURCE_DIR}/editor/grid_renderer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/editor/gizmo_renderer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/editor/file_dialog.h
)

set(TREMOR_ALL_SOURCES
    ${TREMOR_APP_SOURCES}
    ${TREMOR_RUNTIME_ASSET_SOURCES}
    ${TREMOR_RUNTIME_SCRIPTING_SOURCES}
    ${TREMOR_RUNTIME_PHYSICS_SOURCES}
    ${TREMOR_RUNTIME_AUDIO_SOURCES}
    ${TREMOR_RUNTIME_VM_SOURCES}
    ${TREMOR_RUNTIME_RHI_SOURCES}
    ${TREMOR_RUNTIME_RENDERER_SOURCES}
    ${TREMOR_EDITOR_SOURCES}
)

set(TREMOR_ALL_HEADERS
    ${TREMOR_FOUNDATION_HEADERS}
    ${TREMOR_RUNTIME_ASSET_HEADERS}
    ${TREMOR_RUNTIME_SCRIPTING_HEADERS}
    ${TREMOR_RUNTIME_GAMEPLAY_HEADERS}
    ${TREMOR_RUNTIME_PHYSICS_HEADERS}
    ${TREMOR_RUNTIME_AUDIO_HEADERS}
    ${TREMOR_RUNTIME_VM_HEADERS}
    ${TREMOR_RUNTIME_RHI_HEADERS}
    ${TREMOR_RUNTIME_RENDERER_HEADERS}
    ${TREMOR_EDITOR_HEADERS}
)

function(tremor_assign_source_groups)
    source_group("Source\\Bootstrap" FILES ${TREMOR_APP_SOURCES})

    source_group("Source\\Foundation\\TremorCore" FILES
        ${TREMOR_FOUNDATION_HEADERS})

    source_group("Source\\Runtime\\TremorAssets" FILES
        ${TREMOR_RUNTIME_ASSET_SOURCES}
        ${TREMOR_RUNTIME_ASSET_HEADERS})

    source_group("Source\\Runtime\\TremorGameplay" FILES
        ${TREMOR_RUNTIME_SCRIPTING_SOURCES}
        ${TREMOR_RUNTIME_SCRIPTING_HEADERS}
        ${TREMOR_RUNTIME_GAMEPLAY_HEADERS})

    source_group("Source\\Runtime\\TremorPhysics" FILES
        ${TREMOR_RUNTIME_PHYSICS_SOURCES}
        ${TREMOR_RUNTIME_PHYSICS_HEADERS})

    source_group("Source\\Runtime\\TremorAudio" FILES
        ${TREMOR_RUNTIME_AUDIO_SOURCES}
        ${TREMOR_RUNTIME_AUDIO_HEADERS})

    source_group("Source\\Runtime\\TremorVM" FILES
        ${TREMOR_RUNTIME_VM_SOURCES}
        ${TREMOR_RUNTIME_VM_HEADERS})

    source_group("Source\\Runtime\\TremorRHI" FILES
        ${TREMOR_RUNTIME_RHI_SOURCES}
        ${TREMOR_RUNTIME_RHI_HEADERS})

    source_group("Source\\Runtime\\TremorRenderer" FILES
        ${TREMOR_RUNTIME_RENDERER_SOURCES}
        ${TREMOR_RUNTIME_RENDERER_HEADERS})

    source_group("Source\\Editor\\TremorEditorCore" FILES
        ${TREMOR_EDITOR_SOURCES}
        ${TREMOR_EDITOR_HEADERS})
endfunction()
