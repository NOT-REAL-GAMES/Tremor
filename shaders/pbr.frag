#version 450
layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

layout(binding = 1) uniform sampler2D texSampler;

void main() {
    // Simple output - either use a red color or the texture
    //outColor = vec4(1.0, 0.0, 0.0, 1.0); // Uncomment for solid red
    outColor = texture(texSampler, fragTexCoord);
}