#pragma once

#include <vulkan/vulkan.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>
#include <cstdint>
#include "util/Units.hpp"

class Camera;
enum class CameraProjectionMode : uint8_t;

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
    void setWorldUnit(units::LengthUnit unit);
    void focusOn(const glm::vec3& target);
    void setCameraState(
        const glm::vec3& lookAt,
        const glm::quat& orientation,
        float radius,
        float fov,
        CameraProjectionMode projectionMode,
        float orthographicHeight);
    void snapToDirection(
        const glm::vec3& lookDirection,
        const glm::vec3& screenUp,
        CameraProjectionMode projectionMode);
    void orbitFromNavigationGizmo(float dx, float dy);
    void cancelTransition();
    void tick(float deltaTime);

    render::SceneView buildSceneView(VkExtent2D extent) const;

private:
    struct Transition {
        bool active = false;
        glm::quat startOrientation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::quat targetOrientation{1.0f, 0.0f, 0.0f, 0.0f};
        CameraProjectionMode targetProjectionMode;
        float elapsedSeconds = 0.0f;
        float durationSeconds = 0.30f;
    };

    Camera& camera;
    Transition transition;
};
