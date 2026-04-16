#include "ContactSystemComputeController.hpp"

#include "ContactSystem.hpp"
#include "ContactSystemRuntime.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/VulkanDevice.hpp"
#include "vulkan/UniformBufferManager.hpp"

ContactSystemComputeController::ContactSystemComputeController(
    VulkanDevice& device,
    MemoryAllocator& allocator,
    UniformBufferManager& uniformBufferManager,
    uint32_t maxFramesInFlight)
    : vulkanDevice(device),
      memoryAllocator(allocator),
      uniformBufferManager(uniformBufferManager),
      maxFramesInFlight(maxFramesInFlight) {
}

ContactSystemComputeController::~ContactSystemComputeController() {
    disableAll();
}

std::unique_ptr<ContactSystem> ContactSystemComputeController::buildContactSystem(VkRenderPass renderPass) {
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

void ContactSystemComputeController::createContactSystem(VkExtent2D extent, VkRenderPass renderPass) {
    currentExtent = extent;
    currentRenderPass = renderPass;
}

void ContactSystemComputeController::updateRenderContext(VkExtent2D extent, VkRenderPass renderPass) {
    currentExtent = extent;
    currentRenderPass = renderPass;
}

void ContactSystemComputeController::updateRenderResources() {
    for (auto& [socketKey, system] : contactSystems) {
        (void)socketKey;
        if (!system) {
            continue;
        }
        system->updateRenderResources(maxFramesInFlight, currentRenderPass);
    }
}

void ContactSystemComputeController::configure(uint64_t socketKey, const Config& config) {
    if (socketKey == 0) {
        return;
    }

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
    }
}

void ContactSystemComputeController::disable(uint64_t socketKey) {
    auto it = contactSystems.find(socketKey);
    if (it != contactSystems.end()) {
        if (it->second) {
            it->second->clearPreview();
            it->second->disable();
        }
        contactSystems.erase(it);
    }
}

void ContactSystemComputeController::disableAll() {
    for (auto& [key, system] : contactSystems) {
        if (system) {
            system->clearPreview();
            system->disable();
        }
    }
    contactSystems.clear();
}

bool ContactSystemComputeController::exportProduct(uint64_t socketKey, ContactProduct& outProduct) {
    outProduct = {};
    const auto it = contactSystems.find(socketKey);
    if (it == contactSystems.end() || !it->second) {
        return false;
    }

    ContactSystem& system = *it->second;
    system.refreshPreview();

    const ContactSystemRuntime* runtime = system.runtimeState();
    const ContactSystem::Result& preview = system.previewState();
    if (!runtime) {
        return false;
    }

    const ContactProduct* runtimeProduct = runtime->getProduct();
    if (!runtimeProduct) {
        return false;
    }

    outProduct = *runtimeProduct;
    outProduct.outlineVertices = preview.outlineVertices;
    outProduct.correspondenceVertices = preview.correspondenceVertices;
    outProduct.contentHash = computeContentHash(outProduct);
    return outProduct.isValid();
}

ContactSystem* ContactSystemComputeController::getContactSystem(uint64_t socketKey) const {
    const auto it = contactSystems.find(socketKey);
    if (it == contactSystems.end()) {
        return nullptr;
    }
    return it->second.get();
}

std::vector<ContactSystem*> ContactSystemComputeController::getActiveSystems() const {
    std::vector<ContactSystem*> systems;
    systems.reserve(contactSystems.size());
    for (const auto& [key, system] : contactSystems) {
        if (system) {
            systems.push_back(system.get());
        }
    }
    return systems;
}
