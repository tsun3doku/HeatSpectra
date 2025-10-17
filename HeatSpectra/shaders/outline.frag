#version 450

// Sample depth and stencil textures
layout(set = 0, binding = 0) uniform sampler2D depthTexture;
layout(set = 0, binding = 1) uniform usampler2DMS stencilTexture;  // MSAA stencil with SAMPLED_BIT

layout(push_constant) uniform PushConstants {
    float outlineThickness;
    uint selectedModelID;
    vec3 outlineColor;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    ivec2 pixelCoord = ivec2(gl_FragCoord.xy);
    int sampleCount = textureSamples(stencilTexture);
    float thickness = max(pc.outlineThickness, 1.0);
    int thickInt = int(thickness);
    
    // Count how many samples at this pixel match the selected model
    int selectedSamples = 0;
    for (int s = 0; s < sampleCount; s++) {
        if (texelFetch(stencilTexture, pixelCoord, s).r == pc.selectedModelID) {
            selectedSamples++;
        }
    }
    
    // Only process pixels containing the selected model
    if (selectedSamples == 0) {
        discard;
    }
    
    // Check neighbors for edge detection
    bool isEdge = false;
    
    // Check all 4 cardinal neighbors
    ivec2 neighbors[4] = ivec2[4](
        ivec2(-thickInt, 0),
        ivec2(thickInt, 0),
        ivec2(0, thickInt),
        ivec2(0, -thickInt)
    );
    
    for (int n = 0; n < 4; n++) {
        ivec2 neighborCoord = pixelCoord + neighbors[n];
        for (int s = 0; s < sampleCount; s++) {
            uint neighborStencil = texelFetch(stencilTexture, neighborCoord, s).r;
            if (neighborStencil != pc.selectedModelID) {
                isEdge = true;
                break;
            }
        }
        if (isEdge) break;
    }
    
    // Check if current pixel has mixed samples 
    if (!isEdge && selectedSamples < sampleCount) {
        isEdge = true;
    }
    
    if (isEdge) {
        // Coverage = how many samples are selected / total samples
        float coverage = float(selectedSamples) / float(sampleCount);
        outColor = vec4(pc.outlineColor, coverage);
    } else {
        discard;
    }
}
