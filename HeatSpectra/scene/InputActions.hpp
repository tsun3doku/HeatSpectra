#pragma once

class InputActionHandler {
public:
    virtual ~InputActionHandler() = default;

    virtual void onWireframeToggleRequested() = 0;
    virtual void onIntrinsicOverlayToggleRequested() = 0;
    virtual void onHeatOverlayToggleRequested() = 0;
    virtual void onTimingOverlayToggleRequested() = 0;
};
