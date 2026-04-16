#include "ContactDisplayController.hpp"

#include "contact/ContactSystemDisplayController.hpp"
#include "render/ContactOverlayRenderer.hpp"

#include <vector>

void ContactDisplayController::setController(ContactSystemDisplayController* updatedController) {
    controller = updatedController;
}

void ContactDisplayController::setOverlayRenderer(render::ContactOverlayRenderer* updatedOverlayRenderer) {
    overlayRenderer = updatedOverlayRenderer;
}

void ContactDisplayController::apply(uint64_t socketKey, const Config& config) {
    if (socketKey == 0) {
        return;
    }

    touchedSocketKeys.insert(socketKey);

    const bool previewEnabled =
        config.authoredActive &&
        config.hasValidContact &&
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
        controller->apply(socketKey, ContactSystemDisplayController::Config{ true });
    }

    if (overlayRenderer) {
        overlayRenderer->apply(socketKey, config);
    }

    activeConfigsBySocket[socketKey] = config;
}

void ContactDisplayController::remove(uint64_t socketKey) {
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

void ContactDisplayController::finalizeSync() {
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
