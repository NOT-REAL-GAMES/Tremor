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
       color += vec4(((bufferIndex+1)%4)/4.0, ((bufferIndex+1)%16)/16.0, ((bufferIndex+1)%256)/256.0 , 1.0); 

    
    // Add some shading to make triangles more visible
    vec2 fragCoord = gl_FragCoord.xy;
    float gridFactor = 0.0;
    if (mod(gl_FragCoord.x, 20.0) < 1.0 || mod(gl_FragCoord.y, 20.0) < 1.0) {
        gridFactor = 1.0;
    }
    
    color.rgb *= 1.0 - 0.2 * gridFactor;
    
    fragColor = color;
}