#pragma once

#include "HeatContactRuntime.hpp"
#include "contact/ContactTypes.hpp"
#include "framegraph/ComputePass.hpp"
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
class CommandPool;
class HeatSystemSimStage;
class HeatSystemSurfaceStage;
class HeatSystemVoronoiStage;
class HeatSystemResources;

class HeatSystem : public ComputePass {
public:
    HeatSystem(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, ModelRegistry& resourceManager, HeatSystemResources& resources,
        uint32_t maxFramesInFlight, CommandPool& renderCommandPool);
    ~HeatSystem();

    void update() override;
    void ensureConfigured();
    void setActive(bool active);
    bool isInitialized() const { return initialized; }

    void recordComputeCommands(VkCommandBuffer commandBuffer, uint32_t currentFrame, VkQueryPool timingQueryPool, uint32_t timingQueryBase) override;
    
    bool createComputeCommandBuffers(uint32_t maxFramesInFlight);

    void cleanupResources();
    void cleanup();

    const std::vector<VkCommandBuffer>& getComputeCommandBuffers() const override { return computeCommandBuffers; }

    bool getIsActive() const { return isActive; }
    bool getIsPaused() const { return isPaused; } 
    void setIsPaused(bool paused) { isPaused = paused; } 
    const std::vector<HeatSystemRuntime::SourceBinding>& getSourceBindings() const { return heatSources; }
    const std::vector<std::unique_ptr<HeatReceiverRuntime>>& getReceivers() const { return surfaceRuntime.getReceivers(); }
    bool hasDispatchableComputeWork() const override;
    bool voronoiReady() const;
    void resetHeatState();
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
    void setParams(float contactThermalConductance);
    void setContactCouplings(const std::vector<ContactCoupling>& contactCouplings);
    void clearVoronoiInputs();
    void setVoronoiBuffers(
        uint32_t nodeCount,
        const voronoi::Node* voronoiNodes,
        VkBuffer nodeBuffer,
        VkDeviceSize nodeBufferOffset,
        VkBuffer gmlsInterfaceBuffer,
        VkDeviceSize gmlsInterfaceBufferOffset,
        VkBuffer seedFlagsBuffer,
        VkDeviceSize seedFlagsBufferOffset);
    void addVoronoiReceiverInput(
        uint32_t runtimeModelId,
        uint32_t nodeOffset,
        uint32_t nodeCount,
        VkBuffer gmlsSurfaceStencilBuffer,
        VkDeviceSize gmlsSurfaceStencilBufferOffset,
        VkBuffer gmlsSurfaceWeightBuffer,
        VkDeviceSize gmlsSurfaceWeightBufferOffset,
        VkBuffer gmlsSurfaceGradientWeightBuffer,
        VkDeviceSize gmlsSurfaceGradientWeightBufferOffset,
        const std::vector<uint32_t>& seedFlags,
        const std::vector<glm::vec3>& seedPositions);

private:    
    using SourceBinding = HeatSystemRuntime::SourceBinding;
    using ContactCouplingType = ::ContactCouplingType;

    void failInitialization(const char* stage);
    bool rebuildHeatStateRuntimes(bool forceDescriptorReallocate);
    bool rebuildVoronoiRuntime();
    bool initializeVoronoiMaterialNodes();
    void rebuildReceiverThermalMaterialMap();
    void cleanupVoronoiRuntime();

    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    ModelRegistry& resourceManager;
    HeatSystemResources& resources;
    CommandPool& renderCommandPool; 
    HeatSystemRuntime runtime;
    HeatSystemSimRuntime simRuntime;
    HeatSystemSurfaceRuntime surfaceRuntime;
    std::vector<SourceBinding>& heatSources;
    HeatContactRuntime heatContactRuntime;
    uint32_t voronoiNodeCount = 0;
    const voronoi::Node* voronoiNodes = nullptr;
    VkBuffer voronoiNodeBuffer = VK_NULL_HANDLE;
    VkDeviceSize voronoiNodeBufferOffset = 0;
    VkBuffer gmlsInterfaceBuffer = VK_NULL_HANDLE;
    VkDeviceSize gmlsInterfaceBufferOffset = 0;
    VkBuffer seedFlagsBuffer = VK_NULL_HANDLE;
    VkDeviceSize seedFlagsBufferOffset = 0;
    std::vector<uint32_t> receiverRuntimeModelIds;
    std::vector<RuntimeThermalMaterial> runtimeThermalMaterials;
    float contactThermalConductance = 16000.0f;
    
    std::unique_ptr<HeatSystemSimStage> simStage;
    std::unique_ptr<HeatSystemSurfaceStage> surfaceStage;
    std::unique_ptr<HeatSystemVoronoiStage> voronoiStage;
    std::unordered_map<uint32_t, uint32_t> receiverVoronoiNodeOffsetByModelId;
    std::unordered_map<uint32_t, uint32_t> receiverVoronoiNodeCountByModelId;
    std::unordered_map<uint32_t, VkBuffer> receiverGMLSSurfaceStencilBufferByModelId;
    std::unordered_map<uint32_t, VkDeviceSize> receiverGMLSSurfaceStencilBufferOffsetByModelId;
    std::unordered_map<uint32_t, VkBuffer> receiverGMLSSurfaceWeightBufferByModelId;
    std::unordered_map<uint32_t, VkDeviceSize> receiverGMLSSurfaceWeightBufferOffsetByModelId;
    std::unordered_map<uint32_t, VkBuffer> receiverGMLSSurfaceGradientWeightBufferByModelId;
    std::unordered_map<uint32_t, VkDeviceSize> receiverGMLSSurfaceGradientWeightBufferOffsetByModelId;
    std::unordered_map<uint32_t, std::vector<uint32_t>> receiverVoronoiSeedFlagsByModelId;
    std::unordered_map<uint32_t, std::vector<glm::vec3>> receiverVoronoiSeedPositionsByModelId;
    std::unordered_map<uint32_t, RuntimeThermalMaterial> receiverThermalMaterialByModelId;
    
    uint32_t maxFramesInFlight;
    std::vector<VkCommandBuffer> computeCommandBuffers;

    bool isActive = false;
    bool isPaused = false;
    bool initialized = false;
    bool voronoiConfigDirty = true;
    bool thermalMaterialsDirty = true;
    bool heatParamsDirty = true;
    static constexpr uint32_t MAX_NODE_NEIGHBORS = 50;
};

