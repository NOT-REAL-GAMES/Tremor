#version 450

// Inputs from vertex shader
layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec4 fragColor;

// Output
layout(location = 0) out vec4 outColor;

// SDF font texture
layout(binding = 1) uniform sampler2D fontTexture;

// Text rendering parameters
layout(push_constant) uniform TextParams {
    float smoothing;        // Edge smoothing factor
    float outlineWidth;     // Outline thickness (0 = no outline)
    vec4 outlineColor;      // Outline color
    float shadowOffsetX;    // Shadow X offset
    float shadowOffsetY;    // Shadow Y offset
    float shadowSoftness;   // Shadow blur amount
    vec4 shadowColor;       // Shadow color
} params;

float median(float r, float g, float b) {
    return max(min(r, g), min(max(r, g), b));
}

void main() {
    // Sample the SDF texture
    float distance = texture(fontTexture, fragTexCoord).r;
    
    // Calculate alpha from distance field with anti-aliasing
    float signedDist = (distance - 0.5) * 2.0;
    float smoothing = params.smoothing * fwidth(signedDist);
    float alpha = smoothstep(-smoothing, smoothing, signedDist);
    
    // For UI text, make alpha more opaque to block grid
    // Apply a power curve to make anti-aliased edges more opaque
    alpha = pow(alpha, 0.5); // Square root makes values closer to 1.0
    
    // Initialize with text color
    vec4 color = fragColor;
    color.a *= alpha;
    
    // Add outline if enabled
    if (params.outlineWidth > 0.0) {
        float outlineDist = signedDist + params.outlineWidth;
        float outlineAlpha = smoothstep(-smoothing, smoothing, outlineDist);
        
        // Blend outline with text
        vec4 outlineResult = params.outlineColor;
        outlineResult.a *= outlineAlpha * (1.0 - alpha);
        
        color = mix(outlineResult, color, color.a);
        color.a = max(color.a, outlineResult.a);
    }
    
    // Add shadow if enabled
    if (params.shadowOffsetX != 0.0 || params.shadowOffsetY != 0.0) {
        vec2 shadowCoord = fragTexCoord + vec2(params.shadowOffsetX, params.shadowOffsetY);
        
        // Check bounds
        if (shadowCoord.x >= 0.0 && shadowCoord.x <= 1.0 && 
            shadowCoord.y >= 0.0 && shadowCoord.y <= 1.0) {
            
            float shadowDist = texture(fontTexture, shadowCoord).r;
            shadowDist = (shadowDist - 0.5) * 2.0;
            
            float shadowSmoothing = params.shadowSoftness * fwidth(shadowDist);
            float shadowAlpha = smoothstep(-shadowSmoothing, shadowSmoothing, shadowDist);
            
            // Apply shadow behind text
            vec4 shadowResult = params.shadowColor;
            shadowResult.a *= shadowAlpha * (1.0 - color.a);
            
            color = mix(shadowResult, color, color.a);
            color.a = max(color.a, shadowResult.a);
        }
    }
    
    // Discard fully transparent pixels
    if (color.a < 0.001) {
        discard;
    }
    
    outColor = color;
}