#pragma once

#include <cstdint>

#include <glm/glm.hpp>

namespace heat {

struct TimeUniform {
    float deltaTime;
    float totalTime;
};

struct SurfacePoint {
    glm::vec3 position;
    float temperature;
    glm::vec3 normal;
    float area;
    glm::vec4 color;
};

struct SourcePushConstant {
    uint32_t maxNodeNeighbors;
    uint32_t substepIndex;
    float heatSourceTemperature;
    uint32_t hasContact;
};

struct BufferPushConstant {
    alignas(16) glm::mat4 modelMatrix;
    alignas(16) glm::vec4 sourceParams;
};

struct SourceRenderPushConstant {
    alignas(16) glm::mat4 modelMatrix;
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;
    alignas(16) glm::vec4 sourceParams;
};

}
