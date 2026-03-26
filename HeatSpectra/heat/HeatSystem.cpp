#include "HeatSystem.hpp"

#include "HeatReceiverRuntime.hpp"
#include "HeatSourceRuntime.hpp"
#include "HeatSystemContactStage.hpp"
#include "HeatSystemRenderStage.hpp"
#include "HeatSystemResources.hpp"
#include "HeatSystemSimStage.hpp"
#include "HeatSystemSurfaceStage.hpp"
#include "HeatSystemStageContext.hpp"
#include "HeatSystemVoronoiStage.hpp"
#include "nodegraph/NodeModelTransform.hpp"
#include "renderers/HeatReceiverRenderer.hpp"
#include "renderers/HeatSourceRenderer.hpp"
#include "renderers/PointRenderer.hpp"
#include "runtime/ContactPreviewStore.hpp"
#include "runtime/RuntimeIntrinsicCache.hpp"
#include "scene/Model.hpp"
#include "vulkan/CommandBufferManager.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/ResourceManager.hpp"
#include "vulkan/UniformBufferManager.hpp"
#include "vulkan/VulkanBuffer.hpp"
#include "vulkan/VulkanDevice.hpp"
#include "voronoi/VoronoiBuilder.hpp"
#include "voronoi/VoronoiGeoCompute.hpp"
#include "voronoi/VoronoiModelRuntime.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <unordered_set>

bool HeatSystem::isSameContactPairData(const ContactPairData& lhs, const ContactPairData& rhs) {
    return lhs.hasValidContact == rhs.hasValidContact &&
        lhs.kind == rhs.kind &&
        lhs.endpointA.role == rhs.endpointA.role &&
        lhs.endpointA.payloadHandle.key == rhs.endpointA.payloadHandle.key &&
        lhs.endpointA.payloadHandle.revision == rhs.endpointA.payloadHandle.revision &&
        lhs.endpointA.payloadHandle.count == rhs.endpointA.payloadHandle.count &&
        lhs.endpointA.geometryHandle.key == rhs.endpointA.geometryHandle.key &&
        lhs.endpointA.geometryHandle.revision == rhs.endpointA.geometryHandle.revision &&
        lhs.endpointA.geometryHandle.count == rhs.endpointA.geometryHandle.count &&
        lhs.endpointB.role == rhs.endpointB.role &&
        lhs.endpointB.payloadHandle.key == rhs.endpointB.payloadHandle.key &&
        lhs.endpointB.payloadHandle.revision == rhs.endpointB.payloadHandle.revision &&
        lhs.endpointB.payloadHandle.count == rhs.endpointB.payloadHandle.count &&
        lhs.endpointB.geometryHandle.key == rhs.endpointB.geometryHandle.key &&
        lhs.endpointB.geometryHandle.revision == rhs.endpointB.geometryHandle.revision &&
        lhs.endpointB.geometryHandle.count == rhs.endpointB.geometryHandle.count &&
        lhs.minNormalDot == rhs.minNormalDot &&
        lhs.contactRadius == rhs.contactRadius;
}

bool HeatSystem::isSameRuntimeContactBinding(const RuntimeContactBinding& lhs, const RuntimeContactBinding& rhs) {
    return isSameContactPairData(lhs.contactPair, rhs.contactPair) &&
        lhs.emitterRuntimeModelId == rhs.emitterRuntimeModelId &&
        lhs.receiverRuntimeModelId == rhs.receiverRuntimeModelId;
}

bool HeatSystem::isSameContactPair(const ContactPair& lhs, const ContactPair& rhs) {
    return std::memcmp(lhs.samples, rhs.samples, sizeof(lhs.samples)) == 0 &&
        lhs.contactArea == rhs.contactArea;
}

bool HeatSystem::isSameRuntimeContactResult(const RuntimeContactResult& lhs, const RuntimeContactResult& rhs) {
    if (!isSameRuntimeContactBinding(lhs.binding, rhs.binding) ||
        lhs.contactPairsCPU.size() != rhs.contactPairsCPU.size()) {
        return false;
    }

    for (std::size_t index = 0; index < lhs.contactPairsCPU.size(); ++index) {
        if (!isSameContactPair(lhs.contactPairsCPU[index], rhs.contactPairsCPU[index])) {
            return false;
        }
    }

    return true;
}

bool HeatSystem::areRuntimeContactResultsEqual(const std::vector<RuntimeContactResult>& lhs, const std::vector<RuntimeContactResult>& rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        if (!isSameRuntimeContactResult(lhs[index], rhs[index])) {
            return false;
        }
    }
    return true;
}

bool HeatSystem::isSameRuntimeThermalMaterial(const RuntimeThermalMaterial& lhs, const RuntimeThermalMaterial& rhs) {
    return lhs.runtimeModelId == rhs.runtimeModelId &&
        lhs.density == rhs.density &&
        lhs.specificHeat == rhs.specificHeat &&
        lhs.conductivity == rhs.conductivity;
}

bool HeatSystem::areRuntimeThermalMaterialsEqual(
    const std::vector<RuntimeThermalMaterial>& lhs,
    const std::vector<RuntimeThermalMaterial>& rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }

    for (std::size_t index = 0; index < lhs.size(); ++index) {
        if (!isSameRuntimeThermalMaterial(lhs[index], rhs[index])) {
            return false;
        }
    }
    return true;
}

HeatSystem::HeatSystem(
    VulkanDevice& vulkanDevice,
    MemoryAllocator& memoryAllocator,
    ResourceManager& resourceManager,
    RuntimeIntrinsicCache& remeshResources,
    HeatSystemResources& resources,
    UniformBufferManager& uniformBufferManager,
    uint32_t maxFramesInFlight,
    CommandPool& renderCommandPool,
    VkExtent2D extent,
    VkRenderPass renderPass)
    : vulkanDevice(vulkanDevice),
      memoryAllocator(memoryAllocator),
      resourceManager(resourceManager),
      remeshResources(remeshResources),
      resources(resources),
      uniformBufferManager(uniformBufferManager),
      renderCommandPool(renderCommandPool),
      runtime(),
      heatSources(runtime.getSourceBindingsMutable()),
      voronoiBuilder(std::make_unique<VoronoiBuilder>(vulkanDevice, memoryAllocator, resources.voronoi)),
      maxFramesInFlight(maxFramesInFlight) {
    (void)extent;

    HeatSystemStageContext stageContext{
        vulkanDevice,
        memoryAllocator,
        resourceManager,
        uniformBufferManager,
        renderCommandPool,
        resources
    };

    contactStage = std::make_unique<HeatSystemContactStage>(stageContext);
    simStage = std::make_unique<HeatSystemSimStage>(stageContext);
    surfaceStage = std::make_unique<HeatSystemSurfaceStage>(stageContext);
    voronoiStage = std::make_unique<HeatSystemVoronoiStage>(stageContext);
    renderStage = std::make_unique<HeatSystemRenderStage>(stageContext);
    voronoiGeoCompute = std::make_unique<VoronoiGeoCompute>(vulkanDevice, renderCommandPool);

    if (!createContactDescriptorPool(maxFramesInFlight) ||
        !createContactDescriptorSetLayout() ||
        !createContactPipeline() ||
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

    heatSourceRenderer = std::make_unique<HeatSourceRenderer>(vulkanDevice, uniformBufferManager);
    if (!heatSourceRenderer) {
        failInitialization("create HeatSourceRenderer");
        return;
    }

    heatReceiverRenderer = std::make_unique<HeatReceiverRenderer>(vulkanDevice, uniformBufferManager);
    if (!heatReceiverRenderer) {
        failInitialization("create HeatReceiverRenderer");
        return;
    }

    heatSourceRenderer->initialize(renderPass);
    heatReceiverRenderer->initialize(renderPass, maxFramesInFlight);

    pointRenderer = std::make_unique<PointRenderer>(vulkanDevice, memoryAllocator, uniformBufferManager);
    if (!pointRenderer) {
        failInitialization("create PointRenderer");
        return;
    }
    pointRenderer->initialize(renderPass, 2, maxFramesInFlight);

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

void HeatSystem::setContactPreviewStore(ContactPreviewStore* store) {
    contactPreviewStore = store;
}

void HeatSystem::setHeatPackage(const HeatPackage* package) {
    heatPackage = package;
    rebuildHeatContactCouplings();
}

void HeatSystem::configureSourceRuntimes(const HeatPackage& heatPackage) {
    runtime.initializeModelBindings(
        vulkanDevice,
        memoryAllocator,
        renderCommandPool,
        heatPackage);
    rebuildHeatContactCouplings();
}

void HeatSystem::setResolvedContacts(const std::vector<RuntimeContactResult>& contacts) {
    if (!areRuntimeContactResultsEqual(resolvedContacts, contacts)) {
        resolvedContacts = contacts;
        markHeatStructureDirty();
    }

    rebuildHeatContactCouplings();
}

void HeatSystem::setThermalMaterials(const std::vector<RuntimeThermalMaterial>& materials) {
    if (areRuntimeThermalMaterialsEqual(configuredThermalMaterials, materials)) {
        return;
    }

    configuredThermalMaterials = materials;
    markHeatStructureDirty();
}

void HeatSystem::setSourceTemperatures(const std::unordered_map<uint32_t, float>& temperatures) {
    sourceTemperatureByModelId = temperatures;
    for (SourceBinding& sourceBinding : heatSources) {
        if (!sourceBinding.heatSource) {
            continue;
        }
        const auto tempIt = sourceTemperatureByModelId.find(sourceBinding.geometryPackage.runtimeModelId);
        const float temperature = (tempIt != sourceTemperatureByModelId.end()) ? tempIt->second : 100.0f;
        sourceBinding.heatSource->setUniformTemperature(temperature);
    }
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
        if (!sourceBinding.heatSource || sourceBinding.geometryPackage.runtimeModelId == 0) {
            continue;
        }

        sourceBinding.heatSource->setHeatSourcePushConstant(
            NodeModelTransform::toMat4(sourceBinding.geometryPackage.geometry.localToWorld));
    }
}

void HeatSystem::recreateResources(uint32_t maxFramesInFlight, VkExtent2D extent, VkRenderPass renderPass) {
    (void)extent;
    this->maxFramesInFlight = maxFramesInFlight;

    if (!heatSourceRenderer) {
        heatSourceRenderer = std::make_unique<HeatSourceRenderer>(vulkanDevice, uniformBufferManager);
    }
    if (!heatReceiverRenderer) {
        heatReceiverRenderer = std::make_unique<HeatReceiverRenderer>(vulkanDevice, uniformBufferManager);
    }
    if (!pointRenderer) {
        pointRenderer = std::make_unique<PointRenderer>(vulkanDevice, memoryAllocator, uniformBufferManager);
    }
    if (heatSourceRenderer) {
        heatSourceRenderer->initialize(renderPass);
    }
    if (heatReceiverRenderer) {
        heatReceiverRenderer->initialize(renderPass, maxFramesInFlight);
        heatReceiverRenderer->updateDescriptors(surfaceRuntime.getReceivers(), resourceManager, maxFramesInFlight, true);
    }
    if (pointRenderer) {
        pointRenderer->initialize(renderPass, 2, maxFramesInFlight);
    }

    if (contactPreviewStore) {
        contactPreviewStore->reinitRenderer(renderPass, maxFramesInFlight);
    }

    if (resources.surfacePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(vulkanDevice.getDevice(), resources.surfacePipeline, nullptr);
        resources.surfacePipeline = VK_NULL_HANDLE;
    }
    if (resources.surfacePipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vulkanDevice.getDevice(), resources.surfacePipelineLayout, nullptr);
        resources.surfacePipelineLayout = VK_NULL_HANDLE;
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
    if (resources.voronoiDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vulkanDevice.getDevice(), resources.voronoiDescriptorSetLayout, nullptr);
        resources.voronoiDescriptorSetLayout = VK_NULL_HANDLE;
    }

    if (surfaceStage) {
        surfaceStage->createDescriptorSetLayout();
        surfaceStage->createPipeline();
    }
    if (voronoiStage) {
        voronoiStage->createDescriptorSetLayout();
        voronoiStage->createPipeline();
    }

    createComputeCommandBuffers(maxFramesInFlight);
    if (simRuntime.isInitialized()) {
        rebuildHeatStateRuntimes(true);
    }
}

void HeatSystem::processResetRequest() {
    processHeatReset();
}

void HeatSystem::processHeatReset() {
    if (!needsReset.exchange(false, std::memory_order_acq_rel)) {
        return;
    }

    vkDeviceWaitIdle(vulkanDevice.getDevice());
    rebuildHeatStateRuntimes(true);
    resetHeatState();
    heatStructureDirty = false;
}

void HeatSystem::requestHeatReset() {
    needsReset.store(true, std::memory_order_release);
}

void HeatSystem::markHeatStructureDirty() {
    heatStructureDirty = true;
    if (isActive) {
        requestHeatReset();
    }
}

void HeatSystem::rebuildHeatContactCouplings() {
    heatContactRuntime.rebuildCouplings(
        memoryAllocator,
        heatPackage ? heatPackage->receiverRuntimeModelIds : std::vector<uint32_t>{},
        resolvedContacts,
        heatSources);
}

bool HeatSystem::rebuildHeatStateRuntimes(bool forceDescriptorReallocate) {
    if (!heatPackage) {
        return false;
    }

    const bool heatVoronoiReady = rebuildVoronoiRuntime();
    if (heatPackage->voronoiActive && !heatVoronoiReady) {
        return false;
    }

    const bool simReady =
        (resources.voronoi.voronoiNodeCount == 0
             ? (simRuntime.cleanup(memoryAllocator), resources.voronoiDescriptorSets.clear(), resources.voronoiDescriptorSetsB.clear(), true)
             :
        (simRuntime.initialize(vulkanDevice, memoryAllocator, resources.voronoi.voronoiNodeCount) &&
         voronoiStage &&
         voronoiStage->createDescriptorSets(maxFramesInFlight, simRuntime)));
    if (!simReady) {
        return false;
    }

    const bool receiversReady = surfaceRuntime.initializeReceiverBindings(
        vulkanDevice,
        memoryAllocator,
        remeshResources,
        *heatPackage,
        resources.voronoi.voronoiNodeCount > 0 ? &voronoiModelRuntimes : nullptr);

    if (receiversReady) {
        surfaceRuntime.executeBufferTransfers(renderCommandPool);
    }

    if (receiversReady &&
        simRuntime.isInitialized() &&
        resources.surfaceDescriptorSetLayout != VK_NULL_HANDLE &&
        resources.surfaceDescriptorPool != VK_NULL_HANDLE) {
        surfaceRuntime.refreshDescriptors(
            simRuntime,
            resources.surfaceDescriptorSetLayout,
            resources.surfaceDescriptorPool,
            forceDescriptorReallocate);
    }

    if (heatReceiverRenderer) {
        heatReceiverRenderer->updateDescriptors(surfaceRuntime.getReceivers(), resourceManager, maxFramesInFlight, forceDescriptorReallocate);
    }

    if (contactStage && heatPackage && simRuntime.isInitialized()) {
        for (ContactCoupling& coupling : heatContactRuntime.getCouplingsMutable()) {
            contactStage->updateCouplingDescriptors(
                coupling,
                simRuntime,
                *heatPackage,
                surfaceRuntime.getReceivers(),
                voronoiModelRuntimes);
        }
    }

    return receiversReady;
}

void HeatSystem::setActive(bool active) {
    if (active && !isActive) {
        vkDeviceWaitIdle(vulkanDevice.getDevice());
    }

    if (active) {
        if (heatStructureDirty) {
            requestHeatReset();
        }
        processHeatReset();
    }
    isActive = active;
}

void HeatSystem::resetHeatState() {
    simRuntime.reset();
    surfaceRuntime.resetSurfaceTemperatures(renderCommandPool);
}

void HeatSystem::renderContactLines(VkCommandBuffer cmdBuffer, uint32_t frameIndex, VkExtent2D extent) {
    if (contactPreviewStore) {
        contactPreviewStore->renderLines(cmdBuffer, frameIndex, extent);
    }
}

bool HeatSystem::createContactDescriptorPool(uint32_t maxFramesInFlight) {
    return contactStage && contactStage->createDescriptorPool(maxFramesInFlight);
}

bool HeatSystem::createContactDescriptorSetLayout() {
    return contactStage && contactStage->createDescriptorSetLayout();
}

bool HeatSystem::createContactPipeline() {
    return contactStage && contactStage->createPipeline();
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
    return heatPackage &&
        heatPackage->voronoiActive &&
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
        heatPackage &&
        contactStage &&
        simStage &&
        surfaceStage &&
        voronoiStage) {
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
            surfaceRuntime.getReceivers(),
            *heatPackage,
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
    }

    vkEndCommandBuffer(commandBuffer);
}

void HeatSystem::renderHeatOverlay(VkCommandBuffer cmdBuffer, uint32_t frameIndex) {
    if (!renderStage) {
        return;
    }
    renderStage->renderHeatOverlay(cmdBuffer, frameIndex, heatSourceRenderer.get(), heatReceiverRenderer.get(), heatSources, surfaceRuntime.getReceivers(), isActive, isPaused);
}

void HeatSystem::renderSurfels(VkCommandBuffer cmdBuffer, uint32_t frameIndex, const glm::mat4& heatSourceModel, float radius) {
    (void)cmdBuffer;
    (void)frameIndex;
    (void)heatSourceModel;
    (void)radius;
}

void HeatSystem::cleanupResources() {
    if (heatSourceRenderer) {
        heatSourceRenderer->cleanup();
    }
    if (heatReceiverRenderer) {
        heatReceiverRenderer->cleanup();
    }
    if (pointRenderer) {
        pointRenderer->cleanup();
    }

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

    if (voronoiGeoCompute) {
        voronoiGeoCompute->cleanupResources();
    }
}

void HeatSystem::cleanup() {
    heatContactRuntime.clearCouplings(memoryAllocator);
    surfaceRuntime.cleanup();
    cleanupVoronoiRuntime();
    simRuntime.cleanup(memoryAllocator);
    runtime.cleanupModelBindings();
}

bool HeatSystem::initializeVoronoiReceiverRuntimes() {
    voronoiModelRuntimes.clear();
    if (!heatPackage) {
        return false;
    }

    std::unordered_set<uint32_t> seenReceiverIds;
    for (std::size_t index = 0; index < heatPackage->receiverGeometries.size(); ++index) {
        const GeometryData& geometry = heatPackage->receiverGeometries[index];
        const uint32_t runtimeModelId = heatPackage->receiverRuntimeModelIds[index];
        if (runtimeModelId == 0 ||
            geometry.intrinsicHandle.key == 0 ||
            !seenReceiverIds.insert(runtimeModelId).second) {
            continue;
        }

        Model* receiverModel = resourceManager.getModelByID(runtimeModelId);
        if (!receiverModel) {
            continue;
        }

        const RuntimeIntrinsicCache::Entry* intrinsicEntry = remeshResources.get(geometry.intrinsicHandle);
        if (!intrinsicEntry) {
            continue;
        }

        auto modelRuntime = std::make_unique<VoronoiModelRuntime>(
            vulkanDevice,
            memoryAllocator,
            *receiverModel,
            geometry,
            heatPackage->receiverIntrinsics[index],
            *intrinsicEntry,
            renderCommandPool);
        if (!modelRuntime->createVoronoiBuffers()) {
            modelRuntime->cleanup();
            return false;
        }

        voronoiModelRuntimes.push_back(std::move(modelRuntime));
    }

    return !voronoiModelRuntimes.empty();
}

bool HeatSystem::initializeVoronoiMaterialNodes() {
    if (!heatPackage || resources.voronoi.voronoiNodeCount == 0) {
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
    for (const VoronoiDomain& domain : receiverVoronoiDomains) {
        RuntimeThermalMaterial material = defaultMaterial;
        const auto materialIt = receiverThermalMaterialByModelId.find(domain.receiverModelId);
        if (materialIt != receiverThermalMaterialByModelId.end()) {
            material = materialIt->second;
        }

        for (uint32_t localNodeIndex = 0; localNodeIndex < domain.nodeCount; ++localNodeIndex) {
            const uint32_t nodeIndex = domain.nodeOffset + localNodeIndex;
            if (nodeIndex >= materialNodes.size()) {
                continue;
            }

            VoronoiMaterialNode& materialNode = materialNodes[nodeIndex];
            if ((domain.seedFlags[localNodeIndex] & 1u) != 0u) {
                continue;
            }

            materialNode.density = material.density;
            materialNode.specificHeat = material.specificHeat;
            materialNode.conductivity = material.conductivity;
            materialNode.thermalMass = material.density * material.specificHeat * std::abs(voronoiNodes[nodeIndex].volume);
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

void HeatSystem::uploadVoronoiModelStagingBuffers() {
    if (voronoiModelRuntimes.empty()) {
        return;
    }

    VkCommandBuffer copyCmd = renderCommandPool.beginCommands();
    if (copyCmd == VK_NULL_HANDLE) {
        return;
    }

    bool issuedTransfers = false;
    for (auto& modelRuntime : voronoiModelRuntimes) {
        if (!modelRuntime) {
            continue;
        }

        modelRuntime->executeBufferTransfers(copyCmd);
        issuedTransfers = true;
    }

    if (issuedTransfers) {
        VkMemoryBarrier memBarrier{};
        memBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        memBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        memBarrier.dstAccessMask =
            VK_ACCESS_SHADER_READ_BIT |
            VK_ACCESS_SHADER_WRITE_BIT |
            VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;

        vkCmdPipelineBarrier(
            copyCmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                VK_PIPELINE_STAGE_VERTEX_INPUT_BIT |
                VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT |
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,
            1,
            &memBarrier,
            0,
            nullptr,
            0,
            nullptr);
    }

    renderCommandPool.endCommands(copyCmd);

    for (auto& modelRuntime : voronoiModelRuntimes) {
        if (modelRuntime) {
            modelRuntime->cleanupStagingBuffers();
        }
    }
}

void HeatSystem::rebuildReceiverThermalMaterialMap() {
    receiverThermalMaterialByModelId.clear();
    if (!heatPackage) {
        return;
    }

    for (const RuntimeThermalMaterial& material : heatPackage->runtimeThermalMaterials) {
        if (material.runtimeModelId == 0) {
            continue;
        }
        receiverThermalMaterialByModelId[material.runtimeModelId] = material;
    }
}

void HeatSystem::cleanupVoronoiRuntime() {
    auto freeBuffer = [this](VkBuffer& buffer, VkDeviceSize& offset) {
        if (buffer != VK_NULL_HANDLE) {
            memoryAllocator.free(buffer, offset);
            buffer = VK_NULL_HANDLE;
            offset = 0;
        }
    };

    freeBuffer(resources.voronoi.voronoiNodeBuffer, resources.voronoi.voronoiNodeBufferOffset);
    resources.voronoi.mappedVoronoiNodeData = nullptr;
    freeBuffer(resources.voronoi.voronoiNeighborBuffer, resources.voronoi.voronoiNeighborBufferOffset);
    freeBuffer(resources.voronoi.neighborIndicesBuffer, resources.voronoi.neighborIndicesBufferOffset);
    freeBuffer(resources.voronoi.interfaceAreasBuffer, resources.voronoi.interfaceAreasBufferOffset);
    resources.voronoi.mappedInterfaceAreasData = nullptr;
    freeBuffer(resources.voronoi.interfaceNeighborIdsBuffer, resources.voronoi.interfaceNeighborIdsBufferOffset);
    resources.voronoi.mappedInterfaceNeighborIdsData = nullptr;
    freeBuffer(resources.voronoi.meshTriangleBuffer, resources.voronoi.meshTriangleBufferOffset);
    freeBuffer(resources.voronoi.seedPositionBuffer, resources.voronoi.seedPositionBufferOffset);
    resources.voronoi.mappedSeedPositionData = nullptr;
    freeBuffer(resources.voronoi.seedFlagsBuffer, resources.voronoi.seedFlagsBufferOffset);
    resources.voronoi.mappedSeedFlagsData = nullptr;
    freeBuffer(resources.voronoi.debugCellGeometryBuffer, resources.voronoi.debugCellGeometryBufferOffset);
    resources.voronoi.mappedDebugCellGeometryData = nullptr;
    freeBuffer(resources.voronoi.voronoiDumpBuffer, resources.voronoi.voronoiDumpBufferOffset);
    resources.voronoi.mappedVoronoiDumpData = nullptr;
    freeBuffer(resources.voronoi.voxelGridParamsBuffer, resources.voronoi.voxelGridParamsBufferOffset);
    freeBuffer(resources.voronoi.voxelOccupancyBuffer, resources.voronoi.voxelOccupancyBufferOffset);
    freeBuffer(resources.voronoi.voxelTrianglesListBuffer, resources.voronoi.voxelTrianglesListBufferOffset);
    freeBuffer(resources.voronoi.voxelOffsetsBuffer, resources.voronoi.voxelOffsetsBufferOffset);
    freeBuffer(resources.voronoiMaterialNodeBuffer, resources.voronoiMaterialNodeBufferOffset);
    resources.mappedVoronoiMaterialNodeData = nullptr;
    resources.voronoi.voronoiNodeCount = 0;

    receiverVoronoiDomains.clear();
    receiverThermalMaterialByModelId.clear();

    for (auto& modelRuntime : voronoiModelRuntimes) {
        if (modelRuntime) {
            modelRuntime->cleanup();
        }
    }
    voronoiModelRuntimes.clear();
}

bool HeatSystem::rebuildVoronoiRuntime() {
    cleanupVoronoiRuntime();

    if (!heatPackage || !heatPackage->voronoiActive || heatPackage->receiverRuntimeModelIds.empty()) {
        return false;
    }
    if (!voronoiBuilder || !voronoiGeoCompute) {
        return false;
    }
    if (!initializeVoronoiReceiverRuntimes()) {
        return false;
    }

    rebuildReceiverThermalMaterialMap();
    if (!voronoiBuilder->buildDomains(
            voronoiModelRuntimes,
            receiverVoronoiDomains,
            heatPackage->voronoiParams,
            MAX_NODE_NEIGHBORS)) {
        return false;
    }
    if (!voronoiBuilder->generateDiagram(
            receiverVoronoiDomains,
            debugEnable,
            MAX_NODE_NEIGHBORS,
            voronoiGeoCompute.get(),
            pointRenderer.get())) {
        return false;
    }
    if (!initializeVoronoiMaterialNodes()) {
        return false;
    }
    if (!voronoiBuilder->stageSurfaceMappings(receiverVoronoiDomains, MAX_NODE_NEIGHBORS)) {
        return false;
    }

    uploadVoronoiModelStagingBuffers();

    return resources.voronoi.voronoiNodeCount > 0;
}
