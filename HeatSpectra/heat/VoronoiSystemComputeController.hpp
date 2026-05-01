#pragma once

#include "HeatSystemPresets.hpp"
#include "heat/VoronoiSystem.hpp"
#include "mesh/remesher/SupportingHalfedge.hpp"
#include "runtime/RuntimeProducts.hpp"
#include "voronoi/VoronoiModelRuntime.hpp"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include <glm/mat4x4.hpp>
#include <vulkan/vulkan.h>

class MemoryAllocator;
class ModelRegistry;
class VulkanDevice;
class CommandPool;
struct VoronoiProduct;

class VoronoiSystemComputeController {
public:
    struct Config {
        bool active = false;
        float cellSize = 0.005f;
        int voxelResolution = 128;
        std::vector<uint32_t> receiverNodeModelIds;
        std::vector<std::vector<glm::vec3>> receiverGeometryPositions;
        std::vector<std::vector<uint32_t>> receiverGeometryTriangleIndices;
        std::vector<SupportingHalfedge::IntrinsicMesh> receiverIntrinsicMeshes;
        std::vector<std::vector<VoronoiModelRuntime::SurfaceVertex>> receiverSurfaceVertices;
        std::vector<std::vector<uint32_t>> receiverIntrinsicTriangleIndices;
        std::vector<uint32_t> receiverRuntimeModelIds;
        std::vector<VkBuffer> meshVertexBuffers;
        std::vector<VkDeviceSize> meshVertexBufferOffsets;
        std::vector<VkBuffer> meshIndexBuffers;
        std::vector<VkDeviceSize> meshIndexBufferOffsets;
        std::vector<uint32_t> meshIndexCounts;
        std::vector<glm::mat4> meshModelMatrices;
        std::vector<VkBufferView> supportingHalfedgeViews;
        std::vector<VkBufferView> supportingAngleViews;
        std::vector<VkBufferView> halfedgeViews;
        std::vector<VkBufferView> edgeViews;
        std::vector<VkBufferView> triangleViews;
        std::vector<VkBufferView> lengthViews;
        std::vector<VkBufferView> inputHalfedgeViews;
        std::vector<VkBufferView> inputEdgeViews;
        std::vector<VkBufferView> inputTriangleViews;
        std::vector<VkBufferView> inputLengthViews;
        uint64_t computeHash = 0;
    };

    VoronoiSystemComputeController(
        VulkanDevice& vulkanDevice, 
        MemoryAllocator& memoryAllocator,
        ModelRegistry& resourceManager,
        CommandPool& renderCommandPool,
        uint32_t maxFramesInFlight);

    void configure(uint64_t socketKey, const Config& config);
    void disable(uint64_t socketKey);
    void disableAll();
    bool exportProduct(uint64_t socketKey, VoronoiProduct& outProduct) const;
    std::vector<VoronoiSystem*> getActiveSystems() const;

private:
    std::unique_ptr<VoronoiSystem> buildVoronoiSystem();

    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    ModelRegistry& resourceManager;
    CommandPool& renderCommandPool;
    std::unordered_map<uint64_t, std::unique_ptr<VoronoiSystem>> activeSystems;
    std::unordered_map<uint64_t, Config> configuredConfigs;
    uint32_t maxFramesInFlight = 0;
};

inline uint64_t buildComputeHash(const VoronoiSystemComputeController::Config& config) {
    uint64_t hash = 1469598103934665603ull;
    hash = RuntimeProductHash::mixPod(hash, static_cast<uint64_t>(config.active ? 1u : 0u));
    hash = RuntimeProductHash::mixPod(hash, config.cellSize);
    hash = RuntimeProductHash::mixPod(hash, config.voxelResolution);
    hash = RuntimeProductHash::mixPodVector(hash, config.receiverNodeModelIds);
    hash = RuntimeProductHash::mix(hash, static_cast<uint64_t>(config.receiverGeometryPositions.size()));
    for (const auto& positions : config.receiverGeometryPositions) {
        hash = RuntimeProductHash::mixPodVector(hash, positions);
    }
    hash = RuntimeProductHash::mix(hash, static_cast<uint64_t>(config.receiverGeometryTriangleIndices.size()));
    for (const auto& indices : config.receiverGeometryTriangleIndices) {
        hash = RuntimeProductHash::mixPodVector(hash, indices);
    }
    hash = RuntimeProductHash::mix(hash, static_cast<uint64_t>(config.receiverIntrinsicMeshes.size()));
    for (const SupportingHalfedge::IntrinsicMesh& mesh : config.receiverIntrinsicMeshes) {
        hash = RuntimeProductHash::mixPodVector(hash, mesh.vertices);
        hash = RuntimeProductHash::mixPodVector(hash, mesh.indices);
        hash = RuntimeProductHash::mixPodVector(hash, mesh.faceIds);
        hash = RuntimeProductHash::mixPodVector(hash, mesh.triangles);
    }
    hash = RuntimeProductHash::mix(hash, static_cast<uint64_t>(config.receiverSurfaceVertices.size()));
    for (const auto& vertices : config.receiverSurfaceVertices) {
        hash = RuntimeProductHash::mixPodVector(hash, vertices);
    }
    hash = RuntimeProductHash::mix(hash, static_cast<uint64_t>(config.receiverIntrinsicTriangleIndices.size()));
    for (const auto& indices : config.receiverIntrinsicTriangleIndices) {
        hash = RuntimeProductHash::mixPodVector(hash, indices);
    }
    hash = RuntimeProductHash::mixPodVector(hash, config.receiverRuntimeModelIds);
    hash = RuntimeProductHash::mixPodVector(hash, config.meshVertexBuffers);
    hash = RuntimeProductHash::mixPodVector(hash, config.meshVertexBufferOffsets);
    hash = RuntimeProductHash::mixPodVector(hash, config.meshIndexBuffers);
    hash = RuntimeProductHash::mixPodVector(hash, config.meshIndexBufferOffsets);
    hash = RuntimeProductHash::mixPodVector(hash, config.meshIndexCounts);
    hash = RuntimeProductHash::mixPodVector(hash, config.meshModelMatrices);
    hash = RuntimeProductHash::mixPodVector(hash, config.supportingHalfedgeViews);
    hash = RuntimeProductHash::mixPodVector(hash, config.supportingAngleViews);
    hash = RuntimeProductHash::mixPodVector(hash, config.halfedgeViews);
    hash = RuntimeProductHash::mixPodVector(hash, config.edgeViews);
    hash = RuntimeProductHash::mixPodVector(hash, config.triangleViews);
    hash = RuntimeProductHash::mixPodVector(hash, config.lengthViews);
    hash = RuntimeProductHash::mixPodVector(hash, config.inputHalfedgeViews);
    hash = RuntimeProductHash::mixPodVector(hash, config.inputEdgeViews);
    hash = RuntimeProductHash::mixPodVector(hash, config.inputTriangleViews);
    hash = RuntimeProductHash::mixPodVector(hash, config.inputLengthViews);
    return hash;
}
