#version 450
#extension GL_KHR_vulkan_glsl:enable

layout(input_attachment_index=0, set=0, binding=0) uniform subpassInput gridInput;
layout(input_attachment_index=1, set=0, binding=1) uniform subpassInput lightingInput;
layout(location=0) in vec2 inUV;
layout(location=0) out vec4 outColor;

void main() {
    vec4 lighting = subpassLoad(lightingInput);
    vec4 grid = subpassLoad(gridInput);
    
    // Standard alpha blend: grid over lighting
    if (grid.a > 0.01) {
        outColor = vec4(lighting.rgb * (1.0 - grid.a) + grid.rgb, 1.0);
    } else {
        outColor = vec4(lighting.rgb, 1.0);
    }
}