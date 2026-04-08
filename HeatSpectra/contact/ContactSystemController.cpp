#include "ContactSystemController.hpp"

#include "ContactSystem.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/VulkanDevice.hpp"
#include "vulkan/UniformBufferManager.hpp"

ContactSystemController::ContactSystemController(
    VulkanDevice& device,
    MemoryAllocator& allocator,
    UniformBufferManager& uniformBufferManager,
    uint32_t maxFramesInFlight)
    : vulkanDevice(device),
      memoryAllocator(allocator),
      uniformBufferManager(uniformBufferManager),
      maxFramesInFlight(maxFramesInFlight) {
}

ContactSystemController::~ContactSystemController() {
    disableAll();
}

std::unique_ptr<ContactSystem> ContactSystemController::buildContactSystem(VkRenderPass renderPass) {
    auto system = std::make_unique<ContactSystem>(
        vulkanDevice,
        memoryAllocator,
        uniformBufferManager,
        maxFramesInFlight,
        renderPass);
    if (!system || !system->isInitialized()) {
        return nullptr;
    }
    return system;
}

void ContactSystemController::createContactSystem(VkExtent2D extent, VkRenderPass renderPass) {
    currentExtent = extent;
    currentRenderPass = renderPass;
}

void ContactSystemController::updateRenderContext(VkExtent2D extent, VkRenderPass renderPass) {
    currentExtent = extent;
    currentRenderPass = renderPass;
}

void ContactSystemController::updateRenderResources() {
    for (auto& [socketKey, system] : contactSystems) {
        (void)socketKey;
        if (!system) {
            continue;
        }
        system->updateRenderResources(maxFramesInFlight, currentRenderPass);
    }
}

void ContactSystemController::configure(uint64_t socketKey, const Config& config) {
    if (socketKey == 0) {
        return;
    }

    configuredConfigs[socketKey] = config;
    auto& system = contactSystems[socketKey];
    if (!system && currentRenderPass != VK_NULL_HANDLE) {
        system = buildContactSystem(currentRenderPass);
    }

    if (system) {
        system->setParams(
            config.couplingType,
            config.minNormalDot,
            config.contactRadius);
        system->setEmitterState(
            config.emitterModelId,
            config.emitterLocalToWorld,
            config.emitterIntrinsicMesh,
            config.emitterRuntimeModelId);
        system->setReceiverState(
            config.receiverModelId,
            config.receiverLocalToWorld,
            config.receiverIntrinsicMesh,
            config.receiverRuntimeModelId);
        system->setReceiverTriangleIndices(config.receiverTriangleIndices);
        system->ensureConfigured();
        if (previewEnabledSockets.find(socketKey) != previewEnabledSockets.end()) {
            system->refreshPreview();
        } else {
            system->clearPreview();
        }
    }
}

void ContactSystemController::disable(uint64_t socketKey) {
    previewEnabledSockets.erase(socketKey);
    configuredConfigs.erase(socketKey);
    auto it = contactSystems.find(socketKey);
    if (it != contactSystems.end()) {
        if (it->second) {
            it->second->clearPreview();
            it->second->disable();
        }
        contactSystems.erase(it);
    }
}

void ContactSystemController::disableAll() {
    previewEnabledSockets.clear();
    configuredConfigs.clear();
    for (auto& [key, system] : contactSystems) {
        if (system) {
            system->clearPreview();
            system->disable();
        }
    }
    contactSystems.clear();
}

bool ContactSystemController::exportProduct(uint64_t socketKey, ContactProduct& outProduct) const {
    const auto it = contactSystems.find(socketKey);
    if (it == contactSystems.end() || !it->second) {
        return false;
    }
    return it->second->exportProduct(outProduct);
}

ContactSystem* ContactSystemController::getContactSystem(uint64_t socketKey) const {
    const auto it = contactSystems.find(socketKey);
    if (it == contactSystems.end()) {
        return nullptr;
    }
    return it->second.get();
}

std::vector<ContactSystem*> ContactSystemController::getActiveSystems() const {
    std::vector<ContactSystem*> systems;
    systems.reserve(contactSystems.size());
    for (const auto& [key, system] : contactSystems) {
        if (system) {
            systems.push_back(system.get());
        }
    }
    return systems;
}

void ContactSystemController::setPreviewEnabled(uint64_t socketKey, bool enabled) {
    if (socketKey == 0) {
        return;
    }

    if (enabled) {
        previewEnabledSockets.insert(socketKey);
        refreshPreview(socketKey);
        return;
    }

    previewEnabledSockets.erase(socketKey);
    clearPreview(socketKey);
}

void ContactSystemController::refreshPreview(uint64_t socketKey) {
    ContactSystem* system = getContactSystem(socketKey);
    if (system) {
        system->refreshPreview();
    }
}

void ContactSystemController::clearPreview(uint64_t socketKey) {
    ContactSystem* system = getContactSystem(socketKey);
    if (system) {
        system->clearPreview();
    }
}

void ContactSystemController::clearAllPreviews() {
    for (auto& [key, system] : contactSystems) {
        if (system) {
            system->clearPreview();
        }
    }
}
