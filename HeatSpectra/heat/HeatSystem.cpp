#include "HeatSystem.hpp"

#include "HeatReceiverRuntime.hpp"
#include "HeatSourceRuntime.hpp"
#include "HeatSystemResources.hpp"
#include "HeatSystemSimStage.hpp"
#include "HeatSystemSurfaceStage.hpp"
#include "HeatSystemStageContext.hpp"
#include "HeatSystemVoronoiStage.hpp"
#include "heat/HeatGpuStructs.hpp"
#include "nodegraph/NodeModelTransform.hpp"
#include "vulkan/CommandBufferManager.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/ModelRegistry.hpp"
#include "vulkan/VulkanBuffer.hpp"
#include "vulkan/VulkanDevice.hpp"
#include "voronoi/VoronoiGpuStructs.hpp"

#include <cmath>
#include <chrono>
#include <iostream>

HeatSystem::HeatSystem(
    VulkanDevice& vulkanDevice,
    MemoryAllocator& memoryAllocator,
    ModelRegistry& resourceManager,
    HeatSystemResources& resources,
    uint32_t maxFramesInFlight,
    CommandPool& renderCommandPool)
    : vulkanDevice(vulkanDevice),
      memoryAllocator(memoryAllocator),
      resourceManager(resourceManager),
      resources(resources),
      renderCommandPool(renderCommandPool),
      runtime(),
      heatSources(runtime.getSourceBindingsMutable()),
      maxFramesInFlight(maxFramesInFlight) {

    HeatSystemStageContext stageContext{
        vulkanDevice,
        memoryAllocator,
        resourceManager,
        renderCommandPool,
        resources
    };

    simStage = std::make_unique<HeatSystemSimStage>(stageContext);
    surfaceStage = std::make_unique<HeatSystemSurfaceStage>(stageContext);
    voronoiStage = std::make_unique<HeatSystemVoronoiStage>(stageContext);

    if (!surfaceStage ||
        !surfaceStage->createDescriptorPool(maxFramesInFlight) ||
        !surfaceStage->createDescriptorSetLayout() ||
        !surfaceStage->createPipeline() ||
        !voronoiStage ||
        !voronoiStage->createDescriptorPool(maxFramesInFlight) ||
        !voronoiStage->createDescriptorSetLayout() ||
        !voronoiStage->createPipeline()) {
        failInitialization("create contact compute resources");
        return;
    }

    if (!createComputeCommandBuffers(maxFramesInFlight)) {
        failInitialization("allocate compute command buffers");
        return;
    }

    initialized = true;
}

HeatSystem::~HeatSystem() {
}

void HeatSystem::failInitialization(const char* stage) {
    std::cerr << "[HeatSystem] Initialization failed at stage: " << stage << std::endl;
    cleanupResources();
    cleanup();
}

void HeatSystem::setSourcePayloads(
    const std::vector<SupportingHalfedge::IntrinsicMesh>& sourceIntrinsicMeshes,
    const std::vector<uint32_t>& sourceRuntimeModelIds,
    const std::unordered_map<uint32_t, float>& sourceTemperatureByRuntimeId) {
    runtime.setSourcePayloads(
        sourceIntrinsicMeshes,
        sourceRuntimeModelIds,
        sourceTemperatureByRuntimeId);
}

void HeatSystem::setReceiverPayloads(
    const std::vector<SupportingHalfedge::IntrinsicMesh>& updatedReceiverIntrinsicMeshes,
    const std::vector<uint32_t>& updatedReceiverRuntimeModelIds,
    const std::vector<VkBufferView>& supportingHalfedgeViews,
    const std::vector<VkBufferView>& supportingAngleViews,
    const std::vector<VkBufferView>& halfedgeViews,
    const std::vector<VkBufferView>& edgeViews,
    const std::vector<VkBufferView>& triangleViews,
    const std::vector<VkBufferView>& lengthViews,
    const std::vector<VkBufferView>& inputHalfedgeViews,
    const std::vector<VkBufferView>& inputEdgeViews,
    const std::vector<VkBufferView>& inputTriangleViews,
    const std::vector<VkBufferView>& inputLengthViews) {
    receiverRuntimeModelIds = updatedReceiverRuntimeModelIds;
    surfaceRuntime.setReceiverPayloads(
        updatedReceiverIntrinsicMeshes,
        receiverRuntimeModelIds,
        supportingHalfedgeViews,
        supportingAngleViews,
        halfedgeViews,
        edgeViews,
        triangleViews,
        lengthViews,
        inputHalfedgeViews,
        inputEdgeViews,
        inputTriangleViews,
        inputLengthViews);
    voronoiConfigDirty = true;
}

void HeatSystem::setThermalMaterials(const std::vector<RuntimeThermalMaterial>& updatedRuntimeThermalMaterials) {
    runtimeThermalMaterials = updatedRuntimeThermalMaterials;
    thermalMaterialsDirty = true;
}

void HeatSystem::setParams(float updatedContactThermalConductance) {
    if (contactThermalConductance == updatedContactThermalConductance) {
        return;
    }

    contactThermalConductance = updatedContactThermalConductance;
    heatParamsDirty = true;
}

void HeatSystem::setContactCouplings(const std::vector<ContactCoupling>& contactCouplings) {
    heatContactRuntime.setContactCouplings(receiverRuntimeModelIds, contactCouplings);
}

void HeatSystem::clearVoronoiInputs() {
    voronoiNodeCount = 0;
    voronoiNodes = nullptr;
    voronoiNodeBuffer = VK_NULL_HANDLE;
    voronoiNodeBufferOffset = 0;
    gmlsInterfaceBuffer = VK_NULL_HANDLE;
    gmlsInterfaceBufferOffset = 0;
    seedFlagsBuffer = VK_NULL_HANDLE;
    seedFlagsBufferOffset = 0;
    receiverVoronoiNodeOffsetByModelId.clear();
    receiverVoronoiNodeCountByModelId.clear();
    receiverGMLSSurfaceStencilBufferByModelId.clear();
    receiverGMLSSurfaceStencilBufferOffsetByModelId.clear();
    receiverGMLSSurfaceWeightBufferByModelId.clear();
    receiverGMLSSurfaceWeightBufferOffsetByModelId.clear();
    receiverGMLSSurfaceGradientWeightBufferByModelId.clear();
    receiverGMLSSurfaceGradientWeightBufferOffsetByModelId.clear();
    receiverVoronoiSeedFlagsByModelId.clear();
    receiverVoronoiSeedPositionsByModelId.clear();
    voronoiConfigDirty = true;
}

void HeatSystem::setVoronoiBuffers(
    uint32_t nodeCount,
    const voronoi::Node* inputVoronoiNodes,
    VkBuffer inputNodeBuffer,
    VkDeviceSize inputNodeBufferOffset,
    VkBuffer inputGMLSInterfaceBuffer,
    VkDeviceSize inputGMLSInterfaceBufferOffset,
    VkBuffer inputSeedFlagsBuffer,
    VkDeviceSize inputSeedFlagsBufferOffset) {
    voronoiNodeCount = nodeCount;
    voronoiNodes = inputVoronoiNodes;
    voronoiNodeBuffer = inputNodeBuffer;
    voronoiNodeBufferOffset = inputNodeBufferOffset;
    gmlsInterfaceBuffer = inputGMLSInterfaceBuffer;
    gmlsInterfaceBufferOffset = inputGMLSInterfaceBufferOffset;
    seedFlagsBuffer = inputSeedFlagsBuffer;
    seedFlagsBufferOffset = inputSeedFlagsBufferOffset;
    voronoiConfigDirty = true;
}

void HeatSystem::addVoronoiReceiverInput(
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
    const std::vector<glm::vec3>& seedPositions) {
    if (runtimeModelId == 0) {
        return;
    }

    receiverVoronoiNodeOffsetByModelId[runtimeModelId] = nodeOffset;
    receiverVoronoiNodeCountByModelId[runtimeModelId] = nodeCount;
    receiverGMLSSurfaceStencilBufferByModelId[runtimeModelId] = gmlsSurfaceStencilBuffer;
    receiverGMLSSurfaceStencilBufferOffsetByModelId[runtimeModelId] = gmlsSurfaceStencilBufferOffset;
    receiverGMLSSurfaceWeightBufferByModelId[runtimeModelId] = gmlsSurfaceWeightBuffer;
    receiverGMLSSurfaceWeightBufferOffsetByModelId[runtimeModelId] = gmlsSurfaceWeightBufferOffset;
    receiverGMLSSurfaceGradientWeightBufferByModelId[runtimeModelId] = gmlsSurfaceGradientWeightBuffer;
    receiverGMLSSurfaceGradientWeightBufferOffsetByModelId[runtimeModelId] = gmlsSurfaceGradientWeightBufferOffset;
    receiverVoronoiSeedFlagsByModelId[runtimeModelId] = seedFlags;
    receiverVoronoiSeedPositionsByModelId[runtimeModelId] = seedPositions;
    voronoiConfigDirty = true;
}

void HeatSystem::update() {
    if (isPaused) {
        return;
    }

    static auto lastTime = std::chrono::steady_clock::now();
    const auto currentTime = std::chrono::steady_clock::now();
    float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
    lastTime = currentTime;
    if (deltaTime > (1.0f / 30.0f)) {
        deltaTime = 1.0f / 30.0f;
    }

    auto* timeData = simRuntime.getMappedTimeData();
    if (timeData) {
        timeData->deltaTime = deltaTime / static_cast<float>(NUM_SUBSTEPS);
        timeData->totalTime += deltaTime;
    }

}

void HeatSystem::ensureConfigured() {
    const bool needsHardRebuild =
        runtime.needsRebuild() ||
        surfaceRuntime.needsRebuild() ||
        heatContactRuntime.needsRebuild() ||
        voronoiConfigDirty ||
        thermalMaterialsDirty ||
        heatParamsDirty;

    if (!needsHardRebuild) {
        return;
    }

    vkDeviceWaitIdle(vulkanDevice.getDevice());
    rebuildHeatStateRuntimes(true);
    resetHeatState();
}

bool HeatSystem::rebuildHeatStateRuntimes(bool forceDescriptorReallocate) {
    const bool sourcesReady = runtime.ensureModelBindings(
        vulkanDevice,
        memoryAllocator,
        renderCommandPool);
    if (!sourcesReady) {
        return false;
    }

    const bool receiversReady = surfaceRuntime.ensureReceiverBindings(
        vulkanDevice,
        memoryAllocator);

    if (!receiversReady) {
        return false;
    }

    surfaceRuntime.executeBufferTransfers(renderCommandPool);
    if (!heatContactRuntime.ensureCouplings(
            vulkanDevice,
            memoryAllocator,
            heatSources,
            surfaceRuntime.getReceivers(),
            receiverVoronoiNodeOffsetByModelId,
            receiverVoronoiSeedFlagsByModelId,
            receiverVoronoiSeedPositionsByModelId,
            contactThermalConductance,
            heatParamsDirty,
            voronoiNodeCount)) {
        std::cerr << "[HeatSystem] rebuildHeatStateRuntimes: ensureCouplings FAILED" << std::endl;
        return false;
    }

    // Copy aggregate source-to-receiver contact conductance to resources for the Voronoi stage.
    if (heatContactRuntime.getContactConductanceBuffer() != VK_NULL_HANDLE) {
        resources.contactConductanceBuffer = heatContactRuntime.getContactConductanceBuffer();
        resources.contactConductanceBufferOffset = heatContactRuntime.getContactConductanceBufferOffset();
        resources.contactConductanceNodeCount = heatContactRuntime.getContactConductanceNodeCount();
        resources.hasContact = true;
    } else {
        resources.contactConductanceBuffer = VK_NULL_HANDLE;
        resources.contactConductanceBufferOffset = 0;
        resources.contactConductanceNodeCount = 0;
        resources.hasContact = false;
    }

    const bool heatVoronoiReady = rebuildVoronoiRuntime();

    for (const auto& receiver : surfaceRuntime.getReceivers()) {
        if (!receiver) {
            continue;
        }

        const uint32_t runtimeModelId = receiver->getRuntimeModelId();
        const auto stencilIt = receiverGMLSSurfaceStencilBufferByModelId.find(runtimeModelId);
        const auto stencilOffsetIt = receiverGMLSSurfaceStencilBufferOffsetByModelId.find(runtimeModelId);
        const auto valueWeightIt = receiverGMLSSurfaceWeightBufferByModelId.find(runtimeModelId);
        const auto valueWeightOffsetIt = receiverGMLSSurfaceWeightBufferOffsetByModelId.find(runtimeModelId);
        const auto gradientWeightIt = receiverGMLSSurfaceGradientWeightBufferByModelId.find(runtimeModelId);
        const auto gradientWeightOffsetIt = receiverGMLSSurfaceGradientWeightBufferOffsetByModelId.find(runtimeModelId);
        if (!heatVoronoiReady) {
            receiver->setGMLSSurfaceWeights(VK_NULL_HANDLE, 0, VK_NULL_HANDLE, 0, VK_NULL_HANDLE, 0);
            continue;
        }

        receiver->setGMLSSurfaceWeights(
            stencilIt != receiverGMLSSurfaceStencilBufferByModelId.end() ? stencilIt->second : VK_NULL_HANDLE,
            stencilOffsetIt != receiverGMLSSurfaceStencilBufferOffsetByModelId.end() ? stencilOffsetIt->second : 0,
            valueWeightIt != receiverGMLSSurfaceWeightBufferByModelId.end() ? valueWeightIt->second : VK_NULL_HANDLE,
            valueWeightOffsetIt != receiverGMLSSurfaceWeightBufferOffsetByModelId.end() ? valueWeightOffsetIt->second : 0,
            gradientWeightIt != receiverGMLSSurfaceGradientWeightBufferByModelId.end() ? gradientWeightIt->second : VK_NULL_HANDLE,
            gradientWeightOffsetIt != receiverGMLSSurfaceGradientWeightBufferOffsetByModelId.end() ? gradientWeightOffsetIt->second : 0);
    }

    if (!heatVoronoiReady) {
        simRuntime.cleanup(memoryAllocator);
        resources.voronoiDescriptorSets.clear();
        resources.voronoiDescriptorSetsB.clear();
        voronoiConfigDirty = false;
        thermalMaterialsDirty = false;
        return true;
    }

    const bool simReady =
        simRuntime.initialize(vulkanDevice, memoryAllocator, resources.voronoiNodeCount) &&
        voronoiStage &&
        voronoiStage->createDescriptorSets(maxFramesInFlight, simRuntime);
    if (!simReady) {
        return false;
    }

    if (resources.surfaceDescriptorSetLayout != VK_NULL_HANDLE &&
        resources.surfaceDescriptorPool != VK_NULL_HANDLE) {
        if (forceDescriptorReallocate) {
            vkResetDescriptorPool(vulkanDevice.getDevice(), resources.surfaceDescriptorPool, 0);
        }
        surfaceRuntime.refreshDescriptors(
            simRuntime,
            resources.surfaceDescriptorSetLayout,
            resources.surfaceDescriptorPool,
            forceDescriptorReallocate);
    }

    voronoiConfigDirty = false;
    thermalMaterialsDirty = false;
    heatParamsDirty = false;
    return true;
}

void HeatSystem::setActive(bool active) {
    isActive = active;
}

void HeatSystem::resetHeatState() {
    simRuntime.reset();
    surfaceRuntime.resetSurfaceTemperatures(renderCommandPool);
}

bool HeatSystem::createComputeCommandBuffers(uint32_t maxFramesInFlight) {
    computeCommandBuffers.resize(maxFramesInFlight);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = renderCommandPool.getHandle();
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(computeCommandBuffers.size());

    if (vkAllocateCommandBuffers(vulkanDevice.getDevice(), &allocInfo, computeCommandBuffers.data()) != VK_SUCCESS) {
        computeCommandBuffers.clear();
        return false;
    }
    return true;
}

bool HeatSystem::hasDispatchableComputeWork() const {
    return isActive &&
        !isPaused &&
        voronoiReady() &&
        !computeCommandBuffers.empty();
}

bool HeatSystem::voronoiReady() const {
    return voronoiNodeCount != 0 &&
        resources.voronoiNodeCount > 0 &&
        simRuntime.isInitialized();
}

void HeatSystem::recordComputeCommands(VkCommandBuffer commandBuffer, uint32_t currentFrame, VkQueryPool timingQueryPool, uint32_t timingQueryBase) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        return;
    }

    if (hasDispatchableComputeWork() &&
        simStage &&
        surfaceStage &&
        voronoiStage) {
        heat::SourcePushConstant basePushConstant{};
        basePushConstant.maxNodeNeighbors = MAX_NODE_NEIGHBORS;
        basePushConstant.substepIndex = 0;
        basePushConstant.hasContact = resources.hasContact ? 1u : 0u;
        basePushConstant.heatSourceTemperature = 0.0f;

        if (const SourceBinding* baseSource = runtime.findBaseSourceBinding();
            baseSource && baseSource->heatSource) {
            basePushConstant.heatSourceTemperature = baseSource->heatSource->getUniformTemperature();
        }

        if (timingQueryPool != VK_NULL_HANDLE) {
            vkCmdResetQueryPool(commandBuffer, timingQueryPool, timingQueryBase, 2);
            vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, timingQueryPool, timingQueryBase);
        }

        simStage->recordComputeCommands(
            commandBuffer,
            currentFrame,
            simRuntime,
            basePushConstant,
            surfaceRuntime.getReceivers(),
            *voronoiStage,
            *surfaceStage,
            MAX_NODE_NEIGHBORS,
            NUM_SUBSTEPS);

        if (timingQueryPool != VK_NULL_HANDLE) {
            vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timingQueryPool, timingQueryBase + 1);
        }
    } 

    vkEndCommandBuffer(commandBuffer);
}

void HeatSystem::cleanupResources() {
    if (resources.contactPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(vulkanDevice.getDevice(), resources.contactPipeline, nullptr);
        resources.contactPipeline = VK_NULL_HANDLE;
    }
    if (resources.contactPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vulkanDevice.getDevice(), resources.contactPipelineLayout, nullptr);
        resources.contactPipelineLayout = VK_NULL_HANDLE;
    }
    if (resources.contactDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vulkanDevice.getDevice(), resources.contactDescriptorPool, nullptr);
        resources.contactDescriptorPool = VK_NULL_HANDLE;
    }
    if (resources.contactDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vulkanDevice.getDevice(), resources.contactDescriptorSetLayout, nullptr);
        resources.contactDescriptorSetLayout = VK_NULL_HANDLE;
    }
    if (resources.surfacePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(vulkanDevice.getDevice(), resources.surfacePipeline, nullptr);
        resources.surfacePipeline = VK_NULL_HANDLE;
    }
    if (resources.surfacePipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vulkanDevice.getDevice(), resources.surfacePipelineLayout, nullptr);
        resources.surfacePipelineLayout = VK_NULL_HANDLE;
    }
    if (resources.surfaceDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vulkanDevice.getDevice(), resources.surfaceDescriptorPool, nullptr);
        resources.surfaceDescriptorPool = VK_NULL_HANDLE;
    }
    if (resources.surfaceDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vulkanDevice.getDevice(), resources.surfaceDescriptorSetLayout, nullptr);
        resources.surfaceDescriptorSetLayout = VK_NULL_HANDLE;
    }
    if (resources.voronoiPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(vulkanDevice.getDevice(), resources.voronoiPipeline, nullptr);
        resources.voronoiPipeline = VK_NULL_HANDLE;
    }
    if (resources.voronoiPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vulkanDevice.getDevice(), resources.voronoiPipelineLayout, nullptr);
        resources.voronoiPipelineLayout = VK_NULL_HANDLE;
    }
    if (resources.voronoiDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vulkanDevice.getDevice(), resources.voronoiDescriptorPool, nullptr);
        resources.voronoiDescriptorPool = VK_NULL_HANDLE;
    }
    if (resources.voronoiDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vulkanDevice.getDevice(), resources.voronoiDescriptorSetLayout, nullptr);
        resources.voronoiDescriptorSetLayout = VK_NULL_HANDLE;
    }
    resources.voronoiDescriptorSets.clear();
    resources.voronoiDescriptorSetsB.clear();
    resources.contactConductanceBuffer = VK_NULL_HANDLE;
    resources.contactConductanceBufferOffset = 0;
    resources.contactConductanceNodeCount = 0;
    resources.hasContact = false;
}

void HeatSystem::cleanup() {
    heatContactRuntime.clearCouplings(memoryAllocator);
    surfaceRuntime.cleanup();
    cleanupVoronoiRuntime();
    simRuntime.cleanup(memoryAllocator);
    runtime.cleanupModelBindings();
}

bool HeatSystem::initializeVoronoiMaterialNodes() {
    if (resources.voronoiNodeCount == 0) {
        return false;
    }

    const voronoi::Node* voronoiNodes =
        static_cast<const voronoi::Node*>(resources.mappedVoronoiNodeData);
    if (!voronoiNodes) {
        return false;
    }

    std::vector<voronoi::MaterialNode> materialNodes(resources.voronoiNodeCount);
    for (voronoi::MaterialNode& materialNode : materialNodes) {
        materialNode.temperature = 1.0f;
        materialNode.conductivityPerMass = 0.0f;
        materialNode.thermalMass = 0.0f;
        materialNode.density = 0.0f;
        materialNode.specificHeat = 0.0f;
        materialNode.conductivity = 0.0f;
    }

    const RuntimeThermalMaterial defaultMaterial{};
    for (const auto& [runtimeModelId, nodeOffset] : receiverVoronoiNodeOffsetByModelId) {
        const auto countIt = receiverVoronoiNodeCountByModelId.find(runtimeModelId);
        const auto seedFlagsIt = receiverVoronoiSeedFlagsByModelId.find(runtimeModelId);
        if (countIt == receiverVoronoiNodeCountByModelId.end() ||
            seedFlagsIt == receiverVoronoiSeedFlagsByModelId.end()) {
            continue;
        }

        RuntimeThermalMaterial material = defaultMaterial;
        const auto materialIt = receiverThermalMaterialByModelId.find(runtimeModelId);
        if (materialIt != receiverThermalMaterialByModelId.end()) {
            material = materialIt->second;
        }

        for (uint32_t localNodeIndex = 0; localNodeIndex < countIt->second; ++localNodeIndex) {
            const uint32_t nodeIndex = nodeOffset + localNodeIndex;
            if (nodeIndex >= materialNodes.size()) {
                continue;
            }

            voronoi::MaterialNode& materialNode = materialNodes[nodeIndex];
            if (localNodeIndex < seedFlagsIt->second.size() &&
                (seedFlagsIt->second[localNodeIndex] & 1u) != 0u) {
                continue;
            }

            materialNode.density = material.density;
            materialNode.specificHeat = material.specificHeat;
            materialNode.conductivity = material.conductivity;
            const float volume = std::abs(voronoiNodes[nodeIndex].volume);
            materialNode.thermalMass = material.density * material.specificHeat * volume;
            if (materialNode.thermalMass > 1e-20f) {
                materialNode.conductivityPerMass = material.conductivity / materialNode.thermalMass;
            }
        }
    }

    if (resources.voronoiMaterialNodeBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(resources.voronoiMaterialNodeBuffer, resources.voronoiMaterialNodeBufferOffset);
        resources.voronoiMaterialNodeBuffer = VK_NULL_HANDLE;
        resources.voronoiMaterialNodeBufferOffset = 0;
        resources.mappedVoronoiMaterialNodeData = nullptr;
    }

    return createStorageBuffer(
               memoryAllocator,
               vulkanDevice,
               materialNodes.data(),
               sizeof(voronoi::MaterialNode) * materialNodes.size(),
               resources.voronoiMaterialNodeBuffer,
               resources.voronoiMaterialNodeBufferOffset,
               &resources.mappedVoronoiMaterialNodeData) == VK_SUCCESS &&
        resources.voronoiMaterialNodeBuffer != VK_NULL_HANDLE;
}

void HeatSystem::rebuildReceiverThermalMaterialMap() {
    receiverThermalMaterialByModelId.clear();
    for (const RuntimeThermalMaterial& material : runtimeThermalMaterials) {
        if (material.runtimeModelId == 0) {
            continue;
        }
        receiverThermalMaterialByModelId[material.runtimeModelId] = material;
    }
}

void HeatSystem::cleanupVoronoiRuntime() {
    resources.voronoiNodeCount = 0;
    resources.mappedVoronoiNodeData = nullptr;
    resources.voronoiNodeBuffer = VK_NULL_HANDLE;
    resources.voronoiNodeBufferOffset = 0;
    resources.gmlsInterfaceBuffer = VK_NULL_HANDLE;
    resources.gmlsInterfaceBufferOffset = 0;
    resources.seedFlagsBuffer = VK_NULL_HANDLE;
    resources.seedFlagsBufferOffset = 0;
    if (resources.voronoiMaterialNodeBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(resources.voronoiMaterialNodeBuffer, resources.voronoiMaterialNodeBufferOffset);
        resources.voronoiMaterialNodeBuffer = VK_NULL_HANDLE;
        resources.voronoiMaterialNodeBufferOffset = 0;
    }
    resources.mappedVoronoiMaterialNodeData = nullptr;
    receiverThermalMaterialByModelId.clear();
}

bool HeatSystem::rebuildVoronoiRuntime() {
    cleanupVoronoiRuntime();

    if (voronoiNodeCount == 0 || receiverRuntimeModelIds.empty()) {
        return false;
    }

    resources.voronoiNodeCount = voronoiNodeCount;
    resources.voronoiNodeBuffer = voronoiNodeBuffer;
    resources.voronoiNodeBufferOffset = voronoiNodeBufferOffset;
    resources.mappedVoronoiNodeData = voronoiNodes;
    resources.gmlsInterfaceBuffer = gmlsInterfaceBuffer;
    resources.gmlsInterfaceBufferOffset = gmlsInterfaceBufferOffset;
    resources.seedFlagsBuffer = seedFlagsBuffer;
    resources.seedFlagsBufferOffset = seedFlagsBufferOffset;

    if (receiverVoronoiNodeOffsetByModelId.empty()) {
        return false;
    }

    rebuildReceiverThermalMaterialMap();
    if (!initializeVoronoiMaterialNodes()) {
        return false;
    }
    return resources.voronoiNodeCount > 0;
}
