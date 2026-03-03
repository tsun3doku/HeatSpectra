#pragma once

#include "render/RenderSettings.hpp"

class RenderSettingsManager;

class RenderSettingsController {
public:
    using WireframeMode = app::WireframeMode;

    explicit RenderSettingsController(RenderSettingsManager* settingsManager = nullptr);

    void bind(RenderSettingsManager* settingsManager);

    void setWireframeMode(WireframeMode mode);
    void setIntrinsicOverlayEnabled(bool enabled);
    void setHeatOverlayEnabled(bool enabled);
    void setIntrinsicNormalsEnabled(bool enabled);
    void setIntrinsicVertexNormalsEnabled(bool enabled);
    void setSurfelsEnabled(bool enabled);
    void setVoronoiEnabled(bool enabled);
    void setPointsEnabled(bool enabled);
    void setContactLinesEnabled(bool enabled);
    void setIntrinsicNormalLength(float length);

    app::RenderSettings getSnapshot() const;

private:
    RenderSettingsManager* settingsManager = nullptr;
};

