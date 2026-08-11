#pragma once

#include <cuda_runtime.h>

#include <cstdint>

namespace voronoi {

constexpr uint32_t KNNBlockSize = 32;
constexpr uint32_t InvalidNeighborId = 0xffffffffu;

struct DeviceGrid {
    float3 gridMin;
    float invCellWidth;
    int3 gridDim;
    int dimXY;
    uint32_t totalCells;
};

struct alignas(16) BucketEntry {
    float4 position;
    uint32_t id;
    uint32_t padding[3];
};

struct DeviceKnn {
    const float4* seeds;
    uint32_t seedCount;
    const uint32_t* cellStart;
    const BucketEntry* bucketEntries;
    const int3* cellOffsets;
    const double* cellOffsetDistances;
    uint32_t cellOffsetCount;
    DeviceGrid grid;
};

__host__ __device__ inline int clampCellCoordinate(int value, int dimension) {
    return value < 0 ? 0 : (value >= dimension ? dimension - 1 : value);
}

__host__ __device__ inline int3 cellIndexOf(float3 position, const DeviceGrid& grid) {
    const double inverseCellWidth = double(grid.invCellWidth);
    const int x = position.x <= grid.gridMin.x
        ? 0 : int((double(position.x) - double(grid.gridMin.x)) * inverseCellWidth);
    const int y = position.y <= grid.gridMin.y
        ? 0 : int((double(position.y) - double(grid.gridMin.y)) * inverseCellWidth);
    const int z = position.z <= grid.gridMin.z
        ? 0 : int((double(position.z) - double(grid.gridMin.z)) * inverseCellWidth);
    return make_int3(clampCellCoordinate(x, grid.gridDim.x),
                     clampCellCoordinate(y, grid.gridDim.y),
                     clampCellCoordinate(z, grid.gridDim.z));
}

__host__ __device__ inline uint32_t linearCell(int x, int y, int z,
                                                const DeviceGrid& grid) {
    return uint32_t(z) * uint32_t(grid.dimXY) +
           uint32_t(y) * uint32_t(grid.gridDim.x) + uint32_t(x);
}

__global__ void knnKernel(DeviceKnn knn, uint32_t* neighbors, double* distanceHeap,
                          uint32_t k, uint32_t* searchFailure,
                          const uint32_t* deviceQueryIds, uint32_t queryCount,
                          uint32_t outputBase,
                          uint32_t outputStride, bool scatterBySeedId);

__global__ void gatherPrefixKernel(const uint32_t* neighbors, uint32_t seedCount,
                                   uint32_t neighborStride, uint32_t maxNeighbors,
                                   uint32_t* out);

} // namespace voronoi
