#include "RemeshController.hpp"

RemeshController::OperatingScope::OperatingScope(std::atomic<bool>& isOperating)
    : isOperating(isOperating) {
    previousState = isOperating.exchange(true, std::memory_order_acq_rel);
}

RemeshController::OperatingScope::~OperatingScope() {
    isOperating.store(previousState, std::memory_order_release);
}

RemeshController::RemeshController(
    Remesher& remesher,
    VulkanDevice& vulkanDevice,
    ModelRegistry& resourceManager,
    std::atomic<bool>& isOperating)
    : vulkanDevice(vulkanDevice),
      resourceManager(resourceManager),
      isOperating(isOperating),
      remesher(remesher) {
}

void RemeshController::configure(const Config& config) {
    if (config.socketKey == 0) {
        return;
    }

    auto& system = remeshSystems[config.socketKey];
    if (!system) {
        system = std::make_unique<RemeshSystem>(remesher, vulkanDevice, resourceManager);
    }

    system->setSourceGeometry(config.pointPositions, config.triangleIndices);
    system->setParams(config.iterations, config.minAngleDegrees, config.maxEdgeLength, config.stepSize);
    system->setRuntimeModelId(config.runtimeModelId);

    OperatingScope operatingScope(isOperating);
    if (!system->ensureConfigured() && !system->isReady()) {
        remeshSystems.erase(config.socketKey);
    }
}

void RemeshController::disable(uint64_t socketKey) {
    if (socketKey == 0) {
        return;
    }

    auto it = remeshSystems.find(socketKey);
    if (it != remeshSystems.end()) {
        if (it->second) {
            it->second->disable();
        }
        remeshSystems.erase(it);
    }
}

void RemeshController::disable() {
    for (auto& [socketKey, system] : remeshSystems) {
        (void)socketKey;
        if (system) {
            system->disable();
        }
    }
    remeshSystems.clear();
}


bool RemeshController::exportProduct(uint64_t socketKey, RemeshProduct& outProduct) const {
    outProduct = {};

    if (socketKey == 0) {
        return false;
    }

    const auto systemIt = remeshSystems.find(socketKey);
    if (systemIt == remeshSystems.end() || !systemIt->second) {
        return false;
    }

    return systemIt->second->exportProduct(outProduct);
}
