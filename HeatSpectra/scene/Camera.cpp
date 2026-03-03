#include "Camera.hpp"
#include <algorithm>

void Camera::update(float deltaTime) {
    // Apply zoom momentum
    radiusVelocity *= (1.0f - dampingFactor);
    radius += radiusVelocity;

    // Clamp radius
    if (radius < minRadius) radius = minRadius;
    if (radius > maxRadius) radius = maxRadius;

    // Dynamic FOV at close range (macro mode)
    if (radius < zoomThreshold) {
        float t = (radius - minRadius) / (zoomThreshold - minRadius);
        t = glm::clamp(t, 0.0f, 1.0f);
        t = t * t * (3.0f - 2.0f * t); 
        currentFov = glm::mix(minFov, baseFov, t);
    } else {
        currentFov = baseFov;
    }

    // Calculate position based on orientation and radius
    glm::vec3 forward = orientation * glm::vec3(0.0f, 0.0f, 1.0f);
    position = lookAt - forward * radius;

    // Update up vector
    up = orientation * glm::vec3(0.0f, 1.0f, 0.0f);
}

void Camera::setLookAt(const glm::vec3& center) {
    lookAt = center;
}


void Camera::processMouseMovement(bool middleButtonPressed, double mouseX, double mouseY, bool shiftPressed) {
    static double lastX = 0.0;
    static double lastY = 0.0;
    
    if (middleButtonPressed) {
        if (!isMousePressed) {
            lastX = mouseX;
            lastY = mouseY;
            isMousePressed = true;
        }

        double dx = mouseX - lastX;
        double dy = mouseY - lastY;

        if (shiftPressed) {
            pan((float)dx, (float)dy);
        } else {
            // Rotate around world UP (Yaw)
            glm::quat yawQuat = glm::angleAxis((float)(-dx * sensitivity), glm::vec3(0.0f, 1.0f, 0.0f));
            
            // Rotate around local right (Pitch)
            glm::vec3 right = orientation * glm::vec3(1.0f, 0.0f, 0.0f);
            glm::quat pitchQuat = glm::angleAxis((float)(dy * sensitivity), right);

            // Apply rotations
            orientation = yawQuat * pitchQuat * orientation;
            orientation = glm::normalize(orientation);
        }

        lastX = mouseX;
        lastY = mouseY;
    }
    else {
        isMousePressed = false;
    }
}

void Camera::pan(float dx, float dy) {
    float panSpeed = radius * panSensitivity;

    glm::vec3 right = orientation * glm::vec3(1.0f, 0.0f, 0.0f);
    glm::vec3 cameraUp = orientation * glm::vec3(0.0f, 1.0f, 0.0f);

    glm::vec3 offset = right * dx * panSpeed + cameraUp * dy * panSpeed;

    lookAt += offset;
}

void Camera::resetRadius() {
    radius = 2.0f;
    radiusVelocity = 0.0f;
}

void Camera::processMouseScroll(double xOffset, double yOffset) {
    float baseZoomSpeed = 0.01f;
    float zoomSpeed = baseZoomSpeed;

    // Slow down zoom at close range
    if (radius < 1.0f) {
        zoomSpeed = baseZoomSpeed * std::max(0.1f, radius);
    }

    radiusVelocity += (float)(-yOffset) * zoomSpeed;

    if (radiusVelocity > maxVelocity) radiusVelocity = maxVelocity;
    if (radiusVelocity < -maxVelocity) radiusVelocity = -maxVelocity;
}

glm::mat4 Camera::getViewMatrix() const {
    return glm::lookAt(position, lookAt, up);
}

glm::mat4 Camera::getProjectionMatrix(float aspectRatio) const {
    return glm::perspective(glm::radians(currentFov), aspectRatio, nearPlane, farPlane);
}

glm::mat4 Camera::getRotationMatrix() const {
    return glm::mat4_cast(orientation);
}

glm::vec3 Camera::screenToWorldRay(double mouseX, double mouseY, int screenWidth, int screenHeight) {
    float x = (2.0f * mouseX) / screenWidth - 1.0f;
    float y = 1.0f - (2.0f * mouseY) / screenHeight;

    glm::vec4 rayClip = glm::vec4(x, y, -1.0f, 1.0f);
    glm::vec4 rayView = glm::inverse(getProjectionMatrix(screenWidth / (float)screenHeight)) * rayClip;
    rayView = glm::vec4(rayView.x, rayView.y, -1.0f, 0.0f);

    glm::vec3 rayWorld = glm::vec3(glm::inverse(getViewMatrix()) * rayView);
    return glm::normalize(rayWorld);
}
