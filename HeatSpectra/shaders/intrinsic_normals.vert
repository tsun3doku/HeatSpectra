#version 450

// Input: vertex ID is used as triangle index
layout(location = 0) out vec3 outCenter;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out float outArea;

// Storage buffer containing intrinsic triangle data
// Using a single buffer with an array inside
struct IntrinsicTriangleData {
    vec3 center;
    float area;
    vec3 normal;
    float padding;
};

layout(set = 0, binding = 0) readonly buffer IntrinsicTriangles {
    IntrinsicTriangleData data[];
} triangles;

void main() {
    // Fetch triangle data using vertex ID as index
    uint triangleIdx = gl_VertexIndex;
    
    outCenter = triangles.data[triangleIdx].center;
    outNormal = triangles.data[triangleIdx].normal;
    outArea = triangles.data[triangleIdx].area;
    
    // Pass center position to geometry shader (no transformation yet)
    gl_Position = vec4(outCenter, 1.0);
}
