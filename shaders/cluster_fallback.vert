#version 460

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inColor;
layout(location = 3) in vec2 inTexCoord;
layout(location = 4) in vec4 inTangent;

layout(push_constant) uniform FallbackPushConstants {
    mat4 mvp;
    vec4 baseColor;
} pc;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;

void main() {
    gl_Position = pc.mvp * vec4(inPosition, 1.0);

    vec4 vertexColor = inColor;
    if (length(vertexColor.rgb) < 0.0001 && vertexColor.a < 0.0001) {
        vertexColor = vec4(1.0);
    }

    fragColor = vertexColor * pc.baseColor;
    fragNormal = normalize(inNormal);
    fragTexCoord = inTexCoord + inTangent.xy * 0.0;
}
