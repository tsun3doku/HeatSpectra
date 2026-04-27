#version 450
#extension GL_KHR_vulkan_glsl : enable

layout(set = 0, binding = 0) uniform ViewUniforms {
    mat4 view;  // View matrix
    mat4 proj;  // Projection matrix
    vec3 pos;   // Camera position
    vec3 gridSize;  // Grid size (width, depth, height)
} viewUniforms;

layout(location = 0) out vec3 worldPos;

void main() {
    int vertexInPlane = gl_VertexIndex % 6;

    float halfW = viewUniforms.gridSize.x * 0.5;
    float halfD = viewUniforms.gridSize.y * 0.5;

    vec3 positions[6] = vec3[](
        vec3(1, 1, 0), vec3(-1, -1, 0), vec3(-1, 1, 0),
        vec3(-1, -1, 0), vec3(1, 1, 0), vec3(1, -1, 0)
    );
    vec3 p = positions[vertexInPlane];

    worldPos = vec3(p.x * halfW, 0.0, p.y * halfD);
    gl_Position = viewUniforms.proj * viewUniforms.view * vec4(worldPos, 1.0);
}
