#pragma once

#include "render/RenderSettings.hpp"

class RenderSettingsManager;

class RenderSettingsController {
public:
    using WireframeMode = app::WireframeMode;

    explicit RenderSettingsController(RenderSettingsManager* settingsManager = nullptr);

    void bind(RenderSettingsManager* settingsManager);

    void setWireframeMode(WireframeMode mode);

    app::RenderSettings getSnapshot() const;

private:
    RenderSettingsManager* settingsManager = nullptr;
};
