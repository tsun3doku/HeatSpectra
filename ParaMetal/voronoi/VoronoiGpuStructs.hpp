#pragma once

#include <cstdint>

#include <glm/glm.hpp>

namespace voronoi {

struct SurfaceVertex {
    glm::vec4 position{0.0f, 0.0f, 0.0f, 1.0f};
    glm::vec4 normal{0.0f, 0.0f, 1.0f, 0.0f};
};

static_assert(sizeof(SurfaceVertex) == 32, "SurfaceVertex must match GPU stride");

struct NodeFlags {
    static constexpr uint32_t Ghost = 1u << 0;
    static constexpr uint32_t Surface = 1u << 1;
    static constexpr uint32_t TriangleOverflow = 1u << 2;
};

struct Neighbor {
    uint32_t neighborIndex;
    float areaOverDistance;
};

struct Node {
    float volume;
    uint32_t neighborOffset;
    uint32_t neighborCount;
    uint32_t interfaceNeighborCount;
};

struct NodeCoupling {
    uint32_t neighborNodeId;
    float conductance;
};

static_assert(sizeof(NodeCoupling) == 8, "NodeCoupling must match GPU stride");

struct GMLSSurfaceStencil {
    uint32_t valueWeightOffset;
    uint32_t valueWeightCount;
    uint32_t gradientWeightOffset;
    uint32_t gradientWeightCount;
};

struct GMLSSurfaceWeight {
    uint32_t cellIndex;
    float weight;
};

struct GMLSSurfaceGradientWeight {
    uint32_t cellIndex;
    float dTdxWeight;
    float dTdyWeight;
    float dTdzWeight;
};

}
