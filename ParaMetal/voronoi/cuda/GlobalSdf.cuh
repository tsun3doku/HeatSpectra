#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <cuda_runtime.h>
#include <glm/glm.hpp>

class VoxelGrid;

namespace voronoi::globalsdf {

struct Instance {
    const VoxelGrid* voxelGrid = nullptr;
    const std::vector<std::array<glm::vec4, 3>>* triangles = nullptr;
};

struct BuildInput {
    const std::vector<Instance>* instances = nullptr;
    glm::vec3 gridMin{};
    glm::ivec3 gridDim{};
    float cellSize = 0.0f;
};

struct BuildResult {
    std::vector<float> values;
    uint32_t channelStride = 0;
    uint32_t channelCount = 0;
};

bool build(const BuildInput& input, BuildResult& result, cudaStream_t stream);

} // namespace voronoi::globalsdf