#include "RemeshDisplayController.hpp"

#include "renderers/IntrinsicRenderer.hpp"

#include <vector>

void RemeshDisplayController::setIntrinsicRenderer(IntrinsicRenderer* updatedIntrinsicRenderer) {
    intrinsicRenderer = updatedIntrinsicRenderer;
}

void RemeshDisplayController::apply(uint64_t socketKey, const Config& config) {
    if (socketKey == 0) {
        return;
    }

    syncedSockets.insert(socketKey);

    if (!config.anyVisible() || !config.isValid()) {
        remove(socketKey);
        return;
    }

    const auto existingIt = configsBySocket.find(socketKey);
    if (existingIt != configsBySocket.end() && existingIt->second.displayHash == config.displayHash) {
        return;
    }

    if (intrinsicRenderer) {
        intrinsicRenderer->apply(socketKey, config);
    }

    configsBySocket[socketKey] = config;
}

void RemeshDisplayController::remove(uint64_t socketKey) {
    if (socketKey == 0) {
        return;
    }

    if (intrinsicRenderer) {
        intrinsicRenderer->remove(socketKey);
    }

    configsBySocket.erase(socketKey);
}

void RemeshDisplayController::finalizeSync() {
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
