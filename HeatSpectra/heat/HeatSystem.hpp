#pragma once

#include "HeatContactRuntime.hpp"
#include "contact/ContactTypes.hpp"
#include "util/Structs.hpp"
#include "HeatSystemSimRuntime.hpp"
#include "HeatSystemSurfaceRuntime.hpp"
#include "HeatSystemRuntime.hpp"
#include "HeatSystemPresets.hpp"
#include "mesh/remesher/SupportingHalfedge.hpp"
#include "runtime/RuntimeProducts.hpp"
#include "runtime/RuntimeThermalTypes.hpp"

#include <memory>
#include <unordered_map>

static constexpr int NUM_SUBSTEPS = 8;

class ModelRegistry;
class MemoryAllocator;
class VulkanDevice;
class UniformBufferManager;
class CommandPool;
class HeatSystemContactStage;
class HeatSystemSimStage;
class HeatSystemSurfaceStage;
class HeatSystemVoronoiStage;
class HeatSystemResources;

class HeatSystem {
public:
    HeatSystem(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, ModelRegistry& resourceManager, HeatSystemResources& resources,
        UniformBufferManager& uniformBufferManager, uint32_t maxFramesInFlight, CommandPool& renderCommandPool,
        VkExtent2D extent, VkRenderPass renderPass);
    ~HeatSystem();

    void update();
    void ensureConfigured();
    void setActive(bool active);
    bool isInitialized() const { return initialized; }

    void recordComputeCommands(VkCommandBuffer commandBuffer, uint32_t currentFrame, VkQueryPool timingQueryPool = VK_NULL_HANDLE, uint32_t timingQueryBase = 0);
    
    bool createComputeCommandBuffers(uint32_t maxFramesInFlight);

    void cleanupResources();
    void cleanup();

    const std::vector<VkCommandBuffer>& getComputeCommandBuffers() const { return computeCommandBuffers; }

    bool getIsActive() const { return isActive; }
    bool getIsPaused() const { return isPaused; } 
    void setIsPaused(bool paused) { isPaused = paused; } 
    const std::vector<HeatSystemRuntime::SourceBinding>& getSourceBindings() const { return heatSources; }
    const std::vector<std::unique_ptr<HeatReceiverRuntime>>& getReceivers() const { return surfaceRuntime.getReceivers(); }
    bool hasDispatchableComputeWork() const;
    bool voronoiReady() const;
    void setSourcePayloads(
        const std::vector<SupportingHalfedge::IntrinsicMesh>& sourceIntrinsicMeshes,
        const std::vector<uint32_t>& sourceRuntimeModelIds,
        const std::unordered_map<uint32_t, float>& sourceTemperatureByRuntimeId);
    void setReceiverPayloads(
        const std::vector<SupportingHalfedge::IntrinsicMesh>& receiverIntrinsicMeshes,
        const std::vector<uint32_t>& receiverRuntimeModelIds,
        const std::vector<VkBufferView>& supportingHalfedgeViews,
        const std::vector<VkBufferView>& supportingAngleViews,
        const std::vector<VkBufferView>& halfedgeViews,
        const std::vector<VkBufferView>& edgeViews,
        const std::vector<VkBufferView>& triangleViews,
        const std::vector<VkBufferView>& lengthViews,
        const std::vector<VkBufferView>& inputHalfedgeViews,
        const std::vector<VkBufferView>& inputEdgeViews,
        const std::vector<VkBufferView>& inputTriangleViews,
        const std::vector<VkBufferView>& inputLengthViews);
    void setThermalMaterials(const std::vector<RuntimeThermalMaterial>& runtimeThermalMaterials);
    void setContactCouplings(const std::vector<ContactProduct>& contactCouplings);
    void clearVoronoiInputs();
    void setVoronoiBuffers(
        uint32_t nodeCount,
        const VoronoiNode* voronoiNodes,
        VkBuffer nodeBuffer,
        VkDeviceSize nodeBufferOffset,
        VkBuffer voronoiNeighborBuffer,
        VkDeviceSize voronoiNeighborBufferOffset,
        VkBuffer neighborIndicesBuffer,
        VkDeviceSize neighborIndicesBufferOffset,
        VkBuffer interfaceAreasBuffer,
        VkDeviceSize interfaceAreasBufferOffset,
        VkBuffer interfaceNeighborIdsBuffer,
        VkDeviceSize interfaceNeighborIdsBufferOffset,
        VkBuffer seedFlagsBuffer,
        VkDeviceSize seedFlagsBufferOffset);
    void addVoronoiReceiverInput(
        uint32_t runtimeModelId,
        uint32_t nodeOffset,
        uint32_t nodeCount,
        VkBuffer surfaceMappingBuffer,
        VkDeviceSize surfaceMappingBufferOffset,
        const std::vector<uint32_t>& surfaceCellIndices,
        const std::vector<uint32_t>& seedFlags);

private:    
    using SourceBinding = HeatSystemRuntime::SourceBinding;
    using ContactCoupling = HeatContactRuntime::ContactCoupling;
    using ContactCouplingType = ::ContactCouplingType;

    void failInitialization(const char* stage);
    bool rebuildHeatStateRuntimes(bool forceDescriptorReallocate);
    bool rebuildVoronoiRuntime();
    bool initializeVoronoiMaterialNodes();
    void rebuildReceiverThermalMaterialMap();
    void cleanupVoronoiRuntime();
    void resetHeatState();

    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    ModelRegistry& resourceManager;
    HeatSystemResources& resources;
    UniformBufferManager& uniformBufferManager;
    CommandPool& renderCommandPool; 
    HeatSystemRuntime runtime;
    HeatSystemSimRuntime simRuntime;
    HeatSystemSurfaceRuntime surfaceRuntime;
    std::vector<SourceBinding>& heatSources;
    HeatContactRuntime heatContactRuntime;
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
    std::vector<uint32_t> receiverRuntimeModelIds;
    std::vector<RuntimeThermalMaterial> runtimeThermalMaterials;
    
    std::unique_ptr<HeatSystemContactStage> contactStage;
    std::unique_ptr<HeatSystemSimStage> simStage;
    std::unique_ptr<HeatSystemSurfaceStage> surfaceStage;
    std::unique_ptr<HeatSystemVoronoiStage> voronoiStage;
    std::unordered_map<uint32_t, uint32_t> receiverVoronoiNodeOffsetByModelId;
    std::unordered_map<uint32_t, uint32_t> receiverVoronoiNodeCountByModelId;
    std::unordered_map<uint32_t, VkBuffer> receiverVoronoiSurfaceMappingBufferByModelId;
    std::unordered_map<uint32_t, VkDeviceSize> receiverVoronoiSurfaceMappingBufferOffsetByModelId;
    std::unordered_map<uint32_t, std::vector<uint32_t>> receiverVoronoiSurfaceCellIndicesByModelId;
    std::unordered_map<uint32_t, std::vector<uint32_t>> receiverVoronoiSeedFlagsByModelId;
    std::unordered_map<uint32_t, RuntimeThermalMaterial> receiverThermalMaterialByModelId;
    
    uint32_t maxFramesInFlight;
    std::vector<VkCommandBuffer> computeCommandBuffers;

    bool isActive = false;
    bool isPaused = false;
    bool initialized = false;
    bool voronoiConfigDirty = true;
    bool thermalMaterialsDirty = true;
    static constexpr uint32_t MAX_NODE_NEIGHBORS = 50;
};

