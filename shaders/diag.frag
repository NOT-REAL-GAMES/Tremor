// diag.frag - Diagnostic fragment shader with labels
#version 460

layout(location = 0) in vec4 meshColor;
layout(location = 1) flat in uint bufferIndex;

layout(location = 0) out vec4 fragColor;

// Test texture bindings
layout(set = 0, binding = 9) uniform sampler2D albedoTexture;
layout(set = 0, binding = 10) uniform sampler2D normalTexture;

void main() {
    vec4 color = meshColor;
    
    // For texture triangles, test if we can sample
    if (bufferIndex == 9) {
        // Try to sample albedo texture
        vec4 texColor = texture(albedoTexture, vec2(0.5, 0.5));
        // If texture is valid, it should return non-zero
        color = vec4(texColor.rgb, 1.0); 
    } else if (bufferIndex == 10) {
        // Try to sample normal texture
        vec4 texColor = texture(normalTexture, vec2(0.5, 0.5));
        color = vec4(texColor.rgb, 1.0); 
    }
    
    // Add some shading to make triangles more visible
    vec2 fragCoord = gl_FragCoord.xy;
    float pattern = sin(fragCoord.x * 0.1) * sin(fragCoord.y * 0.1);
    color.rgb *= 0.8 + 0.2 * pattern;
    
    fragColor = color;
}