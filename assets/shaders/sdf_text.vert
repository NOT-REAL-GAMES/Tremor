#version 450

// Vertex attributes
layout(location = 0) in vec2 inPosition;    // 2D position for UI text
layout(location = 1) in vec2 inTexCoord;    // UV coordinates
layout(location = 2) in vec4 inColor;       // Text color

// Outputs to fragment shader
layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec4 fragColor;

// Uniforms
layout(binding = 0) uniform UniformBufferObject {
    mat4 projection;    // Orthographic projection for UI
    vec2 screenSize;    // Screen dimensions
    float time;         // For animations
    float pad;
} ubo;

void main() {
    // Transform 2D position to clip space
    gl_Position = ubo.projection * vec4(inPosition, 0.0, 1.0);
    
    // Pass through texture coordinates and color
    fragTexCoord = inTexCoord;
    fragColor = inColor;
}