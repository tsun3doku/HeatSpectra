#version 450

layout(binding = 0) uniform GridUniformBufferObject {
    mat4 view;
    mat4 proj;
    vec3 viewPos;
} ubo;

// Hash grid parameters
struct HashGridParams {
    vec3 gridMin;
    float cellSize;
    ivec3 gridDimensions;
    uint maxPointsPerCell;
    uint totalCells;
};

layout(binding = 1) buffer OccupiedCellsBuffer {
    uint occupiedCells[];  // List of occupied cell indices
} occupiedCellsBuffer;

layout(binding = 2) uniform HashGridParamsBuffer {
    HashGridParams params;
} hashGridParamsBuffer;

layout(push_constant) uniform PushConstants {
    mat4 modelMatrix;
    vec3 gridColor;
} pushConstants;

layout(location = 0) out vec3 fragColor;

// Cube corner offsets (8 corners)
const vec3 corners[8] = vec3[](
    vec3(0, 0, 0), vec3(1, 0, 0), vec3(1, 1, 0), vec3(0, 1, 0),  // Bottom face
    vec3(0, 0, 1), vec3(1, 0, 1), vec3(1, 1, 1), vec3(0, 1, 1)   // Top face
);

// Cube edge indices: 12 edges, 2 vertices per edge = 24 vertices total
const int edges[24] = int[](
    0, 1,  1, 2,  2, 3,  3, 0,  // Bottom face
    4, 5,  5, 6,  6, 7,  7, 4,  // Top face
    0, 4,  1, 5,  2, 6,  3, 7   // Vertical edges
);

// Convert 1D cell index to 3D grid coordinates
ivec3 cellIndexToCoord(uint cellIdx, ivec3 gridDims) {
    int z = int(cellIdx) / (gridDims.x * gridDims.y);
    int remainder = int(cellIdx) % (gridDims.x * gridDims.y);
    int y = remainder / gridDims.x;
    int x = remainder % gridDims.x;
    return ivec3(x, y, z);
}

void main() {
    HashGridParams params = hashGridParamsBuffer.params;
    
    // Which occupied cell are we drawing?
    uint occupiedCellIdx = gl_VertexIndex / 24;
    uint edgeVertIdx = gl_VertexIndex % 24;
    
    // Get the actual cell index from occupied cells list
    uint cellIdx = occupiedCellsBuffer.occupiedCells[occupiedCellIdx];
    
    // Convert cell index to 3D grid coordinates
    ivec3 gridCoord = cellIndexToCoord(cellIdx, params.gridDimensions);
    
    // Calculate position of cell's minimum corner in MODEL SPACE
    vec3 cellMinModel = params.gridMin + vec3(gridCoord) * params.cellSize;
    
    // Get which corner of the cube this vertex represents
    uint cornerIdx = edges[edgeVertIdx];
    vec3 cornerOffset = corners[cornerIdx] * params.cellSize;
    
    // Model space position
    vec3 modelPos = cellMinModel + cornerOffset;
    
    // Transform to world space using model matrix
    vec3 worldPos = (pushConstants.modelMatrix * vec4(modelPos, 1.0)).xyz;
    gl_Position = ubo.proj * ubo.view * vec4(worldPos, 1.0);
    
    // Use color from push constant
    fragColor = pushConstants.gridColor;
}
