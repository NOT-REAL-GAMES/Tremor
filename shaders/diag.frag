#version 460

// Input from mesh shader
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inColor;

// Output color
layout(location = 0) out vec4 outColor;

void main() {
    // Just output the color determined in the mesh shader
    outColor = inColor;
    
    // If you need to visualize which specific buffers are valid/invalid,
    // you can add a debug grid pattern
    float gridFactor = 0.9;
    if (mod(gl_FragCoord.x, 20.0) < 1.0 || mod(gl_FragCoord.y, 20.0) < 1.0) {
        gridFactor = 1.0;
    }
    
    outColor *= gridFactor;
}