#pragma once

#include <vulkan/vulkan.h>

#include <glm/vec3.hpp>

class Camera;

namespace render {
struct SceneView;
}

class CameraController {
public:
    explicit CameraController(Camera& camera);

    Camera& getCamera() {
        return camera;
    }
    const Camera& getCamera() const {
        return camera;
    }

    void setPanSensitivity(float sensitivity);
    void focusOn(const glm::vec3& target);
    void resetFocusToOrigin();
    void tick(float deltaTime);

    render::SceneView buildSceneView(VkExtent2D extent) const;

private:
    Camera& camera;
};
