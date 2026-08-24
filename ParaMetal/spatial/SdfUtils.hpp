#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace spatial {

inline float sampleTrilinearSdf(
    const glm::vec3& gridMin,
    const glm::ivec3& gridDim,
    float cellSize,
    const std::vector<float>& sdfValues,
    uint32_t channel,
    const glm::vec3& p) {
    if (gridDim.x < 2 || gridDim.y < 2 || gridDim.z < 2 || cellSize <= 0.0f || sdfValues.empty()) {
        return 1.0f;
    }

    const glm::vec3 gridCoord = (p - gridMin) / cellSize;
    const int x0 = std::clamp(static_cast<int>(std::floor(gridCoord.x)), 0, gridDim.x - 2);
    const int y0 = std::clamp(static_cast<int>(std::floor(gridCoord.y)), 0, gridDim.y - 2);
    const int z0 = std::clamp(static_cast<int>(std::floor(gridCoord.z)), 0, gridDim.z - 2);
    const int x1 = x0 + 1;
    const int y1 = y0 + 1;
    const int z1 = z0 + 1;

    const float fx = std::clamp(gridCoord.x - static_cast<float>(x0), 0.0f, 1.0f);
    const float fy = std::clamp(gridCoord.y - static_cast<float>(y0), 0.0f, 1.0f);
    const float fz = std::clamp(gridCoord.z - static_cast<float>(z0), 0.0f, 1.0f);

    const size_t channelStride = static_cast<size_t>(gridDim.x) * gridDim.y * gridDim.z;
    const size_t channelOffset = static_cast<size_t>(channel) * channelStride;
    if (channelOffset + channelStride > sdfValues.size()) {
        return 1.0f;
    }

    const auto idx = [&](int x, int y, int z) -> size_t {
        return channelOffset + static_cast<size_t>(x + y * gridDim.x + z * gridDim.x * gridDim.y);
    };

    const float c000 = sdfValues[idx(x0, y0, z0)];
    const float c100 = sdfValues[idx(x1, y0, z0)];
    const float c010 = sdfValues[idx(x0, y1, z0)];
    const float c110 = sdfValues[idx(x1, y1, z0)];
    const float c001 = sdfValues[idx(x0, y0, z1)];
    const float c101 = sdfValues[idx(x1, y0, z1)];
    const float c011 = sdfValues[idx(x0, y1, z1)];
    const float c111 = sdfValues[idx(x1, y1, z1)];

    const float c00 = c000 * (1.0f - fx) + c100 * fx;
    const float c10 = c010 * (1.0f - fx) + c110 * fx;
    const float c01 = c001 * (1.0f - fx) + c101 * fx;
    const float c11 = c011 * (1.0f - fx) + c111 * fx;

    const float c0 = c00 * (1.0f - fy) + c10 * fy;
    const float c1 = c01 * (1.0f - fy) + c11 * fy;

    return c0 * (1.0f - fz) + c1 * fz;
}

inline bool segmentStaysInside(
    const glm::vec3& gridMin,
    const glm::ivec3& gridDim,
    float cellSize,
    const std::vector<float>& sdfValues,
    uint32_t channel,
    const glm::vec3& surfacePoint,
    const glm::vec3& nodePoint,
    uint32_t sampleCount = 8) {
    const float maxTolerance = cellSize * 0.5f;

    for (uint32_t i = 1; i < sampleCount; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(sampleCount);
        const glm::vec3 p = glm::mix(surfacePoint, nodePoint, t);
        const float sdf = sampleTrilinearSdf(gridMin, gridDim, cellSize, sdfValues, channel, p);
        if (sdf > maxTolerance) {
            return false;
        }
    }
    return true;
}

} // namespace spatial
