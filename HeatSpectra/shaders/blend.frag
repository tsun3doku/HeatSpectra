#version 450
#extension GL_KHR_vulkan_glsl:enable

layout(input_attachment_index=0, set=0, binding=0) uniform subpassInput gridInput;
layout(location=0) in vec2 inUV;
layout(location=0) out vec4 outColor;

void main() {
    vec4 grid = subpassLoad(gridInput);
    outColor = grid;
}