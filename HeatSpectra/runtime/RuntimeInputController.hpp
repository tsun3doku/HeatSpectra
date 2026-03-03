#pragma once

struct WindowRuntimeState;
class InputController;

class RuntimeInputController {
public:
    RuntimeInputController(WindowRuntimeState& windowRuntimeState, InputController& inputController);

    void tick(float deltaTime);

private:
    WindowRuntimeState& windowRuntimeState;
    InputController& inputController;
};

