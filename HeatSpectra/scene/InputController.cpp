#include "InputController.hpp"

#include "Camera.hpp"
#include "GizmoController.hpp"
#include "ModelSelection.hpp"
#include "app/SwapchainManager.hpp"
#include "nodegraph/NodeGraphBridge.hpp"
#include "scene/SceneController.hpp"
#include "vulkan/ModelRegistry.hpp"

#include <algorithm>
#include <cmath>

namespace {
bool resolveSelectedTransformNode(
    SceneController& sceneController,
    NodeGraphEditor& nodeGraphEditor,
    const ModelSelection& modelSelection,
    NodeGraphNodeId& outTransformNodeId) {
    outTransformNodeId = {};
    const auto& selectedIDs = modelSelection.getSelectedModelIDsRenderThread();
    if (selectedIDs.size() != 1) {
        return false;
    }

    uint32_t nodeModelId = 0;
    if (!sceneController.tryGetRuntimeModelNodeId(selectedIDs.front(), nodeModelId) || nodeModelId == 0) {
        return false;
    }

    return nodeGraphEditor.ensureTransformForModelNode(NodeGraphNodeId{nodeModelId}, outTransformNodeId);
}

}

InputController::InputController(Camera& camera, GizmoController& gizmoController, ModelSelection& modelSelection, ModelRegistry& resourceManager,
    SceneController& sceneController, NodeGraphBridge& nodeGraphBridge,
    const SwapchainManager& swapchainManager, InputActionHandler& actionHandler)
    : camera(camera),
      gizmoController(gizmoController),
      modelSelection(modelSelection),
      resourceManager(resourceManager),
      sceneController(sceneController),
      nodeGraphEditor(nodeGraphBridge),
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
    else if (key == Qt::Key_F) {
        if (modelSelection.getSelected()) {
            const uint32_t selectedID = modelSelection.getSelectedModelID();
            glm::vec3 worldCenter(0.0f);
            if (resourceManager.tryGetWorldBoundingBoxCenter(selectedID, worldCenter)) {
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
        activeTransformNodeId = {};
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
                NodeGraphNodeId transformNodeId{};
                if (!resolveSelectedTransformNode(sceneController, nodeGraphEditor, modelSelection, transformNodeId)) {
                    modelSelection.clearLastPickedResult();
                    return;
                }

                glm::vec3 initialTranslation(0.0f);
                glm::vec3 initialRotationDegrees(0.0f);
                if (!nodeGraphEditor.readTransformNodeValues(transformNodeId, initialTranslation, initialRotationDegrees)) {
                    modelSelection.clearLastPickedResult();
                    return;
                }

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
                accumulatedTranslation = glm::vec3(0.0f);
                lastAppliedTranslation = glm::vec3(0.0f);
                accumulatedRotation = 0.0f;
                lastAppliedRotation = 0.0f;
                activeTransformNodeId = transformNodeId;
                transformDragStartTranslation = initialTranslation;
                transformDragStartRotationDegrees = initialRotationDegrees;

                modelSelection.clearLastPickedResult();
            }
        }
    }

    if (!isDraggingGizmo) {
        return;
    }
    if (!activeTransformNodeId.isValid()) {
        return;
    }

    if (gizmoController.getMode() == GizmoMode::Translate) {
        const glm::vec3 currentTranslation = accumulatedTranslation;
        if (glm::length(currentTranslation - lastAppliedTranslation) < 1e-6f) {
            return;
        }

        const glm::vec3 authoredTranslation = transformDragStartTranslation + currentTranslation;
        if (!nodeGraphEditor.writeTransformTranslation(activeTransformNodeId, authoredTranslation)) {
            return;
        }

        lastAppliedTranslation = currentTranslation;
    }
    else if (gizmoController.getMode() == GizmoMode::Rotate) {
        const float currentRotation = accumulatedRotation;
        const float deltaRotation = currentRotation - lastAppliedRotation;

        if (fabs(deltaRotation) < 0.01f) {
            return;
        }

        const GizmoAxis activeAxis = gizmoController.getActiveAxis();
        if (activeAxis != GizmoAxis::X &&
            activeAxis != GizmoAxis::Y &&
            activeAxis != GizmoAxis::Z) {
            return;
        }

        glm::vec3 authoredRotation = transformDragStartRotationDegrees;
        if (activeAxis == GizmoAxis::X) {
            authoredRotation.x += currentRotation;
        }
        else if (activeAxis == GizmoAxis::Y) {
            authoredRotation.y += currentRotation;
        }
        else if (activeAxis == GizmoAxis::Z) {
            authoredRotation.z += currentRotation;
        }

        if (!nodeGraphEditor.writeTransformRotation(activeTransformNodeId, authoredRotation)) {
            return;
        }

        lastAppliedRotation = currentRotation;
    }
}

