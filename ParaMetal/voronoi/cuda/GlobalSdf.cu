#include "GlobalSdf.cuh"

#include "RVDGeometry.cuh"
#include "SDF.cuh"

#include "../VoronoiGpuStructs.hpp"
#include "../../spatial/VoxelGrid.hpp"

#include <cuda_runtime.h>

#include <cmath>
#include <cstdint>
#include <iostream>

namespace voronoi::globalsdf {

using cudaGeometry::DeviceTriangle;
using cudaGeometry::DeviceVoxelGrid;
using cudaGeometry::canonicalFromWorld;
using cudaGeometry::cornerIndex;
using cudaGeometry::voxelIndex;
using cudaGeometry::voxelSize;
template <class T>
using DeviceBuffer = cudaUtils::CudaBuffer<T>;
constexpr auto cudaOk = cudaUtils::cudaCheck;
constexpr uint32_t BlockSize = 256;

struct SDFGrid {
    float3 gridMin;
    float cellSize;
    int3 gridDim;
    uint32_t sampleCount;
};

__device__ uint32_t sampleIndex(const SDFGrid& grid, int x, int y, int z) {
    return uint32_t(x + y * grid.gridDim.x + z * grid.gridDim.x * grid.gridDim.y);
}

__device__ int clampInt(int value, int minimum, int maximum) {
    return max(minimum, min(value, maximum));
}

__device__ float3 toFloat3(float4 value) {
    return make_float3(value.x, value.y, value.z);
}

__device__ float3 subtract(float3 a, float3 b) {
    return make_float3(a.x - b.x, a.y - b.y, a.z - b.z);
}

__device__ float3 add(float3 a, float3 b) {
    return make_float3(a.x + b.x, a.y + b.y, a.z + b.z);
}

__device__ float3 multiply(float3 a, float value) {
    return make_float3(a.x * value, a.y * value, a.z * value);
}

__device__ float3 cross3(float3 a, float3 b) {
    return make_float3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}

__device__ float dot3(float3 a, float3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

__device__ float3 closestPointOnTriangle(float3 point, float3 a, float3 b, float3 c) {
    const float3 ab = subtract(b, a);
    const float3 ac = subtract(c, a);
    const float3 ap = subtract(point, a);
    const float d1 = dot3(ab, ap);
    const float d2 = dot3(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) return a;

    const float3 bp = subtract(point, b);
    const float d3 = dot3(ab, bp);
    const float d4 = dot3(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) return b;

    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
        return add(a, multiply(ab, d1 / (d1 - d3)));

    const float3 cp = subtract(point, c);
    const float d5 = dot3(ab, cp);
    const float d6 = dot3(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) return c;

    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
        return add(a, multiply(ac, d2 / (d2 - d6)));

    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && d4 - d3 >= 0.0f && d5 - d6 >= 0.0f)
        return add(b, multiply(subtract(c, b), (d4 - d3) / ((d4 - d3) + (d5 - d6))));

    const float denominator = 1.0f / (va + vb + vc);
    return add(a, add(multiply(ab, vb * denominator), multiply(ac, vc * denominator)));
}

__device__ bool signFromBorderSample(
    float3 point,
    DeviceVoxelGrid voxelGrid,
    const int32_t* offsets,
    const int32_t* triangleIds,
    const DeviceTriangle* triangles) {
    const float canonicalVoxelSize = voxelSize(voxelGrid);
    const float3 canonical = canonicalFromWorld(voxelGrid, point);
    const int vx = clampInt(int(canonical.x / canonicalVoxelSize), 0, voxelGrid.gridDim.x - 1);
    const int vy = clampInt(int(canonical.y / canonicalVoxelSize), 0, voxelGrid.gridDim.y - 1);
    const int vz = clampInt(int(canonical.z / canonicalVoxelSize), 0, voxelGrid.gridDim.z - 1);
    const int voxel = voxelIndex(voxelGrid, vx, vy, vz);
    for (int32_t item = offsets[voxel]; item < offsets[voxel + 1]; ++item) {
        const DeviceTriangle triangle = triangles[triangleIds[item]];
        const float3 a = toFloat3(triangle.v0);
        const float3 b = toFloat3(triangle.v1);
        const float3 c = toFloat3(triangle.v2);
        const float3 closest = closestPointOnTriangle(point, a, b, c);
        const float3 normal = cross3(subtract(b, a), subtract(c, a));
        if (dot3(normal, normal) <= 1e-30f) continue;
        if (dot3(subtract(point, closest), normal) >= 0.0f) return false;
        return true;
    }
    return false;
}

__global__ void signSamplesKernel(
    const float* unsignedValues,
    float* signedValues,
    SDFGrid grid,
    const DeviceTriangle* triangles,
    const int32_t* offsets,
    const int32_t* triangleIds,
    const uint8_t* occupancy,
    DeviceVoxelGrid voxelGrid) {
    const uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= grid.sampleCount) return;
    const int x = int(index % uint32_t(grid.gridDim.x));
    const int y = int((index / uint32_t(grid.gridDim.x)) % uint32_t(grid.gridDim.y));
    const int z = int(index / uint32_t(grid.gridDim.x * grid.gridDim.y));
    const float3 point = make_float3(grid.gridMin.x + x * grid.cellSize,
                                     grid.gridMin.y + y * grid.cellSize,
                                     grid.gridMin.z + z * grid.cellSize);

    const float canonicalVoxelSize = voxelSize(voxelGrid);
    const float3 canonical = canonicalFromWorld(voxelGrid, point);
    const int vx = clampInt(int(canonical.x / canonicalVoxelSize), 0, voxelGrid.gridDim.x);
    const int vy = clampInt(int(canonical.y / canonicalVoxelSize), 0, voxelGrid.gridDim.y);
    const int vz = clampInt(int(canonical.z / canonicalVoxelSize), 0, voxelGrid.gridDim.z);
    const uint8_t value = occupancy[cornerIndex(voxelGrid, vx, vy, vz)];

    const float distance = unsignedValues[index];
    float signedValue = distance;
    if (value == 2u) {
        signedValue = -distance;
    } else if (value == 1u) {
        if (signFromBorderSample(point, voxelGrid, offsets, triangleIds, triangles)) {
            signedValue = -distance;
        }
    }
    signedValues[index] = signedValue;
}

bool build(const BuildInput& input, BuildResult& result, cudaStream_t stream) {
    result = {};
    if (!input.instances || input.instances->empty() ||
        !(input.cellSize > 0.0f) || !std::isfinite(input.cellSize) ||
        input.gridDim.x < 2 || input.gridDim.y < 2 || input.gridDim.z < 2) return false;

    const size_t sampleCount64 = size_t(input.gridDim.x) * input.gridDim.y * input.gridDim.z;
    if (sampleCount64 == 0 || sampleCount64 > UINT32_MAX) return false;
    const size_t channelCount = input.instances->size();
    if (channelCount > SIZE_MAX / sampleCount64) return false;

    const size_t totalBytes = channelCount * sampleCount64 * sizeof(float);
    size_t freeBytes = 0;
    size_t totalBytesAvailable = 0;
    if (!cudaOk(cudaMemGetInfo(&freeBytes, &totalBytesAvailable), "query GlobalSdf memory") ||
        freeBytes < totalBytes) {
        std::cerr << "[GlobalSdf] allocation preflight failed dims=("
                  << input.gridDim.x << "," << input.gridDim.y << "," << input.gridDim.z
                  << ") channels=" << channelCount
                  << " requiredMiB=" << (double(totalBytes) / (1024.0 * 1024.0))
                  << " freeMiB=" << (double(freeBytes) / (1024.0 * 1024.0)) << std::endl;
        return false;
    }

    const SDFGrid grid{make_float3(input.gridMin.x, input.gridMin.y, input.gridMin.z),
                       input.cellSize,
                       make_int3(input.gridDim.x, input.gridDim.y, input.gridDim.z),
                       uint32_t(sampleCount64)};

    result.values.resize(channelCount * sampleCount64);
    result.channelStride = uint32_t(sampleCount64);
    result.channelCount = uint32_t(channelCount);

    DeviceBuffer<float> unsignedValues;
    DeviceBuffer<float> signedValues;
    if (!signedValues.allocate(sampleCount64)) return false;
    const uint32_t gridBlocks = (grid.sampleCount + BlockSize - 1) / BlockSize;

    for (uint32_t channel = 0; channel < uint32_t(channelCount); ++channel) {
        const Instance& instance = (*input.instances)[channel];
        if (!instance.voxelGrid || !instance.triangles || instance.triangles->empty()) {
            return false;
        }

        cudaGeometry::RVDGeometry geometry;
        if (!uploadGeometry(*instance.triangles, *instance.voxelGrid, geometry, stream)) {
            std::cerr << "[GlobalSdf] geometry upload failed channel=" << channel << std::endl;
            return false;
        }

        std::vector<float> distanceValues;
        if (!sdf::buildUnsignedDistance(geometry, input.gridMin, input.gridDim,
                                        input.cellSize, distanceValues, stream)) {
            std::cerr << "[GlobalSdf] unsigned distance failed channel=" << channel << std::endl;
            geometry.reset();
            return false;
        }
        if (!unsignedValues.allocate(distanceValues.size()) ||
            !unsignedValues.uploadAsync(distanceValues.data(), distanceValues.size(), stream)) {
            geometry.reset();
            return false;
        }
        signSamplesKernel<<<gridBlocks, BlockSize, 0, stream>>>(
            unsignedValues.get(), signedValues.get(), grid,
            geometry.triangles.get(), geometry.voxelOffsets.get(),
            geometry.voxelTriangleIds.get(), geometry.occupancy.get(), geometry.voxelGrid);
        if (!cudaOk(cudaGetLastError(), "launch GlobalSdf sign pass")) {
            geometry.reset();
            return false;
        }
        float* destination = result.values.data() + size_t(channel) * sampleCount64;
        if (!signedValues.downloadAsync(destination, sampleCount64, stream)) {
            geometry.reset();
            return false;
        }
        unsignedValues.reset();
        geometry.reset();
    }
    if (!cudaOk(cudaStreamSynchronize(stream), "synchronize GlobalSdf readback")) return false;
    return true;
}

} // namespace voronoi::globalsdf