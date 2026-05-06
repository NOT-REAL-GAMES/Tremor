#version 460

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 normal = normalize(fragNormal);
    vec3 lightDir = normalize(vec3(0.35, 0.75, 0.55));
    float lighting = 0.35 + 0.65 * max(dot(normal, lightDir), 0.0);

    float checker = mod(floor(fragTexCoord.x * 8.0) + floor(fragTexCoord.y * 8.0), 2.0);
    float detail = mix(0.98, 1.02, checker);

    outColor = vec4(fragColor.rgb * lighting * detail, fragColor.a);
}
