#pragma once

#include <cstdint>
#include <type_traits>
#include <vector>

#include <glm/vec3.hpp>
#include <vulkan/vulkan.h>

#include "domain/GeometryData.hpp"
#include "mesh/remesher/SupportingHalfedge.hpp"
#include "nodegraph/NodeGraphCoreTypes.hpp"
#include "contact/ContactTypes.hpp"
#include "util/Structs.hpp"

namespace RuntimeProductHash {

inline uint64_t mix(uint64_t hash, uint64_t value) {
    hash ^= value + 0x9E3779B97F4A7C15ull + (hash << 6) + (hash >> 2);
    return hash;
}

inline uint64_t mixBytes(uint64_t hash, const void* data, size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (size_t index = 0; index < size; ++index) {
        hash = mix(hash, static_cast<uint64_t>(bytes[index]));
    }
    return hash;
}

template <typename T>
inline uint64_t mixPod(uint64_t hash, const T& value) {
    static_assert(std::is_trivially_copyable_v<T>, "mixPod requires trivially copyable type");
    return mixBytes(hash, &value, sizeof(T));
}

template <typename T>
inline uint64_t mixPodVector(uint64_t hash, const std::vector<T>& values) {
    static_assert(std::is_trivially_copyable_v<T>, "mixPodVector requires trivially copyable type");
    hash = mix(hash, static_cast<uint64_t>(values.size()));
    if (!values.empty()) {
        hash = mixBytes(hash, values.data(), sizeof(T) * values.size());
    }
    return hash;
}

} // namespace RuntimeProductHash

struct ModelProduct {
    uint32_t runtimeModelId = 0;
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceSize vertexBufferOffset = 0;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkDeviceSize indexBufferOffset = 0;
    uint32_t indexCount = 0;
    VkBuffer renderVertexBuffer = VK_NULL_HANDLE;
    VkDeviceSize renderVertexBufferOffset = 0;
    VkBuffer renderIndexBuffer = VK_NULL_HANDLE;
    VkDeviceSize renderIndexBufferOffset = 0;
    uint32_t renderIndexCount = 0;
    glm::mat4 modelMatrix{ 1.0f };
    uint64_t contentHash = 0;

    bool isValid() const {
        return runtimeModelId != 0 &&
            renderVertexBuffer != VK_NULL_HANDLE &&
            renderIndexBuffer != VK_NULL_HANDLE &&
            renderIndexCount != 0;
    }

    bool operator==(const ModelProduct& other) const {
        return runtimeModelId == other.runtimeModelId &&
            vertexBuffer == other.vertexBuffer &&
            vertexBufferOffset == other.vertexBufferOffset &&
            indexBuffer == other.indexBuffer &&
            indexBufferOffset == other.indexBufferOffset &&
            indexCount == other.indexCount &&
            renderVertexBuffer == other.renderVertexBuffer &&
            renderVertexBufferOffset == other.renderVertexBufferOffset &&
            renderIndexBuffer == other.renderIndexBuffer &&
            renderIndexBufferOffset == other.renderIndexBufferOffset &&
            renderIndexCount == other.renderIndexCount &&
            modelMatrix == other.modelMatrix &&
            contentHash == other.contentHash;
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
    uint32_t runtimeModelId = 0;
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
    uint64_t contentHash = 0;

    bool isValid() const {
        return runtimeModelId != 0 &&
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
        return runtimeModelId == other.runtimeModelId &&
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
            inputLengthView == other.inputLengthView &&
            contentHash == other.contentHash;
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
    uint64_t contentHash = 0;

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
            receiverProducts == other.receiverProducts &&
            contentHash == other.contentHash;
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
    uint64_t contentHash = 0;

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
            mappedContactPairs == other.mappedContactPairs &&
            contentHash == other.contentHash;
    }
};

inline uint64_t computeContentHash(const ModelProduct& product) {
    uint64_t hash = 1469598103934665603ull;
    hash = RuntimeProductHash::mixPod(hash, product.runtimeModelId);
    hash = RuntimeProductHash::mixPod(hash, product.indexCount);
    hash = RuntimeProductHash::mixPod(hash, product.renderIndexCount);
    hash = RuntimeProductHash::mixPod(hash, product.modelMatrix);
    return hash;
}

inline uint64_t computeContentHash(const RemeshProduct& product) {
    uint64_t hash = 1469598103934665603ull;
    hash = RuntimeProductHash::mixPod(hash, product.runtimeModelId);
    hash = RuntimeProductHash::mixPodVector(hash, product.geometryPositions);
    hash = RuntimeProductHash::mixPodVector(hash, product.geometryTriangleIndices);
    hash = RuntimeProductHash::mixPodVector(hash, product.intrinsicMesh.vertices);
    hash = RuntimeProductHash::mixPodVector(hash, product.intrinsicMesh.indices);
    hash = RuntimeProductHash::mixPodVector(hash, product.intrinsicMesh.faceIds);
    hash = RuntimeProductHash::mixPodVector(hash, product.intrinsicMesh.triangles);
    hash = RuntimeProductHash::mixPod(hash, product.intrinsicTriangleCount);
    hash = RuntimeProductHash::mixPod(hash, product.intrinsicVertexCount);
    hash = RuntimeProductHash::mixPod(hash, product.averageTriangleArea);
    return hash;
}

inline uint64_t computeContentHash(const VoronoiProduct& product) {
    uint64_t hash = 1469598103934665603ull;
    hash = RuntimeProductHash::mixPod(hash, product.nodeCount);
    if (product.nodeCount != 0 && product.mappedVoronoiNodes != nullptr) {
        hash = RuntimeProductHash::mixBytes(
            hash,
            product.mappedVoronoiNodes,
            sizeof(VoronoiNode) * product.nodeCount);
    }
    hash = RuntimeProductHash::mix(hash, static_cast<uint64_t>(product.receiverProducts.size()));
    for (const VoronoiReceiverProduct& receiverProduct : product.receiverProducts) {
        hash = RuntimeProductHash::mixPod(hash, receiverProduct.runtimeModelId);
        hash = RuntimeProductHash::mixPod(hash, receiverProduct.nodeOffset);
        hash = RuntimeProductHash::mixPod(hash, receiverProduct.nodeCount);
        hash = RuntimeProductHash::mixPodVector(hash, receiverProduct.surfaceCellIndices);
        hash = RuntimeProductHash::mixPodVector(hash, receiverProduct.seedFlags);
    }
    return hash;
}

inline uint64_t computeContentHash(const ContactProduct& product) {
    uint64_t hash = 1469598103934665603ull;
    hash = RuntimeProductHash::mixPod(hash, static_cast<uint32_t>(product.couplingType));
    hash = RuntimeProductHash::mixPod(hash, product.emitterRuntimeModelId);
    hash = RuntimeProductHash::mixPod(hash, product.receiverRuntimeModelId);
    hash = RuntimeProductHash::mixPodVector(hash, product.receiverTriangleIndices);
    hash = RuntimeProductHash::mixPod(hash, product.contactPairCount);
    if (product.contactPairCount != 0 && product.mappedContactPairs != nullptr) {
        hash = RuntimeProductHash::mixBytes(
            hash,
            product.mappedContactPairs,
            sizeof(ContactPair) * product.contactPairCount);
    }
    return hash;
}
