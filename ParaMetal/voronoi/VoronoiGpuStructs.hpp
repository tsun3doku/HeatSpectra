#pragma once

#include <cstdint>

#include <glm/glm.hpp>

namespace voronoi {

struct SurfaceVertex {
    glm::vec4 position{0.0f, 0.0f, 0.0f, 1.0f};
    glm::vec4 normal{0.0f, 0.0f, 1.0f, 0.0f};
};

static_assert(sizeof(SurfaceVertex) == 32, "SurfaceVertex must match GPU stride");

struct CandidatePushConstants {
    uint32_t faceCount;
    uint32_t seedCount;
    uint32_t _pad0;
    uint32_t _pad1;
};

static_assert(sizeof(CandidatePushConstants) == 16, "CandidatePushConstants must match GPU stride");

struct LloydParams {
    uint32_t nodeCount;
    float alpha;
    float maxStep;
    float pad0;
};

static_assert(sizeof(LloydParams) == 16, "LloydParams must match GPU stride");

struct NodeFlags {
    static constexpr uint32_t Ghost = 1u << 0;
    static constexpr uint32_t Surface = 1u << 1;
    static constexpr uint32_t TriangleOverflow = 1u << 2;
};

struct Node {
    float volume;
    uint32_t neighborOffset;
    uint32_t neighborCount;
    uint32_t interfaceNeighborCount;
};

static_assert(sizeof(Node) == 16, "Node must match GPU stride");

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

static_assert(sizeof(GMLSSurfaceStencil) == 16, "GMLSSurfaceStencil must match GPU stride");

struct GMLSSurfaceWeight {
    uint32_t cellIndex;
    float weight;
};

static_assert(sizeof(GMLSSurfaceWeight) == 8, "GMLSSurfaceWeight must match GPU stride");

struct GMLSSurfaceGradientWeight {
    uint32_t cellIndex;
    float dTdxWeight;
    float dTdyWeight;
    float dTdzWeight;
};

static_assert(sizeof(GMLSSurfaceGradientWeight) == 16, "GMLSSurfaceGradientWeight must match GPU stride");

}