#include "HeatSystemComputeController.hpp"

#include "HeatSystem.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/ModelRegistry.hpp"
#include "vulkan/UniformBufferManager.hpp"
#include "vulkan/VulkanDevice.hpp"

HeatSystemComputeController::HeatSystemComputeController(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, ModelRegistry& resourceManager,
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

void HeatSystemComputeController::configureHeatSystem(HeatSystem& system, const Config& config) {
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
        config.sourceIntrinsicMeshes,
        config.sourceRuntimeModelIds,
        config.sourceTemperatureByRuntimeId);
    system.setReceiverPayloads(
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

void HeatSystemComputeController::configure(uint64_t socketKey, const Config& config) {
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
        instance.system->setActive(config.active);
        instance.system->setIsPaused(config.active && config.paused);
        instance.system->ensureConfigured();
    }
}

void HeatSystemComputeController::disable(uint64_t socketKey) {
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

void HeatSystemComputeController::disableAll() {
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

bool HeatSystemComputeController::isAnyHeatSystemActive() const {
    for (const auto& [key, config] : configuredConfigs) {
        auto systemIt = activeSystems.find(key);
        if (config.active && systemIt != activeSystems.end() && systemIt->second.system) {
            return true;
        }
    }
    return false;
}

bool HeatSystemComputeController::isAnyHeatSystemPaused() const {
    for (const auto& [key, config] : configuredConfigs) {
        auto systemIt = activeSystems.find(key);
        if (config.active && config.paused && systemIt != activeSystems.end() && systemIt->second.system) {
            return true;
        }
    }
    return false;
}

bool HeatSystemComputeController::isHeatSystemActive(uint64_t socketKey) const {
    auto configIt = configuredConfigs.find(socketKey);
    auto systemIt = activeSystems.find(socketKey);
    return configIt != configuredConfigs.end() && 
           systemIt != activeSystems.end() && 
           systemIt->second.system &&
           configIt->second.active;
}

bool HeatSystemComputeController::isHeatSystemPaused(uint64_t socketKey) const {
    auto configIt = configuredConfigs.find(socketKey);
    auto systemIt = activeSystems.find(socketKey);
    return configIt != configuredConfigs.end() && 
           systemIt != activeSystems.end() && 
           systemIt->second.system &&
           configIt->second.active &&
           configIt->second.paused;
}

std::unique_ptr<HeatSystem> HeatSystemComputeController::buildHeatSystem(HeatSystemResources& heatSystemResources, VkExtent2D extent, VkRenderPass renderPass) {
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
        std::cerr << "[HeatSystemComputeController] HeatSystem initialization failed" << std::endl;
        return nullptr;
    }
    return system;
}

HeatSystem* HeatSystemComputeController::getHeatSystem(uint64_t socketKey) const {
    auto it = activeSystems.find(socketKey);
    if (it != activeSystems.end()) {
        return it->second.system.get();
    }
    return nullptr;
}

std::vector<HeatSystem*> HeatSystemComputeController::getActiveSystems() const {
    std::vector<HeatSystem*> systems;
    systems.reserve(activeSystems.size());
    for (const auto& [key, instance] : activeSystems) {
        if (instance.system) {
            systems.push_back(instance.system.get());
        }
    }
    return systems;
}

bool HeatSystemComputeController::exportProduct(uint64_t socketKey, HeatProduct& outProduct) const {
    outProduct = {};

    const auto configIt = configuredConfigs.find(socketKey);
    if (configIt == configuredConfigs.end()) {
        return false;
    }

    const auto systemIt = activeSystems.find(socketKey);
    if (systemIt == activeSystems.end() || !systemIt->second.system) {
        return false;
    }
    const HeatSystem& system = *systemIt->second.system;
    outProduct.active = system.getIsActive();
    outProduct.paused = system.getIsPaused();

    const auto& sourceBindings = system.getSourceBindings();
    outProduct.sourceRuntimeModelIds.reserve(sourceBindings.size());
    for (const HeatSystemRuntime::SourceBinding& sourceBinding : sourceBindings) {
        if (sourceBinding.runtimeModelId == 0) {
            continue;
        }

        outProduct.sourceRuntimeModelIds.push_back(sourceBinding.runtimeModelId);
    }

    const auto& receivers = system.getReceivers();
    outProduct.receiverRuntimeModelIds.reserve(receivers.size());
    outProduct.receiverSurfaceBufferViews.reserve(receivers.size());
    for (const auto& receiver : receivers) {
        if (!receiver || receiver->getRuntimeModelId() == 0 || receiver->getSurfaceBufferView() == VK_NULL_HANDLE) {
            continue;
        }

        outProduct.receiverRuntimeModelIds.push_back(receiver->getRuntimeModelId());
        outProduct.receiverSurfaceBufferViews.push_back(receiver->getSurfaceBufferView());
    }

    outProduct.contentHash = computeContentHash(outProduct);
    return outProduct.isValid();
}

void HeatSystemComputeController::createHeatSystem(VkExtent2D extent, VkRenderPass renderPass) {
    currentExtent = extent;
    currentRenderPass = renderPass;
}

void HeatSystemComputeController::destroyHeatSystem(uint64_t socketKey) {
    auto it = activeSystems.find(socketKey);
    if (it != activeSystems.end()) {
        if (it->second.system) {
            it->second.system->cleanupResources();
            it->second.system->cleanup();
        }
        activeSystems.erase(it);
    }
}

void HeatSystemComputeController::updateRenderContext(VkExtent2D extent, VkRenderPass renderPass) {
    currentExtent = extent;
    currentRenderPass = renderPass;
}

void HeatSystemComputeController::updateRenderResources() {
    (void)currentRenderPass;
}

