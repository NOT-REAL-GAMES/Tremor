#version 460

// Add struct definitions
struct Cluster {
    uint lightOffset;
    uint lightCount;
    uint objectOffset;
    uint objectCount;
};

struct RenderableObject {
    mat4 transform;
    mat4 prevTransform;
    uint meshID;
    uint materialID;
};

struct ClusterLight {
    vec3 position;
    float radius;
    vec3 color;
    float intensity;
    int type;
    float spotAngle;
    float spotSoftness;
    float padding;
};

// Add descriptor bindings
layout(set = 0, binding = 0) uniform ClusterUBO {
    mat4 viewMatrix;
    mat4 projMatrix;
    vec4 cameraPos;
    uvec4 clusterDimensions;
    vec4 zPlanes;
    uint numLights;
    uint numObjects;
    uint numClusters;
    uint padding;
};

layout(set = 0, binding = 3) buffer LightBuffer {
    ClusterLight lights[];
};

// Vertex inputs
layout(location = 0) in vec3 inPosition;  // From outPosition in mesh shader
layout(location = 1) in vec3 inNormal;    // From outNormal in mesh shader
layout(location = 2) in vec2 inTexCoord;  // From outTexCoord in mesh shader 
layout(location = 3) in vec4 inColor;     // From outColor in mesh shader
layout(location = 4) flat in uint inObjectID;  // From outObjectID in mesh shader

// Output color
layout(location = 0) out vec4 outColor;

void main() {
    // Simple color calculation
    outColor = inColor;
}