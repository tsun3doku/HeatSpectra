#version 450

layout(set = 0, binding = 0) uniform CameraUbo {
    mat4 view;
    mat4 proj;
    mat4 invView;
} camera;

layout(push_constant) uniform ContactPushConstant {
    vec3 gridMin;
    float cellSize;
    ivec3 gridDim;
    float range;
} pc;

struct ContactRegion {
    vec4 centerExtentW; // xyz = region center, w = extentW 
    vec4 normalExtentU; // xyz = region normal, w = extentU 
    vec4 uAxisExtentV;  // xyz = uAxis,         w = extentV
    vec4 vAxis;         // xyz = vAxis,         w = 0.0
    uint channelA;      // SDF A index
    uint channelB;      // SDF B index
    uint psiChannel;    // Precomputed Psi (sdfA - sdfB)
    uint pad0;
};

layout(std430, set = 0, binding = 1) readonly buffer RegionBuffer {
    ContactRegion regions[];
};

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) flat out uint outInstanceIndex;

// 36 vertices of unit box [-1, 1]^3 with CCW outward normals
const vec3 cubeVertices[36] = vec3[36](
    // -X face (normal = -1, 0, 0)
    vec3(-1, -1, -1), vec3(-1,  1,  1), vec3(-1,  1, -1),
    vec3(-1, -1, -1), vec3(-1, -1,  1), vec3(-1,  1,  1),
    // +X face (normal = +1, 0, 0)
    vec3( 1, -1, -1), vec3( 1,  1, -1), vec3( 1,  1,  1),
    vec3( 1, -1, -1), vec3( 1,  1,  1), vec3( 1, -1,  1),
    // -Y face (normal = 0, -1, 0)
    vec3(-1, -1, -1), vec3( 1, -1, -1), vec3( 1, -1,  1),
    vec3(-1, -1, -1), vec3( 1, -1,  1), vec3(-1, -1,  1),
    // +Y face (normal = 0, +1, 0)
    vec3(-1,  1, -1), vec3( 1,  1,  1), vec3( 1,  1, -1),
    vec3(-1,  1, -1), vec3(-1,  1,  1), vec3( 1,  1,  1),
    // -Z face (normal = 0, 0, -1)
    vec3(-1, -1, -1), vec3( 1,  1, -1), vec3( 1, -1, -1),
    vec3(-1, -1, -1), vec3(-1,  1, -1), vec3( 1,  1, -1),
    // +Z face (normal = 0, 0, +1)
    vec3(-1, -1,  1), vec3( 1, -1,  1), vec3( 1,  1,  1),
    vec3(-1, -1,  1), vec3( 1,  1,  1), vec3(-1,  1,  1)
);

void main() {
    uint regionIdx = gl_InstanceIndex;
    ContactRegion region = regions[regionIdx];

    vec3 center = region.centerExtentW.xyz;
    float extentW = region.centerExtentW.w;
    vec3 normal = region.normalExtentU.xyz;
    float extentU = region.normalExtentU.w;
    vec3 uAxis = region.uAxisExtentV.xyz;
    float extentV = region.uAxisExtentV.w;
    vec3 vAxis = region.vAxis.xyz;

    // Half extents of the oriented proxy box
    float rEffU = extentU + pc.range * pc.cellSize + 0.5 * pc.cellSize;
    float rEffV = extentV + pc.range * pc.cellSize + 0.5 * pc.cellSize;
    float hHalf = extentW + 1.25 * pc.cellSize;

    // Check if camera is inside the expanded proxy OBB
    vec3 cameraPos = camera.invView[3].xyz;
    vec3 rel = cameraPos - center;
    vec3 cameraLocal = vec3(
        dot(rel, uAxis),
        dot(rel, vAxis),
        dot(rel, normal)
    );

    float insideEps = 0.05 * pc.cellSize;
    bool cameraInside =
        abs(cameraLocal.x) <= (rEffU + insideEps) &&
        abs(cameraLocal.y) <= (rEffV + insideEps) &&
        abs(cameraLocal.z) <= (hHalf + insideEps);

    // Reverse triangle winding when inside the proxy box so inner shell is rasterized with backface culling
    uint vertexIndex = uint(gl_VertexIndex);
    if (cameraInside) {
        uint triLocal = vertexIndex % 3u;
        if (triLocal == 1u)
            vertexIndex += 1u;
        else if (triLocal == 2u)
            vertexIndex -= 1u;
    }

    vec3 localPos = cubeVertices[vertexIndex];
    vec3 worldPos = center + localPos.x * rEffU * uAxis
                           + localPos.y * rEffV * vAxis
                           + localPos.z * hHalf * normal;

    fragWorldPos = worldPos;
    outInstanceIndex = regionIdx;
    gl_Position = camera.proj * camera.view * vec4(worldPos, 1.0);
}