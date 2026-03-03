#version 450
#extension GL_KHR_vulkan_glsl : enable

layout(location = 0) out vec2 v_texcoord;

out gl_PerVertex {
    vec4 gl_Position;
};

void main()
{
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    v_texcoord = uv;
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
