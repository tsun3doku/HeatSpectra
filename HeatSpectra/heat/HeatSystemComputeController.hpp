#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "heat/HeatSystem.hpp"
#include "HeatSystemPresets.hpp"
#include "HeatSystemResources.hpp"
#include "framegraph/ComputePass.hpp"
#include "mesh/remesher/SupportingHalfedge.hpp"
#include "runtime/RuntimeProducts.hpp"
#include "runtime/RuntimeThermalTypes.hpp"

class VulkanDevice;
class MemoryAllocator;
class ModelRegistry;
class CommandPool;

class HeatSystemComputeController {
public:
    struct Config {
        bool active = false;
        bool paused = false;
        bool resetRequested = false;
        std::vector<SupportingHalfedge::IntrinsicMesh> sourceIntrinsicMeshes;
        std::vector<uint32_t> sourceRuntimeModelIds;
        std::vector<SupportingHalfedge::IntrinsicMesh> receiverIntrinsicMeshes;
        std::vector<uint32_t> receiverRuntimeModelIds;
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
        std::vector<RuntimeThermalMaterial> runtimeThermalMaterials;
        std::unordered_map<uint32_t, float> sourceTemperatureByRuntimeId;
        uint32_t voronoiNodeCount = 0;
        const VoronoiNode* voronoiNodes = nullptr;
        VkBuffer voronoiNodeBuffer = VK_NULL_HANDLE;
        VkDeviceSize voronoiNodeBufferOffset = 0;
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
        std::unordered_map<uint32_t, uint32_t> receiverVoronoiNodeOffsetByModelId;
        std::unordered_map<uint32_t, uint32_t> receiverVoronoiNodeCountByModelId;
        std::unordered_map<uint32_t, VkBuffer> receiverVoronoiSurfaceMappingBufferByModelId;
        std::unordered_map<uint32_t, VkDeviceSize> receiverVoronoiSurfaceMappingBufferOffsetByModelId;
        std::unordered_map<uint32_t, std::vector<uint32_t>> receiverVoronoiSurfaceCellIndicesByModelId;
        std::unordered_map<uint32_t, std::vector<uint32_t>> receiverVoronoiSeedFlagsByModelId;
        std::vector<ContactCoupling> contactCouplings;
        uint64_t computeHash = 0;
    };

    HeatSystemComputeController(
        VulkanDevice& vulkanDevice,
        MemoryAllocator& memoryAllocator,
        ModelRegistry& resourceManager,
        CommandPool& renderCommandPool,
        uint32_t maxFramesInFlight);

    bool isAnyHeatSystemActive() const;
    bool isAnyHeatSystemPaused() const;
    bool isHeatSystemActive(uint64_t socketKey) const;
    bool isHeatSystemPaused(uint64_t socketKey) const;

    void configure(uint64_t socketKey, const Config& config);
    void disable(uint64_t socketKey);
    void disableAll();
    std::vector<ComputePass*> getActiveSystems() const;
    bool exportProduct(uint64_t socketKey, HeatProduct& outProduct) const;

    void destroyHeatSystem(uint64_t socketKey);

private:
    struct SystemInstance {
        HeatSystemResources resources;
        std::unique_ptr<HeatSystem> system;
    };

    std::unique_ptr<HeatSystem> buildHeatSystem(HeatSystemResources& heatSystemResources);
    void configureHeatSystem(HeatSystem& system, const Config& config);

    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    ModelRegistry& resourceManager;
    CommandPool& renderCommandPool;

    std::unordered_map<uint64_t, SystemInstance> activeSystems;
    std::unordered_map<uint64_t, Config> configuredConfigs;
    const uint32_t maxFramesInFlight;
};

inline uint64_t buildComputeHash(const HeatSystemComputeController::Config& config) {
    uint64_t hash = 1469598103934665603ull;
    hash = RuntimeProductHash::mix(hash, static_cast<uint64_t>(config.sourceIntrinsicMeshes.size()));
    for (const SupportingHalfedge::IntrinsicMesh& mesh : config.sourceIntrinsicMeshes) {
        hash = RuntimeProductHash::mixPodVector(hash, mesh.vertices);
        hash = RuntimeProductHash::mixPodVector(hash, mesh.indices);
        hash = RuntimeProductHash::mixPodVector(hash, mesh.faceIds);
        hash = RuntimeProductHash::mixPodVector(hash, mesh.triangles);
    }
    hash = RuntimeProductHash::mixPodVector(hash, config.sourceRuntimeModelIds);
    hash = RuntimeProductHash::mix(hash, static_cast<uint64_t>(config.receiverIntrinsicMeshes.size()));
    for (const SupportingHalfedge::IntrinsicMesh& mesh : config.receiverIntrinsicMeshes) {
        hash = RuntimeProductHash::mixPodVector(hash, mesh.vertices);
        hash = RuntimeProductHash::mixPodVector(hash, mesh.indices);
        hash = RuntimeProductHash::mixPodVector(hash, mesh.faceIds);
        hash = RuntimeProductHash::mixPodVector(hash, mesh.triangles);
    }
    hash = RuntimeProductHash::mixPodVector(hash, config.receiverRuntimeModelIds);
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
    hash = RuntimeProductHash::mixPodVector(hash, config.runtimeThermalMaterials);
    hash = RuntimeProductHash::mix(hash, static_cast<uint64_t>(config.sourceTemperatureByRuntimeId.size()));
    for (const auto& [id, temp] : config.sourceTemperatureByRuntimeId) {
        hash = RuntimeProductHash::mixPod(hash, id);
        hash = RuntimeProductHash::mixPod(hash, temp);
    }
    hash = RuntimeProductHash::mixPod(hash, config.voronoiNodeCount);
    if (config.voronoiNodeCount != 0 && config.voronoiNodes != nullptr) {
        hash = RuntimeProductHash::mixBytes(
            hash,
            config.voronoiNodes,
            sizeof(VoronoiNode) * config.voronoiNodeCount);
    }
    hash = RuntimeProductHash::mixPod(hash, config.voronoiNodeBuffer);
    hash = RuntimeProductHash::mixPod(hash, config.voronoiNodeBufferOffset);
    hash = RuntimeProductHash::mixPod(hash, config.voronoiNeighborBuffer);
    hash = RuntimeProductHash::mixPod(hash, config.voronoiNeighborBufferOffset);
    hash = RuntimeProductHash::mixPod(hash, config.neighborIndicesBuffer);
    hash = RuntimeProductHash::mixPod(hash, config.neighborIndicesBufferOffset);
    hash = RuntimeProductHash::mixPod(hash, config.interfaceAreasBuffer);
    hash = RuntimeProductHash::mixPod(hash, config.interfaceAreasBufferOffset);
    hash = RuntimeProductHash::mixPod(hash, config.interfaceNeighborIdsBuffer);
    hash = RuntimeProductHash::mixPod(hash, config.interfaceNeighborIdsBufferOffset);
    hash = RuntimeProductHash::mixPod(hash, config.seedFlagsBuffer);
    hash = RuntimeProductHash::mixPod(hash, config.seedFlagsBufferOffset);
    hash = RuntimeProductHash::mix(hash, static_cast<uint64_t>(config.receiverVoronoiNodeOffsetByModelId.size()));
    for (const auto& [id, offset] : config.receiverVoronoiNodeOffsetByModelId) {
        hash = RuntimeProductHash::mixPod(hash, id);
        hash = RuntimeProductHash::mixPod(hash, offset);
    }
    hash = RuntimeProductHash::mix(hash, static_cast<uint64_t>(config.receiverVoronoiNodeCountByModelId.size()));
    for (const auto& [id, count] : config.receiverVoronoiNodeCountByModelId) {
        hash = RuntimeProductHash::mixPod(hash, id);
        hash = RuntimeProductHash::mixPod(hash, count);
    }
    hash = RuntimeProductHash::mix(hash, static_cast<uint64_t>(config.receiverVoronoiSurfaceMappingBufferByModelId.size()));
    for (const auto& [id, buffer] : config.receiverVoronoiSurfaceMappingBufferByModelId) {
        hash = RuntimeProductHash::mixPod(hash, id);
        hash = RuntimeProductHash::mixPod(hash, buffer);
    }
    hash = RuntimeProductHash::mix(hash, static_cast<uint64_t>(config.receiverVoronoiSurfaceMappingBufferOffsetByModelId.size()));
    for (const auto& [id, offset] : config.receiverVoronoiSurfaceMappingBufferOffsetByModelId) {
        hash = RuntimeProductHash::mixPod(hash, id);
        hash = RuntimeProductHash::mixPod(hash, offset);
    }
    hash = RuntimeProductHash::mix(hash, static_cast<uint64_t>(config.receiverVoronoiSurfaceCellIndicesByModelId.size()));
    for (const auto& [id, indices] : config.receiverVoronoiSurfaceCellIndicesByModelId) {
        hash = RuntimeProductHash::mixPod(hash, id);
        hash = RuntimeProductHash::mixPodVector(hash, indices);
    }
    hash = RuntimeProductHash::mix(hash, static_cast<uint64_t>(config.receiverVoronoiSeedFlagsByModelId.size()));
    for (const auto& [id, flags] : config.receiverVoronoiSeedFlagsByModelId) {
        hash = RuntimeProductHash::mixPod(hash, id);
        hash = RuntimeProductHash::mixPodVector(hash, flags);
    }
    hash = RuntimeProductHash::mix(hash, static_cast<uint64_t>(config.contactCouplings.size()));
    for (const ContactCoupling& coupling : config.contactCouplings) {
        hash = RuntimeProductHash::mixPod(hash, static_cast<uint32_t>(coupling.couplingType));
        hash = RuntimeProductHash::mixPod(hash, coupling.emitterRuntimeModelId);
        hash = RuntimeProductHash::mixPod(hash, coupling.receiverRuntimeModelId);
        hash = RuntimeProductHash::mixPodVector(hash, coupling.receiverTriangleIndices);
        hash = RuntimeProductHash::mixPod(hash, coupling.contactPairCount);
        if (coupling.contactPairCount != 0 && coupling.mappedContactPairs != nullptr) {
            hash = RuntimeProductHash::mixBytes(
                hash,
                coupling.mappedContactPairs,
                sizeof(ContactPair) * coupling.contactPairCount);
        }
    }
    return hash;
}