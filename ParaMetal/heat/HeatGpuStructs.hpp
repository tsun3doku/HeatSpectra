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

struct FixedContribution {
    uint32_t boundaryValueIndex = 0;
    float coefficient = 0.0f;
};

struct FixedRow {
    uint32_t contributionOffset = 0;
    uint32_t contributionCount = 0;
};

static_assert(sizeof(BoundaryState) == 16, "BoundaryState must match GPU stride");
static_assert(sizeof(BoundaryNode) == 12, "BoundaryNode must match GPU stride");
static_assert(sizeof(BoundaryContribution) == 8, "BoundaryContribution must match GPU stride");
static_assert(sizeof(FixedContribution) == 8, "FixedContribution must match GPU stride");
static_assert(sizeof(FixedRow) == 8, "FixedRow must match GPU stride");

struct ContactCameraUbo {
    glm::mat4 view;
    glm::mat4 proj;
    glm::mat4 invView;
};
static_assert(sizeof(ContactCameraUbo) == 192);

struct ContactLevelSetPushConstant {
    glm::vec3 gridMin;
    float cellSize;
    glm::ivec3 gridDim;
    float range;
};
static_assert(sizeof(ContactLevelSetPushConstant) == 32);

struct ContactFace {
    glm::vec4 centroidArea{0.0f};  // xyz = contact centroid, w = face area
    glm::vec4 normalGap{0.0f};     // xyz = contact normal,   w = physical gap
    uint32_t channelA{0};          // runtime SDF channel A
    uint32_t channelB{0};          // runtime SDF channel B
    uint32_t pad0{0};
    uint32_t pad1{0};
};
static_assert(sizeof(ContactFace) == 48);
static_assert(offsetof(ContactFace, centroidArea) == 0);
static_assert(offsetof(ContactFace, normalGap) == 16);
static_assert(offsetof(ContactFace, channelA) == 32);
static_assert(offsetof(ContactFace, channelB) == 36);

struct ContactRegion {
    glm::vec4 centerExtentW{0.0f}; // xyz = region center, w = extentW 
    glm::vec4 normalExtentU{0.0f}; // xyz = region normal, w = extentU 
    glm::vec4 uAxisExtentV{0.0f};  // xyz = uAxis,         w = extentV 
    glm::vec4 vAxis{0.0f};         // xyz = vAxis,         w = 0.0
    uint32_t channelA{0};          // SDF channel A index
    uint32_t channelB{0};          // SDF channel B index
    uint32_t psiChannel{0};        // Precomputed Psi (sdfA - sdfB)
    uint32_t pad0{0};
};
static_assert(sizeof(ContactRegion) == 80);
static_assert(offsetof(ContactRegion, centerExtentW) == 0);
static_assert(offsetof(ContactRegion, normalExtentU) == 16);
static_assert(offsetof(ContactRegion, uAxisExtentV) == 32);
static_assert(offsetof(ContactRegion, vAxis) == 48);
static_assert(offsetof(ContactRegion, channelA) == 64);
static_assert(offsetof(ContactRegion, channelB) == 68);
static_assert(offsetof(ContactRegion, psiChannel) == 72);

}

