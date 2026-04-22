#include "HeatSystem.hpp"

#include "HeatReceiverRuntime.hpp"
#include "HeatSourceRuntime.hpp"
#include "HeatSystemContactStage.hpp"
#include "HeatSystemResources.hpp"
#include "HeatSystemSimStage.hpp"
#include "HeatSystemSurfaceStage.hpp"
#include "HeatSystemStageContext.hpp"
#include "HeatSystemVoronoiStage.hpp"
#include "nodegraph/NodeModelTransform.hpp"
#include "vulkan/CommandBufferManager.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/ModelRegistry.hpp"
#include "vulkan/VulkanBuffer.hpp"
#include "vulkan/VulkanDevice.hpp"

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

    contactStage = std::make_unique<HeatSystemContactStage>(stageContext);
    simStage = std::make_unique<HeatSystemSimStage>(stageContext);
    surfaceStage = std::make_unique<HeatSystemSurfaceStage>(stageContext);
    voronoiStage = std::make_unique<HeatSystemVoronoiStage>(stageContext);

    if (!contactStage ||
        !contactStage->createDescriptorPool(maxFramesInFlight) ||
        !contactStage->createDescriptorSetLayout() ||
        !contactStage->createPipeline() ||
        !surfaceStage ||
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

void HeatSystem::setContactCouplings(const std::vector<ContactCoupling>& contactCouplings) {
    std::cerr << "[HeatSystem] setContactCouplings"
              << " receiverRuntimeModelIds=" << receiverRuntimeModelIds.size()
              << " products=" << contactCouplings.size()
              << std::endl;
    heatContactRuntime.setContactCouplings(receiverRuntimeModelIds, contactCouplings);
}

void HeatSystem::clearVoronoiInputs() {
    voronoiNodeCount = 0;
    voronoiNodes = nullptr;
    voronoiNodeBuffer = VK_NULL_HANDLE;
    voronoiNodeBufferOffset = 0;
    voronoiNeighborBuffer = VK_NULL_HANDLE;
    voronoiNeighborBufferOffset = 0;
    neighborIndicesBuffer = VK_NULL_HANDLE;
    neighborIndicesBufferOffset = 0;
    interfaceAreasBuffer = VK_NULL_HANDLE;
    interfaceAreasBufferOffset = 0;
    interfaceNeighborIdsBuffer = VK_NULL_HANDLE;
    interfaceNeighborIdsBufferOffset = 0;
    seedFlagsBuffer = VK_NULL_HANDLE;
    seedFlagsBufferOffset = 0;
    receiverVoronoiNodeOffsetByModelId.clear();
    receiverVoronoiNodeCountByModelId.clear();
    receiverVoronoiSurfaceMappingBufferByModelId.clear();
    receiverVoronoiSurfaceMappingBufferOffsetByModelId.clear();
    receiverVoronoiSurfaceCellIndicesByModelId.clear();
    receiverVoronoiSeedFlagsByModelId.clear();
    voronoiConfigDirty = true;
}

void HeatSystem::setVoronoiBuffers(
    uint32_t nodeCount,
    const VoronoiNode* inputVoronoiNodes,
    VkBuffer inputNodeBuffer,
    VkDeviceSize inputNodeBufferOffset,
    VkBuffer inputVoronoiNeighborBuffer,
    VkDeviceSize inputVoronoiNeighborBufferOffset,
    VkBuffer inputNeighborIndicesBuffer,
    VkDeviceSize inputNeighborIndicesBufferOffset,
    VkBuffer inputInterfaceAreasBuffer,
    VkDeviceSize inputInterfaceAreasBufferOffset,
    VkBuffer inputInterfaceNeighborIdsBuffer,
    VkDeviceSize inputInterfaceNeighborIdsBufferOffset,
    VkBuffer inputSeedFlagsBuffer,
    VkDeviceSize inputSeedFlagsBufferOffset) {
    voronoiNodeCount = nodeCount;
    voronoiNodes = inputVoronoiNodes;
    voronoiNodeBuffer = inputNodeBuffer;
    voronoiNodeBufferOffset = inputNodeBufferOffset;
    voronoiNeighborBuffer = inputVoronoiNeighborBuffer;
    voronoiNeighborBufferOffset = inputVoronoiNeighborBufferOffset;
    neighborIndicesBuffer = inputNeighborIndicesBuffer;
    neighborIndicesBufferOffset = inputNeighborIndicesBufferOffset;
    interfaceAreasBuffer = inputInterfaceAreasBuffer;
    interfaceAreasBufferOffset = inputInterfaceAreasBufferOffset;
    interfaceNeighborIdsBuffer = inputInterfaceNeighborIdsBuffer;
    interfaceNeighborIdsBufferOffset = inputInterfaceNeighborIdsBufferOffset;
    seedFlagsBuffer = inputSeedFlagsBuffer;
    seedFlagsBufferOffset = inputSeedFlagsBufferOffset;
    voronoiConfigDirty = true;
}

void HeatSystem::addVoronoiReceiverInput(
    uint32_t runtimeModelId,
    uint32_t nodeOffset,
    uint32_t nodeCount,
    VkBuffer surfaceMappingBuffer,
    VkDeviceSize surfaceMappingBufferOffset,
    const std::vector<uint32_t>& surfaceCellIndices,
    const std::vector<uint32_t>& seedFlags) {
    if (runtimeModelId == 0) {
        return;
    }

    receiverVoronoiNodeOffsetByModelId[runtimeModelId] = nodeOffset;
    receiverVoronoiNodeCountByModelId[runtimeModelId] = nodeCount;
    receiverVoronoiSurfaceMappingBufferByModelId[runtimeModelId] = surfaceMappingBuffer;
    receiverVoronoiSurfaceMappingBufferOffsetByModelId[runtimeModelId] = surfaceMappingBufferOffset;
    receiverVoronoiSurfaceCellIndicesByModelId[runtimeModelId] = surfaceCellIndices;
    receiverVoronoiSeedFlagsByModelId[runtimeModelId] = seedFlags;
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

    for (SourceBinding& sourceBinding : heatSources) {
        if (!sourceBinding.heatSource || sourceBinding.runtimeModelId == 0) {
            continue;
        }

        glm::mat4 modelMatrix(1.0f);
        if (!resourceManager.tryGetModelMatrix(sourceBinding.runtimeModelId, modelMatrix)) {
            continue;
        }

        sourceBinding.heatSource->setHeatSourcePushConstant(modelMatrix);
    }
}

void HeatSystem::ensureConfigured() {
    const bool needsHardRebuild =
        runtime.needsRebuild() ||
        surfaceRuntime.needsRebuild() ||
        heatContactRuntime.needsRebuild() ||
        voronoiConfigDirty ||
        thermalMaterialsDirty;

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
            receiverVoronoiSurfaceCellIndicesByModelId,
            receiverVoronoiSurfaceMappingBufferByModelId,
            receiverVoronoiSurfaceMappingBufferOffsetByModelId)) {
        std::cerr << "[HeatSystem] rebuildHeatStateRuntimes: ensureCouplings FAILED" << std::endl;
        return false;
    }

    const bool heatVoronoiReady = rebuildVoronoiRuntime();
    for (const auto& receiver : surfaceRuntime.getReceivers()) {
        if (!receiver) {
            continue;
        }

        const uint32_t runtimeModelId = receiver->getRuntimeModelId();
        const auto bufferIt = receiverVoronoiSurfaceMappingBufferByModelId.find(runtimeModelId);
        const auto offsetIt = receiverVoronoiSurfaceMappingBufferOffsetByModelId.find(runtimeModelId);
        if (!heatVoronoiReady ||
            bufferIt == receiverVoronoiSurfaceMappingBufferByModelId.end() ||
            offsetIt == receiverVoronoiSurfaceMappingBufferOffsetByModelId.end()) {
            receiver->setVoronoiMapping(VK_NULL_HANDLE, 0);
            continue;
        }

        receiver->setVoronoiMapping(bufferIt->second, offsetIt->second);
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
        simRuntime.initialize(vulkanDevice, memoryAllocator, resources.voronoi.voronoiNodeCount) &&
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

    if (contactStage && simRuntime.isInitialized()) {
        for (HeatContactRuntime::CouplingState& coupling : heatContactRuntime.getCouplingsMutable()) {
            contactStage->updateCouplingDescriptors(
                coupling,
                simRuntime);
        }
    }

    voronoiConfigDirty = false;
    thermalMaterialsDirty = false;
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
        resources.voronoi.voronoiNodeCount > 0 &&
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
        contactStage &&
        simStage &&
        surfaceStage &&
        voronoiStage) {
        std::cerr << "[HeatSystem] recordComputeCommands dispatching"
                  << " couplings=" << heatContactRuntime.getCouplings().size()
                  << " nodeCount=" << simRuntime.getNodeCount()
                  << " isActive=" << (isActive ? "true" : "false")
                  << " isPaused=" << (isPaused ? "true" : "false")
                  << std::endl;
        HeatSourcePushConstant basePushConstant{};
        basePushConstant.heatSourceModelMatrix = glm::mat4(1.0f);
        basePushConstant.visModelMatrix = glm::mat4(1.0f);
        basePushConstant.inverseHeatSourceModelMatrix = glm::mat4(1.0f);
        basePushConstant.maxNodeNeighbors = MAX_NODE_NEIGHBORS;
        basePushConstant.substepIndex = 0;

        if (const SourceBinding* baseSource = runtime.findBaseSourceBinding();
            baseSource && baseSource->heatSource) {
            basePushConstant = baseSource->heatSource->getHeatSourcePushConstant();
            basePushConstant.maxNodeNeighbors = MAX_NODE_NEIGHBORS;
        }

        if (timingQueryPool != VK_NULL_HANDLE) {
            vkCmdResetQueryPool(commandBuffer, timingQueryPool, timingQueryBase, 2);
            vkCmdWriteTimestamp(
                commandBuffer,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                timingQueryPool,
                timingQueryBase);
        }

        simStage->recordComputeCommands(
            commandBuffer,
            currentFrame,
            simRuntime,
            basePushConstant,
            heatContactRuntime.getCouplings(),
            heatSources,
            surfaceRuntime.getReceivers(),
            *contactStage,
            *voronoiStage,
            *surfaceStage,
            MAX_NODE_NEIGHBORS,
            NUM_SUBSTEPS);

        if (timingQueryPool != VK_NULL_HANDLE) {
            vkCmdWriteTimestamp(
                commandBuffer,
                VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                timingQueryPool,
                timingQueryBase + 1);
        }
    } else {
        std::cerr << "[HeatSystem] recordComputeCommands skipped"
                  << " hasDispatchableComputeWork=" << (hasDispatchableComputeWork() ? "true" : "false")
                  << " contactStage=" << (contactStage ? "yes" : "no")
                  << " simStage=" << (simStage ? "yes" : "no")
                  << " surfaceStage=" << (surfaceStage ? "yes" : "no")
                  << " voronoiStage=" << (voronoiStage ? "yes" : "no")
                  << std::endl;
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
}

void HeatSystem::cleanup() {
    heatContactRuntime.clearCouplings(memoryAllocator);
    surfaceRuntime.cleanup();
    cleanupVoronoiRuntime();
    simRuntime.cleanup(memoryAllocator);
    runtime.cleanupModelBindings();
}

bool HeatSystem::initializeVoronoiMaterialNodes() {
    if (resources.voronoi.voronoiNodeCount == 0) {
        return false;
    }

    const VoronoiNode* voronoiNodes = static_cast<const VoronoiNode*>(resources.voronoi.mappedVoronoiNodeData);
    if (!voronoiNodes) {
        return false;
    }

    std::vector<VoronoiMaterialNode> materialNodes(resources.voronoi.voronoiNodeCount);
    for (VoronoiMaterialNode& materialNode : materialNodes) {
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

            VoronoiMaterialNode& materialNode = materialNodes[nodeIndex];
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
               sizeof(VoronoiMaterialNode) * materialNodes.size(),
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
    resources.voronoi.voronoiNodeBuffer = VK_NULL_HANDLE;
    resources.voronoi.voronoiNodeBufferOffset = 0;
    resources.voronoi.mappedVoronoiNodeData = nullptr;
    resources.voronoi.voronoiNeighborBuffer = VK_NULL_HANDLE;
    resources.voronoi.voronoiNeighborBufferOffset = 0;
    resources.voronoi.neighborIndicesBuffer = VK_NULL_HANDLE;
    resources.voronoi.neighborIndicesBufferOffset = 0;
    resources.voronoi.interfaceAreasBuffer = VK_NULL_HANDLE;
    resources.voronoi.interfaceAreasBufferOffset = 0;
    resources.voronoi.mappedInterfaceAreasData = nullptr;
    resources.voronoi.interfaceNeighborIdsBuffer = VK_NULL_HANDLE;
    resources.voronoi.interfaceNeighborIdsBufferOffset = 0;
    resources.voronoi.mappedInterfaceNeighborIdsData = nullptr;
    resources.voronoi.meshTriangleBuffer = VK_NULL_HANDLE;
    resources.voronoi.meshTriangleBufferOffset = 0;
    resources.voronoi.seedPositionBuffer = VK_NULL_HANDLE;
    resources.voronoi.seedPositionBufferOffset = 0;
    resources.voronoi.mappedSeedPositionData = nullptr;
    resources.voronoi.seedFlagsBuffer = VK_NULL_HANDLE;
    resources.voronoi.seedFlagsBufferOffset = 0;
    resources.voronoi.mappedSeedFlagsData = nullptr;
    resources.voronoi.debugCellGeometryBuffer = VK_NULL_HANDLE;
    resources.voronoi.debugCellGeometryBufferOffset = 0;
    resources.voronoi.mappedDebugCellGeometryData = nullptr;
    resources.voronoi.voronoiDumpBuffer = VK_NULL_HANDLE;
    resources.voronoi.voronoiDumpBufferOffset = 0;
    resources.voronoi.mappedVoronoiDumpData = nullptr;
    resources.voronoi.voxelGridParamsBuffer = VK_NULL_HANDLE;
    resources.voronoi.voxelGridParamsBufferOffset = 0;
    resources.voronoi.voxelOccupancyBuffer = VK_NULL_HANDLE;
    resources.voronoi.voxelOccupancyBufferOffset = 0;
    resources.voronoi.voxelTrianglesListBuffer = VK_NULL_HANDLE;
    resources.voronoi.voxelTrianglesListBufferOffset = 0;
    resources.voronoi.voxelOffsetsBuffer = VK_NULL_HANDLE;
    resources.voronoi.voxelOffsetsBufferOffset = 0;
    if (resources.voronoiMaterialNodeBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(resources.voronoiMaterialNodeBuffer, resources.voronoiMaterialNodeBufferOffset);
        resources.voronoiMaterialNodeBuffer = VK_NULL_HANDLE;
        resources.voronoiMaterialNodeBufferOffset = 0;
    }
    resources.mappedVoronoiMaterialNodeData = nullptr;
    resources.voronoi.voronoiNodeCount = 0;
    receiverThermalMaterialByModelId.clear();
}

bool HeatSystem::rebuildVoronoiRuntime() {
    cleanupVoronoiRuntime();

    if (voronoiNodeCount == 0 || receiverRuntimeModelIds.empty()) {
        return false;
    }

    resources.voronoi.voronoiNodeCount = voronoiNodeCount;
    resources.voronoi.voronoiNodeBuffer = voronoiNodeBuffer;
    resources.voronoi.voronoiNodeBufferOffset = voronoiNodeBufferOffset;
    resources.voronoi.mappedVoronoiNodeData = const_cast<VoronoiNode*>(voronoiNodes);
    resources.voronoi.voronoiNeighborBuffer = voronoiNeighborBuffer;
    resources.voronoi.voronoiNeighborBufferOffset = voronoiNeighborBufferOffset;
    resources.voronoi.neighborIndicesBuffer = neighborIndicesBuffer;
    resources.voronoi.neighborIndicesBufferOffset = neighborIndicesBufferOffset;
    resources.voronoi.interfaceAreasBuffer = interfaceAreasBuffer;
    resources.voronoi.interfaceAreasBufferOffset = interfaceAreasBufferOffset;
    resources.voronoi.interfaceNeighborIdsBuffer = interfaceNeighborIdsBuffer;
    resources.voronoi.interfaceNeighborIdsBufferOffset = interfaceNeighborIdsBufferOffset;
    resources.voronoi.seedFlagsBuffer = seedFlagsBuffer;
    resources.voronoi.seedFlagsBufferOffset = seedFlagsBufferOffset;

    if (receiverVoronoiNodeOffsetByModelId.empty()) {
        return false;
    }

    rebuildReceiverThermalMaterialMap();
    if (!initializeVoronoiMaterialNodes()) {
        return false;
    }
    return resources.voronoi.voronoiNodeCount > 0;
}

