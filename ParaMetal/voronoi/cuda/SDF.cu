#include "SDF.cuh"

#include "../VoronoiGpuStructs.hpp"

#include <cub/cub.cuh>

#include <cmath>
#include <cfloat>

namespace voronoi::sdf {

using cudaGeometry::DeviceTriangle;
using cudaGeometry::DeviceVoxelGrid;
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

struct TriangleCellRange {
    int3 first;
    int3 last;
};

__device__ float3 add(float3 a, float3 b) {
    return make_float3(a.x + b.x, a.y + b.y, a.z + b.z);
}

__device__ float3 subtract(float3 a, float3 b) {
    return make_float3(a.x - b.x, a.y - b.y, a.z - b.z);
}

__device__ float3 multiply(float3 a, float value) {
    return make_float3(a.x * value, a.y * value, a.z * value);
}

__device__ float dot3(float3 a, float3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

__device__ float3 toFloat3(float4 value) {
    return make_float3(value.x, value.y, value.z);
}

__device__ int clampInt(int value, int minimum, int maximum) {
    return max(minimum, min(value, maximum));
}

__device__ uint32_t sampleIndex(const SDFGrid& grid, int x, int y, int z) {
    return uint32_t(x + y * grid.gridDim.x +
                    z * grid.gridDim.x * grid.gridDim.y);
}

__device__ int worldToSample(float coordinate, float minimum, float cellSize, int dimension) {
    return clampInt(static_cast<int>((coordinate - minimum) / cellSize), 0, dimension - 1);
}

__device__ TriangleCellRange triangleCellRange(
    DeviceTriangle triangle, const SDFGrid& grid) {
    const float3 a = toFloat3(triangle.v0);
    const float3 b = toFloat3(triangle.v1);
    const float3 c = toFloat3(triangle.v2);
    const float3 minimum = make_float3(fminf(a.x, fminf(b.x, c.x)),
                                       fminf(a.y, fminf(b.y, c.y)),
                                       fminf(a.z, fminf(b.z, c.z)));
    const float3 maximum = make_float3(fmaxf(a.x, fmaxf(b.x, c.x)),
                                       fmaxf(a.y, fmaxf(b.y, c.y)),
                                       fmaxf(a.z, fmaxf(b.z, c.z)));
    return {
        make_int3(worldToSample(minimum.x, grid.gridMin.x, grid.cellSize, grid.gridDim.x),
                  worldToSample(minimum.y, grid.gridMin.y, grid.cellSize, grid.gridDim.y),
                  worldToSample(minimum.z, grid.gridMin.z, grid.cellSize, grid.gridDim.z)),
        make_int3(worldToSample(maximum.x, grid.gridMin.x, grid.cellSize, grid.gridDim.x),
                  worldToSample(maximum.y, grid.gridMin.y, grid.cellSize, grid.gridDim.y),
                  worldToSample(maximum.z, grid.gridMin.z, grid.cellSize, grid.gridDim.z))};
}

__global__ void countTriangleCoverageKernel(
    const DeviceTriangle* triangles,
    uint32_t triangleCount,
    SDFGrid grid,
    uint32_t* counts) {
    const uint32_t triangleId = blockIdx.x * blockDim.x + threadIdx.x;
    if (triangleId >= triangleCount) return;

    const TriangleCellRange range = triangleCellRange(triangles[triangleId], grid);

    for (int z = range.first.z; z <= range.last.z; ++z)
        for (int y = range.first.y; y <= range.last.y; ++y)
            for (int x = range.first.x; x <= range.last.x; ++x)
                atomicAdd(counts + sampleIndex(grid, x, y, z), 1u);
}

__global__ void fillTriangleCoverageKernel(
    const DeviceTriangle* triangles,
    uint32_t triangleCount,
    SDFGrid grid,
    uint32_t* cursors,
    int32_t* triangleIds) {
    const uint32_t triangleId = blockIdx.x * blockDim.x + threadIdx.x;
    if (triangleId >= triangleCount) return;

    const DeviceTriangle triangle = triangles[triangleId];
    const TriangleCellRange range = triangleCellRange(triangle, grid);

    for (int z = range.first.z; z <= range.last.z; ++z) {
        for (int y = range.first.y; y <= range.last.y; ++y) {
            for (int x = range.first.x; x <= range.last.x; ++x) {
                const uint32_t destination = atomicAdd(cursors + sampleIndex(grid, x, y, z), 1u);
                triangleIds[destination] = static_cast<int32_t>(triangleId);
            }
        }
    }
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

__device__ bool accumulateRadius(
    float3 point,
    int3 center,
    int radius,
    SDFGrid grid,
    const uint32_t* offsets,
    const int32_t* triangleIds,
    const DeviceTriangle* triangles,
    float& minimumDistanceSquared) {
    bool found = false;
    for (int z = max(0, center.z - radius); z <= min(grid.gridDim.z - 1, center.z + radius); ++z) {
        for (int y = max(0, center.y - radius); y <= min(grid.gridDim.y - 1, center.y + radius); ++y) {
            for (int x = max(0, center.x - radius); x <= min(grid.gridDim.x - 1, center.x + radius); ++x) {
                const uint32_t sample = sampleIndex(grid, x, y, z);
                for (uint32_t item = offsets[sample]; item < offsets[sample + 1]; ++item) {
                    const DeviceTriangle triangle = triangles[triangleIds[item]];
                    const float3 closest = closestPointOnTriangle(
                        point, toFloat3(triangle.v0), toFloat3(triangle.v1), toFloat3(triangle.v2));
                    const float3 delta = subtract(point, closest);
                    minimumDistanceSquared = fminf(minimumDistanceSquared, dot3(delta, delta));
                    found = true;
                }
            }
        }
    }
    return found;
}

__global__ void generateSDFKernel(
    SDFGrid grid,
    const DeviceTriangle* triangles,
    const uint32_t* offsets,
    const int32_t* triangleIds,
    float* sdf) {
    const uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= grid.sampleCount) return;
    const int x = int(index % uint32_t(grid.gridDim.x));
    const int y = int((index / uint32_t(grid.gridDim.x)) % uint32_t(grid.gridDim.y));
    const int z = int(index / uint32_t(grid.gridDim.x * grid.gridDim.y));
    const float3 point = make_float3(grid.gridMin.x + x * grid.cellSize,
                                     grid.gridMin.y + y * grid.cellSize,
                                     grid.gridMin.z + z * grid.cellSize);
    const int3 center = make_int3(x, y, z);
    float minimumDistanceSquared = FLT_MAX;
    bool found = accumulateRadius(point, center, 1, grid, offsets, triangleIds,
                                  triangles, minimumDistanceSquared);
    if (!found)
        found = accumulateRadius(point, center, 4, grid, offsets, triangleIds,
                                 triangles, minimumDistanceSquared);
    if (!found) {
        sdf[index] = sqrtf(FLT_MAX);
        return;
    }
    sdf[index] = sqrtf(minimumDistanceSquared);
}

__device__ float sampleSDF(const float* values, const SDFGrid& grid, float3 position) {
    float gx = fminf(fmaxf((position.x - grid.gridMin.x) / grid.cellSize, 0.0f),
                     float(grid.gridDim.x - 1));
    float gy = fminf(fmaxf((position.y - grid.gridMin.y) / grid.cellSize, 0.0f),
                     float(grid.gridDim.y - 1));
    float gz = fminf(fmaxf((position.z - grid.gridMin.z) / grid.cellSize, 0.0f),
                     float(grid.gridDim.z - 1));
    const int x0 = int(floorf(gx)); const int x1 = min(x0 + 1, grid.gridDim.x - 1);
    const int y0 = int(floorf(gy)); const int y1 = min(y0 + 1, grid.gridDim.y - 1);
    const int z0 = int(floorf(gz)); const int z1 = min(z0 + 1, grid.gridDim.z - 1);
    gx -= x0; gy -= y0; gz -= z0;
    const float c00 = values[sampleIndex(grid, x0, y0, z0)] * (1.0f - gx) +
                      values[sampleIndex(grid, x1, y0, z0)] * gx;
    const float c10 = values[sampleIndex(grid, x0, y1, z0)] * (1.0f - gx) +
                      values[sampleIndex(grid, x1, y1, z0)] * gx;
    const float c01 = values[sampleIndex(grid, x0, y0, z1)] * (1.0f - gx) +
                      values[sampleIndex(grid, x1, y0, z1)] * gx;
    const float c11 = values[sampleIndex(grid, x0, y1, z1)] * (1.0f - gx) +
                      values[sampleIndex(grid, x1, y1, z1)] * gx;
    const float c0 = c00 * (1.0f - gy) + c10 * gy;
    const float c1 = c01 * (1.0f - gy) + c11 * gy;
    return c0 * (1.0f - gz) + c1 * gz;
}

__global__ void classifyGhostsKernel(
    const float4* seeds,
    uint32_t* flags,
    uint32_t seedCount,
    const uint8_t* occupancy,
    DeviceVoxelGrid voxelGrid,
    const float* sdf,
    SDFGrid sdfGrid,
    float maximumSurfaceDistance) {
    const uint32_t seedId = blockIdx.x * blockDim.x + threadIdx.x;
    if (seedId >= seedCount) return;
    const float4 seed = seeds[seedId];

    const float canonicalVoxelSize = cudaGeometry::voxelSize(voxelGrid);
    const float3 canonical = cudaGeometry::canonicalFromWorld(
        voxelGrid, make_float3(seed.x, seed.y, seed.z));
    const int vx = clampInt(int(canonical.x / canonicalVoxelSize), 0, voxelGrid.gridDim.x);
    const int vy = clampInt(int(canonical.y / canonicalVoxelSize), 0, voxelGrid.gridDim.y);
    const int vz = clampInt(int(canonical.z / canonicalVoxelSize), 0, voxelGrid.gridDim.z);
    const uint32_t occupancyIndex = uint32_t(cudaGeometry::cornerIndex(voxelGrid, vx, vy, vz));
    const uint8_t value = occupancy[occupancyIndex];
    const bool inside = value == 1u || value == 2u;

    const float surfaceDistance = sampleSDF(
        sdf, sdfGrid, make_float3(seed.x, seed.y, seed.z));
    const bool shouldGhost = !inside && surfaceDistance > maximumSurfaceDistance;
    flags[seedId] = shouldGhost ? NodeFlags::Ghost : 0u;
}

bool buildAndClassify(
    const cudaGeometry::RVDGeometry& geometry,
    const std::vector<glm::vec4>& seeds,
    const glm::vec3& sdfGridMin,
    const glm::ivec3& sdfGridDim,
    float nominalCellSize,
    std::vector<uint32_t>& seedFlags,
    cudaStream_t stream) {
    if (!geometry.valid() || seeds.empty() ||
        seeds.size() != seedFlags.size() || seeds.size() > size_t(UINT32_MAX) ||
        !(nominalCellSize > 0.0f) || !std::isfinite(nominalCellSize) ||
        sdfGridDim.x < 2 || sdfGridDim.y < 2 || sdfGridDim.z < 2) return false;

    const size_t sampleCount64 = size_t(sdfGridDim.x) * sdfGridDim.y * sdfGridDim.z;
    if (sampleCount64 == 0 || sampleCount64 > UINT32_MAX) return false;
    const SDFGrid grid{make_float3(sdfGridMin.x, sdfGridMin.y, sdfGridMin.z), nominalCellSize,
                       make_int3(sdfGridDim.x, sdfGridDim.y, sdfGridDim.z), uint32_t(sampleCount64)};

    DeviceBuffer<uint32_t> counts;
    DeviceBuffer<uint32_t> offsets;
    DeviceBuffer<uint32_t> cursors;
    DeviceBuffer<uint8_t> scanStorage;
    if (!counts.allocate(size_t(grid.sampleCount) + 1) ||
        !offsets.allocate(size_t(grid.sampleCount) + 1) ||
        !cursors.allocate(grid.sampleCount) ||
        !cudaOk(cudaMemsetAsync(counts.get(), 0,
            (size_t(grid.sampleCount) + 1) * sizeof(uint32_t), stream),
                "clear SDF triangle counts")) return false;

    size_t scanBytes = 0;
    const uint32_t triangleBlocks = (geometry.triangleCount + BlockSize - 1) / BlockSize;
    countTriangleCoverageKernel<<<triangleBlocks, BlockSize, 0, stream>>>(
        geometry.triangles.get(), geometry.triangleCount, grid, counts.get());
    if (!cudaOk(cudaGetLastError(), "launch SDF coverage count") ||
        !cudaOk(cub::DeviceScan::ExclusiveSum(
            nullptr, scanBytes, counts.get(), offsets.get(), grid.sampleCount + 1, stream),
            "query SDF scan storage") ||
        !scanStorage.allocate(scanBytes)) return false;
    if (!cudaOk(cub::DeviceScan::ExclusiveSum(
            scanStorage.get(), scanBytes, counts.get(), offsets.get(), grid.sampleCount + 1, stream),
            "scan SDF triangle counts") ||
        !cudaOk(cudaGetLastError(), "launch SDF triangle scan")) return false;

    uint32_t triangleIdCount = 0;
    if (!cudaOk(cudaMemcpyAsync(&triangleIdCount, offsets.get() + grid.sampleCount,
                                sizeof(uint32_t), cudaMemcpyDeviceToHost, stream),
                "read SDF triangle ID count") ||
        !cudaOk(cudaStreamSynchronize(stream), "synchronize SDF index scan")) return false;
    DeviceBuffer<int32_t> triangleIds;
    if (triangleIdCount == 0 || !triangleIds.allocate(triangleIdCount) ||
        !cudaOk(cudaMemcpyAsync(cursors.get(), offsets.get(),
                                size_t(grid.sampleCount) * sizeof(uint32_t),
                                cudaMemcpyDeviceToDevice, stream),
                "initialize SDF fill cursors")) return false;
    fillTriangleCoverageKernel<<<triangleBlocks, BlockSize, 0, stream>>>(
        geometry.triangles.get(), geometry.triangleCount, grid, cursors.get(), triangleIds.get());
    if (!cudaOk(cudaGetLastError(), "launch SDF coverage fill")) return false;

    DeviceBuffer<float> sdf;
    if (!sdf.allocate(grid.sampleCount)) return false;
    const uint32_t gridBlocks = (grid.sampleCount + BlockSize - 1) / BlockSize;
    generateSDFKernel<<<gridBlocks, BlockSize, 0, stream>>>(
        grid, geometry.triangles.get(), offsets.get(), triangleIds.get(), sdf.get());
    if (!cudaOk(cudaGetLastError(), "launch SDF generation")) return false;

    DeviceBuffer<float4> deviceSeeds;
    DeviceBuffer<uint32_t> deviceFlags;
    if (!deviceSeeds.allocate(seeds.size()) || !deviceFlags.allocate(seedFlags.size())) return false;
    if (!deviceSeeds.uploadAsync(reinterpret_cast<const float4*>(seeds.data()), seeds.size(), stream))
        return false;
    const uint32_t seedCount = uint32_t(seeds.size());
    const uint32_t seedBlocks = (seedCount + BlockSize - 1) / BlockSize;
    classifyGhostsKernel<<<seedBlocks, BlockSize, 0, stream>>>(
        deviceSeeds.get(), deviceFlags.get(), seedCount, geometry.occupancy.get(),
        geometry.voxelGrid, sdf.get(), grid, 1.5f * nominalCellSize);
    if (!cudaOk(cudaGetLastError(), "launch SDF classification")) return false;
    if (!deviceFlags.downloadAsync(seedFlags.data(), seedFlags.size(), stream)) return false;
    return cudaOk(cudaStreamSynchronize(stream), "synchronize SDF readback");
}

} // namespace voronoi::sdf
