#include "ContactSystemComputeController.hpp"

#include "ContactSystem.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/VulkanDevice.hpp"

ContactSystemComputeController::ContactSystemComputeController(
    VulkanDevice& device,
    MemoryAllocator& allocator)
    : vulkanDevice(device),
      memoryAllocator(allocator) {
}

ContactSystemComputeController::~ContactSystemComputeController() {
    disableAll();
}

void ContactSystemComputeController::configure(uint64_t socketKey, const Config& config) {
    if (socketKey == 0) {
        return;
    }

    auto& system = activeSystems[socketKey];
    if (!system) {
        system = std::make_unique<ContactSystem>(vulkanDevice, memoryAllocator);
    }

    const auto configIt = configuredConfigs.find(socketKey);
    if (configIt != configuredConfigs.end() && configIt->second.computeHash == config.computeHash) {
        return;
    }

    configuredConfigs[socketKey] = config;

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

void ContactSystemComputeController::disable(uint64_t socketKey) {
    configuredConfigs.erase(socketKey);
    auto it = activeSystems.find(socketKey);
    if (it != activeSystems.end()) {
        if (it->second) {
            it->second->disable();
        }
        activeSystems.erase(it);
    }
}

void ContactSystemComputeController::disableAll() {
    configuredConfigs.clear();
    for (auto& [key, system] : activeSystems) {
        if (system) {
            system->disable();
        }
    }
    activeSystems.clear();
}

bool ContactSystemComputeController::exportProduct(uint64_t socketKey, ContactProduct& outProduct) {
    outProduct = {};
    if (configuredConfigs.find(socketKey) == configuredConfigs.end()) {
        return false;
    }

    const auto it = activeSystems.find(socketKey);
    if (it == activeSystems.end() || !it->second) {
        return false;
    }

    ContactSystem& system = *it->second;

    const ContactCoupling* coupling = system.getContactCoupling();
    if (!coupling) {
        return false;
    }

    outProduct.coupling = *coupling;
    outProduct.contactPairBuffer = system.getContactPairBuffer();
    outProduct.contactPairBufferOffset = system.getContactPairBufferOffset();
    outProduct.emitterRuntimeModelId = coupling->emitterRuntimeModelId;
    outProduct.receiverRuntimeModelId = coupling->receiverRuntimeModelId;
    outProduct.outlineVertices = system.getOutlineVertices();
    outProduct.correspondenceVertices = system.getCorrespondenceVertices();
    outProduct.productHash = buildProductHash(outProduct);
    return outProduct.isValid();
}