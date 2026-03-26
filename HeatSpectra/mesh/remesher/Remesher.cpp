#include "Remesher.hpp"

#include "iODT.hpp"
#include "nodegraph/NodeGraphDataTypes.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/VulkanDevice.hpp"

#include <iostream>

Remesher::Remesher(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator)
    : vulkanDevice(vulkanDevice),
      memoryAllocator(memoryAllocator) {
}

namespace {

IntrinsicMeshData toIntrinsicMeshData(const SupportingHalfedge::IntrinsicMesh& intrinsicMesh) {
    IntrinsicMeshData intrinsicData{};
    intrinsicData.vertices.reserve(intrinsicMesh.vertices.size());
    for (const SupportingHalfedge::IntrinsicVertex& vertex : intrinsicMesh.vertices) {
        IntrinsicMeshVertexData data{};
        data.intrinsicVertexId = vertex.intrinsicVertexId;
        data.position[0] = vertex.position.x;
        data.position[1] = vertex.position.y;
        data.position[2] = vertex.position.z;
        data.normal[0] = vertex.normal.x;
        data.normal[1] = vertex.normal.y;
        data.normal[2] = vertex.normal.z;
        data.inputLocationType = vertex.inputLocationType;
        data.inputElementId = vertex.inputElementId;
        data.inputBaryCoords[0] = vertex.inputBaryCoords.x;
        data.inputBaryCoords[1] = vertex.inputBaryCoords.y;
        data.inputBaryCoords[2] = vertex.inputBaryCoords.z;
        intrinsicData.vertices.push_back(data);
    }

    intrinsicData.triangleIndices = intrinsicMesh.indices;
    intrinsicData.faceIds = intrinsicMesh.faceIds;
    intrinsicData.triangles.reserve(intrinsicMesh.triangles.size());
    for (const SupportingHalfedge::IntrinsicTriangle& triangle : intrinsicMesh.triangles) {
        IntrinsicMeshTriangleData data{};
        data.center[0] = triangle.center.x;
        data.center[1] = triangle.center.y;
        data.center[2] = triangle.center.z;
        data.normal[0] = triangle.normal.x;
        data.normal[1] = triangle.normal.y;
        data.normal[2] = triangle.normal.z;
        data.area = triangle.area;
        data.vertexIndices[0] = triangle.vertexIndices[0];
        data.vertexIndices[1] = triangle.vertexIndices[1];
        data.vertexIndices[2] = triangle.vertexIndices[2];
        data.faceId = triangle.faceId;
        intrinsicData.triangles.push_back(data);
    }

    return intrinsicData;
}

void appendSupportingBuffers(const SupportingHalfedge& supportingHalfedge, IntrinsicMeshData& intrinsicData) {
    const SupportingHalfedge::GPUBuffers gpuBuffers = supportingHalfedge.buildGPUBuffers();
    intrinsicData.supportingHalfedges = gpuBuffers.S;
    intrinsicData.supportingAngles = gpuBuffers.A;
    intrinsicData.intrinsicHalfedges = gpuBuffers.H;
    intrinsicData.intrinsicEdges = gpuBuffers.E;
    intrinsicData.intrinsicTriangles = gpuBuffers.T;
    intrinsicData.intrinsicEdgeLengths = gpuBuffers.L;
    intrinsicData.inputHalfedges = gpuBuffers.H_input;
    intrinsicData.inputEdges = gpuBuffers.E_input;
    intrinsicData.inputTriangles = gpuBuffers.T_input;
    intrinsicData.inputEdgeLengths = gpuBuffers.L_input;
}

} // namespace

bool Remesher::remesh(
    const GeometryData& inputGeometry,
    int iterations,
    double minAngleDegrees,
    double maxEdgeLength,
    double stepSize,
    RemeshResultData& outResult) const {
    outResult = {};
    if (inputGeometry.pointPositions.empty() || inputGeometry.triangleIndices.empty()) {
        std::cerr << "[Remesher] Cannot remesh empty geometry payload" << std::endl;
        return false;
    }

    iODT remesher(inputGeometry, vulkanDevice, memoryAllocator);
    const bool success = remesher.optimalDelaunayTriangulation(
        iterations,
        minAngleDegrees,
        maxEdgeLength,
        stepSize);
    if (!success) {
        std::cerr << "[Remesher] Payload remeshing failed" << std::endl;
        return false;
    }

    const SupportingHalfedge* supportingHalfedge = remesher.getSupportingHalfedge();
    if (!supportingHalfedge) {
        std::cerr << "[Remesher] Missing supporting halfedge after remesh" << std::endl;
        return false;
    }

    const SupportingHalfedge::IntrinsicMesh intrinsicMesh = supportingHalfedge->buildIntrinsicMesh();
    if (intrinsicMesh.vertices.empty() || intrinsicMesh.indices.empty()) {
        std::cerr << "[Remesher] Intrinsic mesh output was empty" << std::endl;
        return false;
    }

    outResult.intrinsic = toIntrinsicMeshData(intrinsicMesh);
    appendSupportingBuffers(*supportingHalfedge, outResult.intrinsic);
    outResult.geometry = inputGeometry;
    remesher.cleanup();
    return true;
}
