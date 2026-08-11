#include "HeatSystemComputeController.hpp"

#include <iostream>
#include "HeatSystem.hpp"
#include "hash/HashBuilder.hpp"
#include "hash/HashProduct.hpp"
#include "heat/HeatModelRuntime.hpp"
#include "runtime/RuntimeProducts.hpp"
#include "util/GeometryUtils.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/VulkanBuffer.hpp"
#include "vulkan/VulkanDevice.hpp"

#include <unordered_set>

HeatSystemComputeController::HeatSystemComputeController(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator,
    CommandPool& renderCommandPool,
    CommandPool& transferCommandPool,
    uint32_t maxFramesInFlight)
    : vulkanDevice(vulkanDevice),
      memoryAllocator(memoryAllocator),
      renderCommandPool(renderCommandPool),
      transferCommandPool(transferCommandPool),
      maxFramesInFlight(maxFramesInFlight) {
}

void HeatSystemComputeController::configureHeatSystem(HeatSystem& system, const Config& config) {
    system.clearVoronoiInputs();
    if (!config.simNodeCounts.empty()) {
        for (const auto& [runtimeModelId, simNodeCount] : config.simNodeCounts) {
            const auto simNodeBufferIt = config.modelSimNodeBufferByModelId.find(runtimeModelId);
            const auto simNodeBufferOffsetIt = config.modelSimNodeBufferOffsetByModelId.find(runtimeModelId);
            const auto simNodeCouplingIt = config.modelSimNodeCouplingBufferByModelId.find(runtimeModelId);
            const auto simNodeCouplingOffsetIt = config.modelSimNodeCouplingBufferOffsetByModelId.find(runtimeModelId);
            const auto simNodeCouplingCountIt = config.simNodeCouplingCounts.find(runtimeModelId);
            const auto gmlsStencilIt = config.modelGMLSSurfaceStencilBufferByModelId.find(runtimeModelId);
            const auto gmlsStencilOffsetIt = config.modelGMLSSurfaceStencilBufferOffsetByModelId.find(runtimeModelId);
            const auto gmlsWeightIt = config.modelGMLSSurfaceWeightBufferByModelId.find(runtimeModelId);
            const auto gmlsWeightOffsetIt = config.modelGMLSSurfaceWeightBufferOffsetByModelId.find(runtimeModelId);
            const auto gmlsWeightCountIt = config.modelGMLSSurfaceWeightCountByModelId.find(runtimeModelId);
            const auto gmlsGradientIt = config.modelGMLSSurfaceGradientWeightBufferByModelId.find(runtimeModelId);
            const auto gmlsGradientOffsetIt = config.modelGMLSSurfaceGradientWeightBufferOffsetByModelId.find(runtimeModelId);
            const auto gmlsGradientCountIt = config.modelGMLSSurfaceGradientWeightCountByModelId.find(runtimeModelId);
            const auto nodePositionsIt = config.modelNodePositionsByModelId.find(runtimeModelId);
            const auto nodesIt = config.modelNodesByModelId.find(runtimeModelId);
            const auto nodeCouplingsIt = config.modelNodeCouplingsByModelId.find(runtimeModelId);
            const auto surfaceNodeIdsIt = config.modelSurfaceNodeIdsByModelId.find(runtimeModelId);
            const auto surfacePatchAreasIt = config.modelSurfacePatchAreasByModelId.find(runtimeModelId);
            
            if (simNodeBufferIt == config.modelSimNodeBufferByModelId.end() ||
                simNodeBufferOffsetIt == config.modelSimNodeBufferOffsetByModelId.end() ||
                simNodeCouplingIt == config.modelSimNodeCouplingBufferByModelId.end() ||
                simNodeCouplingOffsetIt == config.modelSimNodeCouplingBufferOffsetByModelId.end() ||
                simNodeCouplingCountIt == config.simNodeCouplingCounts.end() ||
                nodePositionsIt == config.modelNodePositionsByModelId.end() ||
                nodesIt == config.modelNodesByModelId.end() ||
                nodeCouplingsIt == config.modelNodeCouplingsByModelId.end() ||
                surfaceNodeIdsIt == config.modelSurfaceNodeIdsByModelId.end() ||
                surfacePatchAreasIt == config.modelSurfacePatchAreasByModelId.end()) {
                continue;
            }

            system.addVoronoiModelInput(
                runtimeModelId,
                simNodeCount,
                simNodeBufferIt->second,
                simNodeBufferOffsetIt->second,
                simNodeCouplingIt->second,
                simNodeCouplingOffsetIt->second,
                simNodeCouplingCountIt->second,
                (gmlsStencilIt != config.modelGMLSSurfaceStencilBufferByModelId.end()) ? gmlsStencilIt->second : VK_NULL_HANDLE,
                (gmlsStencilOffsetIt != config.modelGMLSSurfaceStencilBufferOffsetByModelId.end()) ? gmlsStencilOffsetIt->second : 0,
                (gmlsWeightIt != config.modelGMLSSurfaceWeightBufferByModelId.end()) ? gmlsWeightIt->second : VK_NULL_HANDLE,
                (gmlsWeightOffsetIt != config.modelGMLSSurfaceWeightBufferOffsetByModelId.end()) ? gmlsWeightOffsetIt->second : 0,
                (gmlsWeightCountIt != config.modelGMLSSurfaceWeightCountByModelId.end()) ? gmlsWeightCountIt->second : 0,
                (gmlsGradientIt != config.modelGMLSSurfaceGradientWeightBufferByModelId.end()) ? gmlsGradientIt->second : VK_NULL_HANDLE,
                (gmlsGradientOffsetIt != config.modelGMLSSurfaceGradientWeightBufferOffsetByModelId.end()) ? gmlsGradientOffsetIt->second : 0,
                (gmlsGradientCountIt != config.modelGMLSSurfaceGradientWeightCountByModelId.end()) ? gmlsGradientCountIt->second : 0,
                nodePositionsIt->second,
                nodesIt->second,
                nodeCouplingsIt->second,
                surfaceNodeIdsIt->second,
                surfacePatchAreasIt->second);
        }
    }
    system.setHeatModels(
        config.modelSurfacePositions,
        config.modelSurfaceNormals,
        config.modelSurfaceTriangleIndices,
        config.modelRuntimeModelIds,
        config.modelInitialTemperaturesCByRuntimeId,
        config.modelBoundaryConditionTypesByRuntimeId,
        config.modelBoundaryTemperaturesCByRuntimeId,
        config.modelBoundaryHeatFluxesByRuntimeId,
        config.modelBoundaryHeatTransferCoefficientsByRuntimeId,
        config.modelVolumetricPowerDensitiesByRuntimeId,
        config.modelDensity,
        config.modelSpecificHeat,
        config.modelConductivity,
        config.worldUnit);
    system.setParams(config.contactThermalConductance, config.simulationDuration);
    system.setContactCouplings(config.contactCouplings);
}

void HeatSystemComputeController::applyRuntimeState(HeatSystem& system, const Config& config) {
    system.setActive(config.active);
    system.setSyntheticDirichletTestEnabled(config.syntheticDirichletTestEnabled);
    system.setPlaybackState(!timelinePlaying, timelineResetCounter);
    system.setRewindFrame(timelineScrubFrame);
}

void HeatSystemComputeController::setTimelinePlaying(bool playing) {
    timelinePlaying = playing;
    if (playing) timelineScrubFrame = heat::NoRewindFrame;
    for (auto& [key, system] : systemsBySocket) {
        if (!system || !system->getIsActive()) continue;
        system->setPlaybackState(!timelinePlaying, timelineResetCounter);
        system->setRewindFrame(timelineScrubFrame);
    }
}

void HeatSystemComputeController::resetTimeline() {
    ++timelineResetCounter;
    timelinePlaying = true;
    timelineScrubFrame = heat::NoRewindFrame;
    for (auto& [key, system] : systemsBySocket) {
        if (!system || !system->getIsActive()) continue;
        system->setPlaybackState(false, timelineResetCounter);
    }
}

void HeatSystemComputeController::scrubTimeline(uint32_t frame) {
    timelinePlaying = false;
    timelineScrubFrame = frame;
    for (auto& [key, system] : systemsBySocket) {
        if (!system || !system->getIsActive()) continue;
        system->setPlaybackState(true, timelineResetCounter);
        system->setRewindFrame(frame);
    }
}

void HeatSystemComputeController::apply(uint64_t socketKey, const Config& config) {
    if (socketKey == 0) {
        return;
    }

    auto it = systemsBySocket.find(socketKey);
    if (it == systemsBySocket.end()) {
        auto system = buildHeatSystem();
        it = systemsBySocket.emplace(socketKey, std::move(system)).first;
    }

    auto& system = it->second;
    if (!system) {
        return;
    }

    const auto configIt = configuredConfigs.find(socketKey);
    if (configIt != configuredConfigs.end() && configIt->second.computeHash == config.computeHash) {
        applyRuntimeState(*system, config);
        return;
    }

    if (configIt != configuredConfigs.end() && configIt->second.authoredSimulationHash != 0 &&
        configIt->second.authoredSimulationHash == config.authoredSimulationHash) {
        for (const auto& [runtimeModelId, sourceKey] : config.modelRobinSourceKeys) {
            const auto temperatureIt = config.modelBoundaryTemperaturesCByRuntimeId.find(runtimeModelId);
            if (temperatureIt != config.modelBoundaryTemperaturesCByRuntimeId.end()) {
                system->setRuntimeRobinTemperatureC(runtimeModelId, 0u, temperatureIt->second);
            }
        }
        configuredConfigs[socketKey] = config;
        syncSerialInputs();
        applyRuntimeState(*system, config);
        return;
    }

    if (configIt != configuredConfigs.end() && config.structuralHash != 0 &&
        configIt->second.structuralHash == config.structuralHash) {
        bool updated = true;
        for (uint32_t runtimeModelId : config.modelRuntimeModelIds) {
            const uint32_t conditionType = config.modelBoundaryConditionTypesByRuntimeId.at(runtimeModelId);
            if (conditionType == 1u) {
                updated = system->setRuntimeDirichletTemperatureC(
                    runtimeModelId, 0u,
                    config.modelBoundaryTemperaturesCByRuntimeId.at(runtimeModelId)) && updated;
            } else if (conditionType == 2u) {
                updated = system->setRuntimeNeumannHeatFlux(
                    runtimeModelId, 0u, config.modelBoundaryHeatFluxesByRuntimeId.at(runtimeModelId)) && updated;
            } else if (conditionType == 3u) {
                updated = system->setRuntimeRobinState(
                    runtimeModelId, 0u,
                    config.modelBoundaryTemperaturesCByRuntimeId.at(runtimeModelId),
                    config.modelBoundaryHeatTransferCoefficientsByRuntimeId.at(runtimeModelId)) && updated;
            }
            updated = system->setRuntimeVolumetricPowerDensity(
                runtimeModelId, config.modelVolumetricPowerDensitiesByRuntimeId.at(runtimeModelId)) && updated;
        }
        if (updated) {
            configuredConfigs[socketKey] = config;
            syncSerialInputs();
            system->resetSimulationState();
            applyRuntimeState(*system, config);
            return;
        }
    }

    configureHeatSystem(*system, config);
    const bool configured = system->ensureConfigured();
    if (configured) {
        configuredConfigs[socketKey] = config;
    } else {
        configuredConfigs.erase(socketKey);
    }
    syncSerialInputs();

    applyRuntimeState(*system, config);
}

void HeatSystemComputeController::remove(uint64_t socketKey) {
    if (socketKey == 0) {
        return;
    }

    configuredConfigs.erase(socketKey);
    auto it = systemsBySocket.find(socketKey);
    if (it != systemsBySocket.end()) {
        if (it->second) {
            it->second->setPlaybackState(false, 0);
            it->second->setActive(false);
            vkDeviceWaitIdle(vulkanDevice.getDevice());
            it->second->cleanup();
        }
        systemsBySocket.erase(it);
    }
    syncSerialInputs();
}

void HeatSystemComputeController::disableAll() {
    configuredConfigs.clear();
    if (!systemsBySocket.empty()) {
        vkDeviceWaitIdle(vulkanDevice.getDevice());
    }
    for (auto& [key, system] : systemsBySocket) {
        if (system) {
            system->setPlaybackState(false, 0);
            system->setActive(false);
            system->cleanup();
        }
    }
    systemsBySocket.clear();
    serialRuntimes.clear();
    lastSerialRevisions.clear();
}

void HeatSystemComputeController::syncSerialInputs() {
    std::unordered_set<uint64_t> requiredSourceKeys;
    for (const auto& [socketKey, config] : configuredConfigs) {
        for (const auto& [sourceKey, portName] : config.serialPortNamesBySourceKey) {
            requiredSourceKeys.insert(sourceKey);
            auto& runtime = serialRuntimes[sourceKey];
            if (!runtime) {
                runtime = std::make_unique<SerialTemperatureRuntime>();
            }
            runtime->configure(
                config.serialEnabledBySourceKey.at(sourceKey),
                portName,
                config.serialBaudRatesBySourceKey.at(sourceKey));
        }
    }

    for (auto it = serialRuntimes.begin(); it != serialRuntimes.end();) {
        if (requiredSourceKeys.find(it->first) == requiredSourceKeys.end()) {
            it = serialRuntimes.erase(it);
        } else {
            ++it;
        }
    }
    lastSerialRevisions.clear();
}

void HeatSystemComputeController::updateSerialInputs() {
    for (auto& [sourceKey, runtime] : serialRuntimes) {
        if (runtime) runtime->update();
    }

    for (const auto& [socketKey, config] : configuredConfigs) {
        const auto systemIt = systemsBySocket.find(socketKey);
        if (systemIt == systemsBySocket.end() || !systemIt->second) continue;

        for (const auto& [runtimeModelId, sourceKey] : config.modelRobinSourceKeys) {
            const auto sourceIt = serialRuntimes.find(sourceKey);
            if (sourceIt == serialRuntimes.end() || !sourceIt->second) continue;
            const SerialTemperatureRuntime::Reading* reading = sourceIt->second->latestReading();
            if (!reading) continue;

            uint64_t bindingKey = HashBuilder::start();
            HashBuilder::combine(bindingKey, socketKey);
            HashBuilder::combine(bindingKey, runtimeModelId);
            HashBuilder::combine(bindingKey, sourceKey);
            if (lastSerialRevisions[bindingKey] == reading->revision) continue;

            if (systemIt->second->setRuntimeRobinTemperatureC(
                    runtimeModelId, 0u, reading->temperatureC)) {
                lastSerialRevisions[bindingKey] = reading->revision;
            }
        }
    }
}

bool HeatSystemComputeController::getSerialTemperatureStatus(
    uint64_t sourceKey, SerialTemperatureRuntime::Status& outStatus) const {
    const auto it = serialRuntimes.find(sourceKey);
    if (it == serialRuntimes.end() || !it->second) return false;
    outStatus = it->second->status();
    return true;
}

bool HeatSystemComputeController::isAnyHeatSystemActive() const {
    for (const auto& [key, config] : configuredConfigs) {
        auto systemIt = systemsBySocket.find(key);
        if (config.active && systemIt != systemsBySocket.end() && systemIt->second) {
            return true;
        }
    }
    return false;
}

bool HeatSystemComputeController::isAnyHeatSystemPaused() const {
    for (const auto& [key, system] : systemsBySocket) {
        if (system && system->getIsActive() && system->getIsPaused()) {
            return true;
        }
    }
    return false;
}

std::unique_ptr<HeatSystem> HeatSystemComputeController::buildHeatSystem() {
    std::unique_ptr<HeatSystem> system = std::make_unique<HeatSystem>(
        vulkanDevice,
        memoryAllocator,
        maxFramesInFlight,
        renderCommandPool,
        transferCommandPool);
    if (!system || !system->isInitialized()) {
        std::cerr << "[HeatSystemComputeController] HeatSystem initialization failed" << std::endl;
        return nullptr;
    }
    return system;
}

std::vector<ComputePass*> HeatSystemComputeController::getActiveSystems() const {
    std::vector<ComputePass*> systems;
    systems.reserve(systemsBySocket.size());
    for (const auto& [key, system] : systemsBySocket) {
        if (system && system->hasDispatchableComputeWork()) {
            systems.push_back(system.get());
        }
    }
    return systems;
}

bool HeatSystemComputeController::buildProduct(uint64_t socketKey, HeatProduct& outProduct) {
    outProduct = {};
    if (socketKey == 0) return false;

    auto sysIt = systemsBySocket.find(socketKey);
    if (sysIt == systemsBySocket.end() || !sysIt->second) return false;
    HeatSystem& system = *sysIt->second;

    const Config* config = getConfig(socketKey);
    if (!config) return false;

    const auto freeProduct = [&]() {
        for (size_t j = 0; j < outProduct.modelSurfaceBuffers.size(); ++j) {
            if (outProduct.modelSurfaceBuffers[j] != VK_NULL_HANDLE)
                memoryAllocator.free(outProduct.modelSurfaceBuffers[j],
                    j < outProduct.modelSurfaceBufferOffsets.size() ? outProduct.modelSurfaceBufferOffsets[j] : 0);
        }
        for (size_t j = 0; j < outProduct.modelSurfaceGradientBuffers.size(); ++j) {
            if (outProduct.modelSurfaceGradientBuffers[j] != VK_NULL_HANDLE)
                memoryAllocator.free(outProduct.modelSurfaceGradientBuffers[j],
                    j < outProduct.modelSurfaceGradientBufferOffsets.size() ? outProduct.modelSurfaceGradientBufferOffsets[j] : 0);
        }
        outProduct = {};
    };

    outProduct.modelRuntimeModelIds.reserve(config->modelRuntimeModelIds.size());
    outProduct.modelSurfaceBuffers.reserve(config->modelRuntimeModelIds.size());
    outProduct.modelSurfaceBufferOffsets.reserve(config->modelRuntimeModelIds.size());
    outProduct.modelSurfacePointCounts.reserve(config->modelRuntimeModelIds.size());
    outProduct.modelSurfaceGradientBuffers.reserve(config->modelRuntimeModelIds.size());
    outProduct.modelSurfaceGradientBufferOffsets.reserve(config->modelRuntimeModelIds.size());

    for (uint32_t runtimeModelId : config->modelRuntimeModelIds) {
        HeatModelRuntime* model = system.getModelByRuntimeId(runtimeModelId);
        if (!model) continue;

        const std::vector<glm::vec3>& positions = model->getSurfacePositions();
        const std::vector<glm::vec3>& normals = model->getSurfaceNormals();
        const std::vector<uint32_t>& triangleIndices = model->getSurfaceTriangleIndices();
        const size_t vertexCount = model->getSurfaceVertexCount();
        if (vertexCount == 0 || normals.size() != vertexCount || triangleIndices.size() < 3) {
            freeProduct();
            return false;
        }

        const std::vector<float> vertexAreas = computeVertexAreas(positions, triangleIndices);

        std::vector<heat::SurfacePoint> surfacePoints(vertexCount);
        for (size_t vertexId = 0; vertexId < vertexCount; ++vertexId) {
            heat::SurfacePoint& surfacePoint = surfacePoints[vertexId];
            surfacePoint.position = positions[vertexId];
            surfacePoint.temperatureC = model->getInitialTemperatureC();
            surfacePoint.normal = normals[vertexId];
            surfacePoint.vertexArea = vertexId < vertexAreas.size() ? vertexAreas[vertexId] : 0.0f;
            surfacePoint.color = glm::vec4(1.0f);
        }

        const VkDeviceSize storageAlignment =
            vulkanDevice.getPhysicalDeviceProperties().limits.minStorageBufferOffsetAlignment;

        VkBuffer surfaceBuffer = VK_NULL_HANDLE;
        VkDeviceSize surfaceBufferOffset = 0;
        if (uploadDeviceBuffer(
                memoryAllocator,
                renderCommandPool,
                surfacePoints.data(),
                sizeof(heat::SurfacePoint) * vertexCount,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT,
                storageAlignment,
                surfaceBuffer,
                surfaceBufferOffset) != VK_SUCCESS) {
            freeProduct();
            return false;
        }

        std::vector<glm::vec4> zeroGradients(vertexCount, glm::vec4(0.0f));
        VkBuffer gradientBuffer = VK_NULL_HANDLE;
        VkDeviceSize gradientBufferOffset = 0;
        if (uploadDeviceBuffer(
                memoryAllocator,
                renderCommandPool,
                zeroGradients.data(),
                sizeof(glm::vec4) * vertexCount,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                storageAlignment,
                gradientBuffer,
                gradientBufferOffset) != VK_SUCCESS) {
            memoryAllocator.free(surfaceBuffer, surfaceBufferOffset);
            freeProduct();
            return false;
        }

        outProduct.modelRuntimeModelIds.push_back(runtimeModelId);
        outProduct.modelSurfaceBuffers.push_back(surfaceBuffer);
        outProduct.modelSurfaceBufferOffsets.push_back(surfaceBufferOffset);
        outProduct.modelSurfacePointCounts.push_back(static_cast<uint32_t>(vertexCount));
        outProduct.modelSurfaceGradientBuffers.push_back(gradientBuffer);
        outProduct.modelSurfaceGradientBufferOffsets.push_back(gradientBufferOffset);
    }

    if (!outProduct.isValid()) {
        freeProduct();
        return false;
    }

    if (!system.setupDescriptors(
            outProduct.modelSurfaceBuffers,
            outProduct.modelSurfaceBufferOffsets,
            outProduct.modelSurfaceGradientBuffers,
            outProduct.modelSurfaceGradientBufferOffsets)) {
        freeProduct();
        return false;
    }

    HashProduct::seal(outProduct);
    return true;
}

const HeatSystem* HeatSystemComputeController::getSystem(uint64_t socketKey) const {
    const auto systemIt = systemsBySocket.find(socketKey);
    if (systemIt == systemsBySocket.end() || !systemIt->second) {
        return nullptr;
    }
    return systemIt->second.get();
}

const HeatSystemComputeController::Config* HeatSystemComputeController::getConfig(uint64_t socketKey) const {
    const auto configIt = configuredConfigs.find(socketKey);
    return configIt != configuredConfigs.end() ? &configIt->second : nullptr;
}
