#pragma once

#include "../../cuda/CudaBuffer.cuh"

#include <cuda_runtime.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

class VoxelGrid;

namespace voronoi::cudaGeometry {

inline constexpr float CanonicalDomainSize = 1000.0f;

struct DeviceTriangle {
    float4 v0;
    float4 v1;
    float4 v2;
};

struct DeviceVoxelGrid {
    float3 gridMin;
    float scale;
    int3 gridDim;
    uint32_t totalCells;
};

struct RVDGeometry {
    cudaUtils::CudaBuffer<DeviceTriangle> triangles;
    cudaUtils::CudaBuffer<uint8_t> occupancy;
    cudaUtils::CudaBuffer<int32_t> voxelTriangleIds;
    cudaUtils::CudaBuffer<int32_t> voxelOffsets;
    DeviceVoxelGrid voxelGrid{};
    uint32_t triangleCount = 0;

    void reset() {
        triangles.reset();
        occupancy.reset();
        voxelTriangleIds.reset();
        voxelOffsets.reset();
        voxelGrid = {};
        triangleCount = 0;
    }

    bool valid() const {
        return triangleCount != 0 && triangles.size() == triangleCount &&
               occupancy.size() != 0 && voxelOffsets.size() == size_t(voxelGrid.totalCells) + 1 &&
               voxelGrid.gridDim.x > 0 && voxelGrid.gridDim.y > 0 && voxelGrid.gridDim.z > 0 &&
               voxelGrid.scale > 0.0f;
    }
};

bool uploadGeometry(
    const std::vector<std::array<glm::vec4, 3>>& triangles,
    const VoxelGrid& voxelGrid,
    RVDGeometry& geometry,
    cudaStream_t stream);

__device__ inline int cornerIndex(const DeviceVoxelGrid& grid, int x, int y, int z) {
    const int stride = grid.gridDim.x + 1;
    return z * stride * stride + y * stride + x;
}

__device__ inline int voxelIndex(const DeviceVoxelGrid& grid, int x, int y, int z) {
    return x + y * grid.gridDim.x + z * grid.gridDim.x * grid.gridDim.y;
}

__device__ inline float3 canonicalFromWorld(const DeviceVoxelGrid& grid, float3 point) {
    return make_float3((point.x - grid.gridMin.x) * grid.scale,
                       (point.y - grid.gridMin.y) * grid.scale,
                       (point.z - grid.gridMin.z) * grid.scale);
}

__device__ inline float voxelSize(const DeviceVoxelGrid& grid) {
    return CanonicalDomainSize / float(grid.gridDim.x);
}

} // namespace voronoi::cudaGeometry
