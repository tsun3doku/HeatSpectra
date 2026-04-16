#include "RenderSettingsController.hpp"

#include "RenderSettingsManager.hpp"

RenderSettingsController::RenderSettingsController(RenderSettingsManager* settingsManager)
    : settingsManager(settingsManager) {
}

void RenderSettingsController::bind(RenderSettingsManager* settingsManager) {
    this->settingsManager = settingsManager;
}

void RenderSettingsController::setWireframeMode(WireframeMode mode) {
    if (settingsManager) {
        settingsManager->setWireframeMode(mode);
    }
}

app::RenderSettings RenderSettingsController::getSnapshot() const {
    if (settingsManager) {
        return settingsManager->getSnapshot();
    }
    return {};
}
