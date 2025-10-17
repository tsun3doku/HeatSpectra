#version 450
#extension GL_KHR_vulkan_glsl:enable
layout(location = 0) out vec2 outUV; // Pass UV to the fragment shader

out gl_PerVertex {
    vec4 gl_Position;
};

void main() {
    outUV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2); // Calculate UV coordinates
    gl_Position = vec4(outUV * 2.0 - 1.0, 0.0, 1.0);            // Convert to clip space
}
