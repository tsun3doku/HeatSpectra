#pragma once

#include <cstdint>
#include <vector>

#include <glm/vec3.hpp>
#include <vulkan/vulkan.h>

#include "domain/GeometryData.hpp"
#include "mesh/remesher/SupportingHalfedge.hpp"
#include "nodegraph/NodeGraphCoreTypes.hpp"
#include "contact/ContactTypes.hpp"
#include "util/Structs.hpp"

struct ModelProduct {
    uint32_t runtimeModelId = 0;
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceSize vertexBufferOffset = 0;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkDeviceSize indexBufferOffset = 0;
    uint32_t indexCount = 0;
    glm::mat4 modelMatrix{ 1.0f };

    bool isValid() const {
        return runtimeModelId != 0 &&
            vertexBuffer != VK_NULL_HANDLE &&
            indexBuffer != VK_NULL_HANDLE &&
            indexCount != 0;
    }

    bool operator==(const ModelProduct& other) const {
        return runtimeModelId == other.runtimeModelId &&
            vertexBuffer == other.vertexBuffer &&
            vertexBufferOffset == other.vertexBufferOffset &&
            indexBuffer == other.indexBuffer &&
            indexBufferOffset == other.indexBufferOffset &&
            indexCount == other.indexCount &&
            modelMatrix == other.modelMatrix;
    }
};

struct VoronoiReceiverProduct {
    uint32_t runtimeModelId = 0;
    uint32_t nodeOffset = 0;
    uint32_t nodeCount = 0;
    VkBuffer surfaceMappingBuffer = VK_NULL_HANDLE;
    VkDeviceSize surfaceMappingBufferOffset = 0;
    std::vector<uint32_t> surfaceCellIndices;
    std::vector<uint32_t> seedFlags;

    bool operator==(const VoronoiReceiverProduct& other) const {
        return runtimeModelId == other.runtimeModelId &&
            nodeOffset == other.nodeOffset &&
            nodeCount == other.nodeCount &&
            surfaceMappingBuffer == other.surfaceMappingBuffer &&
            surfaceMappingBufferOffset == other.surfaceMappingBufferOffset &&
            surfaceCellIndices == other.surfaceCellIndices &&
            seedFlags == other.seedFlags;
    }
};

struct RemeshProduct {
    NodeDataHandle remeshHandle{};
    uint32_t runtimeModelId = 0;
    GeometryData geometry;
    std::vector<glm::vec3> geometryPositions;
    std::vector<uint32_t> geometryTriangleIndices;
    SupportingHalfedge::IntrinsicMesh intrinsicMesh;
    VkBuffer intrinsicTriangleBuffer = VK_NULL_HANDLE;
    VkDeviceSize intrinsicTriangleBufferOffset = 0;
    VkBuffer intrinsicVertexBuffer = VK_NULL_HANDLE;
    VkDeviceSize intrinsicVertexBufferOffset = 0;
    size_t intrinsicTriangleCount = 0;
    size_t intrinsicVertexCount = 0;
    float averageTriangleArea = 0.0f;
    VkBufferView supportingHalfedgeView = VK_NULL_HANDLE;
    VkBufferView supportingAngleView = VK_NULL_HANDLE;
    VkBufferView halfedgeView = VK_NULL_HANDLE;
    VkBufferView edgeView = VK_NULL_HANDLE;
    VkBufferView triangleView = VK_NULL_HANDLE;
    VkBufferView lengthView = VK_NULL_HANDLE;
    VkBufferView inputHalfedgeView = VK_NULL_HANDLE;
    VkBufferView inputEdgeView = VK_NULL_HANDLE;
    VkBufferView inputTriangleView = VK_NULL_HANDLE;
    VkBufferView inputLengthView = VK_NULL_HANDLE;

    bool isValid() const {
        return remeshHandle.key != 0 &&
            runtimeModelId != 0 &&
            geometry.modelId != 0 &&
            !geometryPositions.empty() &&
            !geometryTriangleIndices.empty() &&
            !intrinsicMesh.vertices.empty() &&
            intrinsicTriangleBuffer != VK_NULL_HANDLE &&
            intrinsicVertexBuffer != VK_NULL_HANDLE &&
            supportingHalfedgeView != VK_NULL_HANDLE &&
            supportingAngleView != VK_NULL_HANDLE &&
            halfedgeView != VK_NULL_HANDLE &&
            edgeView != VK_NULL_HANDLE &&
            triangleView != VK_NULL_HANDLE &&
            lengthView != VK_NULL_HANDLE &&
            inputHalfedgeView != VK_NULL_HANDLE &&
            inputEdgeView != VK_NULL_HANDLE &&
            inputTriangleView != VK_NULL_HANDLE &&
            inputLengthView != VK_NULL_HANDLE;
    }

    bool operator==(const RemeshProduct& other) const {
        return remeshHandle == other.remeshHandle &&
            runtimeModelId == other.runtimeModelId &&
            geometry.payloadHash == other.geometry.payloadHash &&
            geometryPositions.size() == other.geometryPositions.size() &&
            geometryTriangleIndices == other.geometryTriangleIndices &&
            intrinsicMesh.vertices.size() == other.intrinsicMesh.vertices.size() &&
            intrinsicMesh.indices == other.intrinsicMesh.indices &&
            intrinsicMesh.faceIds == other.intrinsicMesh.faceIds &&
            intrinsicMesh.triangles.size() == other.intrinsicMesh.triangles.size() &&
            intrinsicTriangleBuffer == other.intrinsicTriangleBuffer &&
            intrinsicTriangleBufferOffset == other.intrinsicTriangleBufferOffset &&
            intrinsicVertexBuffer == other.intrinsicVertexBuffer &&
            intrinsicVertexBufferOffset == other.intrinsicVertexBufferOffset &&
            intrinsicTriangleCount == other.intrinsicTriangleCount &&
            intrinsicVertexCount == other.intrinsicVertexCount &&
            averageTriangleArea == other.averageTriangleArea &&
            supportingHalfedgeView == other.supportingHalfedgeView &&
            supportingAngleView == other.supportingAngleView &&
            halfedgeView == other.halfedgeView &&
            edgeView == other.edgeView &&
            triangleView == other.triangleView &&
            lengthView == other.lengthView &&
            inputHalfedgeView == other.inputHalfedgeView &&
            inputEdgeView == other.inputEdgeView &&
            inputTriangleView == other.inputTriangleView &&
            inputLengthView == other.inputLengthView;
    }
};

struct VoronoiProduct {
    uint32_t nodeCount = 0;
    const VoronoiNode* mappedVoronoiNodes = nullptr;

    VkBuffer nodeBuffer = VK_NULL_HANDLE;
    VkDeviceSize nodeBufferOffset = 0;

    VkBuffer voronoiNeighborBuffer = VK_NULL_HANDLE;
    VkDeviceSize voronoiNeighborBufferOffset = 0;

    VkBuffer neighborIndicesBuffer = VK_NULL_HANDLE;
    VkDeviceSize neighborIndicesBufferOffset = 0;

    VkBuffer interfaceAreasBuffer = VK_NULL_HANDLE;
    VkDeviceSize interfaceAreasBufferOffset = 0;

    VkBuffer interfaceNeighborIdsBuffer = VK_NULL_HANDLE;
    VkDeviceSize interfaceNeighborIdsBufferOffset = 0;

    VkBuffer seedFlagsBuffer = VK_NULL_HANDLE;
    VkDeviceSize seedFlagsBufferOffset = 0;

    std::vector<VoronoiReceiverProduct> receiverProducts;

    bool isValid() const {
        return nodeCount != 0 &&
            mappedVoronoiNodes != nullptr &&
            nodeBuffer != VK_NULL_HANDLE &&
            voronoiNeighborBuffer != VK_NULL_HANDLE &&
            seedFlagsBuffer != VK_NULL_HANDLE;
    }

    bool operator==(const VoronoiProduct& other) const {
        return nodeCount == other.nodeCount &&
            mappedVoronoiNodes == other.mappedVoronoiNodes &&
            nodeBuffer == other.nodeBuffer &&
            nodeBufferOffset == other.nodeBufferOffset &&
            voronoiNeighborBuffer == other.voronoiNeighborBuffer &&
            voronoiNeighborBufferOffset == other.voronoiNeighborBufferOffset &&
            neighborIndicesBuffer == other.neighborIndicesBuffer &&
            neighborIndicesBufferOffset == other.neighborIndicesBufferOffset &&
            interfaceAreasBuffer == other.interfaceAreasBuffer &&
            interfaceAreasBufferOffset == other.interfaceAreasBufferOffset &&
            interfaceNeighborIdsBuffer == other.interfaceNeighborIdsBuffer &&
            interfaceNeighborIdsBufferOffset == other.interfaceNeighborIdsBufferOffset &&
            seedFlagsBuffer == other.seedFlagsBuffer &&
            seedFlagsBufferOffset == other.seedFlagsBufferOffset &&
            receiverProducts == other.receiverProducts;
    }
};

struct ContactProduct {
    ContactCouplingType couplingType = ContactCouplingType::SourceToReceiver;
    uint32_t emitterRuntimeModelId = 0;
    uint32_t receiverRuntimeModelId = 0;
    std::vector<uint32_t> receiverTriangleIndices;

    VkBuffer contactPairBuffer = VK_NULL_HANDLE;
    VkDeviceSize contactPairBufferOffset = 0;
    uint32_t contactPairCount = 0;
    const ContactPair* mappedContactPairs = nullptr;

    bool isValid() const {
        return emitterRuntimeModelId != 0 &&
            receiverRuntimeModelId != 0 &&
            !receiverTriangleIndices.empty() &&
            contactPairBuffer != VK_NULL_HANDLE &&
            contactPairCount != 0 &&
            mappedContactPairs != nullptr;
    }

    bool operator==(const ContactProduct& other) const {
        return couplingType == other.couplingType &&
            emitterRuntimeModelId == other.emitterRuntimeModelId &&
            receiverRuntimeModelId == other.receiverRuntimeModelId &&
            receiverTriangleIndices == other.receiverTriangleIndices &&
            contactPairBuffer == other.contactPairBuffer &&
            contactPairBufferOffset == other.contactPairBufferOffset &&
            contactPairCount == other.contactPairCount &&
            mappedContactPairs == other.mappedContactPairs;
    }
};
