#include "VoronoiSystemController.hpp"

#include "VoronoiSystem.hpp"
#include "runtime/RuntimeIntrinsicCache.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/ResourceManager.hpp"
#include "vulkan/UniformBufferManager.hpp"
#include "vulkan/VulkanDevice.hpp"

VoronoiSystemController::VoronoiSystemController(
    VulkanDevice& vulkanDevice,
    MemoryAllocator& memoryAllocator,
    ResourceManager& resourceManager,
    UniformBufferManager& uniformBufferManager,
    RuntimeIntrinsicCache& intrinsicCache,
    CommandPool& renderCommandPool,
    uint32_t maxFramesInFlight)
    : vulkanDevice(vulkanDevice),
      memoryAllocator(memoryAllocator),
      resourceManager(resourceManager),
      uniformBufferManager(uniformBufferManager),
      intrinsicCache(intrinsicCache),
      renderCommandPool(renderCommandPool),
      maxFramesInFlight(maxFramesInFlight) {
}

std::unique_ptr<VoronoiSystem> VoronoiSystemController::buildVoronoiSystem(VkExtent2D extent, VkRenderPass renderPass) {
    auto system = std::make_unique<VoronoiSystem>(
        vulkanDevice,
        memoryAllocator,
        resourceManager,
        intrinsicCache,
        uniformBufferManager,
        maxFramesInFlight,
        renderCommandPool,
        extent,
        renderPass);
    if (!system || !system->isInitialized()) {
        return nullptr;
    }

    system->setParams(configuredVoronoiPackage.authored.params);
    if (configuredVoronoiPackage.authored.active &&
        !configuredVoronoiPackage.receiverGeometryHandles.empty()) {
        system->setReceiverPayloads(
            configuredVoronoiPackage.receiverGeometries,
            configuredVoronoiPackage.receiverIntrinsics,
            configuredVoronoiPackage.receiverRuntimeModelIds);
        previewSurfaceRuntime.initializeGeometryBindings(
            vulkanDevice,
            memoryAllocator,
            intrinsicCache,
            configuredVoronoiPackage,
            system->getModelRuntimes());
        system->ensureConfigured(previewSurfaceRuntime);
    }
    return system;
}

void VoronoiSystemController::createVoronoiSystem(VkExtent2D extent, VkRenderPass renderPass) {
    voronoiSystem = buildVoronoiSystem(extent, renderPass);
}

void VoronoiSystemController::recreateVoronoiSystem(VkExtent2D extent, VkRenderPass renderPass) {
    if (voronoiSystem) {
        voronoiSystem->cleanupResources();
        voronoiSystem->cleanup();
        voronoiSystem.reset();
    }
    previewSurfaceRuntime.cleanup();

    voronoiSystem = buildVoronoiSystem(extent, renderPass);
}

void VoronoiSystemController::applyVoronoiPackage(const VoronoiPackage& voronoiPackage) {
    configuredVoronoiPackage = voronoiPackage;
    if (!voronoiSystem) {
        return;
    }

    if (configuredVoronoiPackage.authored.active) {
        voronoiSystem->setReceiverPayloads(
            configuredVoronoiPackage.receiverGeometries,
            configuredVoronoiPackage.receiverIntrinsics,
            configuredVoronoiPackage.receiverRuntimeModelIds);
        voronoiSystem->setParams(configuredVoronoiPackage.authored.params);
        previewSurfaceRuntime.cleanup();
        previewSurfaceRuntime.initializeGeometryBindings(
            vulkanDevice,
            memoryAllocator,
            intrinsicCache,
            configuredVoronoiPackage,
            voronoiSystem->getModelRuntimes());
        voronoiSystem->ensureConfigured(previewSurfaceRuntime);
    } else {
        previewSurfaceRuntime.cleanup();
        voronoiSystem->clearReceiverPayloads();
    }
}

VoronoiSystem* VoronoiSystemController::getVoronoiSystem() const {
    return voronoiSystem.get();
}
