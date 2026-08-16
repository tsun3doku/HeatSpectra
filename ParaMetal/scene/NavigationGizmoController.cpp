#include "NavigationGizmoController.hpp"

#include "Camera.hpp"
#include "CameraController.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

static constexpr float logicalSize = 100.0f;
static constexpr float logicalMargin = 12.0f;
static constexpr float logicalDragThreshold = 4.0f;
static constexpr float edgeThreshold = 0.58f;
static constexpr float cubeProjectionScale = 0.54f;

static bool intersectNavigationUnitCube(const glm::vec3& origin, const glm::vec3& direction, glm::vec3& hit) {
    float tMin = -std::numeric_limits<float>::infinity();
    float tMax = std::numeric_limits<float>::infinity();
    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(direction[axis]) < 1e-7f) {
            if (origin[axis] < -1.0f || origin[axis] > 1.0f) {
                return false;
            }
            continue;
        }
        float a = (-1.0f - origin[axis]) / direction[axis];
        float b = (1.0f - origin[axis]) / direction[axis];
        if (a > b) {
            std::swap(a, b);
        }
        tMin = (std::max)(tMin, a);
        tMax = (std::min)(tMax, b);
        if (tMin > tMax) {
            return false;
        }
    }
    if (tMax < 0.0f) {
        return false;
    }
    const float t = tMin >= 0.0f ? tMin : tMax;
    hit = origin + direction * t;
    return true;
}

NavigationGizmoController::NavigationGizmoController(CameraController& controller)
    : cameraController(controller) {
}

void NavigationGizmoController::setEnabled(bool value) {
    enabled = value;
    if (!enabled) {
        hoveredRegion = 0;
        pressedRegion = 0;
        dragging = false;
    }
}

void NavigationGizmoController::setViewport(VkExtent2D extent, float scale) {
    viewportExtent = extent;
    dpiScale = glm::clamp(scale, 0.5f, 4.0f);
    sizePx = logicalSize * dpiScale;
    const float margin = logicalMargin * dpiScale;
    originPx = glm::vec2(
        (std::max)(0.0f, static_cast<float>(extent.width) - sizePx - margin),
        margin);
}

uint8_t NavigationGizmoController::encodeRegion(const glm::ivec3& side) {
    if (side == glm::ivec3(0)) {
        return 0;
    }
    const int x = glm::clamp(side.x, -1, 1) + 1;
    const int y = glm::clamp(side.y, -1, 1) + 1;
    const int z = glm::clamp(side.z, -1, 1) + 1;
    return static_cast<uint8_t>(1 + x + 3 * y + 9 * z);
}

glm::ivec3 NavigationGizmoController::decodeRegion(uint8_t region) {
    if (region == 0) {
        return glm::ivec3(0);
    }
    int value = static_cast<int>(region) - 1;
    const int x = value % 3;
    value /= 3;
    const int y = value % 3;
    value /= 3;
    const int z = value % 3;
    return glm::ivec3(x - 1, y - 1, z - 1);
}

uint8_t NavigationGizmoController::hitTest(float x, float y) const {
    if (!enabled || sizePx <= 0.0f || x < originPx.x || y < originPx.y ||
        x > originPx.x + sizePx || y > originPx.y + sizePx) {
        return 0;
    }

    const glm::vec2 normalized(
        ((x - originPx.x) / sizePx) * 2.0f - 1.0f,
        1.0f - ((y - originPx.y) / sizePx) * 2.0f);
    const glm::vec3 rayOriginCamera(
        normalized.x / cubeProjectionScale,
        normalized.y / cubeProjectionScale,
        3.0f);
    const glm::vec3 rayDirectionCamera(0.0f, 0.0f, -1.0f);
    const glm::quat orientation = cameraController.getCamera().getOrientation();
    const glm::vec3 rayOrigin = orientation * rayOriginCamera;
    const glm::vec3 rayDirection = orientation * rayDirectionCamera;

    glm::vec3 hit(0.0f);
    if (!intersectNavigationUnitCube(rayOrigin, rayDirection, hit)) {
        return 0;
    }

    glm::ivec3 side(0);
    int primaryAxis = 0;
    if (std::abs(hit.y) > std::abs(hit[primaryAxis])) primaryAxis = 1;
    if (std::abs(hit.z) > std::abs(hit[primaryAxis])) primaryAxis = 2;
    side[primaryAxis] = hit[primaryAxis] >= 0.0f ? 1 : -1;
    for (int axis = 0; axis < 3; ++axis) {
        if (axis != primaryAxis && std::abs(hit[axis]) >= edgeThreshold) {
            side[axis] = hit[axis] >= 0.0f ? 1 : -1;
        }
    }
    return encodeRegion(side);
}

bool NavigationGizmoController::handlePointerMove(float x, float y) {
    const glm::vec2 position(x, y);
    if (pressedRegion != 0) {
        const float threshold = logicalDragThreshold * dpiScale;
        if (!dragging && glm::length(position - pressPosition) >= threshold) {
            dragging = true;
            cameraController.cancelTransition();
        }
        if (dragging) {
            const glm::vec2 delta = position - lastPosition;
            cameraController.orbitFromNavigationGizmo(delta.x, delta.y);
        }
        lastPosition = position;
        hoveredRegion = hitTest(x, y);
        return true;
    }
    hoveredRegion = hitTest(x, y);
    return hoveredRegion != 0;
}

bool NavigationGizmoController::handlePointerPress(float x, float y) {
    const uint8_t hit = hitTest(x, y);
    if (hit == 0) {
        return false;
    }
    pressedRegion = hit;
    hoveredRegion = hit;
    pressPosition = glm::vec2(x, y);
    lastPosition = pressPosition;
    dragging = false;
    return true;
}

bool NavigationGizmoController::handlePointerRelease(float x, float y) {
    if (pressedRegion == 0) {
        return false;
    }
    const uint8_t releasedRegion = pressedRegion;
    const bool wasDragging = dragging;
    pressedRegion = 0;
    dragging = false;
    if (!wasDragging) {
        snapToRegion(releasedRegion);
    }
    hoveredRegion = hitTest(x, y);
    return true;
}

void NavigationGizmoController::snapToRegion(uint8_t region) {
    const glm::ivec3 side = decodeRegion(region);
    const glm::vec3 cameraSide(side);
    const int componentCount = (side.x != 0) + (side.y != 0) + (side.z != 0);
    if (componentCount == 0) {
        return;
    }
    const glm::vec3 lookDirection = -glm::normalize(cameraSide);
    glm::vec3 screenUp(0.0f, 1.0f, 0.0f);
    if (componentCount == 1 && side.y != 0) {
        screenUp = side.y > 0
            ? glm::vec3(0.0f, 0.0f, 1.0f)
            : glm::vec3(0.0f, 0.0f, -1.0f);
    } else {
        screenUp -= glm::dot(screenUp, lookDirection) * lookDirection;
        if (glm::dot(screenUp, screenUp) < 1e-6f) {
            screenUp = glm::vec3(0.0f, 0.0f, side.y >= 0 ? 1.0f : -1.0f);
        } else {
            screenUp = glm::normalize(screenUp);
        }
    }
    cameraController.snapToDirection(
        lookDirection,
        screenUp,
        componentCount == 1
            ? CameraProjectionMode::Orthographic
            : CameraProjectionMode::Perspective);
}

NavigationGizmoRenderData NavigationGizmoController::getRenderData() const {
    NavigationGizmoRenderData data{};
    data.extent = viewportExtent;
    data.cameraOrientation = cameraController.getCamera().getOrientation();
    data.originPx = originPx;
    data.sizePx = sizePx;
    data.hoveredRegion = hoveredRegion;
    data.pressedRegion = pressedRegion;
    return data;
}
