#pragma once

#include <cstdint>
#include <glm/glm.hpp>

struct MeshSurfacePoint {
    enum class Type { VERTEX, EDGE, FACE };

    Type type = Type::VERTEX;
    uint32_t elementId = 0;
    glm::dvec3 baryCoords = glm::dvec3(1.0, 0.0, 0.0);
    double split = 0.0;

    MeshSurfacePoint() = default;

    MeshSurfacePoint(uint32_t faceId, const glm::dvec3& bary)
        : type(Type::FACE)
        , elementId(faceId)
        , baryCoords(bary)
        , split(0.0) {
    }

    MeshSurfacePoint(uint32_t edgeId, double t)
        : type(Type::EDGE)
        , elementId(edgeId)
        , baryCoords(0.0)
        , split(t) {
    }

    explicit MeshSurfacePoint(uint32_t vertId)
        : type(Type::VERTEX)
        , elementId(vertId)
        , baryCoords(1.0, 0.0, 0.0)
        , split(0.0) {
    }
};
