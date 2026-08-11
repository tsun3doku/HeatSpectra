#pragma once

#include <cstdint>
#include <cstddef>
#include <limits>

#include <glm/glm.hpp>

namespace heat {

inline constexpr uint32_t NoRewindFrame = std::numeric_limits<uint32_t>::max();

struct SimPlaybackUniform {
    uint32_t resetCounter;
    uint32_t recordedTimelineFrames;
    uint32_t timelineFrameCount;
    float deltaTime;
};

struct SurfacePoint {
    glm::vec3 position;
    float temperatureC;
    glm::vec3 normal;
    float vertexArea;
    glm::vec4 color;
};

struct HeatModelPushConstant {
    uint32_t elementCount;
    float metersPerUnit;
};
static_assert(sizeof(HeatModelPushConstant) == 8);

struct MaterialNode {
    float conductivityPerMass;
    float thermalMass;
    float density;
    float specificHeat;
    float conductivity;
    float pad0;
    uint32_t pad1;
    uint32_t pad2;
};

struct BoundaryState {
    uint32_t conditionType;
    float temperatureC;
    float heatFlux;
    float heatTransferCoefficient;
};

struct BoundaryNode {
    uint32_t contributionOffset;
    uint32_t contributionCount;
    uint32_t dirichletStateIndex;
};

struct BoundaryContribution {
    uint32_t stateIndex;
    float area;
};

static_assert(sizeof(BoundaryState) == 16, "BoundaryState must match GPU stride");
static_assert(sizeof(BoundaryNode) == 12, "BoundaryNode must match GPU stride");
static_assert(sizeof(BoundaryContribution) == 8, "BoundaryContribution must match GPU stride");

struct BufferPushConstant {
    alignas(16) glm::mat4 modelMatrix;
    alignas(16) glm::vec4 sourceParams;
    uint32_t palette = 0;
    float minTemperature = 0.0f;
    float maxTemperature = 100.0f;
};

static_assert(offsetof(BufferPushConstant, palette) == 80);
static_assert(offsetof(BufferPushConstant, minTemperature) == 84);
static_assert(offsetof(BufferPushConstant, maxTemperature) == 88);

struct SourceRenderPushConstant {
    alignas(16) glm::mat4 modelMatrix;
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;
    alignas(16) glm::vec4 sourceParams;
};

}
