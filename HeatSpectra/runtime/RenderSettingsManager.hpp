#pragma once

#include "scene/InputActions.hpp"
#include "render/RenderSettings.hpp"

#include <mutex>

class RenderSettingsManager : public InputActionHandler {
public:
    void setWireframeMode(app::WireframeMode mode);

    void toggleWireframeMode();
    void toggleTimingOverlay();

    app::RenderSettings getSnapshot() const;

private:
    void onWireframeToggleRequested() override;
    void onTimingOverlayToggleRequested() override;

    mutable std::mutex mutex;
    app::RenderSettings settings;
};
