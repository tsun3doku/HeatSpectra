#pragma once

#include <glm/glm.hpp>

class ModelSelection;
class ResourceManager;

enum class GizmoAxis {
    None = 0,
    X = 1,
    Y = 2,
    Z = 3
};

enum class GizmoMode {
    Translate,
    Rotate,
    Scale
};

class GizmoController {
public:
    GizmoController() = default;
    ~GizmoController() = default;

    void setMode(GizmoMode mode) {
        currentMode = mode;
    }

    GizmoMode getMode() const {
        return currentMode;
    }

    void setActiveAxis(GizmoAxis axis) {
        activeAxis = axis;
    }

    GizmoAxis getActiveAxis() const {
        return activeAxis;
    }

    bool isActive() const {
        return activeAxis != GizmoAxis::None;
    }

    void startDrag(GizmoAxis axis, const glm::vec3& rayOrigin, const glm::vec3& rayDir, const glm::vec3& gizmoPosition);
    void endDrag();

    glm::vec3 calculateTranslationDelta(const glm::vec3& rayOrigin, const glm::vec3& rayDir, const glm::vec3& gizmoPosition, GizmoAxis axis);
    float calculateRotationDelta(const glm::vec3& rayOrigin, const glm::vec3& rayDir, const glm::vec3& gizmoPosition, GizmoAxis axis);

    glm::vec3 calculateGizmoPosition(ResourceManager& resourceManager, const ModelSelection& modelSelection);

private:
    GizmoMode currentMode = GizmoMode::Translate;
    GizmoAxis activeAxis = GizmoAxis::None;

    glm::vec3 dragStartPos = glm::vec3(0.0f);
    glm::vec3 dragStartRayOrigin = glm::vec3(0.0f);
    glm::vec3 dragStartRayDir = glm::vec3(0.0f);
    glm::vec3 dragStartIntersection = glm::vec3(0.0f);
};
