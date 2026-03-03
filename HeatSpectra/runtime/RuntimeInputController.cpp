#include "RuntimeInputController.hpp"

#include "render/WindowRuntimeState.hpp"
#include "scene/InputController.hpp"

#include <QtCore/qnamespace.h>

#include <vector>

RuntimeInputController::RuntimeInputController(WindowRuntimeState& windowRuntimeState, InputController& inputController)
    : windowRuntimeState(windowRuntimeState),
      inputController(inputController) {
}

void RuntimeInputController::tick(float deltaTime) {
    std::vector<WindowInputEvent> inputEvents;
    windowRuntimeState.consumeInputEvents(inputEvents);
    for (const WindowInputEvent& inputEvent : inputEvents) {
        switch (inputEvent.type) {
        case WindowInputEventType::Scroll:
            inputController.handleScrollInput(inputEvent.xOffset, inputEvent.yOffset);
            break;
        case WindowInputEventType::Key:
            inputController.handleKeyInput(static_cast<Qt::Key>(inputEvent.key), inputEvent.pressed);
            break;
        case WindowInputEventType::MousePress:
            inputController.handleMouseButton(
                inputEvent.button,
                inputEvent.x,
                inputEvent.y,
                inputEvent.shiftPressed);
            break;
        case WindowInputEventType::MouseRelease:
            inputController.handleMouseRelease(
                inputEvent.button,
                inputEvent.x,
                inputEvent.y);
            break;
        case WindowInputEventType::MouseMove:
            inputController.handleMouseMove(inputEvent.x, inputEvent.y);
            break;
        default:
            break;
        }
    }

    const double x = static_cast<double>(windowRuntimeState.mouseX.load(std::memory_order_acquire));
    const double y = static_cast<double>(windowRuntimeState.mouseY.load(std::memory_order_acquire));
    const bool middlePressed = windowRuntimeState.middleButtonPressed.load(std::memory_order_acquire);
    const bool shiftPressed = windowRuntimeState.shiftPressed.load(std::memory_order_acquire);

    inputController.processInput(shiftPressed, middlePressed, x, y, deltaTime);
}

