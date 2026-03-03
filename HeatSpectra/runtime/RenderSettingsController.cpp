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

void RenderSettingsController::setIntrinsicOverlayEnabled(bool enabled) {
    if (settingsManager) {
        settingsManager->setIntrinsicOverlayEnabled(enabled);
    }
}

void RenderSettingsController::setHeatOverlayEnabled(bool enabled) {
    if (settingsManager) {
        settingsManager->setHeatOverlayEnabled(enabled);
    }
}

void RenderSettingsController::setIntrinsicNormalsEnabled(bool enabled) {
    if (settingsManager) {
        settingsManager->setIntrinsicNormalsEnabled(enabled);
    }
}

void RenderSettingsController::setIntrinsicVertexNormalsEnabled(bool enabled) {
    if (settingsManager) {
        settingsManager->setIntrinsicVertexNormalsEnabled(enabled);
    }
}

void RenderSettingsController::setSurfelsEnabled(bool enabled) {
    if (settingsManager) {
        settingsManager->setSurfelsEnabled(enabled);
    }
}

void RenderSettingsController::setVoronoiEnabled(bool enabled) {
    if (settingsManager) {
        settingsManager->setVoronoiEnabled(enabled);
    }
}

void RenderSettingsController::setPointsEnabled(bool enabled) {
    if (settingsManager) {
        settingsManager->setPointsEnabled(enabled);
    }
}

void RenderSettingsController::setContactLinesEnabled(bool enabled) {
    if (settingsManager) {
        settingsManager->setContactLinesEnabled(enabled);
    }
}

void RenderSettingsController::setIntrinsicNormalLength(float length) {
    if (settingsManager) {
        settingsManager->setIntrinsicNormalLength(length);
    }
}

app::RenderSettings RenderSettingsController::getSnapshot() const {
    if (settingsManager) {
        return settingsManager->getSnapshot();
    }
    return {};
}

