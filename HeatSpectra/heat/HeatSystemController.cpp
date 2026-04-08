#include "HeatSystemController.hpp"

#include "HeatSystem.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/ModelRegistry.hpp"
#include "vulkan/UniformBufferManager.hpp"
#include "vulkan/VulkanDevice.hpp"

HeatSystemController::HeatSystemController(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, ModelRegistry& resourceManager,
    UniformBufferManager& uniformBufferManager,
    CommandPool& renderCommandPool,
    uint32_t maxFramesInFlight)
    : vulkanDevice(vulkanDevice),
      memoryAllocator(memoryAllocator),
      resourceManager(resourceManager),
      uniformBufferManager(uniformBufferManager),
      renderCommandPool(renderCommandPool),
      maxFramesInFlight(maxFramesInFlight) {
}

void HeatSystemController::configureHeatSystem(HeatSystem& system, const Config& config) {
    system.clearVoronoiInputs();
    if (config.voronoiNodeCount != 0 && config.voronoiNodes) {
        system.setVoronoiBuffers(
            config.voronoiNodeCount,
            config.voronoiNodes,
            config.voronoiNodeBuffer,
            config.voronoiNodeBufferOffset,
            config.voronoiNeighborBuffer,
            config.voronoiNeighborBufferOffset,
            config.neighborIndicesBuffer,
            config.neighborIndicesBufferOffset,
            config.interfaceAreasBuffer,
            config.interfaceAreasBufferOffset,
            config.interfaceNeighborIdsBuffer,
            config.interfaceNeighborIdsBufferOffset,
            config.seedFlagsBuffer,
            config.seedFlagsBufferOffset);

        for (const auto& [runtimeModelId, nodeOffset] : config.receiverVoronoiNodeOffsetByModelId) {
            const auto countIt = config.receiverVoronoiNodeCountByModelId.find(runtimeModelId);
            const auto bufferIt = config.receiverVoronoiSurfaceMappingBufferByModelId.find(runtimeModelId);
            const auto bufferOffsetIt = config.receiverVoronoiSurfaceMappingBufferOffsetByModelId.find(runtimeModelId);
            const auto cellIndicesIt = config.receiverVoronoiSurfaceCellIndicesByModelId.find(runtimeModelId);
            const auto seedFlagsIt = config.receiverVoronoiSeedFlagsByModelId.find(runtimeModelId);
            if (countIt == config.receiverVoronoiNodeCountByModelId.end() ||
                bufferIt == config.receiverVoronoiSurfaceMappingBufferByModelId.end() ||
                bufferOffsetIt == config.receiverVoronoiSurfaceMappingBufferOffsetByModelId.end() ||
                cellIndicesIt == config.receiverVoronoiSurfaceCellIndicesByModelId.end() ||
                seedFlagsIt == config.receiverVoronoiSeedFlagsByModelId.end()) {
                continue;
            }

            system.addVoronoiReceiverInput(
                runtimeModelId,
                nodeOffset,
                countIt->second,
                bufferIt->second,
                bufferOffsetIt->second,
                cellIndicesIt->second,
                seedFlagsIt->second);
        }
    }
    system.setSourcePayloads(
        config.sourceGeometries,
        config.sourceIntrinsicMeshes,
        config.sourceRuntimeModelIds,
        config.sourceTemperatureByRuntimeId);
    system.setReceiverPayloads(
        config.receiverGeometries,
        config.receiverIntrinsicMeshes,
        config.receiverRuntimeModelIds,
        config.supportingHalfedgeViews,
        config.supportingAngleViews,
        config.halfedgeViews,
        config.edgeViews,
        config.triangleViews,
        config.lengthViews,
        config.inputHalfedgeViews,
        config.inputEdgeViews,
        config.inputTriangleViews,
        config.inputLengthViews);
    system.setThermalMaterials(config.runtimeThermalMaterials);
    system.setContactCouplings(config.contactCouplings);
}

void HeatSystemController::configure(uint64_t socketKey, const Config& config) {
    if (socketKey == 0) {
        return;
    }

    configuredConfigs[socketKey] = config;
    auto& instance = activeSystems[socketKey];
    if (!instance.system && currentRenderPass != VK_NULL_HANDLE) {
        instance.system = buildHeatSystem(instance.resources, currentExtent, currentRenderPass);
    }

    if (instance.system) {
        configureHeatSystem(*instance.system, config);
        instance.system->setActive(config.authored.active);
        instance.system->setIsPaused(config.authored.active && config.authored.paused);
        instance.system->ensureConfigured();
    }
}

void HeatSystemController::disable(uint64_t socketKey) {
    if (socketKey == 0) {
        return;
    }

    configuredConfigs.erase(socketKey);
    auto it = activeSystems.find(socketKey);
    if (it != activeSystems.end()) {
        if (it->second.system) {
            it->second.system->setIsPaused(false);
            it->second.system->setActive(false);
            it->second.system->cleanupResources();
            it->second.system->cleanup();
        }
        activeSystems.erase(it);
    }
}

void HeatSystemController::disableAll() {
    configuredConfigs.clear();
    for (auto& [key, instance] : activeSystems) {
        if (instance.system) {
            instance.system->setIsPaused(false);
            instance.system->setActive(false);
            instance.system->cleanupResources();
            instance.system->cleanup();
        }
    }
    activeSystems.clear();
}

bool HeatSystemController::isAnyHeatSystemActive() const {
    for (const auto& [key, config] : configuredConfigs) {
        auto systemIt = activeSystems.find(key);
        if (config.authored.active && systemIt != activeSystems.end() && systemIt->second.system) {
            return true;
        }
    }
    return false;
}

bool HeatSystemController::isAnyHeatSystemPaused() const {
    for (const auto& [key, config] : configuredConfigs) {
        auto systemIt = activeSystems.find(key);
        if (config.authored.active && config.authored.paused && systemIt != activeSystems.end() && systemIt->second.system) {
            return true;
        }
    }
    return false;
}

bool HeatSystemController::isHeatSystemActive(uint64_t socketKey) const {
    auto configIt = configuredConfigs.find(socketKey);
    auto systemIt = activeSystems.find(socketKey);
    return configIt != configuredConfigs.end() && 
           systemIt != activeSystems.end() && 
           systemIt->second.system &&
           configIt->second.authored.active;
}

bool HeatSystemController::isHeatSystemPaused(uint64_t socketKey) const {
    auto configIt = configuredConfigs.find(socketKey);
    auto systemIt = activeSystems.find(socketKey);
    return configIt != configuredConfigs.end() && 
           systemIt != activeSystems.end() && 
           systemIt->second.system &&
           configIt->second.authored.active &&
           configIt->second.authored.paused;
}

std::unique_ptr<HeatSystem> HeatSystemController::buildHeatSystem(HeatSystemResources& heatSystemResources, VkExtent2D extent, VkRenderPass renderPass) {
    std::unique_ptr<HeatSystem> system = std::make_unique<HeatSystem>(
        vulkanDevice,
        memoryAllocator,
        resourceManager,
        heatSystemResources,
        uniformBufferManager,
        maxFramesInFlight,
        renderCommandPool,
        extent,
        renderPass);
    if (!system || !system->isInitialized()) {
        std::cerr << "[HeatSystemController] HeatSystem initialization failed" << std::endl;
        return nullptr;
    }
    return system;
}

HeatSystem* HeatSystemController::getHeatSystem(uint64_t socketKey) const {
    auto it = activeSystems.find(socketKey);
    if (it != activeSystems.end()) {
        return it->second.system.get();
    }
    return nullptr;
}

std::vector<HeatSystem*> HeatSystemController::getActiveSystems() const {
    std::vector<HeatSystem*> systems;
    systems.reserve(activeSystems.size());
    for (const auto& [key, instance] : activeSystems) {
        if (instance.system) {
            systems.push_back(instance.system.get());
        }
    }
    return systems;
}

void HeatSystemController::createHeatSystem(VkExtent2D extent, VkRenderPass renderPass) {
    currentExtent = extent;
    currentRenderPass = renderPass;
}

void HeatSystemController::destroyHeatSystem(uint64_t socketKey) {
    auto it = activeSystems.find(socketKey);
    if (it != activeSystems.end()) {
        if (it->second.system) {
            it->second.system->cleanupResources();
            it->second.system->cleanup();
        }
        activeSystems.erase(it);
    }
}

void HeatSystemController::updateRenderContext(VkExtent2D extent, VkRenderPass renderPass) {
    currentExtent = extent;
    currentRenderPass = renderPass;
}

void HeatSystemController::updateRenderResources() {
    for (auto& [socketKey, instance] : activeSystems) {
        if (instance.system) {
            instance.system->updateRenderResources(currentRenderPass);
        }
    }
}

