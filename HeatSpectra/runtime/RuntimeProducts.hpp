#pragma once

#include <array>
#include <cstdint>
#include <type_traits>
#include <vector>

#include <glm/mat4x4.hpp>
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
    uint64_t productHash = 0;

    bool isValid() const {
        return runtimeModelId != 0 &&
            vertexBuffer != VK_NULL_HANDLE &&
            indexBuffer != VK_NULL_HANDLE &&
            indexCount != 0 &&
            renderVertexBuffer != VK_NULL_HANDLE &&
            renderIndexBuffer != VK_NULL_HANDLE &&
            renderIndexCount != 0;
    }

};


struct VoronoiSurfaceProduct {
    uint32_t runtimeModelId = 0;
    uint32_t nodeOffset = 0;
    uint32_t nodeCount = 0;
    VkBuffer surfaceMappingBuffer = VK_NULL_HANDLE;
    VkDeviceSize surfaceMappingBufferOffset = 0;
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceSize vertexBufferOffset = 0;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkDeviceSize indexBufferOffset = 0;
    uint32_t indexCount = 0;
    glm::mat4 modelMatrix{ 1.0f };
    uint32_t intrinsicVertexCount = 0;
    VkBuffer candidateBuffer = VK_NULL_HANDLE;
    VkDeviceSize candidateBufferOffset = 0;
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
    std::vector<uint32_t> surfaceCellIndices;
    std::vector<uint32_t> seedFlags;

    bool isValid() const {
        return runtimeModelId != 0 &&
            nodeCount != 0 &&
            surfaceMappingBuffer != VK_NULL_HANDLE &&
            vertexBuffer != VK_NULL_HANDLE &&
            indexBuffer != VK_NULL_HANDLE &&
            indexCount != 0 &&
            intrinsicVertexCount != 0 &&
            candidateBuffer != VK_NULL_HANDLE &&
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
    uint64_t productHash = 0;

    bool isValid() const {
        return !geometryPositions.empty() &&
            !geometryTriangleIndices.empty() &&
            !intrinsicMesh.vertices.empty() &&
            !intrinsicMesh.indices.empty() &&
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
    VkBuffer seedPositionBuffer = VK_NULL_HANDLE;
    VkDeviceSize seedPositionBufferOffset = 0;
    VkBuffer occupancyPointBuffer = VK_NULL_HANDLE;
    VkDeviceSize occupancyPointBufferOffset = 0;
    uint32_t occupancyPointCount = 0;

    std::vector<VoronoiSurfaceProduct> surfaces;
    uint64_t productHash = 0;

    bool isValid() const {
        return nodeCount != 0 &&
            mappedVoronoiNodes != nullptr &&
            nodeBuffer != VK_NULL_HANDLE &&
            neighborIndicesBuffer != VK_NULL_HANDLE &&
            seedPositionBuffer != VK_NULL_HANDLE;
    }

};

struct ContactProduct {
    // Compute output 
    ContactCoupling coupling{};
    VkBuffer contactPairBuffer = VK_NULL_HANDLE;
    VkDeviceSize contactPairBufferOffset = 0;

    uint32_t emitterRuntimeModelId = 0;
    uint32_t receiverRuntimeModelId = 0;
    std::vector<ContactLineVertex> outlineVertices;
    std::vector<ContactLineVertex> correspondenceVertices;

    uint64_t productHash = 0;

    bool isValid() const {
        return coupling.isValid() &&
            contactPairBuffer != VK_NULL_HANDLE;
    }

};

struct HeatProduct {
    bool active = false;
    bool paused = false;
    std::vector<uint32_t> sourceRuntimeModelIds;
    std::vector<uint32_t> receiverRuntimeModelIds;
    std::vector<VkBufferView> receiverSurfaceBufferViews;
    uint64_t productHash = 0;

    bool isValid() const {
        return !receiverRuntimeModelIds.empty() &&
            receiverRuntimeModelIds.size() == receiverSurfaceBufferViews.size();
    }

};

inline uint64_t buildProductHash(const ModelProduct& product) {
    uint64_t hash = 1469598103934665603ull;
    hash = RuntimeProductHash::mixPod(hash, product.runtimeModelId);
    hash = RuntimeProductHash::mixPod(hash, product.vertexBuffer);
    hash = RuntimeProductHash::mixPod(hash, product.vertexBufferOffset);
    hash = RuntimeProductHash::mixPod(hash, product.indexBuffer);
    hash = RuntimeProductHash::mixPod(hash, product.indexBufferOffset);
    hash = RuntimeProductHash::mixPod(hash, product.indexCount);
    hash = RuntimeProductHash::mixPod(hash, product.renderVertexBuffer);
    hash = RuntimeProductHash::mixPod(hash, product.renderVertexBufferOffset);
    hash = RuntimeProductHash::mixPod(hash, product.renderIndexBuffer);
    hash = RuntimeProductHash::mixPod(hash, product.renderIndexBufferOffset);
    hash = RuntimeProductHash::mixPod(hash, product.renderIndexCount);
    hash = RuntimeProductHash::mixPod(hash, product.modelMatrix);
    return hash;
}

inline uint64_t buildProductHash(const RemeshProduct& product) {
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

inline uint64_t buildProductHash(const VoronoiProduct& product) {
    uint64_t hash = 1469598103934665603ull;
    hash = RuntimeProductHash::mixPod(hash, product.nodeCount);
    if (product.nodeCount != 0 && product.mappedVoronoiNodes != nullptr) {
        hash = RuntimeProductHash::mixBytes(
            hash,
            product.mappedVoronoiNodes,
            sizeof(VoronoiNode) * product.nodeCount);
    }

    hash = RuntimeProductHash::mix(hash, static_cast<uint64_t>(product.surfaces.size()));
    for (const VoronoiSurfaceProduct& surfaceProduct : product.surfaces) {
        hash = RuntimeProductHash::mixPod(hash, surfaceProduct.runtimeModelId);
        hash = RuntimeProductHash::mixPod(hash, surfaceProduct.nodeOffset);
        hash = RuntimeProductHash::mixPod(hash, surfaceProduct.nodeCount);
        hash = RuntimeProductHash::mixPod(hash, surfaceProduct.surfaceMappingBuffer);
        hash = RuntimeProductHash::mixPod(hash, surfaceProduct.surfaceMappingBufferOffset);
        hash = RuntimeProductHash::mixPod(hash, surfaceProduct.vertexBuffer);
        hash = RuntimeProductHash::mixPod(hash, surfaceProduct.vertexBufferOffset);
        hash = RuntimeProductHash::mixPod(hash, surfaceProduct.indexBuffer);
        hash = RuntimeProductHash::mixPod(hash, surfaceProduct.indexBufferOffset);
        hash = RuntimeProductHash::mixPod(hash, surfaceProduct.indexCount);
        hash = RuntimeProductHash::mixPod(hash, surfaceProduct.modelMatrix);
        hash = RuntimeProductHash::mixPod(hash, surfaceProduct.intrinsicVertexCount);
        hash = RuntimeProductHash::mixPod(hash, surfaceProduct.candidateBuffer);
        hash = RuntimeProductHash::mixPod(hash, surfaceProduct.candidateBufferOffset);
        hash = RuntimeProductHash::mixPod(hash, surfaceProduct.supportingHalfedgeView);
        hash = RuntimeProductHash::mixPod(hash, surfaceProduct.supportingAngleView);
        hash = RuntimeProductHash::mixPod(hash, surfaceProduct.halfedgeView);
        hash = RuntimeProductHash::mixPod(hash, surfaceProduct.edgeView);
        hash = RuntimeProductHash::mixPod(hash, surfaceProduct.triangleView);
        hash = RuntimeProductHash::mixPod(hash, surfaceProduct.lengthView);
        hash = RuntimeProductHash::mixPod(hash, surfaceProduct.inputHalfedgeView);
        hash = RuntimeProductHash::mixPod(hash, surfaceProduct.inputEdgeView);
        hash = RuntimeProductHash::mixPod(hash, surfaceProduct.inputTriangleView);
        hash = RuntimeProductHash::mixPod(hash, surfaceProduct.inputLengthView);
        hash = RuntimeProductHash::mixPodVector(hash, surfaceProduct.surfaceCellIndices);
        hash = RuntimeProductHash::mixPodVector(hash, surfaceProduct.seedFlags);
    }
    hash = RuntimeProductHash::mixPod(hash, product.seedPositionBuffer);
    hash = RuntimeProductHash::mixPod(hash, product.seedPositionBufferOffset);
    hash = RuntimeProductHash::mixPod(hash, product.occupancyPointBuffer);
    hash = RuntimeProductHash::mixPod(hash, product.occupancyPointBufferOffset);
    hash = RuntimeProductHash::mixPod(hash, product.occupancyPointCount);
    return hash;
}

inline uint64_t buildProductHash(const ContactProduct& product) {
    uint64_t hash = 1469598103934665603ull;
    hash = RuntimeProductHash::mixPod(hash, static_cast<uint32_t>(product.coupling.couplingType));
    hash = RuntimeProductHash::mixPod(hash, product.coupling.emitterRuntimeModelId);
    hash = RuntimeProductHash::mixPod(hash, product.coupling.receiverRuntimeModelId);
    hash = RuntimeProductHash::mixPodVector(hash, product.coupling.receiverTriangleIndices);
    hash = RuntimeProductHash::mixPod(hash, product.coupling.contactPairCount);
    if (product.coupling.contactPairCount != 0 && product.coupling.mappedContactPairs != nullptr) {
        hash = RuntimeProductHash::mixBytes(
            hash,
            product.coupling.mappedContactPairs,
            sizeof(ContactPair) * product.coupling.contactPairCount);
    }
    hash = RuntimeProductHash::mixPod(hash, product.emitterRuntimeModelId);
    hash = RuntimeProductHash::mixPod(hash, product.receiverRuntimeModelId);
    hash = RuntimeProductHash::mixPodVector(hash, product.outlineVertices);
    hash = RuntimeProductHash::mixPodVector(hash, product.correspondenceVertices);
    return hash;
}

inline uint64_t buildProductHash(const HeatProduct& product) {
    uint64_t hash = 1469598103934665603ull;
    hash = RuntimeProductHash::mixPod(hash, static_cast<uint64_t>(product.active ? 1u : 0u));
    hash = RuntimeProductHash::mixPod(hash, static_cast<uint64_t>(product.paused ? 1u : 0u));
    hash = RuntimeProductHash::mixPodVector(hash, product.sourceRuntimeModelIds);
    hash = RuntimeProductHash::mixPodVector(hash, product.receiverRuntimeModelIds);
    hash = RuntimeProductHash::mixPodVector(hash, product.receiverSurfaceBufferViews);
    return hash;
}
