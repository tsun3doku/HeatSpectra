#pragma once

#include "RVDGeometry.cuh"

#include <cstdint>
#include <vector>

#include <cuda_runtime.h>
#include <glm/glm.hpp>

namespace voronoi::sdf {

bool buildUnsignedDistance(
    const cudaGeometry::RVDGeometry& geometry,
    const glm::vec3& sdfGridMin,
    const glm::ivec3& sdfGridDim,
    float nominalCellSize,
    std::vector<float>& outValues,
    cudaStream_t stream);

bool buildAndClassify(
    const cudaGeometry::RVDGeometry& geometry,
    const std::vector<glm::vec4>& seeds,
    const glm::vec3& sdfGridMin,
    const glm::ivec3& sdfGridDim,
    float nominalCellSize,
    std::vector<uint32_t>& seedFlags,
    cudaStream_t stream);

} // namespace voronoi::sdf
