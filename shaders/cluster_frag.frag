#version 460

// Simple inputs
layout(location = 0) in vec4 inColor;

// Simple output
layout(location = 0) out vec4 outColor;

void main() {
    // Just pass through the color
    outColor = inColor;
}