#version 450

// Input: vertex ID is used as vertex index
layout(location = 0) out vec3 outPosition;
layout(location = 1) out vec3 outNormal;

// Storage buffer containing intrinsic vertex data
struct IntrinsicVertexData {
    vec3 position;
    uint intrinsicVertexId;
    vec3 normal;
    float padding;
};

layout(set = 0, binding = 0) readonly buffer IntrinsicVertices {
    IntrinsicVertexData data[];
} vertices;

void main() {
    // Fetch vertex data using vertex ID as index
    uint vertexIdx = gl_VertexIndex;
    
    outPosition = vertices.data[vertexIdx].position;
    outNormal = vertices.data[vertexIdx].normal;
    
    // Pass position to geometry shader (no transformation yet)
    gl_Position = vec4(outPosition, 1.0);
}
