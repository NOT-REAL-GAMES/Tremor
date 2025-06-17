#version 450

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in uint inColor;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec4 fragColor;

layout(binding = 0) uniform UniformBufferObject {
    mat4 projection;
} ubo;

void main() {
    gl_Position = ubo.projection * vec4(inPosition, 0.0, 1.0);
    fragTexCoord = inTexCoord;
    
    // Unpack color from uint to vec4
    fragColor = vec4(
        float((inColor >> 24) & 0xFF) / 255.0,
        float((inColor >> 16) & 0xFF) / 255.0,
        float((inColor >> 8) & 0xFF) / 255.0,
        float(inColor & 0xFF) / 255.0
    );
}