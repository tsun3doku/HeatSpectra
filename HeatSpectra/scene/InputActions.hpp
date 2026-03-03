#pragma once

class InputActionHandler {
public:
    virtual ~InputActionHandler() = default;

    virtual void onWireframeToggleRequested() = 0;
    virtual void onIntrinsicOverlayToggleRequested() = 0;
    virtual void onHeatOverlayToggleRequested() = 0;
    virtual void onTimingOverlayToggleRequested() = 0;
    virtual void onSimulationToggleRequested() = 0;
    virtual void onSimulationPauseRequested() = 0;
    virtual void onSimulationResetRequested() = 0;
};
