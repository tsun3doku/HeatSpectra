#pragma once

#include "domain/GeometryData.hpp"

#include <cstdint>
#include <vector>

struct IntrinsicMeshVertexData {
    uint32_t intrinsicVertexId = 0;
    float position[3] = {0.0f, 0.0f, 0.0f};
    float normal[3] = {0.0f, 0.0f, 1.0f};
    uint32_t inputLocationType = 0;
    uint32_t inputElementId = 0;
    float inputBaryCoords[3] = {1.0f, 0.0f, 0.0f};
};

struct IntrinsicMeshTriangleData {
    float center[3] = {0.0f, 0.0f, 0.0f};
    float normal[3] = {0.0f, 0.0f, 1.0f};
    float area = 0.0f;
    uint32_t vertexIndices[3] = {0u, 0u, 0u};
    uint32_t faceId = 0;
};

struct IntrinsicMeshData {
    std::vector<IntrinsicMeshVertexData> vertices;
    std::vector<uint32_t> triangleIndices;
    std::vector<uint32_t> faceIds;
    std::vector<IntrinsicMeshTriangleData> triangles;
    std::vector<int32_t> supportingHalfedges;
    std::vector<float> supportingAngles;
    std::vector<int32_t> intrinsicHalfedges;
    std::vector<int32_t> intrinsicEdges;
    std::vector<int32_t> intrinsicTriangles;
    std::vector<float> intrinsicEdgeLengths;
    std::vector<int32_t> inputHalfedges;
    std::vector<int32_t> inputEdges;
    std::vector<int32_t> inputTriangles;
    std::vector<float> inputEdgeLengths;
};

struct RemeshResultData {
    GeometryData geometry;
    IntrinsicMeshData intrinsic;
};
