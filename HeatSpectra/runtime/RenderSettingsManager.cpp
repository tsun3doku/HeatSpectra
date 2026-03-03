#include "RenderSettingsManager.hpp"

void RenderSettingsManager::setWireframeMode(app::WireframeMode mode) {
    std::lock_guard<std::mutex> lock(mutex);
    settings.wireframeMode = mode;
}

void RenderSettingsManager::setIntrinsicOverlayEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex);
    settings.intrinsicOverlayEnabled = enabled;
}

void RenderSettingsManager::setHeatOverlayEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex);
    settings.heatOverlayEnabled = enabled;
}

void RenderSettingsManager::setIntrinsicNormalsEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex);
    settings.intrinsicNormalsEnabled = enabled;
}

void RenderSettingsManager::setIntrinsicVertexNormalsEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex);
    settings.intrinsicVertexNormalsEnabled = enabled;
}

void RenderSettingsManager::setSurfelsEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex);
    settings.surfelsEnabled = enabled;
}

void RenderSettingsManager::setVoronoiEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex);
    settings.voronoiEnabled = enabled;
}

void RenderSettingsManager::setPointsEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex);
    settings.pointsEnabled = enabled;
}

void RenderSettingsManager::setContactLinesEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex);
    settings.contactLinesEnabled = enabled;
}

void RenderSettingsManager::setIntrinsicNormalLength(float length) {
    std::lock_guard<std::mutex> lock(mutex);
    settings.intrinsicNormalLength = length;
}

void RenderSettingsManager::toggleWireframeMode() {
    std::lock_guard<std::mutex> lock(mutex);
    settings.wireframeMode =
        static_cast<app::WireframeMode>((static_cast<int>(settings.wireframeMode) + 1) % 3);
}

void RenderSettingsManager::toggleIntrinsicOverlay() {
    std::lock_guard<std::mutex> lock(mutex);
    settings.intrinsicOverlayEnabled = !settings.intrinsicOverlayEnabled;
}

void RenderSettingsManager::toggleHeatOverlay() {
    std::lock_guard<std::mutex> lock(mutex);
    settings.heatOverlayEnabled = !settings.heatOverlayEnabled;
}

void RenderSettingsManager::toggleTimingOverlay() {
    std::lock_guard<std::mutex> lock(mutex);
    settings.gpuTimingOverlayEnabled = !settings.gpuTimingOverlayEnabled;
}

app::RenderSettings RenderSettingsManager::getSnapshot() const {
    std::lock_guard<std::mutex> lock(mutex);
    return settings;
}
