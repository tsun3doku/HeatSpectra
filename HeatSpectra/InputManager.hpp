#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <functional>
#include <Qt> 

class Camera;
class Gizmo;
class ModelSelection;
class ResourceManager;
class VulkanWindow;

class InputManager {
public:
    InputManager(Camera& camera, Gizmo& gizmo, ModelSelection& modelSelection, 
                 ResourceManager& resourceManager, VkExtent2D& swapChainExtent,
                 VulkanWindow& window);
    ~InputManager() = default;

    void handleScrollInput(double xOffset, double yOffset);
    void handleKeyInput(Qt::Key key, bool pressed);
    void handleMouseMove(float mouseX, float mouseY);
    void handleMouseRelease(int button, float mouseX, float mouseY);
    void handleMouseButton(int button, float mouseX, float mouseY, bool shiftPressed);
    
    void processInput(bool shiftPressed, bool middleButtonPressed, 
                      double mouseX, double mouseY, float deltaTime);

    void updateGizmo();

    // Callbacks for App actions
    std::function<void()> onWireframeToggled;
    std::function<void()> onIntrinsicOverlayToggled;
    std::function<void()> onHeatOverlayToggled;
    std::function<void()> onTimingOverlayToggled;
    std::function<void()> onToggleHeatSystem;
    std::function<void()> onPauseHeatSystem;
    std::function<void()> onResetHeatSystem;

    bool isDraggingGizmo = false;

private:
    Camera& camera;
    Gizmo& gizmo;
    ModelSelection& modelSelection;
    ResourceManager& resourceManager;
    VkExtent2D& swapChainExtent;

    // Gizmo interactions
    glm::vec3 modelStartPosition{0.0f};
    glm::vec3 accumulatedTranslation{0.0f};
    glm::vec3 lastAppliedTranslation{0.0f};
    float accumulatedRotation = 0.0f;
    float lastAppliedRotation = 0.0f;
    glm::vec3 cachedGizmoPosition{0.0f};
};
