#pragma once

#include <cstdint>

#include <glm/glm.hpp>

namespace voronoi {

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

struct MaterialNode {
    float temperature;
    float conductivityPerMass;
    float thermalMass;
    float density;
    float specificHeat;
    float conductivity;
};

struct GMLSInterface {
    uint32_t neighborIdx;
    float conductance;
    uint32_t _pad0 = 0;
    uint32_t _pad1 = 0;
};

static_assert(sizeof(GMLSInterface) == 16, "GMLSInterface must match GPU stride");

struct GMLSSurfaceStencil {
    uint32_t valueWeightOffset;
    uint32_t valueWeightCount;
    uint32_t gradientWeightOffset;
    uint32_t gradientWeightCount;
};

struct GMLSSurfaceWeight {
    uint32_t cellIndex;
    float weight;
    uint32_t _pad0 = 0;
    uint32_t _pad1 = 0;
};

struct GMLSSurfaceGradientWeight {
    uint32_t cellIndex;
    float dTdxWeight;
    float dTdyWeight;
    float dTdzWeight;
};

struct DebugCellGeometry {
    uint32_t cellID;
    uint32_t vertexCount;
    uint32_t triangleCount;
    float volume;
    glm::vec4 vertices[48];
    glm::uvec4 triangles[96];
};

constexpr uint32_t DEBUG_MAX_PLANE_AREAS = 50;

struct DebugPlaneArea {
    uint32_t planeIndex;
    uint32_t neighborCellID;
    float area;
    float _padding;
};

struct DumpInfo {
    uint32_t cellID;
    uint32_t planeAreaCount;
    uint32_t _padding0;
    uint32_t _padding2;

    glm::vec4 seedPos;

    float unrestrictedVolume;
    float restrictedVolume;
    float totalMeshVolume;
    uint32_t negativeVolumeCellCount;
    float negativeVolumeSumAbs;
    glm::vec2 _padding1;

    DebugPlaneArea planeAreas[DEBUG_MAX_PLANE_AREAS];
};

constexpr uint32_t DEBUG_DUMP_CELL_COUNT = 8;

}
