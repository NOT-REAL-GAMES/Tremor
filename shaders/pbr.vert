#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;    // Add normals for lighting
layout(location = 2) in vec2 inTexCoord;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec3 cameraPos;
} ubo;

layout(location = 0) out vec3 fragPosition;  // World position for lighting
layout(location = 1) out vec3 fragNormal;    // Normal vector for lighting
layout(location = 2) out vec2 fragTexCoord;  // Pass texture coords to fragment shader

void main() {
    gl_Position = ubo.proj * ubo.view * ubo.model * vec4(inPosition, 1.0);
    
    // Transform position to world space for lighting calculations
    fragPosition = vec3(ubo.model * vec4(inPosition, 1.0));
    
    // Transform normal to world space (simplified - assumes uniform scaling)
    fragNormal = normalize(mat3(ubo.model) * inNormal);
    
    fragTexCoord = inTexCoord;
}