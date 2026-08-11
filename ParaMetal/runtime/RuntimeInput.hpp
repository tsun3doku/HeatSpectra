#pragma once

#include "nodegraph/NodeGraphTypes.hpp"
#include "nodegraph/NodeTransformParams.hpp"
#include "scene/GizmoController.hpp"

#include <glm/mat4x4.hpp>

#include <cstdint>
#include <deque>
#include <vector>

struct WindowRuntimeState;
class InputController;
class NodeGraphController;
class SceneController;
class ModelRegistry;
struct GizmoDragBegin;
struct GizmoDragUpdate;
enum class ViewportCommand : uint8_t;

class RuntimeInput {
public:
    RuntimeInput(
        WindowRuntimeState& windowRuntimeState,
        InputController& inputController,
        const NodeGraphController& graphController,
        const SceneController& sceneController,
        ModelRegistry& modelRegistry);

    void tick(float deltaTime);
    std::vector<ViewportCommand> takePendingViewportCommands();
    bool takePendingNodeParameters(NodeGraphNodeId& outNodeId, std::vector<NodeGraphParamValue>& outParameters);

private:
    struct PendingNodeParameters {
        NodeGraphNodeId nodeId{};
        std::vector<NodeGraphParamValue> parameters;
    };

    bool beginGizmoDrag(const GizmoDragBegin& begin);
    void updateGizmoDrag(const GizmoDragUpdate& update);
    void endGizmoDrag();
    void resetGizmoDrag();

    WindowRuntimeState& windowRuntimeState;
    InputController& inputController;
    const NodeGraphController& graphController;
    const SceneController& sceneController;
    ModelRegistry& modelRegistry;

    NodeGraphNodeId activeTransformNodeId{};
    uint32_t activeRuntimeModelId = 0;
    TransformNodeParams dragStartParams{};
    TransformNodeParams previewParams{};
    glm::mat4 dragParentWorld{1.0f};
    float canonicalToWorldScale = 1.0f;
    GizmoMode activeMode = GizmoMode::Translate;
    GizmoAxis activeAxis = GizmoAxis::None;
    std::deque<PendingNodeParameters> pendingNodeParameters;
};

