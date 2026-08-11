#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <Qt>
#include <cstdint>
#include <optional>
#include <vector>

#include "GizmoController.hpp"

class Camera;
class CameraController;
class NavigationGizmoController;
class ModelSelection;
class ModelRegistry;
struct WindowRuntimeState;

enum class ViewportCommand : uint8_t {
    ToggleWireframe,
    ToggleTimingOverlay,
    ToggleGrid
};

struct GizmoDragBegin {
    uint32_t runtimeModelId = 0;
    GizmoMode mode = GizmoMode::Translate;
    GizmoAxis axis = GizmoAxis::None;
};

struct GizmoDragUpdate {
    glm::vec3 translationDeltaWorld{0.0f};
    float rotationDeltaDegrees = 0.0f;
};

class InputController {
public:
    InputController(CameraController& cameraController,
        GizmoController& gizmoController,
        NavigationGizmoController& navigationGizmoController,
        ModelSelection& modelSelection,
        ModelRegistry& modelRegistry,
        const WindowRuntimeState& windowState);
    ~InputController() = default;

    void handleScrollInput(double yOffset);
    void handleKeyInput(Qt::Key key, bool pressed, bool ctrlPressed);
    void handleMouseMove(float mouseX, float mouseY);
    void handleMouseRelease(int button, float mouseX, float mouseY);
    void handleMouseButton(int button, float mouseX, float mouseY, bool shiftPressed);

    void processInput(bool shiftPressed, bool middleButtonPressed, double mouseX, double mouseY, float deltaTime);
    void updateGizmo();
    std::vector<ViewportCommand> takePendingViewportCommands();
    std::optional<GizmoDragBegin> takePendingGizmoDragBegin();
    std::optional<GizmoDragUpdate> takePendingGizmoDragUpdate();
    bool takePendingGizmoDragEnd();
    void cancelGizmoDrag();

private:
    CameraController& cameraController;
    Camera& camera;
    GizmoController& gizmoController;
    NavigationGizmoController& navigationGizmoController;
    ModelSelection& modelSelection;
    ModelRegistry& modelRegistry;
    const WindowRuntimeState& windowState;
    std::vector<ViewportCommand> pendingViewportCommands;
    std::optional<GizmoDragBegin> pendingGizmoDragBegin;
    std::optional<GizmoDragUpdate> pendingGizmoDragUpdate;
    bool pendingGizmoDragEnd = false;
    bool isDraggingGizmo = false;

    glm::vec3 accumulatedTranslation{0.0f};
    float accumulatedRotation = 0.0f;
    glm::vec3 cachedGizmoPosition{0.0f};
};


