#version 450

// Sample depth and stencil textures
layout(set = 0, binding = 0) uniform sampler2D depthTexture;
layout(set = 0, binding = 1) uniform usampler2D stencilTexture;  // Unsigned integer sampler for stencil

layout(push_constant) uniform PushConstants {
    float outlineThickness;
    vec3 outlineColor;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    // Stencil test ensures we're on selected model pixels (stencil = selectedModelID)
    // Now we can directly sample stencil values and detect edges cleanly!
    
    ivec2 texSize = textureSize(stencilTexture, 0);
    vec2 texCoord = gl_FragCoord.xy / vec2(texSize);
    vec2 texelSize = 1.0 / vec2(texSize);
    
    // Sample thickness in pixels
    float thickness = max(pc.outlineThickness, 1.0);
    
    // Get center stencil ID
    uint centerStencil = texture(stencilTexture, texCoord).r;
    
    // Check neighbors in a cross pattern for different stencil IDs
    uint stencilLeft = texture(stencilTexture, texCoord + vec2(-texelSize.x * thickness, 0.0)).r;
    uint stencilRight = texture(stencilTexture, texCoord + vec2(texelSize.x * thickness, 0.0)).r;
    uint stencilUp = texture(stencilTexture, texCoord + vec2(0.0, texelSize.y * thickness)).r;
    uint stencilDown = texture(stencilTexture, texCoord + vec2(0.0, -texelSize.y * thickness)).r;
    
    // If any neighbor has a different stencil ID, we're at an edge!
    // This includes edges against background (stencil=0) and other models (different ID)
    if (stencilLeft != centerStencil || 
        stencilRight != centerStencil || 
        stencilUp != centerStencil || 
        stencilDown != centerStencil) {
        outColor = vec4(pc.outlineColor, 1.0);
    } else {
        discard;
    }
}
