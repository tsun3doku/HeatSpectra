#version 450
#extension GL_KHR_vulkan_glsl : enable

layout(set = 0, binding = 0) uniform ViewUniforms {
    mat4 view;  // View matrix
    mat4 proj;  // Projection matrix
    vec3 pos;   // Camera position
    vec3 gridSize;  // Grid size (width, depth, height)
} viewUniforms;

layout(location = 0) out flat int planeID;   // Which plane: 0=floor, 1-4=walls
layout(location = 1) out vec3 worldPos;      // World space position
layout(location = 2) out vec3 cameraPos;     // Camera position

void main() {
    int planeIndex = gl_VertexIndex / 6;  // 0=floor, 1-4=walls
    int vertexInPlane = gl_VertexIndex % 6;
    
    planeID = planeIndex;
    
    float halfW = viewUniforms.gridSize.x * 0.5;
    float halfD = viewUniforms.gridSize.y * 0.5;
    float height = viewUniforms.gridSize.z;
    
    vec3 positions[6] = vec3[](
        vec3(1, 1, 0), vec3(-1, -1, 0), vec3(-1, 1, 0),
        vec3(-1, -1, 0), vec3(1, 1, 0), vec3(1, -1, 0)
    );
    vec3 p = positions[vertexInPlane];
    
    // Generate actual geometry for each plane
    if (planeID == 0) {
        // Floor plane (XZ at y=0)
        worldPos = vec3(p.x * halfW, 0.0, p.y * halfD);
    }
    else if (planeID == 1) {
        // +X wall (YZ plane) - from y=0 to y=height
        worldPos = vec3(halfW, (p.y + 1.0) * 0.5 * height, p.x * halfD);
    }
    else if (planeID == 2) {
        // -X wall (YZ plane) - from y=0 to y=height
        worldPos = vec3(-halfW, (p.y + 1.0) * 0.5 * height, p.x * halfD);
    }
    else if (planeID == 3) {
        // +Z wall (XY plane) - from y=0 to y=height
        worldPos = vec3(p.x * halfW, (p.y + 1.0) * 0.5 * height, halfD);
    }
    else {
        // -Z wall (XY plane) - from y=0 to y=height
        worldPos = vec3(p.x * halfW, (p.y + 1.0) * 0.5 * height, -halfD);
    }
    
    cameraPos = viewUniforms.pos;
    
    // Transform to clip space
    gl_Position = viewUniforms.proj * viewUniforms.view * vec4(worldPos, 1.0);
}
