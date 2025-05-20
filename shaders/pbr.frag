#version 460
#extension GL_EXT_scalar_block_layout : require

// Input from vertex shader
layout(location = 0) in vec3 inPosition;    // World position
layout(location = 1) in vec3 inNormal;      // World normal
layout(location = 2) in vec2 inTexCoord;    // Texture coordinates
layout(location = 3) in vec3 inTangent;     // Optional: for normal mapping

// Output
layout(location = 0) out vec4 outColor;

//-----------------------------------------------------------------------------
// Standard camera/transform uniforms (Set 0, Binding 0)
//-----------------------------------------------------------------------------
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    mat4 viewProj;
    vec3 cameraPos;
    float padding;
} camera;

//-----------------------------------------------------------------------------
// Material properties (Set 0, Binding 1)
//-----------------------------------------------------------------------------
layout(set = 0, binding = 1) uniform MaterialUBO {
    vec4 baseColor;
    float metallic;
    float roughness;
    float ao;
    float emissiveFactor;
    vec3 emissiveColor;
    float padding;
    
    // Flags for available textures
    int hasAlbedoMap;
    int hasNormalMap;
    int hasMetallicRoughnessMap;
    int hasEmissiveMap;
    int hasOcclusionMap;
} material;

//-----------------------------------------------------------------------------
// Material textures (Set 0, Binding 2-6)
//-----------------------------------------------------------------------------
layout(set = 0, binding = 2) uniform sampler2D albedoMap;
layout(set = 0, binding = 3) uniform sampler2D normalMap;
layout(set = 0, binding = 4) uniform sampler2D metallicRoughnessMap;
layout(set = 0, binding = 5) uniform sampler2D occlusionMap;
layout(set = 0, binding = 6) uniform sampler2D emissiveMap;

//=============================================================================
// CLUSTERED LIGHTING DATA (Set 1)
//=============================================================================

// Light definitions
struct Light {
    vec3 position;   // World-space position
    float radius;    // Light radius
    vec3 color;      // RGB color
    float intensity; // Brightness scaling
};

//-----------------------------------------------------------------------------
// Clustered lighting resources (Set 1)
//-----------------------------------------------------------------------------
layout(set = 1, binding = 0, scalar) buffer LightBuffer {
    vec4 positionRadius[1048576];    // xyz = position, w = radius
    vec4 colorIntensity[1048576];    // rgb = color, a = intensity
} lights;

// Light grid contains cluster -> light list mapping
layout(set = 1, binding = 1, scalar) buffer LightGridBuffer {
    uvec2 data[4096];             // x = light list offset, y = light count
} lightGrid;

// Light index list contains actual light indices
layout(set = 1, binding = 2, scalar) buffer LightIndexListBuffer {
    uint count;               // Total number of light indices
    uint indices[1048576];           // Indices into the light buffer
} lightIndexList;

//-----------------------------------------------------------------------------
// Push constants for cluster parameters
//-----------------------------------------------------------------------------
layout(push_constant) uniform ClusterParams {
    uvec4 clusterDimensions;  // xyz = grid dimensions, w = total clusters
    vec4 nearFarParams;       // x = near, y = far, z = log depth enabled
};

//=============================================================================
// PBR FUNCTIONS
//=============================================================================

// Constants
const float PI = 3.14159265359;
const float EPSILON = 0.00001;

// Normal distribution function - GGX/Trowbridge-Reitz
float distributionGGX(vec3 N, vec3 H, float roughness) {
    float a2 = roughness * roughness;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    
    return a2 / max(denom, EPSILON);
}

// Geometry function - Schlick-GGX
float geometrySchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    
    return NdotV / (NdotV * (1.0 - k) + k);
}

// Combined geometry function - Smith
float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx1 = geometrySchlickGGX(NdotV, roughness);
    float ggx2 = geometrySchlickGGX(NdotL, roughness);
    
    return ggx1 * ggx2;
}

// Fresnel - Schlick approximation
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(max(1.0 - cosTheta, 0.0), 5.0);
}

//=============================================================================
// CLUSTERED LIGHTING FUNCTIONS
//=============================================================================

// Calculate cluster coordinates for this fragment
uvec3 getClusterCoordinates(vec3 viewPos) {
    // Viewport dimensions (derived from gl_FragCoord)
    uvec2 screenSize = uvec2(textureSize(albedoMap, 0)); // Or use a separate uniform
    uvec2 screenCoord = uvec2(gl_FragCoord.xy);
    
    // Calculate x,y coordinates
    uint clusterX = min(uint(float(screenCoord.x) / float(screenSize.x) * float(clusterDimensions.x)), 
                        clusterDimensions.x - 1);
    uint clusterY = min(uint(float(screenCoord.y) / float(screenSize.y) * float(clusterDimensions.y)), 
                        clusterDimensions.y - 1);
    
    // Calculate z coordinate based on view-space depth
    float linearDepth = -viewPos.z;  // Convert to positive depth
    float near = nearFarParams.x;
    float far = nearFarParams.y;
    float logDepthEnabled = nearFarParams.z;
    
    uint clusterZ;
    if (logDepthEnabled > 0.5) {
        // Logarithmic depth distribution
        float logDepth = log(linearDepth / near) / log(far / near);
        clusterZ = min(uint(logDepth * float(clusterDimensions.z)), clusterDimensions.z - 1);
    } else {
        // Linear depth distribution
        float linearDepthNorm = (linearDepth - near) / (far - near);
        clusterZ = min(uint(linearDepthNorm * float(clusterDimensions.z)), clusterDimensions.z - 1);
    }
    
    return uvec3(clusterX, clusterY, clusterZ);
}

// Calculate flat cluster index from 3D coordinates
uint getClusterIndex(uvec3 clusterCoord) {
    return clusterCoord.z * clusterDimensions.x * clusterDimensions.y + 
           clusterCoord.y * clusterDimensions.x + 
           clusterCoord.x;
}

// Extract light from the buffer
Light getLight(uint index) {
    vec4 posRadius = lights.positionRadius[index];
    vec4 colIntensity = lights.colorIntensity[index];
    
    Light light;
    light.position = posRadius.xyz;
    light.radius = posRadius.w;
    light.color = colIntensity.rgb;
    light.intensity = colIntensity.a;
    
    return light;
}

//=============================================================================
// MAIN FUNCTION
//=============================================================================
void main() {
    //-------------------------------------------------------------------------
    // 1. Get material properties
    //-------------------------------------------------------------------------
    // Base color
    vec4 albedo = material.baseColor;
    if (material.hasAlbedoMap > 0) {
        albedo *= texture(albedoMap, inTexCoord);
    }
    
    // Early alpha discard for transparent materials
    if (albedo.a < 0.01) {
        discard;
    }
    
    // Calculate normal (potentially with normal mapping)
    vec3 N = normalize(inNormal);
    
    if (material.hasNormalMap > 0 && length(inTangent) > 0.5) {
        // Create TBN matrix for normal mapping
        vec3 T = normalize(inTangent);
        vec3 B = normalize(cross(N, T));
        mat3 TBN = mat3(T, B, N);
        
        // Sample normal map and transform to world space
        vec3 normalMapValue = texture(normalMap, inTexCoord).rgb * 2.0 - 1.0;
        N = normalize(TBN * normalMapValue);
    }
    
    // Metallic and roughness
    float metallic = material.metallic;
    float roughness = material.roughness;
    
    if (material.hasMetallicRoughnessMap > 0) {
        vec2 mrSample = texture(metallicRoughnessMap, inTexCoord).rg;
        metallic = mrSample.r;
        roughness = mrSample.g;
    }
    
    // Ensure roughness is never 0 (to avoid division by zero)
    roughness = max(roughness, 0.04);
    
    // Ambient occlusion
    float ao = material.ao;
    if (material.hasOcclusionMap > 0) {
        ao *= texture(occlusionMap, inTexCoord).r;
    }
    
    // Emissive
    vec3 emissive = material.emissiveColor * material.emissiveFactor;
    if (material.hasEmissiveMap > 0) {
        emissive *= texture(emissiveMap, inTexCoord).rgb;
    }
    
    //-------------------------------------------------------------------------
    // 2. Prepare PBR lighting parameters
    //-------------------------------------------------------------------------
    // View direction
    vec3 V = normalize(camera.cameraPos - inPosition);
    
    // Surface reflection at zero incidence angle (F0)
    // Dielectrics have F0 values around 0.04, while metals use their albedo color
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo.rgb, metallic);
    
    //-------------------------------------------------------------------------
    // 3. Find lights affecting this fragment using clustered lighting
    //-------------------------------------------------------------------------
    // Transform position to view space
    vec3 viewPos = (camera.view * vec4(inPosition, 1.0)).xyz;
    
    // Calculate cluster for this fragment
    uvec3 clusterCoord = getClusterCoordinates(viewPos);
    uint clusterIndex = getClusterIndex(clusterCoord);
    
    // Get light list for this cluster
    uvec2 lightData = lightGrid.data[clusterIndex];
    uint lightOffset = lightData.x;
    uint lightCount = lightData.y;
    
    //-------------------------------------------------------------------------
    // 4. Perform lighting calculations
    //-------------------------------------------------------------------------
    // Initialize lighting result
    vec3 Lo = vec3(0.0);  // Outgoing radiance
    
    // Process each light affecting this fragment
    for (uint i = 0; i < lightCount; i++) {
        uint lightIndex = lightIndexList.indices[lightOffset + i];
        Light light = getLight(lightIndex);
        
        // Calculate light direction and distance
        vec3 L = light.position - inPosition;
        float distance = length(L);
        
        // Skip if outside light radius
        if (distance > light.radius) {
            continue;
        }
        
        L = normalize(L);
        
        // Calculate attenuation: use a smoother falloff than inverse square
        // This can be customized based on your lighting needs
        float attenuation = max(0.0, 1.0 - pow(distance / light.radius, 4.0));
        attenuation = attenuation * attenuation / (1.0 + distance * distance);
        
        // Combine light color and intensity with attenuation
        vec3 radiance = light.color * light.intensity * attenuation;
        
        // Calculate half-vector between light and view directions
        vec3 H = normalize(V + L);
        
        // Cook-Torrance BRDF calculation
        float NDF = distributionGGX(N, H, roughness);
        float G = geometrySmith(N, V, L, roughness);
        vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);
        
        // Combine the specular terms
        vec3 numerator = NDF * G * F;
        float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + EPSILON;
        vec3 specular = numerator / denominator;
        
        // Calculate how much light is reflected vs. refracted
        vec3 kS = F;  // Specular reflection factor
        vec3 kD = vec3(1.0) - kS;  // Diffuse factor
        kD *= 1.0 - metallic;  // Metals have no diffuse reflection
        
        // Add light contribution, combining diffuse and specular terms
        float NdotL = max(dot(N, L), 0.0);
        Lo += (kD * albedo.rgb / PI + specular) * radiance * NdotL;
    }
    
    //-------------------------------------------------------------------------
    // 5. Add ambient and emissive lighting
    //-------------------------------------------------------------------------
    // Simple ambient term - could be replaced with IBL for better results
    vec3 ambient = vec3(0.03) * albedo.rgb * ao;
    
    // Combine everything
    vec3 finalColor = ambient + Lo + emissive;
    
    // Tone mapping (exposure-based Reinhard operator)
    finalColor = finalColor / (finalColor + vec3(1.0));
    
    // Gamma correction
    finalColor = pow(finalColor, vec3(1.0/2.2));
    
    // Output final color
    outColor = vec4(finalColor, albedo.a);
    //outColor = vec4(1.0,0.0,0.4125,1.0);
}