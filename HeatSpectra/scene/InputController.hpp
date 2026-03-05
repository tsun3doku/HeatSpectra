#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <Qt>
#include <cstdint>

#include "InputActions.hpp"

class Camera;
class GizmoController;
class ModelSelection;
class ResourceManager;
class SwapchainManager;

class InputController {
public:
    InputController(Camera& camera,
        GizmoController& gizmoController,
        ModelSelection& modelSelection,
        ResourceManager& resourceManager,
        const SwapchainManager& swapchainManager,
        InputActionHandler& actionHandler);
    ~InputController() = default;

    void handleScrollInput(double xOffset, double yOffset);
    void handleKeyInput(Qt::Key key, bool pressed);
    void handleMouseMove(float mouseX, float mouseY);
    void handleMouseRelease(int button, float mouseX, float mouseY);
    void handleMouseButton(int button, float mouseX, float mouseY, bool shiftPressed);

    void processInput(bool shiftPressed, bool middleButtonPressed, double mouseX, double mouseY, float deltaTime);
    void updateGizmo();

    bool isDraggingGizmo = false;

private:
    Camera& camera;
    GizmoController& gizmoController;
    ModelSelection& modelSelection;
    ResourceManager& resourceManager;
    const SwapchainManager& swapchainManager;
    InputActionHandler& actionHandler;

    glm::vec3 modelStartPosition{0.0f};
    glm::vec3 accumulatedTranslation{0.0f};
    glm::vec3 lastAppliedTranslation{0.0f};
    float accumulatedRotation = 0.0f;
    float lastAppliedRotation = 0.0f;
    glm::vec3 cachedGizmoPosition{0.0f};
};

