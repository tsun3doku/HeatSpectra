#include "InputController.hpp"

#include "Camera.hpp"
#include "CameraController.hpp"
#include "GizmoController.hpp"
#include "NavigationGizmoController.hpp"
#include "ModelSelection.hpp"
#include "render/WindowRuntimeState.hpp"
#include "vulkan/ModelRegistry.hpp"

#include <algorithm>
#include <cmath>

InputController::InputController(CameraController& cameraController, GizmoController& gizmoController,
    NavigationGizmoController& navigationGizmoController, ModelSelection& modelSelection, ModelRegistry& modelRegistry,
    const WindowRuntimeState& windowState)
    : cameraController(cameraController),
      camera(cameraController.getCamera()),
      gizmoController(gizmoController),
      navigationGizmoController(navigationGizmoController),
      modelSelection(modelSelection),
      modelRegistry(modelRegistry),
      windowState(windowState) {
}

std::vector<ViewportCommand> InputController::takePendingViewportCommands() {
    std::vector<ViewportCommand> commands;
    commands.swap(pendingViewportCommands);
    return commands;
}

std::optional<GizmoDragBegin> InputController::takePendingGizmoDragBegin() {
    std::optional<GizmoDragBegin> pending = std::move(pendingGizmoDragBegin);
    pendingGizmoDragBegin.reset();
    return pending;
}

std::optional<GizmoDragUpdate> InputController::takePendingGizmoDragUpdate() {
    std::optional<GizmoDragUpdate> pending = std::move(pendingGizmoDragUpdate);
    pendingGizmoDragUpdate.reset();
    return pending;
}

bool InputController::takePendingGizmoDragEnd() {
    const bool pending = pendingGizmoDragEnd;
    pendingGizmoDragEnd = false;
    return pending;
}

void InputController::cancelGizmoDrag() {
    isDraggingGizmo = false;
    gizmoController.endDrag();
    accumulatedTranslation = glm::vec3(0.0f);
    accumulatedRotation = 0.0f;
    pendingGizmoDragBegin.reset();
    pendingGizmoDragUpdate.reset();
    pendingGizmoDragEnd = false;
}

static VkExtent2D viewportExtent(const WindowRuntimeState& windowState) {
    return {
        windowState.width.load(std::memory_order_acquire),
        windowState.height.load(std::memory_order_acquire)
    };
}

void InputController::handleScrollInput(double yOffset) {
    cameraController.cancelTransition();
    camera.processMouseScroll(yOffset);
}

void InputController::handleKeyInput(Qt::Key key, bool pressed, bool ctrlPressed) {
    if (!pressed) {
        return;
    }

    if (key == Qt::Key_H) {
        pendingViewportCommands.push_back(ViewportCommand::ToggleWireframe);
    }
    else if (key == Qt::Key_AsciiTilde) {
        pendingViewportCommands.push_back(ViewportCommand::ToggleTimingOverlay);
    }
    else if (key == Qt::Key_F) {
        if (modelSelection.getSelected()) {
            const uint32_t selectedID = modelSelection.getSelectedModelID();
            glm::vec3 worldMin(0.0f);
            glm::vec3 worldMax(0.0f);
            if (modelRegistry.tryGetWorldBounds(selectedID, worldMin, worldMax)) {
                camera.setLookAt((worldMin + worldMax) * 0.5f);
            }
        }
    }
    else if (key == Qt::Key_G) {
        if (ctrlPressed) {
            camera.setLookAt(glm::vec3(0.0f));
            camera.resetRadius();
        } else {
            pendingViewportCommands.push_back(ViewportCommand::ToggleGrid);
        }
    }
}

void InputController::handleMouseMove(float mouseX, float mouseY) {
    if (navigationGizmoController.handlePointerMove(mouseX, mouseY)) {
        return;
    }
    const VkExtent2D swapChainExtent = viewportExtent(windowState);

    if (isDraggingGizmo) {
        const glm::vec3 rayOrigin = camera.screenToWorldRayOrigin(
            mouseX, mouseY, swapChainExtent.width, swapChainExtent.height);
        const glm::vec3 rayDir = camera.screenToWorldRay(mouseX, mouseY, swapChainExtent.width, swapChainExtent.height);

        if (gizmoController.getMode() == GizmoMode::Translate) {
            const glm::vec3 newTranslation = gizmoController.calculateTranslationDelta(rayOrigin, rayDir, cachedGizmoPosition, gizmoController.getActiveAxis());
            accumulatedTranslation = newTranslation;
        }
        else if (gizmoController.getMode() == GizmoMode::Rotate) {
            const float angle = gizmoController.calculateRotationDelta(rayOrigin, rayDir, cachedGizmoPosition, gizmoController.getActiveAxis());
            accumulatedRotation = angle;
        }

        pendingGizmoDragUpdate = GizmoDragUpdate{
            accumulatedTranslation,
            accumulatedRotation
        };
    }
}

void InputController::handleMouseRelease(int button, float mouseX, float mouseY) {
    if (button != static_cast<int>(Qt::LeftButton)) {
        return;
    }

    if (navigationGizmoController.handlePointerRelease(mouseX, mouseY)) {
        return;
    }

    if (isDraggingGizmo) {
        pendingGizmoDragEnd = true;
        isDraggingGizmo = false;
        gizmoController.endDrag();
        accumulatedTranslation = glm::vec3(0.0f);
        accumulatedRotation = 0.0f;
    }
}

void InputController::handleMouseButton(int button, float mouseX, float mouseY, bool shiftPressed) {
    if (button != static_cast<int>(Qt::LeftButton)) {
        return;
    }

    if (navigationGizmoController.handlePointerPress(mouseX, mouseY)) {
        return;
    }

    const VkExtent2D swapChainExtent = viewportExtent(windowState);
    int x = static_cast<int>(mouseX);
    int y = static_cast<int>(mouseY);

    x = (std::max)(0, (std::min)(x, static_cast<int>(swapChainExtent.width) - 1));
    y = (std::max)(0, (std::min)(y, static_cast<int>(swapChainExtent.height) - 1));

    modelSelection.queuePickRequest(x, y, shiftPressed, mouseX, mouseY);
}

void InputController::processInput(bool shiftPressed, bool middleButtonPressed, double mouseX, double mouseY, float deltaTime) {
    (void)deltaTime;
    if (middleButtonPressed) {
        cameraController.cancelTransition();
    }
    camera.processMouseMovement(middleButtonPressed, mouseX, mouseY, shiftPressed);
}

void InputController::updateGizmo() {
    const VkExtent2D swapChainExtent = viewportExtent(windowState);

    if (!isDraggingGizmo) {
        const PickedResult lastPick = modelSelection.getLastPickedResult();
        if (lastPick.isGizmo() && modelSelection.getSelected()) {
            GizmoAxis hitAxis = GizmoAxis::None;
            if (lastPick.gizmoAxis == PickedGizmoAxis::X) {
                hitAxis = GizmoAxis::X;
            }
            else if (lastPick.gizmoAxis == PickedGizmoAxis::Y) {
                hitAxis = GizmoAxis::Y;
            }
            else if (lastPick.gizmoAxis == PickedGizmoAxis::Z) {
                hitAxis = GizmoAxis::Z;
            }

            if (hitAxis != GizmoAxis::None) {
                const uint32_t runtimeModelId = modelSelection.getSelectedModelID();
                if (runtimeModelId == 0) {
                    modelSelection.clearLastPickedResult();
                    return;
                }

                if (lastPick.gizmoMode == PickedGizmoMode::Translate) {
                    gizmoController.setMode(GizmoMode::Translate);
                }
                else if (lastPick.gizmoMode == PickedGizmoMode::Rotate) {
                    gizmoController.setMode(GizmoMode::Rotate);
                }

                const PickingRequest pickReq = modelSelection.getLastPickRequest();
                const glm::vec3 gizmoPosition = gizmoController.calculateGizmoPosition(modelRegistry, modelSelection);
                const glm::vec3 rayOrigin = camera.screenToWorldRayOrigin(pickReq.mouseX, pickReq.mouseY, swapChainExtent.width, swapChainExtent.height);
                const glm::vec3 rayDir = camera.screenToWorldRay(pickReq.mouseX, pickReq.mouseY, swapChainExtent.width, swapChainExtent.height);

                isDraggingGizmo = true;
                cachedGizmoPosition = gizmoPosition;
                gizmoController.startDrag(hitAxis, rayOrigin, rayDir, cachedGizmoPosition);
                accumulatedTranslation = glm::vec3(0.0f);
                accumulatedRotation = 0.0f;
                pendingGizmoDragBegin = GizmoDragBegin{
                    runtimeModelId,
                    gizmoController.getMode(),
                    hitAxis
                };

                modelSelection.clearLastPickedResult();
            }
        }
    }

}

