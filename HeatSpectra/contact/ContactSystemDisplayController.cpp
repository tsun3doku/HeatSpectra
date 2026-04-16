#include "ContactSystemDisplayController.hpp"

#include "contact/ContactSystem.hpp"
#include "contact/ContactSystemComputeController.hpp"

void ContactSystemDisplayController::setComputeController(ContactSystemComputeController* updatedComputeController) {
    computeController = updatedComputeController;
}

void ContactSystemDisplayController::apply(uint64_t socketKey, const Config& config) {
    if (socketKey == 0) {
        return;
    }

    ContactSystem* system = computeController ? computeController->getContactSystem(socketKey) : nullptr;
    if (!system) {
        previewEnabledSockets.erase(socketKey);
        return;
    }

    if (config.showPreview) {
        previewEnabledSockets.insert(socketKey);
        system->refreshPreview();
        return;
    }

    previewEnabledSockets.erase(socketKey);
    system->clearPreview();
}

void ContactSystemDisplayController::disable(uint64_t socketKey) {
    if (socketKey == 0) {
        return;
    }

    previewEnabledSockets.erase(socketKey);
    ContactSystem* system = computeController ? computeController->getContactSystem(socketKey) : nullptr;
    if (system) {
        system->clearPreview();
    }
}

void ContactSystemDisplayController::disableAll() {
    if (!computeController) {
        previewEnabledSockets.clear();
        return;
    }

    for (uint64_t socketKey : previewEnabledSockets) {
        ContactSystem* system = computeController->getContactSystem(socketKey);
        if (system) {
            system->clearPreview();
        }
    }

    previewEnabledSockets.clear();
}
