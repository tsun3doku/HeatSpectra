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

    touchedSocketKeys.insert(socketKey);

    if (!config.anyVisible() || !config.isValid()) {
        remove(socketKey);
        return;
    }

    const auto existingIt = activeConfigsBySocket.find(socketKey);
    if (existingIt != activeConfigsBySocket.end() && existingIt->second == config) {
        return;
    }

    if (intrinsicRenderer) {
        intrinsicRenderer->apply(socketKey, config);
    }

    activeConfigsBySocket[socketKey] = config;
}

void RemeshDisplayController::remove(uint64_t socketKey) {
    if (socketKey == 0) {
        return;
    }

    if (intrinsicRenderer) {
        intrinsicRenderer->remove(socketKey);
    }

    activeConfigsBySocket.erase(socketKey);
}

void RemeshDisplayController::finalizeSync() {
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
