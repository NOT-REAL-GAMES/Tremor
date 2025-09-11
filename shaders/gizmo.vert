#version 450

layout(binding = 0) uniform UniformBufferObject {
    mat4 mvp;
    vec3 gizmoPosition;
} ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;

layout(location = 0) out vec3 fragColor;

void main() {
    // Apply gizmo position offset, then MVP transformation to vertex position
    gl_Position = ubo.mvp * vec4(inPosition + ubo.gizmoPosition, 1.0);
    fragColor = inColor;
}