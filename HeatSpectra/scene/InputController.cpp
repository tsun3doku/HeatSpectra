#include "InputController.hpp"

#include "Camera.hpp"
#include "GizmoController.hpp"
#include "Model.hpp"
#include "ModelSelection.hpp"
#include "app/SwapchainManager.hpp"
#include "vulkan/ResourceManager.hpp"

#include <algorithm>
#include <cmath>

InputController::InputController(Camera& camera, GizmoController& gizmoController, ModelSelection& modelSelection, ResourceManager& resourceManager,
    const SwapchainManager& swapchainManager, InputActionHandler& actionHandler)
    : camera(camera),
      gizmoController(gizmoController),
      modelSelection(modelSelection),
      resourceManager(resourceManager),
      swapchainManager(swapchainManager),
      actionHandler(actionHandler) {
}

void InputController::handleScrollInput(double xOffset, double yOffset) {
    camera.processMouseScroll(xOffset, yOffset);
}

void InputController::handleKeyInput(Qt::Key key, bool pressed) {
    if (!pressed) {
        return;
    }

    if (key == Qt::Key_H) {
        actionHandler.onWireframeToggleRequested();
    }
    else if (key == Qt::Key_C) {
        actionHandler.onIntrinsicOverlayToggleRequested();
    }
    else if (key == Qt::Key_V) {
        actionHandler.onHeatOverlayToggleRequested();
    }
    else if (key == Qt::Key_AsciiTilde) {
        actionHandler.onTimingOverlayToggleRequested();
    }
    else if (key == Qt::Key_Space) {
        actionHandler.onSimulationToggleRequested();
    }
    else if (key == Qt::Key_P) {
        actionHandler.onSimulationPauseRequested();
    }
    else if (key == Qt::Key_R) {
        actionHandler.onSimulationResetRequested();
    }
    else if (key == Qt::Key_F) {
        if (modelSelection.getSelected()) {
            const uint32_t selectedID = modelSelection.getSelectedModelID();
            Model* model = resourceManager.getModelByID(selectedID);
            if (model) {
                const glm::vec3 localCenter = model->getBoundingBoxCenter();
                const glm::vec3 worldCenter = glm::vec3(model->getModelMatrix() * glm::vec4(localCenter, 1.0f));
                camera.setLookAt(worldCenter);
            }
        }
    }
    else if (key == Qt::Key_G) {
        camera.setLookAt(glm::vec3(0.0f));
        camera.resetRadius();
    }
}

void InputController::handleMouseMove(float mouseX, float mouseY) {
    const VkExtent2D swapChainExtent = swapchainManager.getExtent();

    if (isDraggingGizmo) {
        const glm::vec3 rayOrigin = camera.getPosition();
        const glm::vec3 rayDir = camera.screenToWorldRay(mouseX, mouseY, swapChainExtent.width, swapChainExtent.height);

        if (gizmoController.getMode() == GizmoMode::Translate) {
            const glm::vec3 newTranslation = gizmoController.calculateTranslationDelta(rayOrigin, rayDir, cachedGizmoPosition, gizmoController.getActiveAxis());
            accumulatedTranslation = newTranslation;
        }
        else if (gizmoController.getMode() == GizmoMode::Rotate) {
            const float angle = gizmoController.calculateRotationDelta(rayOrigin, rayDir, cachedGizmoPosition, gizmoController.getActiveAxis());
            accumulatedRotation = angle;
        }
    }
}

void InputController::handleMouseRelease(int button, float mouseX, float mouseY) {
    (void)mouseX;
    (void)mouseY;
    if (button != static_cast<int>(Qt::LeftButton)) {
        return;
    }

    if (isDraggingGizmo) {
        isDraggingGizmo = false;
        gizmoController.endDrag();
        accumulatedTranslation = glm::vec3(0.0f);
        lastAppliedTranslation = glm::vec3(0.0f);
        accumulatedRotation = 0.0f;
        lastAppliedRotation = 0.0f;
    }
}

void InputController::handleMouseButton(int button, float mouseX, float mouseY, bool shiftPressed) {
    if (button != static_cast<int>(Qt::LeftButton)) {
        return;
    }

    const VkExtent2D swapChainExtent = swapchainManager.getExtent();
    int x = static_cast<int>(mouseX);
    int y = static_cast<int>(mouseY);

    x = (std::max)(0, (std::min)(x, static_cast<int>(swapChainExtent.width) - 1));
    y = (std::max)(0, (std::min)(y, static_cast<int>(swapChainExtent.height) - 1));

    modelSelection.queuePickRequest(x, y, shiftPressed, mouseX, mouseY);
}

void InputController::processInput(bool shiftPressed, bool middleButtonPressed, double mouseX, double mouseY, float deltaTime) {
    (void)deltaTime;
    camera.processMouseMovement(middleButtonPressed, mouseX, mouseY, shiftPressed);
}

void InputController::updateGizmo() {
    const VkExtent2D swapChainExtent = swapchainManager.getExtent();

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
                if (lastPick.stencilValue >= 3 && lastPick.stencilValue <= 5) {
                    gizmoController.setMode(GizmoMode::Translate);
                }
                else if (lastPick.stencilValue >= 6 && lastPick.stencilValue <= 8) {
                    gizmoController.setMode(GizmoMode::Rotate);
                }

                const PickingRequest pickReq = modelSelection.getLastPickRequest();
                const glm::vec3 gizmoPosition = gizmoController.calculateGizmoPosition(resourceManager, modelSelection);
                const glm::vec3 rayOrigin = camera.getPosition();
                const glm::vec3 rayDir = camera.screenToWorldRay(pickReq.mouseX, pickReq.mouseY, swapChainExtent.width, swapChainExtent.height);

                isDraggingGizmo = true;
                cachedGizmoPosition = gizmoPosition;
                gizmoController.startDrag(hitAxis, rayOrigin, rayDir, cachedGizmoPosition);
                modelStartPosition = cachedGizmoPosition;
                accumulatedTranslation = glm::vec3(0.0f);
                accumulatedRotation = 0.0f;

                modelSelection.clearLastPickedResult();
            }
        }
    }

    if (!isDraggingGizmo) {
        return;
    }

    const auto& selectedIDs = modelSelection.getSelectedModelIDsRenderThread();

    if (gizmoController.getMode() == GizmoMode::Translate) {
        const glm::vec3 currentTranslation = accumulatedTranslation;
        const glm::vec3 deltaTranslation = currentTranslation - lastAppliedTranslation;

        if (glm::length(deltaTranslation) < 1e-6f) {
            return;
        }

        for (const uint32_t id : selectedIDs) {
            if (Model* model = resourceManager.getModelByID(id)) {
                model->translate(deltaTranslation);
            }
        }

        lastAppliedTranslation = currentTranslation;
    }
    else if (gizmoController.getMode() == GizmoMode::Rotate) {
        const float currentRotation = accumulatedRotation;
        const float deltaRotation = currentRotation - lastAppliedRotation;

        if (fabs(deltaRotation) < 0.01f) {
            return;
        }

        glm::vec3 rotationAxis{};
        const GizmoAxis activeAxis = gizmoController.getActiveAxis();
        if (activeAxis == GizmoAxis::X) {
            rotationAxis = glm::vec3(1.0f, 0.0f, 0.0f);
        }
        else if (activeAxis == GizmoAxis::Y) {
            rotationAxis = glm::vec3(0.0f, 1.0f, 0.0f);
        }
        else if (activeAxis == GizmoAxis::Z) {
            rotationAxis = glm::vec3(0.0f, 0.0f, 1.0f);
        }
        else {
            return;
        }

        for (const uint32_t id : selectedIDs) {
            if (Model* model = resourceManager.getModelByID(id)) {
                model->rotate(glm::radians(deltaRotation), rotationAxis, cachedGizmoPosition);
            }
        }

        lastAppliedRotation = currentRotation;
    }
}
