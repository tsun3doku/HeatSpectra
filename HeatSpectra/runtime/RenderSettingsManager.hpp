#pragma once

#include "render/RenderSettings.hpp"

#include <mutex>

class RenderSettingsManager {
public:
    void setWireframeMode(app::WireframeMode mode);
    void setIntrinsicOverlayEnabled(bool enabled);
    void setHeatOverlayEnabled(bool enabled);
    void setIntrinsicNormalsEnabled(bool enabled);
    void setIntrinsicVertexNormalsEnabled(bool enabled);
    void setSurfelsEnabled(bool enabled);
    void setVoronoiEnabled(bool enabled);
    void setPointsEnabled(bool enabled);
    void setContactLinesEnabled(bool enabled);
    void setIntrinsicNormalLength(float length);

    void toggleWireframeMode();
    void toggleIntrinsicOverlay();
    void toggleHeatOverlay();
    void toggleTimingOverlay();

    app::RenderSettings getSnapshot() const;

private:
    mutable std::mutex mutex;
    app::RenderSettings settings;
};
