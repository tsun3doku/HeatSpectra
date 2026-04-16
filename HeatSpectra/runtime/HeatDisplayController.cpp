#include "HeatDisplayController.hpp"

#include "heat/HeatSystemDisplayController.hpp"
#include "render/HeatOverlayRenderer.hpp"

void HeatDisplayController::setController(HeatSystemDisplayController* updatedController) {
    controller = updatedController;
}

void HeatDisplayController::setOverlayRenderer(render::HeatOverlayRenderer* updatedOverlayRenderer) {
    overlayRenderer = updatedOverlayRenderer;
}

void HeatDisplayController::apply(uint64_t socketKey, const Config& config) {
    if (socketKey == 0) {
        return;
    }

    touchedSocketKeys.insert(socketKey);

    const bool previewEnabled =
        config.authoredActive &&
        config.anyVisible() &&
        config.isValid();
    if (!previewEnabled) {
        remove(socketKey);
        return;
    }

    const auto existingIt = activeConfigsBySocket.find(socketKey);
    if (existingIt != activeConfigsBySocket.end() && existingIt->second == config) {
        return;
    }

    if (controller) {
        HeatSystemDisplayController::Config displayConfig{};
        displayConfig.showHeatOverlay = true;
        controller->apply(socketKey, displayConfig);
    }

    if (overlayRenderer) {
        overlayRenderer->apply(socketKey, config);
    }

    activeConfigsBySocket[socketKey] = config;
}

void HeatDisplayController::remove(uint64_t socketKey) {
    if (socketKey == 0) {
        return;
    }

    if (controller) {
        controller->disable(socketKey);
    }
    if (overlayRenderer) {
        overlayRenderer->remove(socketKey);
    }

    activeConfigsBySocket.erase(socketKey);
}

void HeatDisplayController::finalizeSync() {
    std::vector<uint64_t> staleSocketKeys;
    staleSocketKeys.reserve(activeConfigsBySocket.size());
    for (const auto& [socketKey, config] : activeConfigsBySocket) {
        (void)config;
        if (touchedSocketKeys.find(socketKey) == touchedSocketKeys.end()) {
            staleSocketKeys.push_back(socketKey);
        }
    }

    for (uint64_t socketKey : staleSocketKeys) {
        remove(socketKey);
    }

    touchedSocketKeys.clear();
}
