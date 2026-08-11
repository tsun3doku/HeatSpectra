#include "Camera.hpp"
#include <algorithm>
#include <cmath>

void Camera::update(float deltaTime) {
    const float elapsedSeconds = (std::max)(0.0f, deltaTime);
    const float frameScale = elapsedSeconds * 60.0f;
    const float velocityDecay = std::pow(1.0f - dampingFactor, frameScale);

    radiusVelocity *= velocityDecay;
    radius += radiusVelocity * elapsedSeconds;

    if (projectionMode == CameraProjectionMode::Orthographic) {
        orthographicZoomVelocity *= velocityDecay;
        orthographicHeight = glm::clamp(
            orthographicHeight * std::exp(orthographicZoomVelocity * elapsedSeconds),
            minOrthographicHeight,
            maxOrthographicHeight);
    }

    // Clamp radius
    if (radius < minRadius) radius = minRadius;
    if (radius > maxRadius) radius = maxRadius;

    // Dynamic FOV at close range (macro mode) is perspective-only.
    if (projectionMode == CameraProjectionMode::Perspective && radius < zoomThreshold) {
        float t = (radius - minRadius) / (zoomThreshold - minRadius);
        t = glm::clamp(t, 0.0f, 1.0f);
        t = t * t * (3.0f - 2.0f * t); 
        currentFov = glm::mix(minFov, baseFov, t);
    } else {
        currentFov = baseFov;
    }

    // Calculate position based on orientation and radius
    glm::vec3 forward = orientation * glm::vec3(0.0f, 0.0f, -1.0f);
    position = lookAt - forward * radius;

}

void Camera::setLookAt(const glm::vec3& center) {
    lookAt = center;
}

void Camera::setOrientation(const glm::quat& q) {
    orientation = glm::normalize(q);
}

void Camera::setRadius(float r) {
    radius = glm::clamp(r, minRadius, maxRadius);
    radiusVelocity = 0.0f;
}

void Camera::setFov(float f) {
    baseFov = glm::clamp(f, 1.0f, 120.0f);
    currentFov = baseFov;
    if (projectionMode == CameraProjectionMode::Orthographic) {
        orthographicReferenceFov = currentFov;
    }
}

void Camera::setProjectionMode(CameraProjectionMode mode) {
    if (mode == projectionMode) {
        return;
    }
    if (mode == CameraProjectionMode::Orthographic) {
        orthographicReferenceFov = currentFov;
        orthographicHeight = glm::clamp(
            2.0f * radius * std::tan(glm::radians(orthographicReferenceFov) * 0.5f),
            minOrthographicHeight,
            maxOrthographicHeight);
    } else {
        const float halfFovTangent = std::tan(glm::radians(orthographicReferenceFov) * 0.5f);
        if (halfFovTangent > 1e-6f) {
            radius = glm::clamp(
                orthographicHeight / (2.0f * halfFovTangent),
                minRadius,
                maxRadius);
        }
    }
    projectionMode = mode;
    radiusVelocity = 0.0f;
    orthographicZoomVelocity = 0.0f;
}

void Camera::setOrthographicHeight(float height) {
    orthographicHeight = glm::clamp(height, minOrthographicHeight, maxOrthographicHeight);
    orthographicZoomVelocity = 0.0f;
}

void Camera::processMouseMovement(bool middleButtonPressed, double mouseX, double mouseY, bool shiftPressed) {
    if (middleButtonPressed) {
        if (!isMousePressed) {
            lastMouseX = mouseX;
            lastMouseY = mouseY;
            isMousePressed = true;
        }

        const double dx = mouseX - lastMouseX;
        const double dy = mouseY - lastMouseY;

        if (shiftPressed) {
            pan((float)dx, (float)dy);
        } else {
            orbit(static_cast<float>(dx), static_cast<float>(dy));
        }

        lastMouseX = mouseX;
        lastMouseY = mouseY;
    }
    else {
        isMousePressed = false;
    }
}

void Camera::orbit(float dx, float dy) {
    if (projectionMode == CameraProjectionMode::Orthographic) {
        setProjectionMode(CameraProjectionMode::Perspective);
    }
    const glm::quat yawQuat = glm::angleAxis(-dx * sensitivity, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::vec3 right = orientation * glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::quat pitchQuat = glm::angleAxis(-dy * sensitivity, right);
    orientation = glm::normalize(yawQuat * pitchQuat * orientation);
}

void Camera::pan(float dx, float dy) {
    float viewScale = radius;
    if (projectionMode == CameraProjectionMode::Orthographic) {
        const float halfFovTangent = std::tan(glm::radians(orthographicReferenceFov) * 0.5f);
        if (halfFovTangent > 1e-6f) {
            viewScale = orthographicHeight / (2.0f * halfFovTangent);
        }
    }
    const float panSpeed = viewScale * panSensitivity;

    glm::vec3 right = orientation * glm::vec3(1.0f, 0.0f, 0.0f);
    glm::vec3 cameraUp = orientation * glm::vec3(0.0f, 1.0f, 0.0f);

    glm::vec3 offset = -right * dx * panSpeed + cameraUp * dy * panSpeed;

    lookAt += offset;
}

void Camera::resetRadius() {
    radius = 2.0f * units::canonicalToWorldScale(worldUnit);
    radiusVelocity = 0.0f;
}

void Camera::setWorldUnit(units::LengthUnit unit) {
    if (unit == worldUnit) return;
    const float factor = units::scaleBetween(worldUnit, unit);
    if (!(factor > 0.0f) || !std::isfinite(factor)) return;
    lookAt *= factor;
    position *= factor;
    radius *= factor;
    orthographicHeight *= factor;
    worldUnit = unit;

    const float metersToWorld = units::canonicalToWorldScale(worldUnit);
    nearPlane = 0.01f * metersToWorld;
    farPlane = 100.0f * metersToWorld;
    minRadius = 0.1f * metersToWorld;
    maxRadius = 200.0f * metersToWorld;
    minOrthographicHeight = 0.001f * metersToWorld;
    maxOrthographicHeight = 1000.0f * metersToWorld;
    maxRadiusVelocity = 300.0f * metersToWorld;
    maxOrthographicZoomVelocity = 7.2f * metersToWorld;
}

void Camera::processMouseScroll(double yOffset) {
    const float metersToWorld = units::canonicalToWorldScale(worldUnit);
    if (projectionMode == CameraProjectionMode::Orthographic) {
        orthographicZoomVelocity += static_cast<float>(-yOffset) * 0.72f * metersToWorld;
        orthographicZoomVelocity = glm::clamp(
            orthographicZoomVelocity,
            -maxOrthographicZoomVelocity,
            maxOrthographicZoomVelocity);
        return;
    }
    const float baseZoomSpeed = 0.6f * metersToWorld;
    float zoomSpeed = baseZoomSpeed;

    // Slow down zoom at close range
    const float closeRange = 1.0f * metersToWorld;
    if (radius < closeRange) {
        const float normalizedRadius = radius / closeRange;
        zoomSpeed = baseZoomSpeed * std::max(0.1f, normalizedRadius);
    }

    radiusVelocity += (float)(-yOffset) * zoomSpeed;

    radiusVelocity = glm::clamp(radiusVelocity, -maxRadiusVelocity, maxRadiusVelocity);
}

glm::mat4 Camera::getViewMatrix() const {
    const glm::mat4 cameraTransform =
        glm::translate(glm::mat4(1.0f), position) * glm::mat4_cast(orientation);
    return glm::inverse(cameraTransform);
}

glm::mat4 Camera::getProjectionMatrix(float aspectRatio) const {
    if (projectionMode == CameraProjectionMode::Orthographic) {
        const float halfHeight = orthographicHeight * 0.5f;
        const float halfWidth = halfHeight * aspectRatio;
        return glm::orthoRH_ZO(
            -halfWidth,
            halfWidth,
            -halfHeight,
            halfHeight,
            nearPlane,
            farPlane);
    }
    return glm::perspectiveRH_ZO(
        glm::radians(currentFov),
        aspectRatio,
        nearPlane,
        farPlane);
}

glm::vec3 Camera::screenToWorldRay(double mouseX, double mouseY, int screenWidth, int screenHeight) {
    if (projectionMode == CameraProjectionMode::Orthographic) {
        return glm::normalize(orientation * glm::vec3(0.0f, 0.0f, -1.0f));
    }
    float x = static_cast<float>((2.0 * mouseX) / screenWidth - 1.0);
    float y = static_cast<float>(1.0 - (2.0 * mouseY) / screenHeight);

    glm::vec4 rayClip = glm::vec4(x, y, 0.0f, 1.0f);
    glm::vec4 rayView = glm::inverse(getProjectionMatrix(screenWidth / (float)screenHeight)) * rayClip;
    rayView = glm::vec4(rayView.x, rayView.y, -1.0f, 0.0f);

    glm::vec3 rayWorld = glm::vec3(glm::inverse(getViewMatrix()) * rayView);
    return glm::normalize(rayWorld);
}

glm::vec3 Camera::screenToWorldRayOrigin(
    double mouseX,
    double mouseY,
    int screenWidth,
    int screenHeight) const {
    if (projectionMode == CameraProjectionMode::Perspective || screenWidth <= 0 || screenHeight <= 0) {
        return position;
    }
    const float normalizedX = static_cast<float>((2.0 * mouseX) / screenWidth - 1.0);
    const float normalizedY = static_cast<float>(1.0 - (2.0 * mouseY) / screenHeight);
    const float halfHeight = orthographicHeight * 0.5f;
    const float halfWidth = halfHeight * (static_cast<float>(screenWidth) / static_cast<float>(screenHeight));
    const glm::vec3 right = orientation * glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 cameraUp = orientation * glm::vec3(0.0f, 1.0f, 0.0f);
    return position + right * normalizedX * halfWidth + cameraUp * normalizedY * halfHeight;
}
