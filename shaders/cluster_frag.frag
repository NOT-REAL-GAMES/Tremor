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

// Enhanced structures
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
    int type;
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

layout(set = 0, binding = 9) uniform sampler2D defaultAlbedoTexture;
layout(set = 0, binding = 10) uniform sampler2D defaultNormalTexture;

// PBR functions
const float PI = 3.14159265359;

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float distributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    
    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    
    return num / denom;
}

float geometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    
    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    
    return num / denom;
}

float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = geometrySchlickGGX(NdotV, roughness);
    float ggx1 = geometrySchlickGGX(NdotL, roughness);
    
    return ggx1 * ggx2;
}

vec3 calculatePBRLighting(vec3 worldPos, vec3 N, vec3 V, PBRMaterial material) {
    vec3 albedo = material.baseColor.rgb;
    float metallic = material.metallic;
    float roughness = max(material.roughness, 0.04); // Prevent zero roughness
    float ao = material.occlusionStrength;
    
    // Calculate F0
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);
    
    vec3 Lo = vec3(0.0);
    
    // Process cluster lights
    if (inClusterIndex < ubo.numClusters) {
        Cluster cluster = clusters[inClusterIndex];
        
        for (uint i = 0; i < cluster.lightCount; i++) {
            uint lightIndex = indices[cluster.lightOffset + i];
            if (lightIndex >= ubo.numLights) continue;
            
            ClusterLight light = lights[lightIndex];
            
            vec3 L = normalize(light.position - worldPos);
            vec3 H = normalize(V + L);
            float distance = length(light.position - worldPos);
            
            // Attenuation
            float attenuation = 1.0 / (distance * distance);
            if (light.type == 0) { // Point light
                attenuation = max(0.0, 1.0 - distance / light.radius);
                attenuation *= attenuation;
            }
            
            vec3 radiance = light.color * light.intensity * attenuation;
            
            // Cook-Torrance BRDF
            float NDF = distributionGGX(N, H, roughness);
            float G = geometrySmith(N, V, L, roughness);
            vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);
            
            vec3 kS = F;
            vec3 kD = vec3(1.0) - kS;
            kD *= 1.0 - metallic;
            
            vec3 numerator = NDF * G * F;
            float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
            vec3 specular = numerator / denominator;
            
            float NdotL = max(dot(N, L), 0.0);
            Lo += (kD * albedo / PI + specular) * radiance * NdotL;
        }
    }
    
    // Ambient lighting (simple IBL approximation)
    vec3 F = fresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
    vec3 kS = F;
    vec3 kD = 1.0 - kS;
    kD *= 1.0 - metallic;
    
    vec3 irradiance = vec3(0.03); // Simple ambient
    vec3 diffuse = irradiance * albedo;
    
    vec3 ambient = (kD * diffuse) * ao;
    vec3 color = ambient + Lo;
    
    return color;
}

void main() {
    // Get material
    PBRMaterial material;
    if (inMaterialID < materials.length()) {
        material = materials[inMaterialID];
    } else {
        // Default material
        material.baseColor = vec4(0.8, 0.8, 0.8, 1.0);
        material.metallic = 0.0;
        material.roughness = 0.5;
        material.normalScale = 1.0;
        material.occlusionStrength = 1.0;
        material.emissiveColor = vec3(0.0);
        material.emissiveFactor = 0.0;
        material.alphaCutoff = 0.5;
        material.flags = 0;
    }
    
    // Sample textures
    vec4 albedoSample = texture(defaultAlbedoTexture, inTexCoord);
    vec3 normalSample = texture(defaultNormalTexture, inTexCoord).xyz * 2.0 - 1.0;
    
    // Apply albedo texture
    vec3 baseColor = material.baseColor.rgb * albedoSample.rgb;
    
    // Calculate normal mapping
    mat3 TBN = mat3(normalize(inTangent), normalize(inBitangent), normalize(inNormal));
    vec3 N = normalize(TBN * normalSample * material.normalScale);
    vec3 V = normalize(ubo.cameraPos.xyz - inWorldPos);
    
    // Calculate lighting
    vec3 color = calculatePBRLighting(inWorldPos, N, V, material);
    
    // Add emissive
    color += material.emissiveColor * material.emissiveFactor;
    
    // Alpha handling
    float alpha = material.baseColor.a * albedoSample.a;
    if ((material.flags & 1) != 0 && alpha < material.alphaCutoff) {
        discard;
    }
    
    // Tonemap and gamma correct
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0/2.2));
    
    // Output
    //outColor = vec4(color, alpha);
    outColor = vec4(1.0,0.0,0.4125,1.0);
    outNormal = vec4(N * 0.5 + 0.5, 1.0); // Pack normal for deferred rendering
    
    // Motion vectors for TAA
    vec2 currentPos = (gl_FragCoord.xy / ubo.screenSize.xy) * 2.0 - 1.0;
    vec2 prevPos = (inPrevClipPos.xy / inPrevClipPos.w) * 0.5 + 0.5;
    outMotionVector = vec4(currentPos - prevPos, 0.0, 1.0);
}