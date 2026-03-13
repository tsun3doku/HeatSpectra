#include <vulkan/vulkan.h>
#include <omp.h>
#include <unordered_set>
#include <memory>

#include "vulkan/VulkanDevice.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/ResourceManager.hpp"
#include "mesh/remesher/Remesher.hpp"
#include "vulkan/VulkanImage.hpp"
#include "vulkan/VulkanBuffer.hpp"
#include "vulkan/CommandBufferManager.hpp"
#include "vulkan/UniformBufferManager.hpp"
#include "scene/Camera.hpp"
#include "scene/Model.hpp"
#include "util/predicates.h"
#include "HeatSource.hpp"
#include "HeatReceiver.hpp"
#include "renderers/SurfelRenderer.hpp"
#include "renderers/VoronoiRenderer.hpp"
#include "renderers/PointRenderer.hpp"
#include "renderers/HeatRenderer.hpp"
#include "ContactSystemController.hpp"
#include "HeatSystem.hpp"
#include "HeatSystemStageContext.hpp"
#include "HeatSystemVoronoiStage.hpp"
#include "HeatSystemContactStage.hpp"
#include "HeatSystemSurfaceStage.hpp"
#include "HeatSystemDebugStage.hpp"
#include "HeatSystemRenderStage.hpp"
#include "voronoi/VoronoiGeoCompute.hpp"
#include "voronoi/VoronoiCandidateCompute.hpp"
#include "voronoi/VoronoiIntegrator.hpp"

#include <iostream>
#include <fstream>
#include <cstring>
#include <numeric>
#include <iomanip>
#include <cmath>
#include <cfloat>
#include <array>

#include <algorithm>

#include <limits>

HeatSystem::HeatSystem(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, ResourceManager& resourceManager, Remesher& remesher,
    UniformBufferManager& uniformBufferManager, uint32_t maxFramesInFlight, CommandPool& cmdPool, VkExtent2D extent, VkRenderPass renderPass)
    : vulkanDevice(vulkanDevice), memoryAllocator(memoryAllocator), resourceManager(resourceManager), remesher(remesher),
      uniformBufferManager(uniformBufferManager), renderCommandPool(cmdPool), runtime(),
      heatSources(runtime.getSourceCouplingsMutable()), receivers(runtime.getReceiversMutable()),
      receiverModelIds(runtime.getReceiverModelIdsMutable()),
      maxFramesInFlight(maxFramesInFlight) {
    (void)extent;

    HeatSystemStageContext stageContext{
        vulkanDevice,
        memoryAllocator,
        resourceManager,
        uniformBufferManager,
        renderCommandPool,
        remesher,
        runtime,
        resources
    };

    voronoiStage = std::make_unique<HeatSystemVoronoiStage>(stageContext);
    contactStage = std::make_unique<HeatSystemContactStage>(stageContext);
    surfaceStage = std::make_unique<HeatSystemSurfaceStage>(stageContext);
    debugStage = std::make_unique<HeatSystemDebugStage>(stageContext);
    renderStage = std::make_unique<HeatSystemRenderStage>(stageContext);

    if (!createTimeBuffer()) {
        failInitialization("create time buffer");
        return;
    }
    if (!createSurfaceDescriptorPool(maxFramesInFlight) ||
        !createSurfaceDescriptorSetLayout() ||
        !createSurfacePipeline()) {
        failInitialization("create surface compute resources");
        return;
    }

    if (!createContactDescriptorPool(maxFramesInFlight) ||
        !createContactDescriptorSetLayout() ||
        !createContactPipeline()) {
        failInitialization("create contact compute resources");
        return;
    }

    heatRenderer = std::make_unique<HeatRenderer>(vulkanDevice, uniformBufferManager);
    if (!heatRenderer) {
        failInitialization("create HeatRenderer");
        return;
    }

    heatRenderer->initialize(renderPass, maxFramesInFlight);
    initializeSurfelRenderers(renderPass, maxFramesInFlight);
    initializeVoronoiRenderer(renderPass, maxFramesInFlight);
    initializePointRenderer(renderPass, maxFramesInFlight);

    initializeHeatModelBindings();

    initializeVoronoiGeoCompute();
    initializeVoronoiCandidateCompute();

    if (!createComputeCommandBuffers(maxFramesInFlight)) {
        failInitialization("allocate compute command buffers");
        return;
    }

    initialized = true;
}

bool HeatSystem::createContactDescriptorPool(uint32_t maxFramesInFlight) {
    if (!contactStage) {
        return false;
    }

    return contactStage->createDescriptorPool(maxFramesInFlight);
}

bool HeatSystem::createContactDescriptorSetLayout() {
    if (!contactStage) {
        return false;
    }

    return contactStage->createDescriptorSetLayout();
}

bool HeatSystem::createContactPipeline() {
    if (!contactStage) {
        return false;
    }

    return contactStage->createPipeline();
}

HeatSystem::~HeatSystem() {
}

void HeatSystem::failInitialization(const char* stage) {
    std::cerr << "[HeatSystem] Initialization failed at stage: " << stage << std::endl;
    cleanupResources();
    cleanup();
}

const HeatSystemVoronoiDomain* HeatSystem::findReceiverDomain(const HeatReceiver* receiver) const {
    if (!receiver) {
        return nullptr;
    }
    return findReceiverDomainByModelId(receiver->getModel().getRuntimeModelId());
}

const HeatSystemVoronoiDomain* HeatSystem::findReceiverDomainByModelId(uint32_t receiverModelId) const {
    if (receiverModelId == 0) {
        return nullptr;
    }

    for (const HeatSystemVoronoiDomain& domain : receiverVoronoiDomains) {
        if (domain.receiverModelId == receiverModelId) {
            return &domain;
        }
    }
    return nullptr;
}

void HeatSystem::clearReceiverDomains() {
    receiverVoronoiDomains.clear();
}

void HeatSystem::setContactSystemController(ContactSystemController* controller) {
    contactSystemController = controller;
}

const HeatSystem::SourceCoupling* HeatSystem::findSourceCouplingByModelId(uint32_t modelId) const {
    if (modelId == 0) {
        return nullptr;
    }

    for (const SourceCoupling& sourceCoupling : heatSources) {
        if (sourceCoupling.modelId == modelId &&
            sourceCoupling.model &&
            sourceCoupling.heatSource) {
            return &sourceCoupling;
        }
    }

    return nullptr;
}

HeatReceiver* HeatSystem::findReceiverByModelId(uint32_t modelId) const {
    if (modelId == 0) {
        return nullptr;
    }

    for (std::size_t receiverIndex = 0; receiverIndex < receivers.size(); ++receiverIndex) {
        HeatReceiver* receiver = receivers[receiverIndex].get();
        if (!receiver) {
            continue;
        }

        uint32_t receiverModelId = 0;
        if (receiverIndex < receiverModelIds.size()) {
            receiverModelId = receiverModelIds[receiverIndex];
        }
        if (receiverModelId == 0) {
            receiverModelId = receiver->getModel().getRuntimeModelId();
        }

        if (receiverModelId == modelId) {
            return receiver;
        }
    }

    return nullptr;
}

bool HeatSystem::uploadContactPairsToCoupling(ContactCoupling& coupling, const std::vector<ContactPairGPU>& pairs) {
    const VkDeviceSize storageAlignment = vulkanDevice.getPhysicalDeviceProperties().limits.minStorageBufferOffsetAlignment;
    const std::size_t pairCount = pairs.empty() ? 1ull : pairs.size();
    const VkDeviceSize bufferSize = sizeof(ContactPairGPU) * pairCount;

    coupling.contactDescriptorsReady = false;
    if (coupling.contactPairBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(coupling.contactPairBuffer, coupling.contactPairBufferOffset);
        coupling.contactPairBuffer = VK_NULL_HANDLE;
        coupling.contactPairBufferOffset = 0;
    }
    coupling.contactPairCount = 0;

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceSize stagingOffset = 0;
    void* stagingData = nullptr;
    if (createStagingBuffer(memoryAllocator, bufferSize, stagingBuffer, stagingOffset, &stagingData) != VK_SUCCESS || !stagingData) {
        std::cerr << "[HeatSystem] Failed to create contact staging buffer" << std::endl;
        return false;
    }

    if (pairs.empty()) {
        std::memset(stagingData, 0, static_cast<std::size_t>(bufferSize));
    } else {
        std::memcpy(stagingData, pairs.data(), static_cast<std::size_t>(bufferSize));
    }

    auto [pairHandle, pairOffset] = memoryAllocator.allocate(
        bufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        storageAlignment);
    if (pairHandle == VK_NULL_HANDLE) {
        std::cerr << "[HeatSystem] Failed to allocate contact pair buffer" << std::endl;
        memoryAllocator.free(stagingBuffer, stagingOffset);
        return false;
    }

    coupling.contactPairBuffer = pairHandle;
    coupling.contactPairBufferOffset = pairOffset;
    coupling.contactPairCount = static_cast<uint32_t>(pairCount);

    VkCommandBuffer cmd = renderCommandPool.beginCommands();
    VkBufferCopy region{};
    region.srcOffset = stagingOffset;
    region.dstOffset = coupling.contactPairBufferOffset;
    region.size = bufferSize;
    vkCmdCopyBuffer(cmd, stagingBuffer, coupling.contactPairBuffer, 1, &region);
    renderCommandPool.endCommands(cmd);

    memoryAllocator.free(stagingBuffer, stagingOffset);
    return true;
}

void HeatSystem::clearContactCouplings() {
    for (ContactCoupling& coupling : contactCouplings) {
        coupling.kind = ContactCouplingKind::SourceToReceiver;
        coupling.contactDescriptorsReady = false;
        coupling.contactComputeSetA = VK_NULL_HANDLE;
        coupling.contactComputeSetB = VK_NULL_HANDLE;
        coupling.source = nullptr;
        coupling.emitterReceiver = nullptr;
        coupling.receiver = nullptr;
        if (coupling.contactPairBuffer != VK_NULL_HANDLE) {
            memoryAllocator.free(coupling.contactPairBuffer, coupling.contactPairBufferOffset);
            coupling.contactPairBuffer = VK_NULL_HANDLE;
            coupling.contactPairBufferOffset = 0;
        }
        if (coupling.paramsBuffer != VK_NULL_HANDLE) {
            memoryAllocator.free(coupling.paramsBuffer, coupling.paramsBufferOffset);
            coupling.paramsBuffer = VK_NULL_HANDLE;
            coupling.paramsBufferOffset = 0;
        }
        coupling.contactPairCount = 0;
        coupling.params = HeatContactParams{};
    }

    contactCouplings.clear();
}

void HeatSystem::rebuildContactCouplings(bool forceContactRebuild) {
    clearContactCouplings();
    if (!contactSystemController) {
        return;
    }

    for (const HeatContactBinding& request : configuredContactPairs) {
        if (request.pair.emitterModelId == 0 ||
            request.pair.receiverModelId == 0 ||
            request.pair.emitterModelId == request.pair.receiverModelId) {
            continue;
        }

        HeatReceiver* receiver = findReceiverByModelId(request.pair.receiverModelId);
        if (!receiver) {
            continue;
        }

        ContactCoupling coupling{};
        coupling.kind = request.pair.kind;
        coupling.emitterModelId = request.pair.emitterModelId;
        coupling.receiverModelId = request.pair.receiverModelId;
        coupling.receiver = receiver;
        coupling.params = request.params;

        if (request.pair.kind == ContactCouplingKind::SourceToReceiver) {
            const SourceCoupling* sourceCoupling = findSourceCouplingByModelId(request.pair.emitterModelId);
            if (!sourceCoupling) {
                continue;
            }
            coupling.source = sourceCoupling->heatSource.get();
        } else {
            HeatReceiver* emitterReceiver = findReceiverByModelId(request.pair.emitterModelId);
            if (!emitterReceiver || emitterReceiver == receiver) {
                continue;
            }
            coupling.emitterReceiver = emitterReceiver;
        }

        std::vector<ContactPairGPU> pairs;
        if (!contactSystemController->computePairsForRuntimeModels(request.pair, pairs, forceContactRebuild)) {
            continue;
        }
        if (!uploadContactPairsToCoupling(coupling, pairs)) {
            continue;
        }

        contactCouplings.push_back(std::move(coupling));
    }
}

void HeatSystem::setActiveModels(
    const std::vector<uint32_t>& sourceModelIds,
    const std::vector<uint32_t>& receiverModelIds,
    bool rebuildContactSystem) {
    if (activeSourceModelIds == sourceModelIds &&
        activeReceiverModelIds == receiverModelIds) {
        return;
    }

    activeSourceModelIds = sourceModelIds;
    activeReceiverModelIds = receiverModelIds;
    clearReceiverDomains();

    if (isActive) {
        isActive = false;
        isVoronoiReady = false;
        isVoronoiSeederReady = false;
        initializeHeatModelBindings();
        if (rebuildContactSystem) {
            rebuildContactCouplings(false);
        }
        setActive(true);
        return;
    }

    initializeHeatModelBindings();
    if (rebuildContactSystem) {
        rebuildContactCouplings(false);
    }
}

void HeatSystem::setContactPairs(
    const std::vector<HeatContactBinding>& contactPairs,
    bool forceContactRebuild) {
    const bool pairsChanged = (configuredContactPairs != contactPairs);
    configuredContactPairs = contactPairs;
    if (!pairsChanged && !forceContactRebuild) {
        return;
    }

    rebuildContactCouplings(forceContactRebuild);

    if (isActive && isVoronoiReady) {
        executeBufferTransfers();
    }
}

void HeatSystem::setSolveParams(const HeatSolveParams& params) {
    if (solveParams == params) {
        return;
    }

    solveParams = params;
    clearReceiverDomains();
    isVoronoiReady = false;
    isVoronoiSeederReady = false;

    if (isActive) {
        setActive(true);
    }
}

void HeatSystem::setMaterialBindings(const std::vector<HeatModelMaterialBindings>& bindings) {
    std::unordered_map<uint32_t, HeatMaterialPresetId> incomingPresetsByModelId;
    incomingPresetsByModelId.reserve(bindings.size());
    for (const HeatModelMaterialBindings& modelBindings : bindings) {
        if (modelBindings.runtimeModelId == 0) {
            continue;
        }

        HeatMaterialPresetId presetId = HeatMaterialPresetId::Aluminum;
        if (!modelBindings.bindings.empty()) {
            presetId = modelBindings.bindings.front().presetId;
        }
        incomingPresetsByModelId[modelBindings.runtimeModelId] = presetId;
    }

    bool bindingsChanged = (receiverMaterialPresetByModelId.size() != incomingPresetsByModelId.size());
    if (!bindingsChanged) {
        for (const auto& entry : receiverMaterialPresetByModelId) {
            auto incomingIt = incomingPresetsByModelId.find(entry.first);
            if (incomingIt == incomingPresetsByModelId.end() || incomingIt->second != entry.second) {
                bindingsChanged = true;
                break;
            }
        }
    }

    if (!bindingsChanged) {
        return;
    }

    receiverMaterialPresetByModelId = std::move(incomingPresetsByModelId);

    if (isVoronoiSeederReady) {
        isVoronoiReady = false;
        isVoronoiSeederReady = false;
    }

}

void HeatSystem::initializeHeatModelBindings() {
    runtime.initializeModelBindings(
        vulkanDevice,
        memoryAllocator,
        resourceManager,
        remesher,
        renderCommandPool,
        activeSourceModelIds,
        activeReceiverModelIds);
}

void HeatSystem::initializeSurfelRenderers(VkRenderPass renderPass, uint32_t maxFramesInFlight) {
    for (Model* model : remesher.getRemeshedModels()) {
        SurfelRenderer* surfel = remesher.getSurfelForModel(model);
        if (surfel) {
            surfel->initialize(renderPass, maxFramesInFlight);
        }
    }
}

void HeatSystem::initializeVoronoiGeoCompute() {
    voronoiGeoCompute = std::make_unique<VoronoiGeoCompute>(vulkanDevice, renderCommandPool);
}

void HeatSystem::initializeVoronoiCandidateCompute() {
    voronoiCandidateCompute = std::make_unique<VoronoiCandidateCompute>(vulkanDevice, renderCommandPool);
    voronoiCandidateCompute->initialize();
}

void HeatSystem::update() {
    if (isPaused) {
        return;
    }

    static auto lastTime = std::chrono::steady_clock::now();
    const auto currentTime = std::chrono::steady_clock::now();

    float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
    lastTime = currentTime;
    constexpr float maxDeltaTimeSeconds = 1.0f / 30.0f;
    if (deltaTime > maxDeltaTimeSeconds) {
        deltaTime = maxDeltaTimeSeconds;
    }

    // Calculate substep deltaTime for iterative solver
    float subStepDeltaTime = deltaTime / static_cast<float>(NUM_SUBSTEPS);

    // Update GPU time buffer with substep delta time
    if (resources.mappedTimeData) {
        TimeUniform* timeData = static_cast<TimeUniform*>(resources.mappedTimeData);
        timeData->deltaTime = subStepDeltaTime;   
        timeData->totalTime += deltaTime;       
    }

    for (SourceCoupling& sourceCoupling : heatSources) {
        if (!sourceCoupling.model || !sourceCoupling.heatSource) {
            continue;
        }

        sourceCoupling.heatSource->setHeatSourcePushConstant(sourceCoupling.model->getModelMatrix());
    }
}

void HeatSystem::recreateResources(uint32_t maxFramesInFlight, VkExtent2D extent, VkRenderPass renderPass) {
    (void)extent;
    std::cout << "[HeatSystem] recreateResources() called" << std::endl;

    if (!heatRenderer) {
        heatRenderer = std::make_unique<HeatRenderer>(vulkanDevice, uniformBufferManager);
    }
    if (!heatRenderer) {
        std::cerr << "[HeatSystem] Failed to recreate HeatRenderer" << std::endl;
        return;
    }
    heatRenderer->initialize(renderPass, maxFramesInFlight);

    // Use Voronoi if ready
    if (isVoronoiSeederReady) {
        if (resources.voronoiDescriptorPool != VK_NULL_HANDLE) {
            vkResetDescriptorPool(vulkanDevice.getDevice(), resources.voronoiDescriptorPool, 0);
        }

        if (resources.surfaceDescriptorPool != VK_NULL_HANDLE) {
            vkResetDescriptorPool(vulkanDevice.getDevice(), resources.surfaceDescriptorPool, 0);
        }

        if (!createVoronoiDescriptorSetLayout() ||
            !createVoronoiPipeline() ||
            !createVoronoiDescriptorSets(maxFramesInFlight)) {
            std::cerr << "[HeatSystem] Failed to recreate Voronoi descriptor/pipeline resources" << std::endl;
            return;
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

        if (!createSurfaceDescriptorSetLayout() || !createSurfacePipeline()) {
            std::cerr << "[HeatSystem] Failed to recreate surface descriptor/pipeline resources" << std::endl;
            return;
        }
        
        uint32_t nodeCount = resources.voronoiNodeCount;
        for (auto& receiver : receivers) {
            receiver->recreateDescriptors(
                resources.surfaceDescriptorSetLayout,
                resources.surfaceDescriptorPool,
                resources.tempBufferA,
                resources.tempBufferAOffset_,
                resources.tempBufferB,
                resources.tempBufferBOffset_,
                resources.timeBuffer,
                resources.timeBufferOffset_,
                nodeCount
            );
        }

        std::vector<HeatRenderer::SourceRenderBinding> sourceRenderBindings;
        sourceRenderBindings.reserve(heatSources.size());

        for (SourceCoupling& sourceCoupling : heatSources) {
            if (!sourceCoupling.model || !sourceCoupling.heatSource) {
                continue;
            }
            HeatRenderer::SourceRenderBinding sourceBinding{};
            sourceBinding.model = sourceCoupling.model;
            sourceBinding.heatSource = sourceCoupling.heatSource.get();
            sourceRenderBindings.push_back(sourceBinding);
        }
        heatRenderer->updateDescriptors(remesher, sourceRenderBindings, receivers, maxFramesInFlight, true);
    }

    if (contactSystemController) {
        contactSystemController->reinitRenderer(renderPass, maxFramesInFlight);
    }

    if (!createComputeCommandBuffers(maxFramesInFlight)) {
        std::cerr << "[HeatSystem] Failed to recreate compute command buffers" << std::endl;
        return;
    }
}

void HeatSystem::processResetRequest() {
    if (needsReset.exchange(false, std::memory_order_acq_rel)) {
        vkDeviceWaitIdle(vulkanDevice.getDevice());
        initializeVoronoi();
    }
}

void HeatSystem::setActive(bool active) {
    if (active && !isActive) {
        vkDeviceWaitIdle(vulkanDevice.getDevice());
    }

    if (active && isVoronoiSeederReady && !isPaused) {
        executeBufferTransfers();
    }

	if (active && !isVoronoiSeederReady) {
        for (auto& receiver : receivers) {
            if (!receiver) {
                continue;
            }
            if (!receiver->initializeReceiverBuffer()) {
                std::cerr << "[HeatSystem] Warning: failed to reinitialize receiver surface buffer for model "
                          << receiver->getModel().getRuntimeModelId() << std::endl;
            }
        }

        if (!voronoiStage ||
            !voronoiStage->buildReceiverDomains(receivers, receiverVoronoiDomains, solveParams, K_NEIGHBORS)) {
            std::cerr << "[HeatSystem] Cannot activate: failed to build voronoi domains" << std::endl;
            isActive = false;
            isVoronoiReady = false;
            isVoronoiSeederReady = false;
            return;
        }

        if (debugEnable && receiverVoronoiDomains.size() > 1) {
            std::cout << "[HeatSystem] Built " << receiverVoronoiDomains.size() << " voronoi domains " << std::endl;
        }

		isVoronoiSeederReady = true;

		if (!generateVoronoiDiagram()) {
            std::cerr << "[HeatSystem] Cannot activate: voronoi generation failed" << std::endl;
            isActive = false;
            isVoronoiReady = false;
            isVoronoiSeederReady = false;
            return;
        }

        if (resources.voronoiNodeCount == 0) {
            std::cerr << "[HeatSystem] Cannot activate: failed to generate Voronoi domains for active receiver set" << std::endl;
            isActive = false;
            isVoronoiReady = false;
            isVoronoiSeederReady = false;
            return;
        }

        for (const HeatSystemVoronoiDomain& domain : receiverVoronoiDomains) {
            if (!domain.receiver || !domain.integrator) {
                continue;
            }

            const auto& surfacePoints = domain.receiver->getIntrinsicSurfacePositions();
            if (surfacePoints.empty()) {
                continue;
            }

			std::vector<uint32_t> cellIndices;
			domain.integrator->computeSurfacePointMapping(
				surfacePoints,
				domain.seedFlags,
				K_NEIGHBORS,
				cellIndices
			);

            uint32_t invalidMappedCells = 0;
            for (uint32_t& cellIndex : cellIndices) {
                if (cellIndex == UINT32_MAX) {
                    invalidMappedCells++;
                    continue;
                }

                cellIndex += domain.nodeOffset;
                if (cellIndex >= resources.voronoiNodeCount) {
                    cellIndex = UINT32_MAX;
                    invalidMappedCells++;
                }
            }

            if (invalidMappedCells > 0) {
                std::cerr << "[HeatSystem] Warning: receiver " << domain.receiverModelId
                          << " has " << invalidMappedCells
                          << " invalid Voronoi mapping entries" << std::endl;
            }
			domain.receiver->stageVoronoiSurfaceMapping(cellIndices);
		}

        executeBufferTransfers();
        isVoronoiReady = true;
    }
    isActive = active;
}

void HeatSystem::renderContactLines(VkCommandBuffer cmdBuffer, uint32_t frameIndex, VkExtent2D extent) {
    if (contactSystemController) {
        contactSystemController->renderLines(cmdBuffer, frameIndex, extent);
    }
}

void HeatSystem::requestReset() {
	needsReset.store(true, std::memory_order_release);
}

void HeatSystem::initializeVoronoi() {
	if (!resources.mappedTempBufferA || !resources.mappedTempBufferB)
		return;

	float* tempsA = static_cast<float*>(resources.mappedTempBufferA);
	float* tempsB = static_cast<float*>(resources.mappedTempBufferB);

	for (uint32_t i = 0; i < resources.voronoiNodeCount; i++) {
		tempsA[i] = AMBIENT_TEMPERATURE;
		tempsB[i] = AMBIENT_TEMPERATURE;
	}
}

bool HeatSystem::createTimeBuffer() {
	VkDeviceSize bufferSize = sizeof(TimeUniform);

    if (createUniformBuffer(
		memoryAllocator, vulkanDevice,
		bufferSize,
		resources.timeBuffer, resources.timeBufferOffset_, &resources.mappedTimeData) != VK_SUCCESS || resources.timeBuffer == VK_NULL_HANDLE || resources.mappedTimeData == nullptr) {
        std::cerr << "[HeatSystem] Failed to create time uniform buffer" << std::endl;
        return false;
    }
    return true;
}

bool HeatSystem::createSurfaceDescriptorPool(uint32_t maxFramesInFlight) {
    if (!surfaceStage) {
        return false;
    }

    return surfaceStage->createDescriptorPool(maxFramesInFlight);
}

bool HeatSystem::createSurfaceDescriptorSetLayout() {
    if (!surfaceStage) {
        return false;
    }

    return surfaceStage->createDescriptorSetLayout();
}

bool HeatSystem::createSurfacePipeline() {
    if (!surfaceStage) {
        return false;
    }

    return surfaceStage->createPipeline();
}

bool HeatSystem::generateVoronoiDiagram() {
    if (!voronoiStage) {
        return false;
    }

    const bool generated = voronoiStage->generateVoronoiDiagram(
        receiverVoronoiDomains,
        receiverMaterialPresetByModelId,
        debugEnable,
        maxFramesInFlight,
        K_NEIGHBORS,
        voronoiGeoCompute.get(),
        pointRenderer.get());

    if (!generated) {
        return false;
    }

    if (debugEnable) {
        exportDebugCellsToOBJ();
        exportCellVolumes();
        exportVoronoiDumpInfo();
    }

    return true;
}

void HeatSystem::exportDebugCellsToOBJ() {    
    if (!debugStage) {
        return;
    }

    debugStage->exportDebugCellsToOBJ(debugEnable, resources.voronoiNodeCount, resources.mappedDebugCellGeometryData);
}

void HeatSystem::exportCellVolumes() {
    if (!debugStage) {
        return;
    }

    debugStage->exportCellVolumes(debugEnable, resources.voronoiNodeCount, resources.mappedVoronoiNodeData);
}

void HeatSystem::exportVoronoiDumpInfo() {
    if (!debugStage) {
        return;
    }

    debugStage->exportVoronoiDumpInfo(debugEnable, resources.voronoiNodeCount, resources.mappedVoronoiNodeData, resources.mappedVoronoiDumpData);
}

bool HeatSystem::createVoronoiDescriptorPool(uint32_t maxFramesInFlight) {
    if (!voronoiStage) {
        return false;
    }

    return voronoiStage->createDescriptorPool(maxFramesInFlight);
}

bool HeatSystem::createVoronoiDescriptorSetLayout() {
    if (!voronoiStage) {
        return false;
    }

    return voronoiStage->createDescriptorSetLayout();
}

bool HeatSystem::createVoronoiDescriptorSets(uint32_t maxFramesInFlight) {
    if (!voronoiStage) {
        return false;
    }

    return voronoiStage->createDescriptorSets(maxFramesInFlight);
}

bool HeatSystem::createVoronoiPipeline() {
    if (!voronoiStage) {
        return false;
    }

    return voronoiStage->createPipeline();
}

void HeatSystem::recordComputeCommands(VkCommandBuffer commandBuffer, uint32_t currentFrame, VkQueryPool timingQueryPool, uint32_t timingQueryBase) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        std::cerr << "[HeatSystem] Failed to begin recording compute command buffer" << std::endl;
        return;
    }

    if (timingQueryPool != VK_NULL_HANDLE) {
        vkCmdResetQueryPool(commandBuffer, timingQueryPool, timingQueryBase, 2);
        vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, timingQueryPool, timingQueryBase);
    }

    if (!isActive || isPaused) {
        if (timingQueryPool != VK_NULL_HANDLE) {
            vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timingQueryPool, timingQueryBase + 1);
        }
        vkEndCommandBuffer(commandBuffer);
        return;
    }
    
    if (!isVoronoiReady) {
        if (timingQueryPool != VK_NULL_HANDLE) {
            vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timingQueryPool, timingQueryBase + 1);
        }
        vkEndCommandBuffer(commandBuffer);
        return;
    }

    static int recordCallCount = 0;
    if (recordCallCount == 0) {
        std::cout << "[HeatSystem] recordComputeCommands called, isActive=" << isActive << std::endl;
    }
    recordCallCount++;

    if (heatSources.empty()) {
        if (timingQueryPool != VK_NULL_HANDLE) {
            vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timingQueryPool, timingQueryBase + 1);
        }
        vkEndCommandBuffer(commandBuffer);
        return;
    }

    uint32_t nodeCount = resources.voronoiNodeCount;
    uint32_t workGroupSize = 256;
    uint32_t workGroupCount = (nodeCount + workGroupSize - 1) / workGroupSize;
        
    const SourceCoupling* primarySourceCoupling = runtime.findPrimarySourceCoupling();
    if (!primarySourceCoupling) {
        if (timingQueryPool != VK_NULL_HANDLE) {
            vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timingQueryPool, timingQueryBase + 1);
        }
        vkEndCommandBuffer(commandBuffer);
        return;
    }

    HeatSourcePushConstant basePushConstant = primarySourceCoupling->heatSource->getHeatSourcePushConstant();
    basePushConstant.heatSourceModelMatrix = primarySourceCoupling->model->getModelMatrix();
    basePushConstant.inverseHeatSourceModelMatrix = glm::inverse(basePushConstant.heatSourceModelMatrix);
    basePushConstant.maxNodeNeighbors = K_NEIGHBORS;

    static int frameCount = 0;
    if (frameCount == 0) {
        std::cout << "[HeatSystem] Frame 0: Starting heat simulation (" << NUM_SUBSTEPS << " substeps, " << workGroupCount << " workgroups)" << std::endl;
    }

    // Substeps
    for (int i = 0; i < NUM_SUBSTEPS; i++) {
        basePushConstant.substepIndex = i;
        
        bool isEven = (i % 2 == 0);
        
        if (frameCount == 0 && i < 2) {
            const char* setName = "B";
            if (isEven) {
                setName = "A";
            }
            std::cout << "  Substep " << i << ": isEven=" << isEven << ", using Set" << setName << std::endl;
        }
        
        // INJECTION: run all active source->receiver couplings.
        {
            for (const ContactCoupling& coupling : contactCouplings) {
                if (!coupling.contactDescriptorsReady || !coupling.receiver) {
                    continue;
                }

                const HeatSystemVoronoiDomain* receiverDomain = findReceiverDomain(coupling.receiver);
                if (!receiverDomain || receiverDomain->nodeCount == 0) {
                    continue;
                }

                uint32_t triCount = static_cast<uint32_t>(coupling.receiver->getIntrinsicTriangleCount());
                if (triCount == 0) {
                    continue;
                }

                uint32_t groupSize = 256;
                uint32_t groupCount = (triCount + groupSize - 1) / groupSize;
                VkDescriptorSet contactSet = coupling.contactComputeSetB;
                if (isEven) {
                    contactSet = coupling.contactComputeSetA;
                }
                if (contactSet == VK_NULL_HANDLE) {
                    continue;
                }

                ContactPushConstant contactPushConstant{};
                contactPushConstant.couplingKind =
                    (coupling.kind == ContactCouplingKind::ReceiverToReceiver) ? 1u : 0u;

                vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, resources.contactPipeline);
                vkCmdPushConstants(
                    commandBuffer,
                    resources.contactPipelineLayout,
                    VK_SHADER_STAGE_COMPUTE_BIT,
                    0,
                    sizeof(ContactPushConstant),
                    &contactPushConstant);
                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, resources.contactPipelineLayout, 0, 1, &contactSet, 0, nullptr);
                vkCmdDispatch(commandBuffer, groupCount, 1, 1);
            }
        }

        VkBufferMemoryBarrier injectionBarriers[2]{};
        injectionBarriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        injectionBarriers[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        injectionBarriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        injectionBarriers[0].buffer = resources.injectionKBuffer;
        injectionBarriers[0].offset = resources.injectionKBufferOffset_;
        injectionBarriers[0].size = VK_WHOLE_SIZE;

        injectionBarriers[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        injectionBarriers[1].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        injectionBarriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        injectionBarriers[1].buffer = resources.injectionKTBuffer;
        injectionBarriers[1].offset = resources.injectionKTBufferOffset_;
        injectionBarriers[1].size = VK_WHOLE_SIZE;

        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 2, injectionBarriers, 0, nullptr);

        if (voronoiStage) {
            voronoiStage->dispatchDiffusionSubstep(
                commandBuffer,
                currentFrame,
                basePushConstant,
                i,
                workGroupCount);
            voronoiStage->insertInterSubstepBarrier(commandBuffer, i, NUM_SUBSTEPS);
        }
    }

    if (voronoiStage) {
        voronoiStage->insertFinalTemperatureBarrier(commandBuffer, NUM_SUBSTEPS);
    }

    // Viz: update receiver surface temps from final ping pong buffer
    {
        bool finalIsEven = true;
        if (voronoiStage) {
            finalIsEven = voronoiStage->finalSubstepWritesBufferB(NUM_SUBSTEPS);
        }
        for (auto& receiver : receivers) {
            const HeatSystemVoronoiDomain* receiverDomain = findReceiverDomain(receiver.get());
            if (!receiverDomain || receiverDomain->nodeCount == 0) {
                continue;
            }

            uint32_t vertexCount = static_cast<uint32_t>(receiver->getIntrinsicVertexCount());
            if (vertexCount == 0) {
                continue;
            }

            uint32_t groupSize = 256;
            uint32_t groupCount = (vertexCount + groupSize - 1) / groupSize;

            VkDescriptorSet surfaceSet = receiver->getSurfaceComputeSetA();
            if (finalIsEven) {
                surfaceSet = receiver->getSurfaceComputeSetB();
            }
            if (surfaceSet == VK_NULL_HANDLE) {
                continue;
            }

            HeatSourcePushConstant visPC = basePushConstant;
            visPC.substepIndex = 0;
            visPC.visModelMatrix = receiver->getModel().getModelMatrix();

            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, resources.surfacePipeline);
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, resources.surfacePipelineLayout, 0, 1, &surfaceSet, 0, nullptr);
            vkCmdPushConstants(commandBuffer, resources.surfacePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(HeatSourcePushConstant), &visPC);
            vkCmdDispatch(commandBuffer, groupCount, 1, 1);
        }
    }
    
    frameCount++;
    
    if (timingQueryPool != VK_NULL_HANDLE) {
        vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timingQueryPool, timingQueryBase + 1);
    }

    vkEndCommandBuffer(commandBuffer);
}

void HeatSystem::initializeVoronoiRenderer(VkRenderPass renderPass, uint32_t maxFramesInFlight) {
    voronoiRenderer = std::make_unique<VoronoiRenderer>(vulkanDevice, uniformBufferManager, renderCommandPool);
    voronoiRenderer->initialize(renderPass, maxFramesInFlight);
}

void HeatSystem::renderVoronoiSurface(VkCommandBuffer cmdBuffer, uint32_t frameIndex) {
    if (!renderStage) {
        return;
    }

    renderStage->renderVoronoiSurface(
        cmdBuffer,
        frameIndex,
        voronoiRenderer.get(),
        receivers,
        receiverVoronoiDomains,
        isActive);
}

void HeatSystem::renderHeatOverlay(VkCommandBuffer cmdBuffer, uint32_t frameIndex) {
    if (!renderStage) {
        return;
    }

    renderStage->renderHeatOverlay(cmdBuffer, frameIndex, heatRenderer.get(), heatSources, receivers, isActive, isPaused);
}

void HeatSystem::initializePointRenderer(VkRenderPass renderPass, uint32_t maxFramesInFlight) {
    pointRenderer = std::make_unique<PointRenderer>(vulkanDevice,  memoryAllocator, uniformBufferManager);
    pointRenderer->initialize(renderPass, 2, maxFramesInFlight);  // Subpass 2 = Grid subpass
}

void HeatSystem::renderOccupancy(VkCommandBuffer cmdBuffer, uint32_t frameIndex, VkExtent2D extent) {
    if (!renderStage) {
        return;
    }

    renderStage->renderOccupancy(cmdBuffer, frameIndex, extent, pointRenderer.get(), isActive);
}

void HeatSystem::renderSurfels(VkCommandBuffer cmdBuffer, uint32_t frameIndex, const glm::mat4& heatSourceModel, float radius) {
    (void)heatSourceModel;
    if (!renderStage) {
        return;
    }

    renderStage->renderSurfels(cmdBuffer, frameIndex, radius, heatSources, receivers);
}

bool HeatSystem::createComputeCommandBuffers(uint32_t maxFramesInFlight) {
    resources.computeCommandBuffers.resize(maxFramesInFlight);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = renderCommandPool.getHandle();
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(resources.computeCommandBuffers.size());

    if (vkAllocateCommandBuffers(vulkanDevice.getDevice(), &allocInfo, resources.computeCommandBuffers.data()) != VK_SUCCESS) {
        std::cerr << "[HeatSystem] Failed to allocate compute command buffers" << std::endl;
        resources.computeCommandBuffers.clear();
        return false;
    }
    return true;
}

void HeatSystem::executeBufferTransfers() {    
    VkCommandBuffer copyCmd = renderCommandPool.beginCommands();
    
    for (auto& receiver : receivers) {
        receiver->executeBufferTransfers(copyCmd);
    }
    
    renderCommandPool.endCommands(copyCmd);
    
    for (auto& receiver : receivers) {
        receiver->cleanupStagingBuffers();
    }

    if (voronoiCandidateCompute && resources.voronoiNodeCount > 0) {
        for (const auto& receiver : receivers) {
            const HeatSystemVoronoiDomain* receiverDomain = findReceiverDomain(receiver.get());
            if (!receiverDomain || receiverDomain->nodeCount == 0) {
                continue;
            }

            uint32_t faceCount = static_cast<uint32_t>(receiver->getIntrinsicTriangleCount());
            if (faceCount == 0 || receiver->getVoronoiCandidateBuffer() == VK_NULL_HANDLE) {
                continue;
            }

            VoronoiCandidateCompute::Bindings bindings{};
            bindings.vertexBuffer = receiver->getSurfaceBuffer();
            bindings.vertexBufferOffset = receiver->getSurfaceBufferOffset();
            bindings.faceIndexBuffer = receiver->getTriangleIndicesBuffer();
            bindings.faceIndexBufferOffset = receiver->getTriangleIndicesBufferOffset();
            bindings.seedPositionBuffer = resources.seedPositionBuffer;
            bindings.seedPositionBufferOffset = resources.seedPositionBufferOffset_;
            bindings.candidateBuffer = receiver->getVoronoiCandidateBuffer();
            bindings.candidateBufferOffset = receiver->getVoronoiCandidateBufferOffset();

            voronoiCandidateCompute->updateDescriptors(bindings);
            voronoiCandidateCompute->dispatch(faceCount, receiverDomain->nodeCount, receiverDomain->nodeOffset);
        }
    }

    if (!heatRenderer) {
        std::cerr << "[HeatSystem] HeatRenderer is not initialized; skipping heat descriptor updates" << std::endl;
        return;
    }
    
    // Update descriptors for all receivers
    uint32_t nodeCount = resources.voronoiNodeCount;
    for (auto& receiver : receivers) {
        if (!receiver) {
            continue;
        }

        const HeatSystemVoronoiDomain* receiverDomain = findReceiverDomain(receiver.get());
        if (!receiverDomain || receiverDomain->nodeCount == 0) {
            continue;
        }

        receiver->updateDescriptors(
            resources.surfaceDescriptorSetLayout, resources.surfaceDescriptorPool,
            resources.tempBufferA, resources.tempBufferAOffset_, resources.tempBufferB, resources.tempBufferBOffset_,
            resources.timeBuffer, resources.timeBufferOffset_, nodeCount
        );
    }

    if (contactStage) {
        for (ContactCoupling& coupling : contactCouplings) {
            contactStage->updateCouplingDescriptors(coupling, nodeCount);
        }
    }
    std::vector<HeatRenderer::SourceRenderBinding> sourceRenderBindings;
    sourceRenderBindings.reserve(heatSources.size());
    for (SourceCoupling& sourceCoupling : heatSources) {
        if (!sourceCoupling.model || !sourceCoupling.heatSource) {
            continue;
        }
        HeatRenderer::SourceRenderBinding sourceBinding{};
        sourceBinding.model = sourceCoupling.model;
        sourceBinding.heatSource = sourceCoupling.heatSource.get();
        sourceRenderBindings.push_back(sourceBinding);
    }

    heatRenderer->updateDescriptors(remesher, sourceRenderBindings, receivers, maxFramesInFlight, false);

    // Update Voronoi renderer descriptors
    if (voronoiRenderer) {
        for (const auto& receiver : receivers) {
            const HeatSystemVoronoiDomain* receiverDomain = findReceiverDomain(receiver.get());
            if (!receiverDomain || receiverDomain->nodeCount == 0) {
                continue;
            }

            Model& model = receiver->getModel();
            uint32_t vertexCount = static_cast<uint32_t>(receiver->getIntrinsicVertexCount());

            if (vertexCount == 0) {
                continue;
            }

            auto* iodt = remesher.getRemesherForModel(&model);
            if (!iodt) {
                continue;
            }

            auto* supportingHalfedge = iodt->getSupportingHalfedge();
            if (!supportingHalfedge || !supportingHalfedge->isUploadedToGPU()) {
                continue;
            }

            for (uint32_t frameIndex = 0; frameIndex < maxFramesInFlight; ++frameIndex) {
                voronoiRenderer->updateDescriptors(
                    frameIndex,
                    vertexCount,
                    getSeedPositionBuffer(),
                    getSeedPositionBufferOffset(),
                    getVoronoiNeighborBuffer(),
                    getVoronoiNeighborBufferOffset(),
                    supportingHalfedge->getSupportingHalfedgeView(),
                    supportingHalfedge->getSupportingAngleView(),
                    supportingHalfedge->getHalfedgeView(),
                    supportingHalfedge->getEdgeView(),
                    supportingHalfedge->getTriangleView(),
                    supportingHalfedge->getLengthView(),
                    supportingHalfedge->getInputHalfedgeView(),
                    supportingHalfedge->getInputEdgeView(),
                    supportingHalfedge->getInputTriangleView(),
                    supportingHalfedge->getInputLengthView(),
                    receiver->getVoronoiCandidateBuffer(),
                    receiver->getVoronoiCandidateBufferOffset()
                );
            }
        }
    }
}

void HeatSystem::cleanupResources() {    
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

    if (heatRenderer) {
        heatRenderer->cleanup();
    }

    if (voronoiRenderer) {
        voronoiRenderer->cleanup();
        voronoiRenderer.reset();
    }

    if (pointRenderer) {
        pointRenderer->cleanup();
        pointRenderer.reset();
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

    if (voronoiGeoCompute) {
        voronoiGeoCompute->cleanupResources();
    }

    if (voronoiCandidateCompute) {
        voronoiCandidateCompute->cleanupResources();
    }
}

void HeatSystem::cleanup() {
    clearReceiverDomains();

    if (resources.timeBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(resources.timeBuffer, resources.timeBufferOffset_);
        resources.timeBuffer = VK_NULL_HANDLE;
        resources.mappedTimeData = nullptr;
    }

    if (resources.tempBufferA != VK_NULL_HANDLE) {
        memoryAllocator.free(resources.tempBufferA, resources.tempBufferAOffset_);
        resources.tempBufferA = VK_NULL_HANDLE;
        resources.mappedTempBufferA = nullptr;
    }
    if (resources.tempBufferB != VK_NULL_HANDLE) {
        memoryAllocator.free(resources.tempBufferB, resources.tempBufferBOffset_);
        resources.tempBufferB = VK_NULL_HANDLE;
        resources.mappedTempBufferB = nullptr;
    }

    if (resources.injectionKBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(resources.injectionKBuffer, resources.injectionKBufferOffset_);
        resources.injectionKBuffer = VK_NULL_HANDLE;
        resources.mappedInjectionKBuffer = nullptr;
    }
    if (resources.injectionKTBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(resources.injectionKTBuffer, resources.injectionKTBufferOffset_);
        resources.injectionKTBuffer = VK_NULL_HANDLE;
        resources.mappedInjectionKTBuffer = nullptr;
    }

    if (resources.voronoiNodeBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(resources.voronoiNodeBuffer, resources.voronoiNodeBufferOffset_);
        resources.voronoiNodeBuffer = VK_NULL_HANDLE;
        resources.mappedVoronoiNodeData = nullptr;
    }
    if (resources.voronoiNeighborBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(resources.voronoiNeighborBuffer, resources.voronoiNeighborBufferOffset_);
        resources.voronoiNeighborBuffer = VK_NULL_HANDLE;
        resources.voronoiNeighborBufferOffset_ = 0;
    }
    if (resources.neighborIndicesBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(resources.neighborIndicesBuffer, resources.neighborIndicesBufferOffset_);
        resources.neighborIndicesBuffer = VK_NULL_HANDLE;
        resources.neighborIndicesBufferOffset_ = 0;
    }

    if (resources.interfaceAreasBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(resources.interfaceAreasBuffer, resources.interfaceAreasBufferOffset_);
        resources.interfaceAreasBuffer = VK_NULL_HANDLE;
        resources.interfaceAreasBufferOffset_ = 0;
        resources.mappedInterfaceAreasData = nullptr;
    }
    if (resources.interfaceNeighborIdsBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(resources.interfaceNeighborIdsBuffer, resources.interfaceNeighborIdsBufferOffset_);
        resources.interfaceNeighborIdsBuffer = VK_NULL_HANDLE;
        resources.interfaceNeighborIdsBufferOffset_ = 0;
        resources.mappedInterfaceNeighborIdsData = nullptr;
    }

    if (resources.meshTriangleBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(resources.meshTriangleBuffer, resources.meshTriangleBufferOffset_);
        resources.meshTriangleBuffer = VK_NULL_HANDLE;
        resources.meshTriangleBufferOffset_ = 0;
    }
    if (resources.seedPositionBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(resources.seedPositionBuffer, resources.seedPositionBufferOffset_);
        resources.seedPositionBuffer = VK_NULL_HANDLE;
        resources.seedPositionBufferOffset_ = 0;
        resources.mappedSeedPositionData = nullptr;
    }
    if (resources.seedFlagsBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(resources.seedFlagsBuffer, resources.seedFlagsBufferOffset_);
        resources.seedFlagsBuffer = VK_NULL_HANDLE;
        resources.seedFlagsBufferOffset_ = 0;
        resources.mappedSeedFlagsData = nullptr;
    }

    if (resources.debugCellGeometryBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(resources.debugCellGeometryBuffer, resources.debugCellGeometryBufferOffset_);
        resources.debugCellGeometryBuffer = VK_NULL_HANDLE;
        resources.debugCellGeometryBufferOffset_ = 0;
        resources.mappedDebugCellGeometryData = nullptr;
    }
    if (resources.voronoiDumpBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(resources.voronoiDumpBuffer, resources.voronoiDumpBufferOffset_);
        resources.voronoiDumpBuffer = VK_NULL_HANDLE;
        resources.voronoiDumpBufferOffset_ = 0;
        resources.mappedVoronoiDumpData = nullptr;
    }

    if (resources.voxelGridParamsBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(resources.voxelGridParamsBuffer, resources.voxelGridParamsBufferOffset_);
        resources.voxelGridParamsBuffer = VK_NULL_HANDLE;
        resources.voxelGridParamsBufferOffset_ = 0;
    }
    if (resources.voxelOccupancyBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(resources.voxelOccupancyBuffer, resources.voxelOccupancyBufferOffset_);
        resources.voxelOccupancyBuffer = VK_NULL_HANDLE;
        resources.voxelOccupancyBufferOffset_ = 0;
    }
    if (resources.voxelTrianglesListBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(resources.voxelTrianglesListBuffer, resources.voxelTrianglesListBufferOffset_);
        resources.voxelTrianglesListBuffer = VK_NULL_HANDLE;
        resources.voxelTrianglesListBufferOffset_ = 0;
    }
    if (resources.voxelOffsetsBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(resources.voxelOffsetsBuffer, resources.voxelOffsetsBufferOffset_);
        resources.voxelOffsetsBuffer = VK_NULL_HANDLE;
        resources.voxelOffsetsBufferOffset_ = 0;
    }

    clearContactCouplings();

    runtime.cleanupModelBindings(memoryAllocator);
}
