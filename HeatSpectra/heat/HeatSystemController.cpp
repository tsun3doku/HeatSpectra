#include "HeatSystemController.hpp"

#include "HeatSystem.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/ResourceManager.hpp"
#include "vulkan/UniformBufferManager.hpp"
#include "vulkan/VulkanDevice.hpp"
#include "runtime/ContactPreviewStore.hpp"

HeatSystemController::HeatSystemController(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, ResourceManager& resourceManager,
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

void HeatSystemController::configureHeatSystem(HeatSystem& system) {
    system.clearVoronoiInputs();
    if (configuredConfig.voronoiNodeCount != 0 && configuredConfig.voronoiNodes) {
        system.setVoronoiBuffers(
            configuredConfig.voronoiNodeCount,
            configuredConfig.voronoiNodes,
            configuredConfig.voronoiNodeBuffer,
            configuredConfig.voronoiNodeBufferOffset,
            configuredConfig.voronoiNeighborBuffer,
            configuredConfig.voronoiNeighborBufferOffset,
            configuredConfig.neighborIndicesBuffer,
            configuredConfig.neighborIndicesBufferOffset,
            configuredConfig.interfaceAreasBuffer,
            configuredConfig.interfaceAreasBufferOffset,
            configuredConfig.interfaceNeighborIdsBuffer,
            configuredConfig.interfaceNeighborIdsBufferOffset,
            configuredConfig.seedFlagsBuffer,
            configuredConfig.seedFlagsBufferOffset);

        for (const auto& [runtimeModelId, nodeOffset] : configuredConfig.receiverVoronoiNodeOffsetByModelId) {
            const auto countIt = configuredConfig.receiverVoronoiNodeCountByModelId.find(runtimeModelId);
            const auto bufferIt = configuredConfig.receiverVoronoiSurfaceMappingBufferByModelId.find(runtimeModelId);
            const auto bufferOffsetIt = configuredConfig.receiverVoronoiSurfaceMappingBufferOffsetByModelId.find(runtimeModelId);
            const auto cellIndicesIt = configuredConfig.receiverVoronoiSurfaceCellIndicesByModelId.find(runtimeModelId);
            const auto seedFlagsIt = configuredConfig.receiverVoronoiSeedFlagsByModelId.find(runtimeModelId);
            if (countIt == configuredConfig.receiverVoronoiNodeCountByModelId.end() ||
                bufferIt == configuredConfig.receiverVoronoiSurfaceMappingBufferByModelId.end() ||
                bufferOffsetIt == configuredConfig.receiverVoronoiSurfaceMappingBufferOffsetByModelId.end() ||
                cellIndicesIt == configuredConfig.receiverVoronoiSurfaceCellIndicesByModelId.end() ||
                seedFlagsIt == configuredConfig.receiverVoronoiSeedFlagsByModelId.end()) {
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
        configuredConfig.sourceGeometries,
        configuredConfig.sourceIntrinsicMeshes,
        configuredConfig.sourceRuntimeModelIds,
        configuredConfig.sourceTemperatureByRuntimeId);
    system.setReceiverPayloads(
        configuredConfig.receiverGeometries,
        configuredConfig.receiverIntrinsicMeshes,
        configuredConfig.receiverRuntimeModelIds,
        configuredConfig.supportingHalfedgeViews,
        configuredConfig.supportingAngleViews,
        configuredConfig.halfedgeViews,
        configuredConfig.edgeViews,
        configuredConfig.triangleViews,
        configuredConfig.lengthViews,
        configuredConfig.inputHalfedgeViews,
        configuredConfig.inputEdgeViews,
        configuredConfig.inputTriangleViews,
        configuredConfig.inputLengthViews);
    system.setThermalMaterials(configuredConfig.runtimeThermalMaterials);
    system.setContactCouplings(configuredConfig.contactCouplings);
}

void HeatSystemController::configure(const Config& config) {
    configuredConfig = config;
    hasConfiguredHeatConfig = true;

    if (!heatSystem) {
        return;
    }

    configureHeatSystem(*heatSystem);
    heatSystem->setActive(configuredConfig.authored.active);
    heatSystem->setIsPaused(configuredConfig.authored.active && configuredConfig.authored.paused);
    heatSystem->ensureConfigured();
}

void HeatSystemController::disable() {
    configuredConfig = {};
    hasConfiguredHeatConfig = true;

    if (!heatSystem) {
        return;
    }

    configureHeatSystem(*heatSystem);
    heatSystem->setIsPaused(false);
    heatSystem->setActive(false);
    heatSystem->ensureConfigured();
}

bool HeatSystemController::isHeatSystemActive() const {
    return hasConfiguredHeatConfig && configuredConfig.authored.active;
}

bool HeatSystemController::isHeatSystemPaused() const {
    return hasConfiguredHeatConfig &&
           configuredConfig.authored.active &&
           configuredConfig.authored.paused;
}

std::unique_ptr<HeatSystem> HeatSystemController::buildHeatSystem(VkExtent2D extent, VkRenderPass renderPass) {
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
    system->setContactPreviewStore(contactPreviewStore);

    if (hasConfiguredHeatConfig) {
        configureHeatSystem(*system);
        system->setActive(configuredConfig.authored.active);
        system->setIsPaused(configuredConfig.authored.active && configuredConfig.authored.paused);
        system->ensureConfigured();
    }
    return system;
}

void HeatSystemController::setContactPreviewStore(ContactPreviewStore* updatedContactPreviewStore) {
    contactPreviewStore = updatedContactPreviewStore;
    if (heatSystem) {
        heatSystem->setContactPreviewStore(contactPreviewStore);
    }
}

HeatSystem* HeatSystemController::getHeatSystem() const {
    return heatSystem.get();
}

void HeatSystemController::createHeatSystem(VkExtent2D extent, VkRenderPass renderPass) {
    heatSystem = buildHeatSystem(extent, renderPass);
}

void HeatSystemController::destroyHeatSystem() {
    if (!heatSystem) {
        return;
    }

    heatSystem->cleanupResources();
    heatSystem->cleanup();
    heatSystem.reset();
}

void HeatSystemController::recreateHeatSystem(VkExtent2D extent, VkRenderPass renderPass) {
    if (heatSystem) {
        destroyHeatSystem();
    }

    heatSystem = buildHeatSystem(extent, renderPass);
}
