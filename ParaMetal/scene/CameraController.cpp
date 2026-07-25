#include "CameraController.hpp"

#include "Camera.hpp"
#include "SceneView.hpp"

#include <algorithm>

CameraController::CameraController(Camera& camera)
    : camera(camera) {
}

void CameraController::setPanSensitivity(float sensitivity) {
    camera.panSensitivity = sensitivity;
}

void CameraController::focusOn(const glm::vec3& target) {
    camera.setLookAt(target);
}

void CameraController::setCameraState(
    const glm::vec3& lookAt,
    const glm::quat& orientation,
    float radius,
    float fov,
    CameraProjectionMode projectionMode,
    float orthographicHeight) {
    cancelTransition();
    camera.setLookAt(lookAt);
    camera.setOrientation(orientation);
    camera.setFov(fov);
    camera.setProjectionMode(projectionMode);
    camera.setRadius(radius);
    camera.setOrthographicHeight(orthographicHeight);
    camera.update(0.0f);
}

void CameraController::snapToDirection(
    const glm::vec3& lookDirection,
    const glm::vec3& screenUp,
    CameraProjectionMode projectionMode) {
    const glm::vec3 forward = glm::normalize(lookDirection);
    glm::vec3 right = glm::cross(forward, screenUp);
    if (glm::dot(right, right) < 1e-8f) {
        right = glm::cross(forward, glm::vec3(0.0f, 0.0f, 1.0f));
    }
    right = glm::normalize(right);
    const glm::vec3 up = glm::normalize(glm::cross(right, forward));
    const glm::mat3 basis(right, up, -forward);

    transition.active = true;
    transition.startOrientation = camera.getOrientation();
    transition.targetOrientation = glm::normalize(glm::quat_cast(basis));
    if (glm::dot(transition.startOrientation, transition.targetOrientation) < 0.0f) {
        transition.targetOrientation = -transition.targetOrientation;
    }
    transition.targetProjectionMode = projectionMode;
    transition.elapsedSeconds = 0.0f;
}

void CameraController::orbitFromNavigationGizmo(float dx, float dy) {
    cancelTransition();
    camera.orbit(dx, dy);
}

void CameraController::cancelTransition() {
    transition.active = false;
}

void CameraController::tick(float deltaTime) {
    if (transition.active) {
        transition.elapsedSeconds += (std::max)(0.0f, deltaTime);
        float t = transition.durationSeconds > 0.0f
            ? transition.elapsedSeconds / transition.durationSeconds
            : 1.0f;
        t = glm::clamp(t, 0.0f, 1.0f);
        const float eased = t * t * (3.0f - 2.0f * t);
        camera.setOrientation(glm::slerp(
            transition.startOrientation,
            transition.targetOrientation,
            eased));
        if (t >= 1.0f) {
            camera.setProjectionMode(transition.targetProjectionMode);
            transition.active = false;
        }
    }
    camera.update(deltaTime);
}

render::SceneView CameraController::buildSceneView(VkExtent2D extent) const {
    render::SceneView sceneView{};
    sceneView.view = camera.getViewMatrix();
    sceneView.proj = camera.getProjectionMatrix(static_cast<float>(extent.width) / static_cast<float>(extent.height));
    sceneView.proj[1][1] *= -1.0f;
    sceneView.cameraPosition = camera.getPosition();
    sceneView.cameraFov = camera.getFov();
    sceneView.orthographic = camera.getProjectionMode() == CameraProjectionMode::Orthographic;
    sceneView.orthographicHeight = camera.getOrthographicHeight();
    return sceneView;
}
