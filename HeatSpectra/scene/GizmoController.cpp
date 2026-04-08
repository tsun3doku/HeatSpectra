#include "GizmoController.hpp"

#include "ModelSelection.hpp"
#include "vulkan/ModelRegistry.hpp"

#include <cmath>

void GizmoController::startDrag(GizmoAxis axis, const glm::vec3& rayOrigin, const glm::vec3& rayDir, const glm::vec3& gizmoPosition) {
    activeAxis = axis;
    dragStartPos = gizmoPosition;
    dragStartRayOrigin = rayOrigin;
    dragStartRayDir = rayDir;

    glm::vec3 axisDir(0.0f);
    if (axis == GizmoAxis::X) {
        axisDir = glm::vec3(1.0f, 0.0f, 0.0f);
    }
    else if (axis == GizmoAxis::Y) {
        axisDir = glm::vec3(0.0f, 1.0f, 0.0f);
    }
    else if (axis == GizmoAxis::Z) {
        axisDir = glm::vec3(0.0f, 0.0f, 1.0f);
    }

    const glm::vec3 w0 = gizmoPosition - rayOrigin;
    const float a = glm::dot(axisDir, axisDir);
    const float b = glm::dot(axisDir, rayDir);
    const float c = glm::dot(rayDir, rayDir);
    const float d = glm::dot(axisDir, w0);
    const float e = glm::dot(rayDir, w0);

    const float denom = a * c - b * b;
    if (fabs(denom) < 0.0001f) {
        dragStartIntersection = gizmoPosition;
        return;
    }

    const float sc = (b * e - c * d) / denom;
    dragStartIntersection = gizmoPosition + axisDir * sc;
}

void GizmoController::endDrag() {
    activeAxis = GizmoAxis::None;
}

glm::vec3 GizmoController::calculateTranslationDelta(const glm::vec3& rayOrigin, const glm::vec3& rayDir, const glm::vec3& gizmoPosition, GizmoAxis axis) {
    (void)gizmoPosition;

    if (axis == GizmoAxis::None) {
        return glm::vec3(0.0f);
    }

    glm::vec3 axisDir(0.0f);
    if (axis == GizmoAxis::X) {
        axisDir = glm::vec3(1.0f, 0.0f, 0.0f);
    }
    else if (axis == GizmoAxis::Y) {
        axisDir = glm::vec3(0.0f, 1.0f, 0.0f);
    }
    else if (axis == GizmoAxis::Z) {
        axisDir = glm::vec3(0.0f, 0.0f, 1.0f);
    }
    else {
        return glm::vec3(0.0f);
    }

    const glm::vec3 w0 = dragStartPos - rayOrigin;
    const float a = glm::dot(axisDir, axisDir);
    const float b = glm::dot(axisDir, rayDir);
    const float c = glm::dot(rayDir, rayDir);
    const float d = glm::dot(axisDir, w0);
    const float e = glm::dot(rayDir, w0);

    const float denom = a * c - b * b;
    if (fabs(denom) < 0.0001f) {
        return glm::vec3(0.0f);
    }

    const float sc = (b * e - c * d) / denom;
    const glm::vec3 currentIntersection = dragStartPos + axisDir * sc;
    const glm::vec3 translation = currentIntersection - dragStartIntersection;

    const float distance = glm::dot(translation, axisDir);
    return axisDir * distance;
}

float GizmoController::calculateRotationDelta(const glm::vec3& rayOrigin, const glm::vec3& rayDir, const glm::vec3& gizmoPosition, GizmoAxis axis) {
    if (axis == GizmoAxis::None) {
        return 0.0f;
    }

    glm::vec3 axisDir(0.0f);
    if (axis == GizmoAxis::X) {
        axisDir = glm::vec3(1.0f, 0.0f, 0.0f);
    }
    else if (axis == GizmoAxis::Y) {
        axisDir = glm::vec3(0.0f, 1.0f, 0.0f);
    }
    else if (axis == GizmoAxis::Z) {
        axisDir = glm::vec3(0.0f, 0.0f, 1.0f);
    }
    else {
        return 0.0f;
    }

    const float rayDotAxis = glm::dot(rayDir, axisDir);
    if (fabs(rayDotAxis) < 0.0001f) {
        return 0.0f;
    }

    const float startRayDotAxis = glm::dot(dragStartRayDir, axisDir);
    if (fabs(startRayDotAxis) < 0.0001f) {
        return 0.0f;
    }

    const float t = glm::dot(gizmoPosition - rayOrigin, axisDir) / rayDotAxis;
    const glm::vec3 currentPoint = rayOrigin + rayDir * t;

    const float t0 = glm::dot(gizmoPosition - dragStartRayOrigin, axisDir) / startRayDotAxis;
    const glm::vec3 startPoint = dragStartRayOrigin + dragStartRayDir * t0;

    const glm::vec3 startVec = glm::normalize(startPoint - gizmoPosition);
    const glm::vec3 currentVec = glm::normalize(currentPoint - gizmoPosition);

    const float cosAngle = glm::clamp(glm::dot(startVec, currentVec), -1.0f, 1.0f);
    float angle = acos(cosAngle);

    const glm::vec3 cross = glm::cross(startVec, currentVec);
    if (glm::dot(cross, axisDir) < 0.0f) {
        angle = -angle;
    }

    return glm::degrees(angle);
}

glm::vec3 GizmoController::calculateGizmoPosition(ModelRegistry& resourceManager, const ModelSelection& modelSelection) {
    const auto& selectedModelIDs = modelSelection.getSelectedModelIDsRenderThread();
    glm::vec3 gizmoPosition(0.0f);
    int count = 0;

    for (uint32_t id : selectedModelIDs) {
        glm::vec3 worldCenter(0.0f);
        if (resourceManager.tryGetWorldBoundingBoxCenter(id, worldCenter)) {
            gizmoPosition += worldCenter;
            count++;
        }
    }

    if (count > 0) {
        gizmoPosition /= static_cast<float>(count);
        return gizmoPosition;
    }

    for (uint32_t modelId : resourceManager.getRenderableModelIds()) {
        glm::vec3 worldCenter(0.0f);
        if (resourceManager.tryGetWorldBoundingBoxCenter(modelId, worldCenter)) {
            return worldCenter;
        }
    }

    return glm::vec3(0.0f);
}

