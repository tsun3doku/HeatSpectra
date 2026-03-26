#include "HeatSystemController.hpp"

#include "HeatSystem.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "render/RenderRuntime.hpp"
#include "vulkan/ResourceManager.hpp"
#include "runtime/RuntimeIntrinsicCache.hpp"
#include "vulkan/UniformBufferManager.hpp"
#include "vulkan/VulkanDevice.hpp"
#include "runtime/ContactPreviewStore.hpp"

#include <iostream>

HeatSystemController::HeatSystemController(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, ResourceManager& resourceManager,
    UniformBufferManager& uniformBufferManager, RuntimeIntrinsicCache& remeshResources,
    CommandPool& renderCommandPool,
    uint32_t maxFramesInFlight)
    : vulkanDevice(vulkanDevice),
      memoryAllocator(memoryAllocator),
      resourceManager(resourceManager),
      uniformBufferManager(uniformBufferManager),
      remeshResources(remeshResources),
      renderCommandPool(renderCommandPool),
      maxFramesInFlight(maxFramesInFlight) {
}

void HeatSystemController::configureHeatSystem(HeatSystem& system) {
    system.setHeatPackage(&heatPackageStorage);
    system.setThermalMaterials(heatPackageStorage.runtimeThermalMaterials);
    system.configureSourceRuntimes(heatPackageStorage);
    system.setResolvedContacts(resolvedContactsStorage);
    system.setSourceTemperatures(heatPackageStorage.sourceTemperatureByRuntimeId);
}

void HeatSystemController::applyHeatActivationState(HeatSystem& system) {
    if (!heatPackageStorage.authored.active) {
        system.setIsPaused(false);
        if (system.getIsActive()) {
            system.setActive(false);
        }
        system.requestHeatReset();
        return;
    }

    system.setActive(true);
    system.setIsPaused(heatPackageStorage.authored.paused);
    if (heatPackageStorage.authored.resetRequested) {
        system.requestHeatReset();
    }
}

void HeatSystemController::applyHeatPackage(const HeatPackage& heatPackage) {
    heatPackageStorage = heatPackage;
    hasConfiguredHeatPackage = true;

    if (!heatSystem) {
        return;
    }

    configureHeatSystem(*heatSystem);
    applyHeatActivationState(*heatSystem);
}

void HeatSystemController::applyResolvedContacts(const std::vector<RuntimeContactResult>& resolvedContacts) {
    resolvedContactsStorage = resolvedContacts;

    if (!heatSystem || !hasConfiguredHeatPackage) {
        return;
    }

    configureHeatSystem(*heatSystem);
    applyHeatActivationState(*heatSystem);
}

bool HeatSystemController::isHeatSystemActive() const {
    return hasConfiguredHeatPackage && heatPackageStorage.authored.active;
}

bool HeatSystemController::isHeatSystemPaused() const {
    return hasConfiguredHeatPackage &&
           heatPackageStorage.authored.active &&
           heatPackageStorage.authored.paused;
}

std::unique_ptr<HeatSystem> HeatSystemController::buildHeatSystem(VkExtent2D extent, VkRenderPass renderPass) {
    std::unique_ptr<HeatSystem> system = std::make_unique<HeatSystem>(
        vulkanDevice,
        memoryAllocator,
        resourceManager,
        remeshResources,
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

    if (hasConfiguredHeatPackage) {
        configureHeatSystem(*system);
        applyHeatActivationState(*system);
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
