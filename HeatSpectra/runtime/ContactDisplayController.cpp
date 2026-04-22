#include "ContactDisplayController.hpp"

#include "render/ContactOverlayRenderer.hpp"

#include <vector>

void ContactDisplayController::setOverlayRenderer(render::ContactOverlayRenderer* updatedOverlayRenderer) {
    overlayRenderer = updatedOverlayRenderer;
}

void ContactDisplayController::apply(uint64_t socketKey, const Config& config) {
    if (socketKey == 0) {
        return;
    }

    syncedSockets.insert(socketKey);

    const bool previewEnabled =
        config.authoredActive &&
        config.hasValidContact &&
        config.isValid();
    if (!previewEnabled) {
        remove(socketKey);
        return;
    }

    const auto existingIt = configsBySocket.find(socketKey);
    if (existingIt != configsBySocket.end() && existingIt->second.displayHash == config.displayHash) {
        return;
    }

    if (overlayRenderer) {
        overlayRenderer->apply(socketKey, config);
    }

    configsBySocket[socketKey] = config;
}

void ContactDisplayController::remove(uint64_t socketKey) {
    if (socketKey == 0) {
        return;
    }

    if (overlayRenderer) {
        overlayRenderer->remove(socketKey);
    }

    configsBySocket.erase(socketKey);
}

void ContactDisplayController::finalizeSync() {
    std::vector<uint64_t> staleSocketKeys;
    staleSocketKeys.reserve(configsBySocket.size());
    for (const auto& [socketKey, config] : configsBySocket) {
        (void)config;
        if (syncedSockets.find(socketKey) == syncedSockets.end()) {
            staleSocketKeys.push_back(socketKey);
        }
    }

    for (uint64_t socketKey : staleSocketKeys) {
        remove(socketKey);
    }

    syncedSockets.clear();
}