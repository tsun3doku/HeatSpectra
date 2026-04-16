#pragma once

class InputActionHandler {
public:
    virtual ~InputActionHandler() = default;

    virtual void onWireframeToggleRequested() = 0;
    virtual void onTimingOverlayToggleRequested() = 0;
};
