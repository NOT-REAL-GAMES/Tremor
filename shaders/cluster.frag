// debug.frag - Simplified fragment shader for debugging
#version 460

// Input from mesh shader
layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec3 fragTangent;
layout(location = 4) in vec3 fragBitangent;
layout(location = 5) flat in uint fragMaterialID;

// Output
layout(location = 0) out vec4 outColor;

void main() {
    // Debug: Different colors based on material ID
    vec3 colors[4] = vec3[](
        vec3(1.0, 0.0, 0.0), // Red
        vec3(0.0, 1.0, 0.0), // Green  
        vec3(0.0, 0.0, 1.0), // Blue
        vec3(1.0, 1.0, 0.0)  // Yellow
    );
    
    vec3 color = colors[fragMaterialID % 4];
    
    // Simple lighting using normal
    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));
    vec3 normal = normalize(fragNormal);
    float ndotl = max(dot(normal, lightDir), 0.2); // Minimum ambient
    
    outColor = vec4(color * ndotl, 1.0);
}