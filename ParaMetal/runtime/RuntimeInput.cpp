#include "RuntimeInput.hpp"

#include "nodegraph/NodeGraphController.hpp"
#include "nodegraph/NodeGraphRegistry.hpp"
#include "nodegraph/NodeTransform.hpp"
#include "render/WindowRuntimeState.hpp"
#include "scene/InputController.hpp"
#include "scene/SceneController.hpp"
#include "util/Units.hpp"
#include "vulkan/ModelRegistry.hpp"

#include <QtCore/qnamespace.h>

#include <glm/gtc/matrix_inverse.hpp>

#include <utility>
#include <vector>

RuntimeInput::RuntimeInput(
    WindowRuntimeState& windowRuntimeState,
    InputController& inputController,
    const NodeGraphController& graphController,
    const SceneController& sceneController,
    ModelRegistry& modelRegistry)
    : windowRuntimeState(windowRuntimeState),
      inputController(inputController),
      graphController(graphController),
      sceneController(sceneController),
      modelRegistry(modelRegistry) {
}

void RuntimeInput::tick(float deltaTime) {
    std::vector<WindowInputEvent> inputEvents;
    windowRuntimeState.consumeInputEvents(inputEvents);
    for (const WindowInputEvent& inputEvent : inputEvents) {
        switch (inputEvent.type) {
        case WindowInputEventType::Scroll:
            inputController.handleScrollInput(inputEvent.yOffset);
            break;
        case WindowInputEventType::Key:
            inputController.handleKeyInput(static_cast<Qt::Key>(inputEvent.key), inputEvent.pressed, inputEvent.ctrlPressed);
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

    if (std::optional<GizmoDragBegin> begin = inputController.takePendingGizmoDragBegin()) {
        if (!beginGizmoDrag(*begin)) {
            inputController.cancelGizmoDrag();
            resetGizmoDrag();
        }
    }

    if (std::optional<GizmoDragUpdate> update = inputController.takePendingGizmoDragUpdate()) {
        updateGizmoDrag(*update);
    }

    if (inputController.takePendingGizmoDragEnd()) {
        endGizmoDrag();
    }
}

std::vector<ViewportCommand> RuntimeInput::takePendingViewportCommands() {
    return inputController.takePendingViewportCommands();
}

bool RuntimeInput::takePendingNodeParameters(
    NodeGraphNodeId& outNodeId,
    std::vector<NodeGraphParamValue>& outParameters) {
    if (pendingNodeParameters.empty()) {
        return false;
    }

    PendingNodeParameters pending = std::move(pendingNodeParameters.front());
    pendingNodeParameters.pop_front();
    outNodeId = pending.nodeId;
    outParameters = std::move(pending.parameters);
    return true;
}

bool RuntimeInput::beginGizmoDrag(const GizmoDragBegin& begin) {
    uint64_t outputSocketKey = 0;
    if (begin.runtimeModelId == 0 ||
        !sceneController.tryGetRuntimeModelSocketKey(begin.runtimeModelId, outputSocketKey) ||
        outputSocketKey == 0) {
        return false;
    }

    NodeGraphNodeId transformNodeId{};
    if (!graphController.resolveGizmoTransformNode(outputSocketKey, transformNodeId)) {
        return false;
    }

    const NodeGraphNode* transformNode = graphController.graphState().node(transformNodeId);
    if (!transformNode) {
        return false;
    }

    glm::mat4 currentWorld{1.0f};
    if (!modelRegistry.tryGetModelMatrix(begin.runtimeModelId, currentWorld)) {
        return false;
    }

    activeTransformNodeId = transformNodeId;
    activeRuntimeModelId = begin.runtimeModelId;
    activeMode = begin.mode;
    activeAxis = begin.axis;
    dragStartParams = readTransformNodeParams(*transformNode);
    previewParams = dragStartParams;
    canonicalToWorldScale = units::canonicalToWorldScale(graphController.worldUnit());

    glm::mat4 initialLocal = NodeTransform::buildLocalTransform(dragStartParams);
    initialLocal[3][0] *= canonicalToWorldScale;
    initialLocal[3][1] *= canonicalToWorldScale;
    initialLocal[3][2] *= canonicalToWorldScale;
    dragParentWorld = currentWorld * glm::inverse(initialLocal);
    return true;
}

void RuntimeInput::updateGizmoDrag(const GizmoDragUpdate& update) {
    if (!activeTransformNodeId.isValid() || activeRuntimeModelId == 0) {
        return;
    }

    previewParams = dragStartParams;
    if (activeMode == GizmoMode::Translate) {
        const glm::vec3 canonicalDelta =
            update.translationDeltaWorld / canonicalToWorldScale;
        previewParams.translateX += canonicalDelta.x;
        previewParams.translateY += canonicalDelta.y;
        previewParams.translateZ += canonicalDelta.z;
    } else if (activeMode == GizmoMode::Rotate) {
        if (activeAxis == GizmoAxis::X) {
            previewParams.rotateXDegrees += update.rotationDeltaDegrees;
        } else if (activeAxis == GizmoAxis::Y) {
            previewParams.rotateYDegrees += update.rotationDeltaDegrees;
        } else if (activeAxis == GizmoAxis::Z) {
            previewParams.rotateZDegrees += update.rotationDeltaDegrees;
        }
    }

    glm::mat4 previewLocal = NodeTransform::buildLocalTransform(previewParams);
    previewLocal[3][0] *= canonicalToWorldScale;
    previewLocal[3][1] *= canonicalToWorldScale;
    previewLocal[3][2] *= canonicalToWorldScale;
    modelRegistry.setModelMatrix(
        activeRuntimeModelId,
        dragParentWorld * previewLocal);
}

void RuntimeInput::endGizmoDrag() {
    if (!activeTransformNodeId.isValid()) {
        resetGizmoDrag();
        return;
    }

    if (activeMode == GizmoMode::Translate) {
        pendingNodeParameters.push_back({
            activeTransformNodeId,
            {
                {nodegraphparams::transform::TranslateX, NodeGraphParamType::Float, previewParams.translateX},
                {nodegraphparams::transform::TranslateY, NodeGraphParamType::Float, previewParams.translateY},
                {nodegraphparams::transform::TranslateZ, NodeGraphParamType::Float, previewParams.translateZ}
            }
        });
    } else if (activeMode == GizmoMode::Rotate) {
        pendingNodeParameters.push_back({
            activeTransformNodeId,
            {
                {nodegraphparams::transform::RotateXDegrees, NodeGraphParamType::Float, previewParams.rotateXDegrees},
                {nodegraphparams::transform::RotateYDegrees, NodeGraphParamType::Float, previewParams.rotateYDegrees},
                {nodegraphparams::transform::RotateZDegrees, NodeGraphParamType::Float, previewParams.rotateZDegrees}
            }
        });
    }

    resetGizmoDrag();
}

void RuntimeInput::resetGizmoDrag() {
    activeTransformNodeId = {};
    activeRuntimeModelId = 0;
    dragStartParams = {};
    previewParams = {};
    dragParentWorld = glm::mat4(1.0f);
    canonicalToWorldScale = 1.0f;
    activeMode = GizmoMode::Translate;
    activeAxis = GizmoAxis::None;
}

