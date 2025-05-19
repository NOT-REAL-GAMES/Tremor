#version 450

layout(location = 0) in vec3 fragPosition;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;

layout(set = 0, binding = 1) uniform sampler2D texSampler;

layout(set = 0, binding = 2) uniform LightUBO {
    vec3 position;        // Light position
    vec3 color;           // Light color
    float ambientStrength;
    float diffuseStrength;
    float specularStrength;
    float shininess;
} light;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec3 cameraPos;      // Need camera position for specular calculation
} ubo;

layout(location = 0) out vec4 outColor;

void main() {
    // Ambient component
    vec3 ambient = light.ambientStrength * light.color;
    
    // Diffuse component
    vec3 norm = normalize(fragNormal);
    vec3 lightDir = normalize(light.position - fragPosition);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = light.diffuseStrength * diff * light.color;
    
    // Specular component (Blinn-Phong)
    vec3 viewDir = normalize(ubo.cameraPos - fragPosition);
    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(norm, halfwayDir), 0.0), light.shininess);
    vec3 specular = light.specularStrength * spec * light.color;
    
    // Sample texture color
    vec4 texColor = texture(texSampler, fragTexCoord);
    
    // Combine lighting with texture
    vec3 result = (ambient + diffuse + specular) * texColor.rgb;
    outColor = vec4(result, texColor.a);
}