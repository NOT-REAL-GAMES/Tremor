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
    float gridFactor = 0.0;
    if (mod(gl_FragCoord.x, 20.0) < 1.0 || mod(gl_FragCoord.y, 20.0) < 1.0) {
        gridFactor = 1.0;
    }
    
    color.rgb *= 1.0 - 0.2 * gridFactor;
    
    fragColor = color;
}