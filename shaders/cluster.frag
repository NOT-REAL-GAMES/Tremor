// cluster.frag - Fixed clustered rendering fragment shader with simplified PBR
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

// UBO and buffers (same as mesh shader)
layout(set = 0, binding = 0) uniform ClusterUBO {
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

struct Cluster {
    uint lightOffset;
    uint lightCount;
    uint objectOffset;
    uint objectCount;
};

struct ClusterLight {
    vec3 position;
    float radius;
    vec3 color;
    float intensity;
    int type;           // 0=point, 1=spot, 2=directional
    float spotAngle;
    float spotSoftness;
    float padding;
};

struct PBRMaterial {
    vec4 baseColor;
    float metallic;
    float roughness;
    float normalScale;
    float occlusionStrength;
    vec3 emissiveColor;
    float emissiveFactor;
    int albedoTexture;
    int normalTexture;
    int metallicRoughnessTexture;
    int occlusionTexture;
    int emissiveTexture;
    float alphaCutoff;
    uint flags;
    float padding;
};

layout(set = 0, binding = 1) readonly buffer ClusterBuffer {
    Cluster clusters[];
};

layout(set = 0, binding = 3) readonly buffer LightBuffer {
    ClusterLight lights[];
};

layout(set = 0, binding = 4) readonly buffer IndexBuffer {
    uint indices[];
};

layout(set = 0, binding = 8) readonly buffer MaterialBuffer {
    PBRMaterial materials[];
};

layout(set = 0, binding = 9) uniform sampler2D albedoTexture;
layout(set = 0, binding = 10) uniform sampler2D normalTexture;

// Constants
const float PI = 3.14159265359;
const float EPSILON = 1e-6;

// Simplified PBR functions
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    
    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    
    return num / max(denom, EPSILON);
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    
    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    
    return num / max(denom, EPSILON);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    
    return ggx1 * ggx2;
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Simplified cluster coordinate calculation
uvec3 worldToCluster(vec3 worldPos) {
    // Transform to view space
    vec4 viewPos = ubo.viewMatrix * vec4(worldPos, 1.0);
    
    // Simple perspective projection for screen space
    vec4 clipPos = ubo.projMatrix * viewPos;
    vec3 ndc = clipPos.xyz / max(clipPos.w, EPSILON);
    
    // Map NDC [-1,1] to cluster coordinates [0, dimensions-1]
    uint clusterX = uint(clamp((ndc.x + 1.0) * 0.5 * float(ubo.clusterDimensions.x), 0.0, float(ubo.clusterDimensions.x - 1)));
    uint clusterY = uint(clamp((ndc.y + 1.0) * 0.5 * float(ubo.clusterDimensions.y), 0.0, float(ubo.clusterDimensions.y - 1)));
    
    // Simple linear Z distribution for now
    float zViewSpace = max(-viewPos.z, ubo.zPlanes.x);
    zViewSpace = min(zViewSpace, ubo.zPlanes.y);
    
    float zNormalized = (zViewSpace - ubo.zPlanes.x) / max(ubo.zPlanes.y - ubo.zPlanes.x, EPSILON);
    uint clusterZ = uint(clamp(zNormalized * float(ubo.clusterDimensions.z - 1), 0.0, float(ubo.clusterDimensions.z - 1)));
    
    return uvec3(clusterX, clusterY, clusterZ);
}

// Get cluster index from 3D coordinates
uint getClusterIndex(uvec3 clusterCoords) {
    return clusterCoords.z * ubo.clusterDimensions.x * ubo.clusterDimensions.y +
           clusterCoords.y * ubo.clusterDimensions.x +
           clusterCoords.x;
}

void main() {
    // Early exit for invalid material
    if (fragMaterialID >= materials.length()) {
        outColor = vec4(1.0, 0.0, 1.0, 1.0); // Magenta for invalid material
        return;
    }
    
    PBRMaterial material = materials[fragMaterialID];
    
    // Sample material properties
    vec4 albedo = material.baseColor;
    if (material.albedoTexture >= 0) {
        albedo *= texture(albedoTexture, fragTexCoord);
    }
    
    // Alpha test
    if (albedo.a < material.alphaCutoff) {
        discard;
    }
    
    float metallic = clamp(material.metallic, 0.0, 1.0);
    float roughness = clamp(material.roughness, 0.04, 1.0); // Prevent division by zero
    float ao = 1.0; // Could sample AO texture here
    
    // Calculate normal (simplified)
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(ubo.cameraPos.xyz - fragWorldPos);
    
    // Calculate reflectance at normal incidence
    vec3 F0 = mix(vec3(0.04), albedo.rgb, metallic);
    
    // Lighting accumulation
    vec3 Lo = vec3(0.0);
    
    // Find the cluster this fragment belongs to
    uvec3 clusterCoords = worldToCluster(fragWorldPos);
    uint clusterIndex = getClusterIndex(clusterCoords);
    
    // Safety check
    if (clusterIndex < ubo.numClusters && clusterIndex < clusters.length()) {
        Cluster cluster = clusters[clusterIndex];
        
        // Process lights in this cluster
        uint maxLights = min(cluster.lightCount, 8u); // Limit lights for performance
        for (uint i = 0; i < maxLights; ++i) {
            uint lightIndexOffset = cluster.lightOffset + i;
            
            // Safety checks
            if (lightIndexOffset >= indices.length()) break;
            
            uint lightIndex = indices[lightIndexOffset];
            if (lightIndex >= lights.length()) continue;
            
            ClusterLight light = lights[lightIndex];
            
            vec3 L;
            vec3 radiance;
            
            if (light.type == 2) {
                // Directional light
                L = normalize(-light.position);
                radiance = light.color * light.intensity;
            } else {
                // Point light
                vec3 lightVec = light.position - fragWorldPos;
                float distance = length(lightVec);
                
                // Skip if outside radius
                if (distance > light.radius) continue;
                
                L = lightVec / max(distance, EPSILON);
                
                // Simple attenuation
                float attenuation = 1.0 / max(distance * distance, EPSILON);
                attenuation *= smoothstep(light.radius, light.radius * 0.5, distance);
                
                radiance = light.color * light.intensity * attenuation;
            }
            
            vec3 H = normalize(V + L);
            
            // Cook-Torrance BRDF (simplified)
            float NDF = DistributionGGX(N, H, roughness);
            float G = GeometrySmith(N, V, L, roughness);
            vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);
            
            vec3 kS = F;
            vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
            
            vec3 numerator = NDF * G * F;
            float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + EPSILON;
            vec3 specular = numerator / denominator;
            
            float NdotL = max(dot(N, L), 0.0);
            Lo += (kD * albedo.rgb / PI + specular) * radiance * NdotL;
        }
    }
    
    // Simple ambient lighting
    vec3 ambient = vec3(0.1) * albedo.rgb * ao;
    
    // Add emissive
    vec3 emissive = material.emissiveColor * material.emissiveFactor;
    
    vec3 color = ambient + Lo + emissive;
    
    // Simple tone mapping
    color = color / (color + vec3(1.0));
    
    // Gamma correction
    color = pow(color, vec3(1.0/2.2));
    
    outColor = vec4(color, albedo.a);
}