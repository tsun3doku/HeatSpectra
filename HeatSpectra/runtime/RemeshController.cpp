#include "RemeshController.hpp"

#include "vulkan/MemoryAllocator.hpp"

RemeshController::OperatingScope::OperatingScope(std::atomic<bool>& isOperating)
    : isOperating(isOperating) {
    previousState = isOperating.exchange(true, std::memory_order_acq_rel);
}

RemeshController::OperatingScope::~OperatingScope() {
    isOperating.store(previousState, std::memory_order_release);
}

RemeshController::RemeshController(
    VulkanDevice& vulkanDevice,
    MemoryAllocator& memoryAllocator,
    ModelRegistry& resourceManager,
    std::atomic<bool>& isOperating)
    : vulkanDevice(vulkanDevice),
      resourceManager(resourceManager),
      isOperating(isOperating),
      remesher(vulkanDevice, memoryAllocator) {
}

void RemeshController::configure(const Config& config) {
    if (config.socketKey == 0) {
        return;
    }

    auto& system = activeSystems[config.socketKey];
    if (!system) {
        system = std::make_unique<RemeshSystem>(remesher, vulkanDevice, resourceManager);
    }

    const auto configIt = configuredConfigs.find(config.socketKey);
    if (configIt != configuredConfigs.end() && configIt->second.computeHash == config.computeHash) {
        return;
    }

    configuredConfigs[config.socketKey] = config;

    system->setSourceGeometry(config.pointPositions, config.triangleIndices);
    system->setParams(config.iterations, config.minAngleDegrees, config.maxEdgeLength, config.stepSize);
    system->setRuntimeModelId(config.runtimeModelId);

    OperatingScope operatingScope(isOperating);
    if (!system->ensureConfigured() && !system->isReady()) {
        activeSystems.erase(config.socketKey);
    }
}

void RemeshController::disable(uint64_t socketKey) {
    if (socketKey == 0) {
        return;
    }

    configuredConfigs.erase(socketKey);
    auto it = activeSystems.find(socketKey);
    if (it != activeSystems.end()) {
        if (it->second) {
            it->second->disable();
        }
        activeSystems.erase(it);
    }
}

void RemeshController::disable() {
    configuredConfigs.clear();
    for (auto& [socketKey, system] : activeSystems) {
        (void)socketKey;
        if (system) {
            system->disable();
        }
    }
    activeSystems.clear();
}


bool RemeshController::exportProduct(uint64_t socketKey, RemeshProduct& outProduct) const {
    outProduct = {};

    if (socketKey == 0) {
        return false;
    }

    if (configuredConfigs.find(socketKey) == configuredConfigs.end()) {
        return false;
    }

    const auto systemIt = activeSystems.find(socketKey);
    if (systemIt == activeSystems.end() || !systemIt->second) {
        return false;
    }

    return systemIt->second->exportProduct(outProduct);
}
