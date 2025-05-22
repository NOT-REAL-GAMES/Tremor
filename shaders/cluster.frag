// cluster_frag.frag - SIMPLIFIED VERSION for testing
#version 460

// Input from mesh shader
layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inTangent;
layout(location = 3) in vec3 inBitangent;
layout(location = 4) in vec2 inTexCoord;
layout(location = 5) in vec3 inViewPos;
layout(location = 6) in vec4 inPrevClipPos;
layout(location = 7) in flat uint inMaterialID;
layout(location = 8) in flat uint inClusterIndex;
layout(location = 9) in flat uint inInstanceID;

// Outputs
layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outMotionVector;

// Enhanced UBO
layout(set = 0, binding = 0) uniform EnhancedClusterUBO {
    mat4 viewMatrix;
    mat4 projMatrix;
    mat4 invViewMatrix;
    mat4 invProjMatrix;
    vec4 cameraPos;
    uvec4 clusterDimensions;
    vec4 zPlanes;
    vec4 screenSize;
    uint numLights;
    uint numObjects;
    uint numClusters;
    uint frameNumber;
    float time;
    float deltaTime;
    uint flags;
} ubo;

void main() {
    // For testing: simple animated color based on cluster and time
    float time = ubo.time;
    
    // Create a color based on cluster index and time
    vec3 clusterColor = vec3(
        sin(float(inClusterIndex) * 0.1 + time) * 0.5 + 0.5,
        cos(float(inClusterIndex) * 0.15 + time) * 0.5 + 0.5,
        sin(float(inClusterIndex) * 0.2 + time * 0.5) * 0.5 + 0.5
    );
    
    outColor = vec4(clusterColor, 1.0);
    outNormal = vec4(inNormal * 0.5 + 0.5, 1.0);
    outMotionVector = vec4(0.0, 0.0, 0.0, 1.0);
}