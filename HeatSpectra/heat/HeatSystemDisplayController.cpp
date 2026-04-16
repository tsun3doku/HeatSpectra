#include "HeatSystemDisplayController.hpp"

#include "heat/HeatSystem.hpp"
#include "heat/HeatSystemComputeController.hpp"

void HeatSystemDisplayController::setComputeController(HeatSystemComputeController* updatedComputeController) {
    computeController = updatedComputeController;
}

void HeatSystemDisplayController::apply(uint64_t socketKey, const Config& config) {
    if (socketKey == 0) {
        return;
    }

    if (!config.anyVisible()) {
        disable(socketKey);
        return;
    }

    activeConfigsBySocket[socketKey] = config;
}

void HeatSystemDisplayController::disable(uint64_t socketKey) {
    if (socketKey == 0) {
        return;
    }

    activeConfigsBySocket.erase(socketKey);
}

void HeatSystemDisplayController::disableAll() {
    activeConfigsBySocket.clear();
}

std::vector<HeatSystem*> HeatSystemDisplayController::getActiveSystems() const {
    std::vector<HeatSystem*> activeSystems;
    activeSystems.reserve(activeConfigsBySocket.size());
    if (!computeController) {
        return activeSystems;
    }

    for (const auto& [socketKey, config] : activeConfigsBySocket) {
        (void)config;
        HeatSystem* system = computeController->getHeatSystem(socketKey);
        if (system && (system->getIsActive() || system->getIsPaused())) {
            activeSystems.push_back(system);
        }
    }

    return activeSystems;
}
