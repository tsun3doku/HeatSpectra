#include "HeatSystem.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <numeric>
#include <unordered_set>

#include "heat/HeatModelRuntime.hpp"
#include "heat/HeatSystemPlayback.hpp"
#include "HeatSystemSurfaceStage.hpp"
#include "heat/HeatGpuStructs.hpp"
#include "vulkan/CommandBufferManager.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/VulkanBuffer.hpp"
#include "vulkan/VulkanDevice.hpp"
#include "voronoi/VoronoiGpuStructs.hpp"
#include "voronoi/VoronoiNodeIndex.hpp"
#include "util/GMLS.hpp"
#include "spatial/SdfUtils.hpp"

HeatSystem::HeatSystem(
    VulkanDevice& vulkanDevice,
    MemoryAllocator& memoryAllocator,
    uint32_t maxFramesInFlight,
    CommandPool& renderCommandPool,
    CommandPool& transferCommandPool)
    : vulkanDevice(vulkanDevice),
      memoryAllocator(memoryAllocator),
      renderCommandPool(renderCommandPool),
      transferCommandPool(transferCommandPool),
      maxFramesInFlight(maxFramesInFlight) {

    surfaceStage = std::make_unique<HeatSystemSurfaceStage>(vulkanDevice);

    if (!surfaceStage->createDescriptorPool(0) ||  
        !surfaceStage->createDescriptorSetLayout() ||
        !surfaceStage->createPipeline()) {
        failInitialization("create compute resources");
        return;
    }

    if (!createComputeCommandBuffers(maxFramesInFlight)) {
        failInitialization("allocate compute command buffers");
        return;
    }

    initialized = true;
}

HeatSystem::~HeatSystem() {
    cleanup();
}

void HeatSystem::failInitialization(const char* stage) {
    std::cerr << "[HeatSystem] Initialization failed at stage: " << stage << std::endl;
    initialized = false;
    cleanup();
}

void HeatSystem::setHeatModels(
    const std::vector<std::vector<glm::vec3>>& modelSurfacePositions,
    const std::vector<std::vector<glm::vec3>>& modelSurfaceNormals,
    const std::vector<std::vector<uint32_t>>& modelSurfaceTriangleIndices,
    const std::vector<uint32_t>& modelRuntimeModelIds,
    const std::unordered_map<uint32_t, float>& modelInitialTemperaturesCByRuntimeId,
    const std::unordered_map<uint32_t, uint32_t>& modelBoundaryConditionTypesByRuntimeId,
    const std::unordered_map<uint32_t, float>& modelBoundaryTemperaturesCByRuntimeId,
    const std::unordered_map<uint32_t, float>& modelBoundaryHeatFluxesByRuntimeId,
    const std::unordered_map<uint32_t, float>& modelBoundaryHeatTransferCoefficientsByRuntimeId,
    const std::unordered_map<uint32_t, float>& modelVolumetricPowerDensitiesByRuntimeId,
    const std::unordered_map<uint32_t, float>& modelDensity,
    const std::unordered_map<uint32_t, float>& modelSpecificHeat,
    const std::unordered_map<uint32_t, float>& modelConductivity,
    units::LengthUnit worldUnit) {
    this->modelSurfacePositions = modelSurfacePositions;
    this->modelSurfaceNormals = modelSurfaceNormals;
    this->modelSurfaceTriangleIndices = modelSurfaceTriangleIndices;
    this->modelRuntimeModelIds = modelRuntimeModelIds;
    this->modelInitialTemperaturesCByRuntimeId = modelInitialTemperaturesCByRuntimeId;
    this->modelBoundaryConditionTypesByRuntimeId = modelBoundaryConditionTypesByRuntimeId;
    this->modelBoundaryTemperaturesCByRuntimeId = modelBoundaryTemperaturesCByRuntimeId;
    this->modelBoundaryHeatFluxesByRuntimeId = modelBoundaryHeatFluxesByRuntimeId;
    this->modelBoundaryHeatTransferCoefficientsByRuntimeId = modelBoundaryHeatTransferCoefficientsByRuntimeId;
    this->modelVolumetricPowerDensitiesByRuntimeId = modelVolumetricPowerDensitiesByRuntimeId;
    this->modelDensity = modelDensity;
    this->modelSpecificHeat = modelSpecificHeat;
    this->modelConductivity = modelConductivity;
    this->worldUnit = worldUnit;
    domainDirty = true;
}

float HeatSystem::getInitialTemperatureC(uint32_t runtimeModelId) const {
    const auto it = modelInitialTemperaturesCByRuntimeId.find(runtimeModelId);
    return it != modelInitialTemperaturesCByRuntimeId.end()
        ? it->second
        : HeatSimDefaults::ambientTemperatureC;
}

void HeatSystem::configureModelProperties(
    HeatModelRuntime& model,
    uint32_t runtimeModelId) const {
    const auto densityIt = modelDensity.find(runtimeModelId);
    const float density = densityIt != modelDensity.end()
        ? densityIt->second
        : HeatSimDefaults::density;
    const auto specificHeatIt = modelSpecificHeat.find(runtimeModelId);
    const float specificHeat = specificHeatIt != modelSpecificHeat.end()
        ? specificHeatIt->second
        : HeatSimDefaults::specificHeat;
    const auto conductivityIt = modelConductivity.find(runtimeModelId);
    const float conductivity = conductivityIt != modelConductivity.end()
        ? conductivityIt->second
        : HeatSimDefaults::conductivity;
    const auto boundaryTypeIt = modelBoundaryConditionTypesByRuntimeId.find(runtimeModelId);
    const uint32_t boundaryConditionType = boundaryTypeIt != modelBoundaryConditionTypesByRuntimeId.end()
        ? boundaryTypeIt->second
        : 0u;
    const auto boundaryTemperatureIt = modelBoundaryTemperaturesCByRuntimeId.find(runtimeModelId);
    const float boundaryTemperatureC = boundaryTemperatureIt != modelBoundaryTemperaturesCByRuntimeId.end()
        ? boundaryTemperatureIt->second
        : HeatSimDefaults::ambientTemperatureC;
    const auto heatFluxIt = modelBoundaryHeatFluxesByRuntimeId.find(runtimeModelId);
    const float heatFlux = heatFluxIt != modelBoundaryHeatFluxesByRuntimeId.end()
        ? heatFluxIt->second
        : 0.0f;
    const auto coefficientIt = modelBoundaryHeatTransferCoefficientsByRuntimeId.find(runtimeModelId);
    const float heatTransferCoefficient = coefficientIt != modelBoundaryHeatTransferCoefficientsByRuntimeId.end()
        ? coefficientIt->second
        : 0.0f;
    const auto powerDensityIt = modelVolumetricPowerDensitiesByRuntimeId.find(runtimeModelId);
    const float volumetricPowerDensity = powerDensityIt != modelVolumetricPowerDensitiesByRuntimeId.end()
        ? powerDensityIt->second
        : 0.0f;

    model.setMaterialProperties(density, specificHeat, conductivity);
    model.setWorldUnit(worldUnit);
    model.setInitialTemperatureC(getInitialTemperatureC(runtimeModelId));
    model.setBoundaryInputs(
        boundaryConditionType,
        boundaryTemperatureC,
        heatFlux,
        heatTransferCoefficient,
        volumetricPowerDensity);
}

bool HeatSystem::rebuildDomainRuntime() {
    if (!domainDirty) {
        return true;
    }

    const std::size_t modelCount = std::min({
        modelRuntimeModelIds.size(),
        modelSurfacePositions.size(),
        modelSurfaceNormals.size(),
        modelSurfaceTriangleIndices.size()});
    std::unordered_set<uint32_t> incomingModelIds;
    for (std::size_t index = 0; index < modelCount; ++index) {
        if (modelRuntimeModelIds[index] != 0) {
            incomingModelIds.insert(modelRuntimeModelIds[index]);
        }
    }
    domainRuntime.removeModelsNotIn(incomingModelIds);

    std::unordered_set<uint32_t> configuredModelIds;
    for (std::size_t index = 0; index < modelCount; ++index) {
        const uint32_t runtimeModelId = modelRuntimeModelIds[index];
        if (runtimeModelId == 0 || !configuredModelIds.insert(runtimeModelId).second) {
            continue;
        }

        const auto& positions = modelSurfacePositions[index];
        const auto& normals = modelSurfaceNormals[index];
        const auto& triangleIndices = modelSurfaceTriangleIndices[index];
        HeatModelRuntime* model = domainRuntime.getModelByRuntimeId(runtimeModelId);
        const bool geometryChanged = model &&
            (model->getSurfacePositions() != positions ||
             model->getSurfaceNormals() != normals ||
             model->getSurfaceTriangleIndices() != triangleIndices);

        if (!model || geometryChanged) {
            auto replacement = std::make_unique<HeatModelRuntime>(
                vulkanDevice,
                memoryAllocator,
                positions,
                normals,
                triangleIndices,
                transferCommandPool,
                getInitialTemperatureC(runtimeModelId));
            configureModelProperties(*replacement, runtimeModelId);
            if (replacement->isInitialized()) {
                domainRuntime.setModel(runtimeModelId, std::move(replacement));
            } else {
                std::cerr << "[HeatSystem] Failed to initialize heat model for model "
                          << runtimeModelId << std::endl;
                domainRuntime.removeModel(runtimeModelId);
            }
        } else {
            configureModelProperties(*model, runtimeModelId);
        }
    }

    domainDirty = false;
    return true;
}


void HeatSystem::setParams(float updatedContactThermalConductance, float updatedSimulationDuration) {
    if (contactThermalConductance != updatedContactThermalConductance) {
        contactThermalConductance = updatedContactThermalConductance;
        heatParamsDirty = true;
        if (domainRuntime.isGlobalDomain()) {
            domainRuntime.markGlobalValuesDirty();
        }
    }
    if (timeline.getDuration() != updatedSimulationDuration) {
        timeline.setDuration(updatedSimulationDuration);
        heatParamsDirty = true;
    }
}

uint32_t HeatSystem::computeTimelineFrameCount() const {
    if (timeline.getDuration() <= 0.0f) {
        return 0;
    }
    return std::max(1u, static_cast<uint32_t>(std::ceil(timeline.getDuration() * TimelineFPS)));
}

void HeatSystem::clearGlobalVoronoiInput() {
    for (auto& [runtimeModelId, buffer] : modelGMLSSurfaceStencilBufferByModelId) {
        if (buffer != VK_NULL_HANDLE) {
            freeBuffer(memoryAllocator, buffer, modelGMLSSurfaceStencilBufferOffsetByModelId[runtimeModelId]);
        }
    }
    for (auto& [runtimeModelId, buffer] : modelGMLSSurfaceWeightBufferByModelId) {
        if (buffer != VK_NULL_HANDLE) {
            freeBuffer(memoryAllocator, buffer, modelGMLSSurfaceWeightBufferOffsetByModelId[runtimeModelId]);
        }
    }
    for (auto& [runtimeModelId, buffer] : modelGMLSSurfaceGradientWeightBufferByModelId) {
        if (buffer != VK_NULL_HANDLE) {
            freeBuffer(memoryAllocator, buffer, modelGMLSSurfaceGradientWeightBufferOffsetByModelId[runtimeModelId]);
        }
    }
    simNodeCounts.clear();
    modelGMLSSurfaceStencilBufferByModelId.clear();
    modelGMLSSurfaceStencilBufferOffsetByModelId.clear();
    modelGMLSSurfaceWeightBufferByModelId.clear();
    modelGMLSSurfaceWeightBufferOffsetByModelId.clear();
    modelGMLSSurfaceWeightCountByModelId.clear();
    modelGMLSSurfaceGradientWeightBufferByModelId.clear();
    modelGMLSSurfaceGradientWeightBufferOffsetByModelId.clear();
    modelGMLSSurfaceGradientWeightCountByModelId.clear();
    modelLocalToWorldByModelId.clear();
    domainRuntime.clearGlobalDomain();
    voronoiConfigDirty = true;
}

void HeatSystem::setGlobalVoronoiInput(
    const HeatDomainRuntime::GlobalThermalDomain& domain,
    const std::unordered_map<uint32_t, std::array<float, 16>>& modelLocalToWorld) {
    if (!domain.isValid()) {
        clearGlobalVoronoiInput();
        return;
    }
    modelLocalToWorldByModelId = modelLocalToWorld;
    domainRuntime.setGlobalDomain(domain);
    domainDirty = true;
    voronoiConfigDirty = true;
}

void HeatSystem::update() {
    const auto currentTime = std::chrono::steady_clock::now();
    float deltaTime = std::chrono::duration<float>(currentTime - lastUpdateTime).count();
    lastUpdateTime = currentTime;

    const float maxFrameDelta = 1.0f / TimelineFPS;
    if (deltaTime > maxFrameDelta) {
        deltaTime = maxFrameDelta;
    }

    processResetTrigger();
    physicsStepCompletedThisFrame = false;
    domainRuntime.clearGlobalSynchronization();

    if (domainRuntime.isGlobalDomain()) {
        bool completedDestinationIsA = false;
        bool newSolveCompleted = false;
        if (domainRuntime.pollGlobalSolve(completedDestinationIsA)) {
            temperatureBufferAIsCurrent = completedDestinationIsA;
            physicsStepCompletedThisFrame = true;
            newSolveCompleted = true;
            if (domainRuntime.hasSolveFailed()) {
                std::cerr << "[HeatSystem-Diag] PCG solve reported numerical breakdown!" << std::endl;
            }
        }

        if (domainRuntime.globalValuesDirty() &&
            !domainRuntime.updateGlobalValues(domainRuntime.getActiveModels(), FixedTimeStep, contactThermalConductance)) {
            std::cerr << "[HeatSystem] global value update failed" << std::endl;
        }

        forwardSim(deltaTime);

        if (syntheticDirichletTestEnabled) {
            const float syntheticTemperatureC = 20.0f + 30.0f * (0.5f + 0.5f * std::sin(simulatedTime * 0.5f));
            for (const auto& [runtimeModelId, _] : domainRuntime.getActiveModels()) {
                setRuntimeDirichletTemperatureC(runtimeModelId, 0u, syntheticTemperatureC);
            }
        }

        if (shouldStepPhysics && !domainRuntime.isGlobalSolveInFlight()) {
            if (domainRuntime.launchGlobalSolve(temperatureBufferAIsCurrent)) {
                physicsAccumulator = std::max(0.0f, physicsAccumulator - FixedTimeStep);
            }
            shouldStepPhysics = false;
        }

        domainRuntime.prepareGlobalRenderSynchronization(temperatureBufferAIsCurrent, newSolveCompleted);
    } else {
        forwardSim(deltaTime);

        if (syntheticDirichletTestEnabled) {
            const float syntheticTemperatureC = 20.0f + 30.0f * (0.5f + 0.5f * std::sin(simulatedTime * 0.5f));
            for (const auto& [runtimeModelId, _] : domainRuntime.getActiveModels()) {
                setRuntimeDirichletTemperatureC(runtimeModelId, 0u, syntheticTemperatureC);
            }
        }

        physicsStepCompletedThisFrame = shouldStepPhysics;
    }
}

ComputePass::Synchronization HeatSystem::getSynchronization() const {
    if (domainRuntime.isGlobalDomain()) {
        return domainRuntime.getGlobalSynchronization();
    }
    return ComputePass::Synchronization{};
}

bool HeatSystem::setRuntimeDirichletTemperatureC(uint32_t runtimeModelId, uint32_t regionId, float temperatureC) {
    HeatModelRuntime* heatModel = domainRuntime.getModelByRuntimeId(runtimeModelId);
    if (!heatModel) {
        return false;
    }
    return heatModel->setRuntimeDirichletTemperatureC(regionId, temperatureC);
}

bool HeatSystem::setRuntimeNeumannHeatFlux(uint32_t runtimeModelId, uint32_t regionId, float heatFlux) {
    HeatModelRuntime* model = domainRuntime.getModelByRuntimeId(runtimeModelId);
    if (!model || !model->setNeumannHeatFlux(regionId, heatFlux)) {
        return false;
    }
    if (domainRuntime.isGlobalDomain()) {
        domainRuntime.markGlobalValuesDirty();
    }
    return true;
}

bool HeatSystem::setRuntimeRobinState(uint32_t runtimeModelId, uint32_t regionId,
    float ambientTemperatureC, float heatTransferCoefficient) {
    HeatModelRuntime* model = domainRuntime.getModelByRuntimeId(runtimeModelId);
    if (!model || !model->setRobinState(regionId, ambientTemperatureC, heatTransferCoefficient)) {
        return false;
    }
    if (domainRuntime.isGlobalDomain()) {
        domainRuntime.markGlobalValuesDirty();
    }
    return true;
}

bool HeatSystem::setRuntimeRobinTemperatureC(uint32_t runtimeModelId, uint32_t regionId, float ambientTemperatureC) {
    HeatModelRuntime* model = domainRuntime.getModelByRuntimeId(runtimeModelId);
    return model && model->setRuntimeRobinTemperatureC(regionId, ambientTemperatureC);
}

bool HeatSystem::setRuntimeVolumetricPowerDensity(uint32_t runtimeModelId, float powerDensity) {
    HeatModelRuntime* model = domainRuntime.getModelByRuntimeId(runtimeModelId);
    if (!model || !model->setVolumetricPowerDensity(powerDensity)) {
        return false;
    }
    return true;
}

void HeatSystem::processResetTrigger() {
    if (timeline.getResetCounter() != processedResetCounter) {
        processedResetCounter = timeline.getResetCounter();
        resetSimulationState();
    }
}

void HeatSystem::forwardSim(float deltaTime) {
    auto* playbackData = playbackRuntime.getMappedPlaybackData();
    if (!playbackData) {
        timeline.setPosition(0.0f);
        shouldStepPhysics = false;
        return;
    }

    const bool isPlaying = timeline.isPlaying();
    const bool isScrubbing = timeline.isScrubbing();
    const float duration = timeline.getDuration();

    playbackData->resetCounter = timeline.getResetCounter();
    playbackData->recordedTimelineFrames = getRecordedTimelineFrames();
    playbackData->timelineFrameCount = computeTimelineFrameCount();

    const bool canAdvance = isPlaying && !isScrubbing && simulatedTime < duration;
    if (canAdvance) {
        physicsAccumulator = std::min(physicsAccumulator + deltaTime, FixedTimeStep);
    } else {
        physicsAccumulator = 0.0f;
    }
    shouldStepPhysics = canAdvance && physicsAccumulator >= FixedTimeStep;

    if (shouldStepPhysics) {
        const float remaining = std::max(0.0f, duration - simulatedTime);
        const float simDelta = std::min(FixedTimeStep, remaining);

        playbackData->deltaTime = simDelta;
        if (!domainRuntime.isGlobalDomain()) {
            simulatedTime = std::min(duration, simulatedTime + simDelta);
            physicsAccumulator = std::max(0.0f, physicsAccumulator - FixedTimeStep);
        }
    } else {
        playbackData->deltaTime = 0.0f;
    }

    if (domainRuntime.isGlobalDomain() && physicsStepCompletedThisFrame) {
        const float remaining = std::max(0.0f, duration - simulatedTime);
        const float simDelta = std::min(FixedTimeStep, remaining);
        simulatedTime = std::min(duration, simulatedTime + simDelta);
    }

    // Advance display timeline
    if (isScrubbing) {
        timeline.setPosition(static_cast<float>(timeline.getScrubFrame()) / TimelineFPS);
    } else if (isPlaying) {
        timeline.advancePosition(deltaTime);
        timeline.setPosition(std::min(timeline.getCurrentPosition(), simulatedTime));
    }

}

bool HeatSystem::ensureConfigured() {
    const bool needsHardRebuild = domainDirty || voronoiConfigDirty || heatParamsDirty;
    if (!needsHardRebuild) return true;


    if (vkDeviceWaitIdle(vulkanDevice.getDevice()) != VK_SUCCESS) {
        std::cerr << "[HeatSystem-Diag] ensureConfigured FAILED: vkDeviceWaitIdle" << std::endl;
        return false;
    }

    const bool modelsReady = rebuildDomainRuntime();
    if (!modelsReady) {
        std::cerr << "[HeatSystem-Diag] ensureConfigured FAILED: rebuildDomainRuntime"
                  << " domainDirty=" << domainDirty
                  << " activeModels=" << domainRuntime.getActiveModels().size() << std::endl;
        return false;
    }

    configureModelSimResources();

    for (const auto& [runtimeModelId, heatModel] : domainRuntime.getActiveModels()) {
        if (!heatModel) continue;
        const auto countIt = simNodeCounts.find(runtimeModelId);
        const uint32_t nodeCount = (countIt != simNodeCounts.end()) ? countIt->second : 0;
        if (!heatModel->ensureSimulationBuffers(nodeCount)) {
            std::cerr << "[HeatSystem-Diag] ensureConfigured FAILED: ensureSimulationBuffers"
                      << " runtimeModelId=" << runtimeModelId
                      << " nodeCount=" << nodeCount << std::endl;
            return false;
        }
    }

    const bool heatVoronoiReady = rebuildVoronoiRuntime();

    configureGMLSSurfaceWeights(heatVoronoiReady);

    if (!heatVoronoiReady) {
        std::cerr << "[HeatSystem-Diag] ensureConfigured FAILED: rebuildVoronoiRuntime"
                  << " modelRuntimeModelIds=" << modelRuntimeModelIds.size()
                  << " activeModels=" << domainRuntime.getActiveModels().size() << std::endl;
        playbackRuntime.cleanup(memoryAllocator);
        return false;
    }

    // Initialize playback after the rebuilt simulation resources are valid
    const uint32_t frameCapacity = computeHistoryFrameCapacity();
    for (const auto& [runtimeModelId, heatModel] : domainRuntime.getActiveModels()) {
        (void)runtimeModelId;
        if (!heatModel) continue;
        heatModel->initializePlayback(vulkanDevice, memoryAllocator, frameCapacity);
    }

    if (domainRuntime.isGlobalDomain()) {
        const auto& models = domainRuntime.getActiveModels();
        if (domainRuntime.globalTopologyDirty() || !domainRuntime.hasGlobalSolver()) {
            if (!domainRuntime.rebuildGlobalSolver(models, vulkanDevice, FixedTimeStep, contactThermalConductance)) {
                std::cerr << "[HeatSystem-Diag] ensureConfigured FAILED: rebuildGlobalSolver"
                          << " activeModels=" << models.size() << std::endl;
                playbackRuntime.cleanup(memoryAllocator);
                return false;
            }
        } else if (domainRuntime.globalValuesDirty()) {
            if (!domainRuntime.updateGlobalValues(models, FixedTimeStep, contactThermalConductance)) {
                std::cerr << "[HeatSystem-Diag] ensureConfigured FAILED: updateGlobalValues"
                          << " activeModels=" << models.size() << std::endl;
                playbackRuntime.cleanup(memoryAllocator);
                return false;
            }
        }
    }
    if (!buildModelBoundaryBuffers()) {
        std::cerr << "[HeatSystem-Diag] ensureConfigured FAILED: buildModelBoundaryBuffers"
                  << " activeModels=" << domainRuntime.getActiveModels().size() << std::endl;
        return false;
    }
    resetSimulationState();

    voronoiConfigDirty = false;
    heatParamsDirty = false;
    return true;
}

bool HeatSystem::setupDescriptors(
    const std::vector<VkBuffer>& surfaceBuffers,
    const std::vector<VkDeviceSize>& surfaceOffsets,
    const std::vector<VkBuffer>& gradientBuffers,
    const std::vector<VkDeviceSize>& gradientOffsets) {
    if (!recreateDescriptorPools()) {
        return false;
    }

    if (!playbackRuntime.initialize(vulkanDevice, memoryAllocator)) return false;

    for (size_t i = 0; i < modelRuntimeModelIds.size() && i < surfaceBuffers.size(); ++i) {
        const uint32_t runtimeModelId = modelRuntimeModelIds[i];
        HeatModelRuntime* heatModel = domainRuntime.getModelByRuntimeId(runtimeModelId);
        if (!heatModel) continue;

        auto itCount = simNodeCounts.find(runtimeModelId);
        uint32_t nodeCount = (itCount != simNodeCounts.end()) ? itCount->second : 0;
        if (nodeCount == 0) continue;

        auto* playback = heatModel->getPlayback();
        VkBuffer historyBuf = VK_NULL_HANDLE;
        VkDeviceSize historyOff = 0;
        uint32_t historyFrameCap = 0;
        if (playback) {
            historyBuf = playback->getHistoryBuffer();
            historyOff = playback->getHistoryBufferOffset();
            historyFrameCap = playback->getFrameCapacity();
        }
        heatModel->setHistoryBuffer(historyBuf, historyOff, historyFrameCap);
        if (!heatModel->updateAllDescriptors(
            surfaceBuffers[i],
            surfaceOffsets[i],
            gradientBuffers[i],
            gradientOffsets[i],
            surfaceStage->getDescriptorSetLayout(),
            surfaceStage->getGradientDescriptorSetLayout(),
            surfaceStage->getDescriptorPool(),
            playbackRuntime.getPlaybackBuffer(),
            playbackRuntime.getPlaybackBufferOffset(),
            true)) {
            return false;
        }
    }

    return true;
}

void HeatSystem::configureGMLSSurfaceWeights(bool heatVoronoiReady) {
    for (const auto& [runtimeModelId, heatModel] : domainRuntime.getActiveModels()) {
        if (!heatModel) continue;

        if (!heatVoronoiReady) {
            heatModel->setGMLSSurfaceWeights(VK_NULL_HANDLE, 0, VK_NULL_HANDLE, 0, 0, VK_NULL_HANDLE, 0, 0);
            continue;
        }

        const auto stencilIt = modelGMLSSurfaceStencilBufferByModelId.find(runtimeModelId);
        const auto stencilOffsetIt = modelGMLSSurfaceStencilBufferOffsetByModelId.find(runtimeModelId);
        const auto valueWeightIt = modelGMLSSurfaceWeightBufferByModelId.find(runtimeModelId);
        const auto valueWeightOffsetIt = modelGMLSSurfaceWeightBufferOffsetByModelId.find(runtimeModelId);
        const auto valueWeightCountIt = modelGMLSSurfaceWeightCountByModelId.find(runtimeModelId);
        const auto gradientWeightIt = modelGMLSSurfaceGradientWeightBufferByModelId.find(runtimeModelId);
        const auto gradientWeightOffsetIt = modelGMLSSurfaceGradientWeightBufferOffsetByModelId.find(runtimeModelId);
        const auto gradientWeightCountIt = modelGMLSSurfaceGradientWeightCountByModelId.find(runtimeModelId);

        heatModel->setGMLSSurfaceWeights(
            stencilIt != modelGMLSSurfaceStencilBufferByModelId.end() ? stencilIt->second : VK_NULL_HANDLE,
            stencilOffsetIt != modelGMLSSurfaceStencilBufferOffsetByModelId.end() ? stencilOffsetIt->second : 0,
            valueWeightIt != modelGMLSSurfaceWeightBufferByModelId.end() ? valueWeightIt->second : VK_NULL_HANDLE,
            valueWeightOffsetIt != modelGMLSSurfaceWeightBufferOffsetByModelId.end() ? valueWeightOffsetIt->second : 0,
            valueWeightCountIt != modelGMLSSurfaceWeightCountByModelId.end() ? valueWeightCountIt->second : 0,
            gradientWeightIt != modelGMLSSurfaceGradientWeightBufferByModelId.end() ? gradientWeightIt->second : VK_NULL_HANDLE,
            gradientWeightOffsetIt != modelGMLSSurfaceGradientWeightBufferOffsetByModelId.end() ? gradientWeightOffsetIt->second : 0,
            gradientWeightCountIt != modelGMLSSurfaceGradientWeightCountByModelId.end() ? gradientWeightCountIt->second : 0);
    }
}

bool HeatSystem::recreateDescriptorPools() {
    uint32_t numModels = static_cast<uint32_t>(modelRuntimeModelIds.size());

    if (!surfaceStage->createDescriptorPool(numModels)) {
        return false;
    }

    return true;
}

void HeatSystem::configureModelSimResources() {
    if (domainRuntime.isGlobalDomain()) {
        const auto& domain = domainRuntime.getGlobalDomain();
        const auto& models = domainRuntime.getActiveModels();
        std::unordered_map<uint32_t, uint32_t> fragmentCounts;
        for (const auto& fragment : domain.fragments) {
            fragmentCounts[fragment.instanceId] = fragmentCounts[fragment.instanceId] + 1;
        }

        for (const auto& [runtimeModelId, heatModel] : models) {
            if (!heatModel) continue;
            const auto countIt = fragmentCounts.find(runtimeModelId);
            if (countIt == fragmentCounts.end() || countIt->second == 0) {
                continue;
            }
            const uint32_t nodeCount = countIt->second;
            simNodeCounts[runtimeModelId] = nodeCount;

            std::vector<glm::vec3> nodePositions;
            nodePositions.reserve(nodeCount);
            for (const auto& fragment : domain.fragments) {
                if (fragment.instanceId != runtimeModelId || fragment.seedId >= domain.seedPositions.size()) {
                    continue;
                }
                nodePositions.push_back(domain.seedPositions[fragment.seedId]);
            }
            if (!nodePositions.empty()) {
                heatModel->setNodePositions(nodePositions);
            }
        }
    }
}

bool HeatSystem::buildModelBoundaryBuffers() {
    for (const auto& [runtimeModelId, model] : domainRuntime.getActiveModels()) {
        if (!model) {
            std::cerr << "[HeatSystem-Diag] buildModelBoundaryBuffers FAILED: null model id=" << runtimeModelId << std::endl;
            return false;
        }
        if (!model->buildBoundaryBuffers()) {
            std::cerr << "[HeatSystem-Diag] buildModelBoundaryBuffers FAILED: buildBoundaryBuffers id=" << runtimeModelId << std::endl;
            return false;
        }
    }
    return true;
}


void HeatSystem::setActive(bool active) {
    isActive = active;
}

void HeatSystem::setPlaybackState(bool paused, uint32_t resetCounter) {
    timeline.setPlaying(!paused);
    timeline.setResetCounter(resetCounter);
}

void HeatSystem::setRewindFrame(uint32_t frame) {
    timeline.setScrubFrame(frame);
    if (frame != heat::NoRewindFrame) {
        timeline.setPosition(static_cast<float>(frame) / TimelineFPS);
    }
}

uint32_t HeatSystem::getRewindFrame() const {
    return timeline.getScrubFrame();
}

uint32_t HeatSystem::getRecordedTimelineFrames() const {
    uint32_t maxRecorded = 0;
    for (const auto& [id, heatModel] : domainRuntime.getActiveModels()) {
        if (heatModel) {
            auto* pb = heatModel->getPlayback();
            if (pb) maxRecorded = std::max(maxRecorded, pb->getRecordedFrameCount());
        }
    }
    return maxRecorded;
}

uint32_t HeatSystem::getTimelineFrameCount() const {
    return computeTimelineFrameCount();
}

void HeatSystem::resetSimulationState() {
    timeline.setPosition(0.0f);
    timeline.setScrubFrame(heat::NoRewindFrame);
    simulatedTime = 0.0f;
    shouldStepPhysics = false;
    needsInitialCapture = true;
    physicsAccumulator = 0.0f;
    temperatureBufferAIsCurrent = true;
    playbackRuntime.reset();
    resetVoronoiTemperatures();

    for (const auto& [id, heatModel] : domainRuntime.getActiveModels()) {
        if (!heatModel) {
            continue;
        }

        auto* playback = heatModel->getPlayback();
        if (playback) {
            playback->reset();
        }
    }
}

void HeatSystem::resetVoronoiTemperatures() {
    for (const auto& [runtimeModelId, heatModel] : domainRuntime.getActiveModels()) {
        if (!heatModel) continue;

        const float temperature = heatModel->getInitialTemperatureC();
        uint32_t nodeCount = heatModel->getSimNodeCount();
        if (nodeCount == 0) continue;

        // Upload temp values via staging buffer
        VkDeviceSize bufferSize = nodeCount * sizeof(float);
        std::vector<float> temps(nodeCount, temperature);

        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        VkDeviceSize stagingOffset = 0;
        void* stagingMapped = nullptr;
        if (createStagingBuffer(memoryAllocator, bufferSize, stagingBuffer, stagingOffset, &stagingMapped) != VK_SUCCESS || !stagingMapped) {
            continue;
        }

        std::memcpy(stagingMapped, temps.data(), static_cast<size_t>(bufferSize));

        VkCommandBuffer cmd = transferCommandPool.beginCommands();
        if (cmd != VK_NULL_HANDLE) {
            // Copy to tempBufferA
            VkBufferCopy regionA{};
            regionA.srcOffset = stagingOffset;
            regionA.dstOffset = 0;
            regionA.size = bufferSize;
            vkCmdCopyBuffer(cmd, stagingBuffer, heatModel->getTempBufferA(), 1, &regionA);

            // Copy to tempBufferB
            VkBufferCopy regionB{};
            regionB.srcOffset = stagingOffset;
            regionB.dstOffset = 0;
            regionB.size = bufferSize;
            vkCmdCopyBuffer(cmd, stagingBuffer, heatModel->getTempBufferB(), 1, &regionB);

            transferCommandPool.endCommands(cmd);
        }

        memoryAllocator.free(stagingBuffer, stagingOffset);
    }
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
    bool voronoiIsReady = voronoiReady();
    bool hasBuffers = !computeCommandBuffers.empty();
    return isActive && voronoiIsReady && hasBuffers;
}

bool HeatSystem::voronoiReady() const {
    if (!playbackRuntime.isInitialized() || modelRuntimeModelIds.empty()) {
        return false;
    }
    if (domainRuntime.isGlobalDomain()) {
        return domainRuntime.hasGlobalSolver() || domainRuntime.allGlobalFragmentsFixed();
    }
    return false;
}

void HeatSystem::recordComputeCommands(VkCommandBuffer commandBuffer, uint32_t currentFrame) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        return;
    }

    if (timingQueryPool != VK_NULL_HANDLE) {
        vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, timingQueryPool, timingStartQuery);
    }

    bool hasWork = hasDispatchableComputeWork();
    if (hasWork &&
        surfaceStage) {
        const auto* pbData = playbackRuntime.getMappedPlaybackData();
        const uint32_t recordedFrames = getRecordedTimelineFrames();

        // Capture the initial state on the first frame after a reset
        if (needsInitialCapture) {
            for (const auto& [id, heatModel] : domainRuntime.getActiveModels()) {
                if (!heatModel || heatModel->getSimNodeCount() == 0) continue;

                VkBufferMemoryBarrier barrier{};
                barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                barrier.buffer = heatModel->getTempBufferA();
                barrier.offset = 0;
                barrier.size = heatModel->getSimNodeCount() * sizeof(float);
                vkCmdPipelineBarrier(commandBuffer,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                    0, 0, nullptr, 1, &barrier, 0, nullptr);

                auto* pb = heatModel->getPlayback();
                if (pb) {
                    pb->recordFrame(commandBuffer,
                        heatModel->getTempBufferA(),
                        0);
                }
            }
            needsInitialCapture = false;
        }

        // Physics step
        const bool captureFrame = physicsStepCompletedThisFrame && recordedFrames < computeHistoryFrameCapacity();

        if (captureFrame) {
            std::vector<VkBufferMemoryBarrier> captureBarriers;
            for (const auto& [id, heatModel] : domainRuntime.getActiveModels()) {
                if (!heatModel || heatModel->getSimNodeCount() == 0) continue;

                VkBuffer finalBuf = temperatureBufferAIsCurrent
                    ? heatModel->getTempBufferA()
                    : heatModel->getTempBufferB();

                VkBufferMemoryBarrier barrier{};
                barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                barrier.buffer = finalBuf;
                barrier.offset = 0;
                barrier.size = heatModel->getSimNodeCount() * sizeof(float);
                captureBarriers.push_back(barrier);
            }
            if (!captureBarriers.empty()) {
                vkCmdPipelineBarrier(commandBuffer,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    0, 0, nullptr,
                    static_cast<uint32_t>(captureBarriers.size()), captureBarriers.data(),
                    0, nullptr);
            }
            for (const auto& [id, heatModel] : domainRuntime.getActiveModels()) {
                if (!heatModel || heatModel->getSimNodeCount() == 0) continue;
                auto* pb = heatModel->getPlayback();
                if (!pb) continue;
                VkBuffer finalBuf = temperatureBufferAIsCurrent
                    ? heatModel->getTempBufferA()
                    : heatModel->getTempBufferB();
                pb->recordFrame(commandBuffer, finalBuf, 0);
            }
        }

        // Display pass
        const float displayTime = timeline.getCurrentPosition();
        const uint32_t displayFrame = static_cast<uint32_t>(displayTime * TimelineFPS);
        const bool replayFromHistory = displayFrame < recordedFrames;

        if (replayFromHistory) {
            for (const auto& [id, heatModel] : domainRuntime.getActiveModels()) {
                if (!heatModel) continue;
                auto* pb = heatModel->getPlayback();
                if (!pb) continue;
                heatModel->updateHistoryDescriptorOffset(displayFrame, pb->getFrameStride(), currentFrame);
            }
        }

        const bool finalWritesBufferB = !temperatureBufferAIsCurrent;

        if (physicsStepCompletedThisFrame) {
            std::vector<VkBufferMemoryBarrier> surfaceReadBarriers;
            for (const auto& [id, heatModel] : domainRuntime.getActiveModels()) {
                if (!heatModel || heatModel->getSimNodeCount() == 0) continue;

                VkBuffer finalBuf = finalWritesBufferB ? heatModel->getTempBufferB() : heatModel->getTempBufferA();

                VkBufferMemoryBarrier barrier{};
                barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                barrier.buffer = finalBuf;
                barrier.offset = 0;
                barrier.size = heatModel->getSimNodeCount() * sizeof(float);
                surfaceReadBarriers.push_back(barrier);
            }
            if (!surfaceReadBarriers.empty()) {
                vkCmdPipelineBarrier(commandBuffer,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    0, 0, nullptr,
                    static_cast<uint32_t>(surfaceReadBarriers.size()), surfaceReadBarriers.data(),
                    0, nullptr);
            }
        }

        surfaceStage->dispatchSurfaceTemperatureUpdates(
            commandBuffer, domainRuntime.getActiveModels(),
            replayFromHistory, finalWritesBufferB, currentFrame);

        if (currentFrame % 4 == 0) {
            surfaceStage->dispatchSurfaceGradientUpdates(
                commandBuffer, domainRuntime.getActiveModels(),
                replayFromHistory, finalWritesBufferB, currentFrame);
        }
    }

    if (timingQueryPool != VK_NULL_HANDLE) {
        vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timingQueryPool, timingEndQuery);
    }
    vkEndCommandBuffer(commandBuffer);
}

void HeatSystem::setComputeTimingQueries(VkQueryPool queryPool, uint32_t startQuery, uint32_t endQuery) {
    timingQueryPool = queryPool;
    timingStartQuery = startQuery;
    timingEndQuery = endQuery;
}

bool HeatSystem::rebuildGlobalThermalDomain() {
    if (!domainRuntime.isGlobalDomain()) {
        return false;
    }
    const HeatDomainRuntime::GlobalThermalDomain& domain = domainRuntime.getGlobalDomain();
    if (domain.fragments.empty()) {
        std::cerr << "[HeatSystem-Diag] rebuildGlobalThermalDomain FAILED: empty fragments" << std::endl;
        return false;
    }

    const VkDeviceSize alignment = vulkanDevice.getPhysicalDeviceProperties().limits.minStorageBufferOffsetAlignment;
    const auto& models = domainRuntime.getActiveModels();
    std::vector<uint32_t> modelIds;
    modelIds.reserve(models.size());
    for (const auto& [runtimeModelId, model] : models) {
        if (model) modelIds.push_back(runtimeModelId);
    }
    std::sort(modelIds.begin(), modelIds.end());

    std::unordered_map<uint32_t, std::vector<uint32_t>> fragmentIdsByModelId;
    for (uint32_t fragmentId = 0; fragmentId < domain.fragments.size(); ++fragmentId) {
        fragmentIdsByModelId[domain.fragments[fragmentId].instanceId].push_back(fragmentId);
    }

    for (uint32_t runtimeModelId : modelIds) {
        HeatModelRuntime* heatModelPtr = domainRuntime.getModelByRuntimeId(runtimeModelId);
        const auto fragmentIdsIt = fragmentIdsByModelId.find(runtimeModelId);
        if (!heatModelPtr) {
            std::cerr << "[HeatSystem-Diag] rebuildGlobalThermalDomain FAILED: no HeatModelRuntime for id=" << runtimeModelId << std::endl;
            return false;
        }
        if (fragmentIdsIt == fragmentIdsByModelId.end() || fragmentIdsIt->second.empty()) {
            std::cerr << "[HeatSystem-Diag] rebuildGlobalThermalDomain FAILED: no fragments for id=" << runtimeModelId << std::endl;
            return false;
        }
        const auto countIt = simNodeCounts.find(runtimeModelId);
        if (countIt == simNodeCounts.end() || countIt->second != fragmentIdsIt->second.size()) {
            std::cerr << "[HeatSystem-Diag] rebuildGlobalThermalDomain FAILED: simNodeCount mismatch id=" << runtimeModelId << std::endl;
            return false;
        }

        const float metersPerUnit = heatModelPtr->getMetersPerWorldUnit();
        const float volumeScale = metersPerUnit * metersPerUnit * metersPerUnit;
        const uint32_t modelNodeCount = countIt->second;

        const float d = heatModelPtr->getDensity();
        const float s = heatModelPtr->getSpecificHeat();

        std::vector<float> nodalThermalMasses(modelNodeCount);
        std::vector<uint32_t> boundaryNodeIds;
        boundaryNodeIds.reserve(modelNodeCount);
        std::vector<float> boundaryAreas;
        boundaryAreas.reserve(modelNodeCount);
        for (uint32_t localNodeIndex = 0; localNodeIndex < modelNodeCount; ++localNodeIndex) {
            const HeatDomainRuntime::GlobalFragment& fragment = domain.fragments[fragmentIdsIt->second[localNodeIndex]];
            if (!std::isfinite(fragment.volume) || fragment.volume <= 0.0f) {
                std::cerr << "[HeatSystem-Diag] rebuildGlobalThermalDomain FAILED: bad volume id=" << runtimeModelId
                          << " fragment=" << localNodeIndex << " seed=" << fragment.seedId
                          << " instance=" << fragment.instanceId
                          << " volume=" << fragment.volume << " area=" << fragment.surfaceBoundaryArea;
                if (fragment.seedId < domain.seedPositions.size()) {
                    const glm::vec3& position = domain.seedPositions[fragment.seedId];
                    std::cerr << " at=(" << position.x << "," << position.y << "," << position.z << ")";
                }
                std::cerr << std::endl;
                return false;
            }
            nodalThermalMasses[localNodeIndex] = d * s * fragment.volume * volumeScale;
            if (fragment.surfaceBoundaryArea > 0.0f) {
                boundaryNodeIds.push_back(localNodeIndex);
            }
            boundaryAreas.push_back(fragment.surfaceBoundaryArea);
        }

        heatModelPtr->setNodalThermalMasses(std::move(nodalThermalMasses));
        if (!heatModelPtr->configureBoundary(boundaryNodeIds, boundaryAreas)) {
            std::cerr << "[HeatSystem-Diag] rebuildGlobalThermalDomain FAILED: configureBoundary id=" << runtimeModelId << std::endl;
            return false;
        }
    }

    // Synthesize identity GMLS surface weights: each surface vertex maps to its
    // nearest fragment slot (weight 1.0), replacing the mesh-mode GMLS fallback.
    for (uint32_t runtimeModelId : modelIds) {
        HeatModelRuntime* heatModelPtr = domainRuntime.getModelByRuntimeId(runtimeModelId);
        const auto fragmentIdsIt = fragmentIdsByModelId.find(runtimeModelId);
        const auto placementIt = modelLocalToWorldByModelId.find(runtimeModelId);
        if (!heatModelPtr || fragmentIdsIt == fragmentIdsByModelId.end() ||
            placementIt == modelLocalToWorldByModelId.end()) {
            return false;
        }
        const std::array<float, 16>& placement = placementIt->second;
        const glm::mat4 localToWorld(
            placement[0], placement[1], placement[2], placement[3],
            placement[4], placement[5], placement[6], placement[7],
            placement[8], placement[9], placement[10], placement[11],
            placement[12], placement[13], placement[14], placement[15]);

        const auto& fragmentIds = fragmentIdsIt->second;
        std::vector<glm::vec3> fragmentSeedPositions;
        fragmentSeedPositions.reserve(fragmentIds.size());
        for (uint32_t fragmentId : fragmentIds) {
            const uint32_t seedId = domain.fragments[fragmentId].seedId;
            if (seedId >= domain.seedPositions.size()) {
                return false;
            }
            fragmentSeedPositions.push_back(domain.seedPositions[seedId]);
        }

        const auto& surfacePositions = heatModelPtr->getSurfacePositions();
        const auto& surfaceNormals = heatModelPtr->getSurfaceNormals();
        const size_t surfaceVertexCount = surfacePositions.size();

        VoronoiNodeIndex nodeIndex;
        nodeIndex.rebuild(fragmentSeedPositions);

        std::vector<voronoi::GMLSSurfaceStencil> stencils(surfaceVertexCount);
        std::vector<voronoi::GMLSSurfaceWeight> valueWeights;
        std::vector<voronoi::GMLSSurfaceGradientWeight> gradientWeights;
        valueWeights.reserve(surfaceVertexCount * 16);
        gradientWeights.reserve(surfaceVertexCount * 16);

        uint32_t sdfChannel = UINT32_MAX;
        for (size_t c = 0; c < domain.sdfRuntimeModelIds.size(); ++c) {
            if (domain.sdfRuntimeModelIds[c] == runtimeModelId) {
                sdfChannel = static_cast<uint32_t>(c);
                break;
            }
        }

        const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(localToWorld)));
        constexpr uint32_t candidateCount = 64;
        constexpr uint32_t maximumSupportCount = 32;

        for (size_t vertexId = 0; vertexId < surfaceVertexCount; ++vertexId) {
            const glm::vec3 worldPos = glm::vec3(localToWorld * glm::vec4(surfacePositions[vertexId], 1.0f));
            glm::vec3 worldNorm = (vertexId < surfaceNormals.size())
                ? normalMatrix * surfaceNormals[vertexId]
                : glm::vec3(0.0f, 1.0f, 0.0f);
            if (glm::dot(worldNorm, worldNorm) > 1e-8f) {
                worldNorm = glm::normalize(worldNorm);
            } else {
                worldNorm = glm::vec3(0.0f, 1.0f, 0.0f);
            }

            std::vector<uint32_t> nearestNodeIds;
            std::vector<float> distanceSquared;
            nodeIndex.findKNearest(
                worldPos,
                std::min(candidateCount, static_cast<uint32_t>(fragmentSeedPositions.size())),
                nearestNodeIds,
                distanceSquared);

            std::vector<uint32_t> sourceLocalIds;
            std::vector<glm::dvec3> sourcePositions;
            double maxDistSq = 0.0;
            for (size_t k = 0; k < nearestNodeIds.size(); ++k) {
                const uint32_t localFrag = nearestNodeIds[k];
                const glm::vec3 nodePos = fragmentSeedPositions[localFrag];

                if (sdfChannel != UINT32_MAX && !spatial::segmentStaysInside(
                        domain.sdfGridMin,
                        domain.sdfGridDim,
                        domain.sdfCellSize,
                        domain.sdfValues,
                        sdfChannel,
                        worldPos,
                        nodePos)) {
                    continue;
                }

                sourceLocalIds.push_back(localFrag);
                sourcePositions.push_back(glm::dvec3(nodePos));
                maxDistSq = std::max(maxDistSq, static_cast<double>(distanceSquared[k]));

                if (sourceLocalIds.size() == maximumSupportCount) {
                    break;
                }
            }

            const double kernelRadius = std::max<double>(std::sqrt(maxDistSq) * 2.0, 1e-6);
            std::vector<double> computedValueWeights;
            std::vector<glm::dvec3> computedGradientWeights;

            bool gmlsSuccess = false;
            if (!sourcePositions.empty()) {
                gmlsSuccess = GMLS::computeSurfaceWeights(
                    glm::dvec3(worldPos),
                    glm::dvec3(worldNorm),
                    sourcePositions,
                    kernelRadius,
                    computedValueWeights,
                    computedGradientWeights);
            }

            voronoi::GMLSSurfaceStencil stencil{};
            if (gmlsSuccess && computedValueWeights.size() == sourceLocalIds.size()) {
                const uint32_t supportCount = static_cast<uint32_t>(sourceLocalIds.size());
                stencil.valueWeightOffset = static_cast<uint32_t>(valueWeights.size());
                stencil.gradientWeightOffset = static_cast<uint32_t>(gradientWeights.size());
                stencil.valueWeightCount = supportCount;
                stencil.gradientWeightCount = supportCount;

                for (uint32_t k = 0; k < supportCount; ++k) {
                    valueWeights.push_back({sourceLocalIds[k], static_cast<float>(computedValueWeights[k])});
                    gradientWeights.push_back({
                        sourceLocalIds[k],
                        static_cast<float>(computedGradientWeights[k].x),
                        static_cast<float>(computedGradientWeights[k].y),
                        static_cast<float>(computedGradientWeights[k].z)
                    });
                }
            } else {
                stencil.valueWeightOffset = 0;
                stencil.gradientWeightOffset = 0;
                stencil.valueWeightCount = 0;
                stencil.gradientWeightCount = 0;
            }
            stencils[vertexId] = stencil;
        }

        VkBuffer stencilBuffer = VK_NULL_HANDLE;
        VkDeviceSize stencilBufferOffset = 0;
        VkBuffer valueWeightBuffer = VK_NULL_HANDLE;
        VkDeviceSize valueWeightBufferOffset = 0;
        VkBuffer gradientWeightBuffer = VK_NULL_HANDLE;
        VkDeviceSize gradientWeightBufferOffset = 0;
        if (uploadDeviceBuffer(memoryAllocator, transferCommandPool, stencils.data(),
                stencils.size() * sizeof(voronoi::GMLSSurfaceStencil), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                alignment, stencilBuffer, stencilBufferOffset) != VK_SUCCESS ||
            uploadDeviceBuffer(memoryAllocator, transferCommandPool, valueWeights.data(),
                valueWeights.size() * sizeof(voronoi::GMLSSurfaceWeight), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                alignment, valueWeightBuffer, valueWeightBufferOffset) != VK_SUCCESS ||
            uploadDeviceBuffer(memoryAllocator, transferCommandPool, gradientWeights.data(),
                gradientWeights.size() * sizeof(voronoi::GMLSSurfaceGradientWeight), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                alignment, gradientWeightBuffer, gradientWeightBufferOffset) != VK_SUCCESS) {
            std::cerr << "[HeatSystem-Diag] rebuildGlobalThermalDomain FAILED: gmls upload id=" << runtimeModelId << std::endl;
            return false;
        }

        modelGMLSSurfaceStencilBufferByModelId[runtimeModelId] = stencilBuffer;
        modelGMLSSurfaceStencilBufferOffsetByModelId[runtimeModelId] = stencilBufferOffset;
        modelGMLSSurfaceWeightBufferByModelId[runtimeModelId] = valueWeightBuffer;
        modelGMLSSurfaceWeightBufferOffsetByModelId[runtimeModelId] = valueWeightBufferOffset;
        modelGMLSSurfaceWeightCountByModelId[runtimeModelId] = valueWeights.size();
        modelGMLSSurfaceGradientWeightBufferByModelId[runtimeModelId] = gradientWeightBuffer;
        modelGMLSSurfaceGradientWeightBufferOffsetByModelId[runtimeModelId] = gradientWeightBufferOffset;
        modelGMLSSurfaceGradientWeightCountByModelId[runtimeModelId] = gradientWeights.size();
    }

    return true;
}

bool HeatSystem::rebuildVoronoiRuntime() {
    if (!domainRuntime.isGlobalDomain()) {
        return false;
    }
    return rebuildGlobalThermalDomain();
}

void HeatSystem::cleanup() {
    playbackRuntime.cleanup(memoryAllocator);
    domainRuntime.cleanup();
}
