#include "RVD.hpp"
#include "RVDStatus.cuh"
#include "ConvexCell.cuh"
#include "SDF.cuh"
#include "KNNNeighbors.hpp"
#include "RVDWorkQueue.cuh"

#include "../../cuda/CudaBuffer.cuh"
#include "../../cuda/CudaVulkanDevice.hpp"
#include "../../spatial/VoxelGrid.hpp"
#include "../../vulkan/VulkanDevice.hpp"

#include <cuda_runtime.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

namespace voronoi {
static const char* rvdStatusName(RVDStatus status);
namespace {

using namespace rvdDevice;

constexpr uint32_t InvalidId = 0xffffffffu;
constexpr uint32_t ReferenceDiscardMask =
    rvdStatusBit(RVDStatus::RestrictedPending) |
    rvdStatusBit(RVDStatus::EmptyCell) |
    rvdStatusBit(RVDStatus::VertexOverflow) |
    rvdStatusBit(RVDStatus::PlaneOverflow) |
    rvdStatusBit(RVDStatus::TriangleOverflow) |
    rvdStatusBit(RVDStatus::SecurityRadiusNotReached) |
    rvdStatusBit(RVDStatus::NeedsRobustPredicate) |
    rvdStatusBit(RVDStatus::InconsistentBoundary) |
    rvdStatusBit(RVDStatus::FindAnotherBeginningVertex);

using cudaGeometry::DeviceTriangle;
using cudaGeometry::DeviceVoxelGrid;
using cudaGeometry::RVDGeometry;
using cudaGeometry::canonicalFromWorld;
using cudaGeometry::cornerIndex;
using cudaGeometry::voxelIndex;

struct DeviceInput {
    const float4* seeds;
    const uint32_t* seedFlags;
    const uint32_t* neighborIds;
    uint32_t neighborStride;
    uint32_t seedCount;
    const DeviceTriangle* triangles;
    uint32_t triangleCount;
    const uint8_t* occupancy;
    uint32_t occupancyCount;
    const int32_t* triangleList;
    uint32_t triangleListCount;
    const int32_t* offsets;
    uint32_t offsetCount;
    DeviceVoxelGrid grid;
    float4 domainPlanes[6];
    float nominalCellSize;
};

struct DeviceOutput {
    Node* nodes;
    uint32_t* nodeFlags;
    float* surfacePatchAreas;
    float* interfaceAreas;
    uint32_t* interfaceNeighborIds;
    RVDStatus* statuses;
};

template <class T>
using DeviceBuffer = cudaUtils::CudaBuffer<T>;
constexpr auto cudaOk = cudaUtils::cudaCheck;

template <bool RobustPredicate, bool FilterPredicate>
__device__ bool buildUnrestrictedCell(const DeviceInput& input, uint32_t cellId,
                                      uint32_t neighborRow,
                                      ConvexCell<RobustPredicate, FilterPredicate>& cell,
                                      uint32_t candidateLimit) {
    const float3 seed = canonicalFromWorld(
        input.grid, make_float3(input.seeds[cellId].x, input.seeds[cellId].y, input.seeds[cellId].z));
    if (!(input.grid.scale > 0.0f)) return false;
    cell.initialize(input.domainPlanes);

    bool exhaustedCandidates = false;
    bool securityReached = false;
    for (int candidate = 0; candidate < int(candidateLimit) && candidate < int(input.neighborStride); ++candidate) {
        const uint32_t neighbor = input.neighborIds[size_t(neighborRow) * input.neighborStride + candidate];
        if (neighbor == InvalidId) {
            exhaustedCandidates = true;
            break;
        }
        if (neighbor >= input.seedCount || neighbor == cellId) {
            cell.fail(RVDStatus::InvalidIndex);
            return false;
        }
        const float4 neighborPosition = input.seeds[neighbor];
        const float3 neighborPoint = canonicalFromWorld(
            input.grid, make_float3(neighborPosition.x, neighborPosition.y, neighborPosition.z));
        const float3 direction = sub(seed, neighborPoint);
        const float directionSquared = lengthSquared(direction);
        if (!isfinite(directionSquared)) {
            cell.fail(RVDStatus::NonFiniteGeometry);
            return false;
        }
        if (directionSquared <= 1e-20f) continue;
        const float directionLength = sqrtf(directionSquared);
        const float3 averageTimesTwo = add(seed, neighborPoint);
        // Positions are clipped in canonical coordinates. Power weights are
        // squared-distance terms, so they must be transformed by scale^2 as
        // well. Homogeneous Voronoi seeds all have the same w and are
        // unchanged by this correction.
        const float canonicalWeightScale = input.grid.scale * input.grid.scale;
        const float weightedDot = dot3(averageTimesTwo, direction) -
                                  (neighborPosition.w - input.seeds[cellId].w) *
                                      canonicalWeightScale;
        const float3 normal = mul(direction, 1.0f / directionLength);
        const float4 plane = make_float4(normal.x, normal.y, normal.z,
                                         -weightedDot / (2.0f * directionLength));
        if (uint32_t(cell.planeCount) >= cell.planeCapacity && !cell.compactPlanes()) return false;
        if (!cell.addPlane(plane, int(neighbor))) return false;
        if (cell.status != RVDStatus::Success) return false;
        // Match CCIsSecurityRadiusReached literally. Recompute from the
        // current cell after every cut instead of relying on cached state.
        const float fourMaximumVertexDistanceSquared =
            4.0f * cell.maximumVertexDistanceSquared(seed);
        if (directionSquared > fourMaximumVertexDistanceSquared) {
            securityReached = true;
            break;
        }
    }
    if (!securityReached && !exhaustedCandidates && input.seedCount > candidateLimit + 1u) {
        cell.fail(RVDStatus::SecurityRadiusNotReached);
        return false;
    }
    return cell.status == RVDStatus::Success;
}

template <bool RobustPredicate, bool FilterPredicate>
__device__ bool cellBounds(ConvexCell<RobustPredicate, FilterPredicate>& cell,
                           float3& minimum, float3& maximum) {
    minimum = make_float3(FLT_MAX, FLT_MAX, FLT_MAX);
    maximum = make_float3(-FLT_MAX, -FLT_MAX, -FLT_MAX);
    for (int vertex = 0; vertex < cell.vertexCount && cell.status == RVDStatus::Success; ++vertex) {
        const float3 point = cell.vertexPosition(cell.vertices[vertex]);
        minimum = min3(minimum, point);
        maximum = max3(maximum, point);
    }
    return cell.status == RVDStatus::Success && finite3(minimum) && finite3(maximum);
}

__device__ void voxelBounds(const DeviceInput& input, float3 minimum, float3 maximum,
                            int3& voxelMinimum, int3& voxelMaximum) {
    const float voxelSize = cudaGeometry::voxelSize(input.grid);
    const float3 canonicalMinimum = minimum;
    const float3 canonicalMaximum = maximum;
    voxelMinimum = make_int3(max(0, int(floorf(canonicalMinimum.x / voxelSize))),
                             max(0, int(floorf(canonicalMinimum.y / voxelSize))),
                             max(0, int(floorf(canonicalMinimum.z / voxelSize))));
    voxelMaximum = make_int3(min(input.grid.gridDim.x - 1, int(floorf(canonicalMaximum.x / voxelSize))),
                             min(input.grid.gridDim.y - 1, int(floorf(canonicalMaximum.y / voxelSize))),
                             min(input.grid.gridDim.z - 1, int(floorf(canonicalMaximum.z / voxelSize))));
}

__device__ bool emptyVoxelSpan(int3 voxelMinimum, int3 voxelMaximum) {
    return voxelMinimum.x > voxelMaximum.x || voxelMinimum.y > voxelMaximum.y ||
           voxelMinimum.z > voxelMaximum.z;
}

__device__ bool requiresRestriction(const DeviceInput& input, int3 voxelMinimum, int3 voxelMaximum) {
    for (int z = voxelMinimum.z; z <= voxelMaximum.z; ++z) {
        for (int y = voxelMinimum.y; y <= voxelMaximum.y; ++y) {
            for (int x = voxelMinimum.x; x <= voxelMaximum.x; ++x) {
                const int voxel = voxelIndex(input.grid, x, y, z);
                if (voxel < 0 || uint32_t(voxel + 1) >= input.offsetCount) return true;
                if (input.offsets[voxel] != input.offsets[voxel + 1]) return true;
            }
        }
    }
    const int3 cornerMaximum = make_int3(min(input.grid.gridDim.x, voxelMaximum.x + 1),
                                         min(input.grid.gridDim.y, voxelMaximum.y + 1),
                                         min(input.grid.gridDim.z, voxelMaximum.z + 1));
    for (int z = voxelMinimum.z; z <= cornerMaximum.z; ++z) {
        for (int y = voxelMinimum.y; y <= cornerMaximum.y; ++y) {
            for (int x = voxelMinimum.x; x <= cornerMaximum.x; ++x) {
                const int corner = cornerIndex(input.grid, x, y, z);
                if (corner < 0 || uint32_t(corner) >= input.occupancyCount || input.occupancy[corner] == 1u) return true;
            }
        }
    }
    return false;
}

template <class Cell>
__device__ void writeCellFailure(uint32_t cellId, DeviceOutput output, const Cell& cell) {
    output.statuses[cellId] = cell.status == RVDStatus::Success ? RVDStatus::NonFiniteGeometry : cell.status;
}

template <bool RobustPredicate, bool FilterPredicate>
__device__ bool writeIntegratedCell(uint32_t cellId,
                                    ConvexCell<RobustPredicate, FilterPredicate>& cell,
                                    const DeviceInput& input, DeviceOutput output, float4 moment,
                                    int staticPlaneCount, float patchArea) {
    if (cell.status != RVDStatus::Success) return false;
    const float inverseScale = 1.0f / input.grid.scale;
    const float inverseAreaScale = inverseScale * inverseScale;
    const float inverseVolumeScale = inverseAreaScale * inverseScale;
    const float worldSignedVolume = moment.w * inverseVolumeScale;
    Node node{};
    node.volume = fabsf(worldSignedVolume);
    node.interfaceNeighborCount = 0;
    const size_t interfaceBase = size_t(cellId) * RVDMaximumInterfaces;
    for (uint32_t index = 0; index < RVDMaximumInterfaces; ++index) {
        output.interfaceAreas[interfaceBase + index] = 0.0f;
        output.interfaceNeighborIds[interfaceBase + index] = InvalidId;
    }
    const float areaEpsilon = fmaxf(1e-20f, input.nominalCellSize * input.nominalCellSize * 1e-9f);
    for (int plane = 0; plane < staticPlaneCount; ++plane) {
        const int neighbor = cell.planeNeighborIds[plane];
        const float area = cell.planeAreas[plane] * inverseAreaScale;
        if (neighbor < 0 || !isfinite(area) || fabsf(area) <= areaEpsilon) continue;
        if (node.interfaceNeighborCount >= RVDMaximumInterfaces) {
            cell.fail(RVDStatus::InterfaceOverflow);
            return false;
        }
        const size_t destination = interfaceBase + node.interfaceNeighborCount++;
        output.interfaceAreas[destination] = area;
        output.interfaceNeighborIds[destination] = uint32_t(neighbor);
    }
    if (!isfinite(node.volume)) {
        cell.fail(RVDStatus::NonFiniteGeometry);
        return false;
    }
    const float volumeEpsilon = fmaxf(1e-20f, input.nominalCellSize * input.nominalCellSize *
                                                    input.nominalCellSize * 1e-9f);
    if (node.volume <= volumeEpsilon) {
        cell.fail(RVDStatus::EmptyCell);
        return false;
    }
    output.nodes[cellId] = node;
    const float worldPatchArea = patchArea * inverseAreaScale;
    output.surfacePatchAreas[cellId] = worldPatchArea;
    uint32_t flags = input.seedFlags[cellId] & ~(NodeFlags::Surface | NodeFlags::TriangleOverflow);
    const float surfaceEpsilon = fmaxf(1e-20f, input.nominalCellSize * input.nominalCellSize * 1e-8f);
    if (worldPatchArea > surfaceEpsilon) flags |= NodeFlags::Surface;
    output.nodeFlags[cellId] = flags;
    output.statuses[cellId] = RVDStatus::Success;
    return true;
}

template <bool RobustPredicate, bool FilterPredicate>
__device__ void processBaseCell(uint32_t cellId, const DeviceInput& input, DeviceOutput output,
                                ConvexCell<RobustPredicate, FilterPredicate>& cell) {
    output.nodes[cellId] = {};
    output.surfacePatchAreas[cellId] = 0.0f;
    output.nodeFlags[cellId] = input.seedFlags[cellId] & ~(NodeFlags::Surface | NodeFlags::TriangleOverflow);
    if ((input.seedFlags[cellId] & NodeFlags::Ghost) != 0u) {
        output.statuses[cellId] = RVDStatus::Ghost;
        return;
    }

    const bool builtUnrestricted = buildUnrestrictedCell(
        input, cellId, cellId, cell, RVDNormalCandidateCount);
    if (!builtUnrestricted) {
        writeCellFailure(cellId, output, cell);
        return;
    }
    if (!cell.compactPlanes()) {
        writeCellFailure(cellId, output, cell);
        return;
    }
    float3 minimum{}, maximum{};
    if (!cellBounds(cell, minimum, maximum)) {
        writeCellFailure(cellId, output, cell);
        return;
    }
    int3 voxelMinimum{}, voxelMaximum{};
    voxelBounds(input, minimum, maximum, voxelMinimum, voxelMaximum);
    if (emptyVoxelSpan(voxelMinimum, voxelMaximum)) {
        output.nodeFlags[cellId] |= NodeFlags::Ghost;
        output.statuses[cellId] = RVDStatus::Ghost;
        return;
    }
    if (requiresRestriction(input, voxelMinimum, voxelMaximum)) {
        output.statuses[cellId] = RVDStatus::RestrictedPending;
        return;
    }
    const int centerCorner = cornerIndex(input.grid, voxelMinimum.x, voxelMinimum.y, voxelMinimum.z);
    if (centerCorner < 0 || uint32_t(centerCorner) >= input.occupancyCount) {
        output.statuses[cellId] = RVDStatus::InvalidIndex;
        return;
    }
    if (input.occupancy[centerCorner] != 2u) {
        output.nodeFlags[cellId] |= NodeFlags::Ghost;
        output.statuses[cellId] = RVDStatus::Ghost;
        return;
    }
    const float4 rawMoment = integrateCell(cell, canonicalFromWorld(
                                               input.grid, make_float3(input.seeds[cellId].x,
                                                                       input.seeds[cellId].y,
                                                                       input.seeds[cellId].z)),
                                           cell.planeCount, 1.0f);
    const float4 moment = make_float4(rawMoment.x / 6.0f, rawMoment.y / 6.0f,
                                      rawMoment.z / 6.0f, rawMoment.w / 6.0f);
    if (!writeIntegratedCell(cellId, cell, input, output, moment, cell.planeCount, 0.0f)) {
        writeCellFailure(cellId, output, cell);
    }
}

__device__ bool findBeginningPoint(const DeviceInput& input, int3 voxelMinimum, int3 voxelMaximum,
                                   float3& origin, uint32_t& occupancyValue) {
    const float voxelSize = cudaGeometry::voxelSize(input.grid);
    const int3 cornerMaximum = make_int3(min(input.grid.gridDim.x, voxelMaximum.x + 1),
                                         min(input.grid.gridDim.y, voxelMaximum.y + 1),
                                         min(input.grid.gridDim.z, voxelMaximum.z + 1));
    bool foundOutside = false;
    float3 outsidePoint{};
    for (int z = voxelMinimum.z; z <= cornerMaximum.z; ++z) {
        for (int y = voxelMinimum.y; y <= cornerMaximum.y; ++y) {
            for (int x = voxelMinimum.x; x <= cornerMaximum.x; ++x) {
                const int index = cornerIndex(input.grid, x, y, z);
                if (index < 0 || uint32_t(index) >= input.occupancyCount) continue;
                const uint32_t value = input.occupancy[index];
                const float3 point = make_float3(x * voxelSize, y * voxelSize, z * voxelSize);
                if (value == 2u) {
                    origin = point;
                    occupancyValue = 2u;
                    return true;
                }
                if (value == 0u && !foundOutside) { outsidePoint = point; foundOutside = true; }
            }
        }
    }
    if (foundOutside) {
        origin = outsidePoint;
        occupancyValue = 0u;
        return true;
    }
    // Match the upstream RVD strategy: inspect the three adjacent positive faces.
    const int faceCoordinates[3] = {voxelMaximum.x + 1, voxelMaximum.y + 1, voxelMaximum.z + 1};
    for (int axis = 2; axis >= 0; --axis) {
        if (faceCoordinates[axis] >= (axis == 0 ? input.grid.gridDim.x : axis == 1 ? input.grid.gridDim.y : input.grid.gridDim.z)) {
            int x = voxelMinimum.x, y = voxelMinimum.y, z = voxelMinimum.z;
            if (axis == 0) x = input.grid.gridDim.x;
            if (axis == 1) y = input.grid.gridDim.y;
            if (axis == 2) z = input.grid.gridDim.z;
            origin = make_float3(x * voxelSize, y * voxelSize, z * voxelSize);
            occupancyValue = 0u;
            return true;
        }
    }
    return false;
}

template <bool RobustPredicate, bool FilterPredicate>
__device__ void processRestrictedCell(uint32_t cellId, const DeviceInput& input,
                                      uint32_t neighborRow,
                                      DeviceOutput output,
                                       uint32_t candidateLimit,
                                       ConvexCell<RobustPredicate, FilterPredicate>& cell) {
    if ((input.seedFlags[cellId] & NodeFlags::Ghost) != 0u) {
        output.statuses[cellId] = RVDStatus::Ghost;
        return;
    }
    output.nodes[cellId] = {};
    output.surfacePatchAreas[cellId] = 0.0f;
    output.nodeFlags[cellId] = input.seedFlags[cellId] & ~(NodeFlags::Surface | NodeFlags::TriangleOverflow);

    const bool builtUnrestricted = buildUnrestrictedCell(
        input, cellId, neighborRow, cell, candidateLimit);
    if (!builtUnrestricted) {
        writeCellFailure(cellId, output, cell);
        return;
    }
    if (!cell.compactPlanes()) {
        writeCellFailure(cellId, output, cell);
        return;
    }
    const int staticPlaneCount = cell.planeCount;
    float3 minimum{}, maximum{};
    if (!cellBounds(cell, minimum, maximum)) {
        writeCellFailure(cellId, output, cell);
        return;
    }
    int3 voxelMinimum{}, voxelMaximum{};
    voxelBounds(input, minimum, maximum, voxelMinimum, voxelMaximum);

    if (emptyVoxelSpan(voxelMinimum, voxelMaximum)) {
        output.nodeFlags[cellId] |= NodeFlags::Ghost;
        output.statuses[cellId] = RVDStatus::Ghost;
        return;
    }

    int uniqueCount = 0;
    for (int z = voxelMinimum.z; z <= voxelMaximum.z; ++z) {
        for (int y = voxelMinimum.y; y <= voxelMaximum.y; ++y) {
            for (int x = voxelMinimum.x; x <= voxelMaximum.x; ++x) {
                const int voxel = voxelIndex(input.grid, x, y, z);
                if (voxel < 0 || uint32_t(voxel + 1) >= input.offsetCount) {
                    output.statuses[cellId] = RVDStatus::InvalidIndex;
                    return;
                }
                const int32_t begin = input.offsets[voxel];
                const int32_t end = input.offsets[voxel + 1];
                if (begin < 0 || end < begin || uint32_t(end) > input.triangleListCount) {
                    output.statuses[cellId] = RVDStatus::InvalidIndex;
                    return;
                }
                if (begin == end) continue;
                const int32_t* voxelTriangles = input.triangleList + begin;
                const int voxelTriangleCount = end - begin;
                for (int incoming = 0; incoming < voxelTriangleCount; ++incoming) {
                    const int32_t triangleId = voxelTriangles[incoming];
                    bool duplicate = false;
                    for (int existing = 0; existing < uniqueCount; ++existing) {
                        if (cell.uniqueTriangles[existing] == triangleId) {
                            duplicate = true;
                            break;
                        }
                    }
                    if (!duplicate) {
                        if (uniqueCount >= int(cell.triangleCapacity)) {
                            output.nodeFlags[cellId] |= NodeFlags::TriangleOverflow;
                            output.statuses[cellId] = RVDStatus::TriangleOverflow;
                            return;
                        }
                        cell.uniqueTriangles[uniqueCount++] = triangleId;
                    }
                }
            }
        }
    }

    float3 origin{};
    uint32_t originOccupancy = 1u;
    if (!findBeginningPoint(input, voxelMinimum, voxelMaximum, origin, originOccupancy)) {
        output.statuses[cellId] = RVDStatus::FindAnotherBeginningVertex;
        return;
    }
    float4 moment = make_float4(0, 0, 0, 0);
    const float3 seed = canonicalFromWorld(
        input.grid, make_float3(input.seeds[cellId].x, input.seeds[cellId].y, input.seeds[cellId].z));
    const float4 unrestricted = integrateCell(cell, seed, staticPlaneCount, 1.0f);
    if (cell.status != RVDStatus::Success) {
        writeCellFailure(cellId, output, cell);
        return;
    }
    if (originOccupancy == 2u) {
        moment = make_float4(unrestricted.x / 6.0f, unrestricted.y / 6.0f,
                             unrestricted.z / 6.0f, unrestricted.w / 6.0f);
    }

    float surfacePatchArea = 0.0f;

    for (int i = 0; i < cell.vertexCount; ++i) cell.backupVertices[i] = cell.vertices[i];
    const int backupVertexCount = cell.vertexCount;
    const int backupPlaneCount = cell.planeCount;

    for (int triangleIndex = 0; triangleIndex < uniqueCount; ++triangleIndex) {
        const DeviceTriangle triangle = input.triangles[cell.uniqueTriangles[triangleIndex]];
        float3 v0 = canonicalFromWorld(input.grid, make_float3(triangle.v0.x, triangle.v0.y, triangle.v0.z));
        float3 v1 = canonicalFromWorld(input.grid, make_float3(triangle.v1.x, triangle.v1.y, triangle.v1.z));
        float3 v2 = canonicalFromWorld(input.grid, make_float3(triangle.v2.x, triangle.v2.y, triangle.v2.z));
        if (!finite3(v0) || !finite3(v1) || !finite3(v2)) {
            output.statuses[cellId] = RVDStatus::NonFiniteGeometry;
            return;
        }
        surfacePatchArea += clippedTriangleArea(v0, v1, v2, cell, staticPlaneCount);
        const bool pointsOut = determinant3(sub(v0, origin), sub(v1, origin), sub(v2, origin)) > 0.0f;
        if (!pointsOut) { const float3 temporary = v1; v1 = v2; v2 = temporary; }

        const float4 tetraPlanes[4] = {
            planeFromPoints(origin, v0, v1), planeFromPoints(origin, v2, v0),
            planeFromPoints(origin, v1, v2), planeFromPoints(v1, v2, v0)};
        bool emptyIntersection = false;
        for (int plane = 0; plane < 4; ++plane) {
            if (!cell.addPlane(tetraPlanes[plane])) {
                if (cell.status == RVDStatus::EmptyCell) emptyIntersection = true;
                break;
            }
        }
        if (!emptyIntersection && cell.status != RVDStatus::Success) {
            writeCellFailure(cellId, output, cell);
            return;
        }
        if (!emptyIntersection) {
            const float factor = originOccupancy == 2u
                ? (pointsOut ? -1.0f : 1.0f)
                : (pointsOut ? 1.0f : -1.0f);
            const float4 chunk = integrateCell(cell, seed, staticPlaneCount, factor);
            moment.x += factor * chunk.x / 6.0f;
            moment.y += factor * chunk.y / 6.0f;
            moment.z += factor * chunk.z / 6.0f;
            moment.w += factor * chunk.w / 6.0f;
            if (cell.status != RVDStatus::Success) {
                writeCellFailure(cellId, output, cell);
                return;
            }
        }
        cell.vertexCount = backupVertexCount;
        cell.planeCount = backupPlaneCount;
        cell.status = RVDStatus::Success;
        for (int i = 0; i < backupVertexCount; ++i) cell.vertices[i] = cell.backupVertices[i];
    }
    if (!isfinite(surfacePatchArea)) {
        output.statuses[cellId] = RVDStatus::NonFiniteGeometry;
        return;
    }
    if (!writeIntegratedCell(cellId, cell, input, output, moment,
                             staticPlaneCount, surfacePatchArea)) {
        writeCellFailure(cellId, output, cell);
    }
}

template <bool RobustPredicate, bool FilterPredicate>
__global__ void baseKernel(DeviceInput input, DeviceOutput output, uint32_t begin, uint32_t count,
                           uint8_t* storageMemory) {
    const uint32_t local = blockIdx.x * blockDim.x + threadIdx.x;
    if (local >= count) return;
    auto* storage = reinterpret_cast<ConvexCellStorage<NormalLimits>*>(storageMemory) + local;
    ConvexCell<RobustPredicate, FilterPredicate> cell;
    cell.attachStorage(*storage);
    processBaseCell<RobustPredicate, FilterPredicate>(begin + local, input, output, cell);
}

template <bool RobustPredicate, bool FilterPredicate>
__global__ void cellKernel(DeviceInput input, DeviceOutput output, const uint32_t* cellIds,
                           uint32_t begin, uint32_t count, uint32_t candidateLimit,
                           uint32_t planeLimit, uint32_t vertexLimit,
                           uint32_t triangleLimit,
                           uint8_t* storageMemory) {
    const uint32_t local = blockIdx.x * blockDim.x + threadIdx.x;
    if (local >= count) return;
    auto* storage = reinterpret_cast<RestrictedConvexCellStorage*>(storageMemory) + local;
    ConvexCell<RobustPredicate, FilterPredicate> cell;
    cell.attachStorage(*storage, planeLimit, vertexLimit, triangleLimit);
    processRestrictedCell<RobustPredicate, FilterPredicate>(
        cellIds[begin + local], input, begin + local, output, candidateLimit, cell);
}

} // namespace

class RVD::Implementation {
public:
    ~Implementation() { cleanup(); }

    bool initialize(VulkanDevice& vulkanDevice) {
        cleanup();
        cudaDevice = cudaVulkan::findDevice(vulkanDevice.getPhysicalDevice());
        if (cudaDevice < 0) {
            std::cerr << "[RVD] Vulkan physical device has no matching CUDA UUID" << std::endl;
            return false;
        }
        if (!cudaOk(cudaSetDevice(cudaDevice), "select Vulkan-matched CUDA device") ||
            !cudaOk(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking), "create stream")) {
            cleanup();
            return false;
        }
        initialized = true;
        return true;
    }

    void cleanup() {
        if (cudaDevice >= 0) cudaSetDevice(cudaDevice);
        geometry.reset();
        if (stream) cudaStreamDestroy(stream);
        stream = nullptr;
        cudaDevice = -1;
        initialized = false;
    }

    bool prepareMeshGeometry(
        const std::vector<std::array<glm::vec4, 3>>& meshTriangles,
        const VoxelGrid& voxelGrid,
        const std::vector<glm::vec4>& seeds,
        const glm::vec3& sdfGridMin,
        const glm::ivec3& sdfGridDim,
        float nominalCellSize,
        std::vector<uint32_t>& seedFlags) {
        if (!initialized || !cudaOk(cudaSetDevice(cudaDevice), "select RVD CUDA device")) return false;
        uint32_t degenerateTriangleCount = 0;
        for (const auto& triangle : meshTriangles) {
            for (const glm::vec4& point : triangle) {
                if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z))
                    return false;
            }
            const glm::dvec3 a(triangle[0]);
            const glm::dvec3 b(triangle[1]);
            const glm::dvec3 c(triangle[2]);
            const glm::dvec3 ab = b - a;
            const glm::dvec3 ac = c - a;
            const glm::dvec3 bc = c - b;
            const double edgeScale = std::max(glm::dot(ab, ab), std::max(glm::dot(ac, ac), glm::dot(bc, bc)));
            const double normalSquared = glm::dot(glm::cross(ab, ac), glm::cross(ab, ac));
            if (!(edgeScale > std::numeric_limits<float>::min()) || !std::isfinite(edgeScale) ||
                !std::isfinite(normalSquared) || normalSquared <= 1.0e-12 * edgeScale * edgeScale)
                ++degenerateTriangleCount;
        }
        if (degenerateTriangleCount != 0)
            std::cerr << "[RVD] ignored degenerate mesh triangles=" << degenerateTriangleCount << std::endl;
        if (!cudaGeometry::uploadGeometry(meshTriangles, voxelGrid, geometry, stream) ||
            !sdf::buildAndClassify(geometry, seeds, sdfGridMin, sdfGridDim, nominalCellSize,
                                       seedFlags, stream)) {
            geometry.reset();
            return false;
        }
        return true;
    }

    bool hasPreparedMeshGeometry() const { return geometry.valid(); }
    void clearMeshGeometry() {
        if (cudaDevice >= 0) cudaSetDevice(cudaDevice);
        geometry.reset();
    }

    bool validate(const RVDInput& input, std::string& reason) const {
        if (!input.seeds || !input.seedFlags || !input.candidateNeighbors || !input.domainCorners) {
            reason = "missing input collection";
            return false;
        }
        if (!geometry.valid()) {
            reason = "mesh geometry was not prepared";
            return false;
        }
        const size_t seedCount = input.seeds->size();
        if (seedCount == 0 || seedCount > std::numeric_limits<uint32_t>::max()) {
            reason = "invalid seed count";
            return false;
        }
        if (input.seedFlags->size() != seedCount ||
            input.candidateNeighbors->seedCount() != seedCount ||
            input.candidateNeighbors->neighborStride() < RVDNormalCandidateCount) {
            reason = "flags or neighbor matrix has the wrong shape";
            return false;
        }
        if (!(input.nominalCellSize > 0.0f) || !std::isfinite(input.nominalCellSize)) {
            reason = "invalid nominal cell size";
            return false;
        }
        // RVD is a public computation boundary: keep a lightweight CPU validation
        // of the seed positions even though KNNNeighbors also validates them.
        for (size_t seed = 0; seed < seedCount; ++seed) {
            const glm::vec4 point = (*input.seeds)[seed];
            if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z) || !std::isfinite(point.w)) {
                reason = "non-finite seed";
                return false;
            }
        }
        return true;
    }

    template <class Launch>
    bool launchChunks(uint32_t count, uint32_t initialChunkSize, Launch launch) {
        if (count == 0) return true;
        const uint32_t chunkSize = std::max(1u, initialChunkSize);
        for (uint32_t begin = 0; begin < count;) {
            const uint32_t chunkCount = std::min(chunkSize, count - begin);
            launch(begin, chunkCount);
            if (!cudaOk(cudaGetLastError(), "launch RVD kernel") ||
                !cudaOk(cudaStreamSynchronize(stream), "synchronize RVD chunk")) return false;
            begin += chunkCount;
        }
        return true;
    }

    template <bool RobustPredicate, bool FilterPredicate>
    bool launchCellPass(const DeviceInput& input, DeviceOutput output,
                        const uint32_t* cellIds, uint32_t count, uint32_t chunkSize,
                        uint32_t candidateLimit, uint32_t planeLimit,
                        uint32_t vertexLimit, uint32_t triangleLimit) {
        if (count == 0) return true;
        const size_t storageCount = std::max(1u, chunkSize);
        const size_t storageBytes = sizeof(RestrictedConvexCellStorage);
        if (storageCount > std::numeric_limits<size_t>::max() / storageBytes) return false;
        DeviceBuffer<uint8_t> storage;
        if (!storage.allocate(storageCount * storageBytes)) {
            std::cerr << "[RVD] cell scratch allocation failed bytes="
                      << storageCount * storageBytes << std::endl;
            return false;
        }
        const bool launched = launchChunks(count, chunkSize,
            [&](uint32_t begin, uint32_t chunkCount) {
                constexpr uint32_t BlockSize = 32;
                const uint32_t blocks = (chunkCount + BlockSize - 1) / BlockSize;
                cellKernel<RobustPredicate, FilterPredicate>
                    <<<blocks, BlockSize, 0, stream>>>(input, output, cellIds, begin, chunkCount,
                                                       candidateLimit, planeLimit, vertexLimit,
                                                       triangleLimit, storage.get());
            });
        if (!launched) std::cerr << "[RVD] cell kernel phase failed" << std::endl;
        return launched;
    }

    bool launchCellPass(RVDPredicateMode mode, const DeviceInput& input, DeviceOutput output,
                        const uint32_t* cellIds, uint32_t count, uint32_t chunkSize,
                        uint32_t candidateLimit, uint32_t planeLimit,
                        uint32_t vertexLimit, uint32_t triangleLimit) {
        if (mode == RVDPredicateMode::Double) {
            return launchCellPass<true, false>(input, output, cellIds, count, chunkSize,
                                               candidateLimit, planeLimit, vertexLimit,
                                               triangleLimit);
        }
        if (mode == RVDPredicateMode::Float) {
            return launchCellPass<false, false>(input, output, cellIds, count, chunkSize,
                                                candidateLimit, planeLimit, vertexLimit,
                                                triangleLimit);
        }
        return launchCellPass<false, true>(input, output, cellIds, count, chunkSize,
                                           candidateLimit, planeLimit, vertexLimit,
                                           triangleLimit);
    }

    bool build(const RVDInput& input, const RVDConfig& config, RVDResult& result) {
        result = {};
        if (!initialized || !cudaOk(cudaSetDevice(cudaDevice), "select RVD CUDA device")) return false;
        std::string validationReason;
        if (!validate(input, validationReason)) {
            std::cerr << "[RVD] input validation failed: " << validationReason << std::endl;
            return false;
        }
        const uint32_t count = uint32_t(input.seeds->size());

        static_assert(sizeof(glm::vec4) == sizeof(float4));
        static_assert(sizeof(std::array<glm::vec4, 3>) == sizeof(DeviceTriangle));
        // Seeds and neighbor rows are CUDA-resident in KNNNeighbors and read by
        // device pointer; RVD does not re-upload them.
        DeviceBuffer<uint32_t> flags;
        DeviceBuffer<Node> nodes;
        DeviceBuffer<uint32_t> outputFlags;
        DeviceBuffer<float> patchAreas;
        DeviceBuffer<float> interfaceAreas;
        DeviceBuffer<uint32_t> interfaceIds;
        DeviceBuffer<RVDStatus> statuses;
        const size_t interfaceCount = size_t(count) * RVDMaximumInterfaces;
        const uint32_t neighborStride = input.candidateNeighbors->neighborStride();
        if (!geometry.valid() || !flags.allocate(count) ||
            !nodes.allocate(count) || !outputFlags.allocate(count) || !patchAreas.allocate(count) ||
            !interfaceAreas.allocate(interfaceCount) || !interfaceIds.allocate(interfaceCount) ||
            !statuses.allocate(count)) return false;
        if (!flags.uploadAsync(input.seedFlags->data(), count, stream)) return false;
        if (!cudaOk(cudaMemsetAsync(nodes.get(), 0, count * sizeof(Node), stream), "clear RVD nodes") ||
            !cudaOk(cudaMemsetAsync(outputFlags.get(), 0, count * sizeof(uint32_t), stream), "clear RVD flags") ||
            !cudaOk(cudaMemsetAsync(patchAreas.get(), 0, count * sizeof(float), stream), "clear RVD patch areas") ||
            !cudaOk(cudaMemsetAsync(interfaceAreas.get(), 0, interfaceCount * sizeof(float), stream), "clear RVD interface areas") ||
            !cudaOk(cudaMemsetAsync(interfaceIds.get(), 0xff, interfaceCount * sizeof(uint32_t), stream), "clear RVD interface IDs") ||
            !cudaOk(cudaMemsetAsync(statuses.get(), 0, count * sizeof(RVDStatus), stream), "clear RVD statuses")) return false;

        DeviceInput deviceInput{};
        deviceInput.seeds = reinterpret_cast<const float4*>(input.candidateNeighbors->deviceSeeds()); deviceInput.seedFlags = flags.get(); deviceInput.neighborIds = input.candidateNeighbors->deviceNeighborIds();
        deviceInput.neighborStride = neighborStride; deviceInput.seedCount = count;
        deviceInput.triangles = geometry.triangles.get(); deviceInput.triangleCount = geometry.triangleCount;
        deviceInput.occupancy = geometry.occupancy.get(); deviceInput.occupancyCount = uint32_t(geometry.occupancy.size());
        deviceInput.triangleList = geometry.voxelTriangleIds.get(); deviceInput.triangleListCount = uint32_t(geometry.voxelTriangleIds.size());
        deviceInput.offsets = geometry.voxelOffsets.get(); deviceInput.offsetCount = uint32_t(geometry.voxelOffsets.size());
        deviceInput.grid = geometry.voxelGrid;
        std::array<glm::dvec3, 8> canonicalCorners{};
        glm::dvec3 domainCenter(0.0);
        const glm::dvec3 gridMinimum(
            geometry.voxelGrid.gridMin.x, geometry.voxelGrid.gridMin.y,
            geometry.voxelGrid.gridMin.z);
        for (uint32_t corner = 0; corner < canonicalCorners.size(); ++corner) {
            canonicalCorners[corner] =
                (glm::dvec3((*input.domainCorners)[corner]) - gridMinimum) *
                double(geometry.voxelGrid.scale);
            domainCenter += canonicalCorners[corner];
        }
        domainCenter *= 1.0 / double(canonicalCorners.size());
        constexpr uint32_t faceCorners[6][3] = {
            {0, 4, 2}, {1, 3, 5}, {0, 1, 4},
            {2, 6, 3}, {0, 2, 1}, {4, 5, 6}
        };
        for (uint32_t face = 0; face < 6; ++face) {
            const glm::dvec3 a = canonicalCorners[faceCorners[face][0]];
            const glm::dvec3 b = canonicalCorners[faceCorners[face][1]];
            const glm::dvec3 c = canonicalCorners[faceCorners[face][2]];
            glm::dvec3 normal = glm::cross(b - a, c - a);
            normal /= std::sqrt(glm::dot(normal, normal));
            double offset = -glm::dot(normal, a);
            if (glm::dot(normal, domainCenter) + offset < 0.0) {
                normal = -normal;
                offset = -offset;
            }
            deviceInput.domainPlanes[face] = make_float4(
                float(normal.x), float(normal.y), float(normal.z), float(offset));
        }
        deviceInput.nominalCellSize = input.nominalCellSize;
        DeviceOutput deviceOutput{nodes.get(), outputFlags.get(), patchAreas.get(), interfaceAreas.get(),
                                  interfaceIds.get(), statuses.get()};

        const size_t baseStorageCount = std::max(1u, config.baseChunkSize);
        const size_t baseStorageBytes = sizeof(ConvexCellStorage<NormalLimits>);
        if (baseStorageCount > std::numeric_limits<size_t>::max() / baseStorageBytes) return false;
        DeviceBuffer<uint8_t> baseStorage;
        if (!baseStorage.allocate(baseStorageCount * baseStorageBytes)) return false;
        bool ok = launchChunks(count, config.baseChunkSize,
            [&](uint32_t begin, uint32_t chunkCount) {
                constexpr uint32_t BlockSize = 32;
                const uint32_t blocks = (chunkCount + BlockSize - 1) / BlockSize;
                if (config.predicateMode == RVDPredicateMode::Double)
                    baseKernel<true, false><<<blocks, BlockSize, 0, stream>>>(deviceInput, deviceOutput, begin, chunkCount, baseStorage.get());
                else if (config.predicateMode == RVDPredicateMode::Float)
                    baseKernel<false, false><<<blocks, BlockSize, 0, stream>>>(deviceInput, deviceOutput, begin, chunkCount, baseStorage.get());
                else
                    baseKernel<false, true><<<blocks, BlockSize, 0, stream>>>(deviceInput, deviceOutput, begin, chunkCount, baseStorage.get());
            });
        if (!ok) {
            std::cerr << "[RVD] base kernel phase failed" << std::endl;
            return false;
        }
        baseStorage.reset();

        RVDWorkQueue workQueue;
        if (!workQueue.initialize(count, stream)) {
            std::cerr << "[RVD] work queue initialization failed" << std::endl;
            return false;
        }
        uint32_t activeCount = 0;
        const uint32_t initialAdaptiveMask = RVDRestrictedPassMask |
            (config.predicateMode == RVDPredicateMode::Adaptive ? RVDPredicateRetryMask : 0u);
        if (!workQueue.compactAll(statuses.get(), count, initialAdaptiveMask, activeCount)) {
            std::cerr << "[RVD] restricted queue compaction failed" << std::endl;
            return false;
        }
        const auto& policy = config.adaptive;
        const auto validCapacity = [](const RVDConfig::Capacity& initial,
                                      const RVDConfig::Capacity& maximum) {
            return initial.K > 0 && initial.P > 0 && initial.V > 0 && initial.T > 0 &&
                   initial.K <= maximum.K && initial.P <= maximum.P &&
                   initial.V <= maximum.V && initial.T <= maximum.T &&
                   maximum.K <= RVDMaximumCandidateCount &&
                   maximum.P <= RVDRetryPlaneCount &&
                   maximum.V <= RVDRetryVertexCount &&
                   maximum.T <= RVDRetryTriangleCount;
        };
        if (!validCapacity(policy.initial, policy.maximum) ||
            !(policy.growthFactor > 1.0f) || !std::isfinite(policy.growthFactor) ||
            policy.maximumPasses == 0) {
            std::cerr << "[RVD] invalid adaptive policy" << std::endl;
            return false;
        }

        RVDConfig::Capacity capacity = policy.initial;
        capacity.K = std::min(capacity.K, count - 1u);
        RVDConfig::Capacity maximum = policy.maximum;
        maximum.K = std::min(maximum.K, count - 1u);
        // The reference switches every failed/restricted row to its exact
        // predicate mode before the adaptive relaunch loop.
        bool robustPredicates = config.predicateMode == RVDPredicateMode::Double;
        std::array<uint32_t, static_cast<size_t>(RVDStatus::Count)> passStatusCounts{};
        const auto grow = [&](uint32_t value, uint32_t limit) {
            if (value >= limit) return value;
            const uint32_t grown = uint32_t(std::ceil(double(value) * policy.growthFactor));
            return std::min(limit, std::max(value + 1u, grown));
        };

        uint32_t capacityRelaunchCount = 0;
        uint32_t passIndex = 0;
        while (activeCount != 0) {
            const uint8_t completedPass = uint8_t(std::min(255u, passIndex + 2u));
            const uint32_t* activeIds = workQueue.deviceQueue();
            KNNBatch neighborBatch;
            if (!input.candidateNeighbors->query(activeIds, activeCount, capacity.K,
                                                  neighborBatch)) {
                std::cerr << "[RVD] compact KNN batch failed pass=" << unsigned(completedPass)
                          << " rows=" << activeCount << " K=" << capacity.K << std::endl;
                return false;
            }
            DeviceInput passInput = deviceInput;
            passInput.neighborIds = neighborBatch.deviceNeighborIds();
            passInput.neighborStride = neighborBatch.neighborStride();
            const RVDPredicateMode passMode = config.predicateMode == RVDPredicateMode::Adaptive
                ? (robustPredicates ? RVDPredicateMode::Double : RVDPredicateMode::Adaptive)
                : config.predicateMode;
            ok = launchCellPass(
                passMode, passInput, deviceOutput, activeIds, activeCount,
                config.restrictedChunkSize,
                capacity.K, capacity.P, capacity.V, capacity.T);
            if (!ok) {
                std::cerr << "[RVD] adaptive pass failed pass=" << unsigned(completedPass)
                          << " rows=" << activeCount << " limits[K/P/V/T]="
                          << capacity.K << "/" << capacity.P << "/" << capacity.V
                          << "/" << capacity.T << std::endl;
                return false;
            }
            if (!workQueue.countActiveStatuses(statuses.get(), activeCount, passStatusCounts)) {
                std::cerr << "[RVD] adaptive status count failed" << std::endl;
                return false;
            }

            const auto statusCount = [&](RVDStatus status) {
                return passStatusCounts[static_cast<size_t>(status)];
            };
            const uint32_t securityFailures = statusCount(RVDStatus::SecurityRadiusNotReached);
            const uint32_t planeFailures = statusCount(RVDStatus::PlaneOverflow);
            const uint32_t vertexFailures = statusCount(RVDStatus::VertexOverflow);
            const uint32_t triangleFailures = statusCount(RVDStatus::TriangleOverflow);
            const uint32_t predicateFailures = statusCount(RVDStatus::NeedsRobustPredicate) +
                                               statusCount(RVDStatus::InconsistentBoundary) +
                                               statusCount(RVDStatus::NonFiniteGeometry);
            RVDConfig::Capacity next = capacity;
            uint32_t capacityRetryMask = 0;
            if (securityFailures && (next.K = grow(next.K, maximum.K)) != capacity.K)
                capacityRetryMask |= rvdStatusBit(RVDStatus::SecurityRadiusNotReached);
            if (planeFailures && (next.P = grow(next.P, maximum.P)) != capacity.P)
                capacityRetryMask |= rvdStatusBit(RVDStatus::PlaneOverflow);
            if (vertexFailures && (next.V = grow(next.V, maximum.V)) != capacity.V)
                capacityRetryMask |= rvdStatusBit(RVDStatus::VertexOverflow);
            if (triangleFailures && (next.T = grow(next.T, maximum.T)) != capacity.T)
                capacityRetryMask |= rvdStatusBit(RVDStatus::TriangleOverflow);
            uint32_t retryMask = capacityRetryMask;
            if (predicateFailures && config.predicateMode == RVDPredicateMode::Adaptive &&
                !robustPredicates) {
                retryMask |= RVDPredicateRetryMask;
            }
            // The OpenCL controller switches the entire compact failure queue
            // to its robust predicate implementation before any extra pass.
            if (retryMask != 0 && config.predicateMode == RVDPredicateMode::Adaptive)
                robustPredicates = true;

            if (retryMask == 0) {
                activeCount = 0;
                break;
            }
            // The reference's five-step "extra" loop budgets capacity growth.
            // Rebuilding an ambiguous float result with robust predicates at
            // unchanged capacities happens before that loop and must not use
            // one of those five slots.
            if (capacityRetryMask != 0 &&
                capacityRelaunchCount >= policy.maximumPasses) {
                std::cerr << "[RVD] reference retry budget exhausted rows=" << activeCount
                          << " relaunches=" << capacityRelaunchCount
                          << " limits[K/P/V/T]=" << capacity.K << "/" << capacity.P << "/"
                          << capacity.V << "/" << capacity.T << std::endl;
                break;
            }
            uint32_t nextActiveCount = 0;
            if (!workQueue.compactActive(statuses.get(), activeCount, retryMask,
                                         nextActiveCount)) {
                std::cerr << "[RVD] adaptive retry queue compaction failed" << std::endl;
                return false;
            }
            activeCount = nextActiveCount;
            capacity = next;
            if (capacityRetryMask != 0) ++capacityRelaunchCount;
            ++passIndex;
        }

        uint32_t finalFailureCount = 0;
        if (!workQueue.compactAll(statuses.get(), count, RVDTerminalFailureMask,
                                  finalFailureCount)) {
            std::cerr << "[RVD] final failure queue compaction failed" << std::endl;
            return false;
        }
        std::vector<uint32_t> failedCellIds;
        if (!workQueue.download(failedCellIds)) {
            std::cerr << "[RVD] final failure queue download failed" << std::endl;
            return false;
        }

        result.nodes.resize(count); result.nodeFlags.resize(count); result.surfacePatchAreas.resize(count);
        result.interfaceAreas.resize(interfaceCount); result.interfaceNeighborIds.resize(interfaceCount);
        std::vector<RVDStatus> downloadedStatuses(count);
        if (!nodes.downloadAsync(result.nodes.data(), count, stream) ||
            !outputFlags.downloadAsync(result.nodeFlags.data(), count, stream) ||
            !patchAreas.downloadAsync(result.surfacePatchAreas.data(), count, stream) ||
            !interfaceAreas.downloadAsync(result.interfaceAreas.data(), interfaceCount, stream) ||
            !interfaceIds.downloadAsync(result.interfaceNeighborIds.data(), interfaceCount, stream) ||
            !statuses.downloadAsync(downloadedStatuses.data(), count, stream) ||
            !cudaOk(cudaStreamSynchronize(stream), "synchronize RVD result downloads")) {
            std::cerr << "[RVD] result buffer download failed" << std::endl;
            return false;
        }
        for (uint32_t cell = 0; cell < count; ++cell) {
            const RVDStatus status = downloadedStatuses[cell];
            if ((ReferenceDiscardMask & rvdStatusBit(status)) != 0u) {
                result.nodes[cell] = {};
                result.nodeFlags[cell] |= NodeFlags::Ghost;
                result.surfacePatchAreas[cell] = 0.0f;
                continue;
            }
        }

        if (!failedCellIds.empty()) {
            std::cerr << "[RVD] unresolved cells=" << failedCellIds.size() << std::endl;
            constexpr size_t FailureLogLimit = 32;
            const size_t failureLogCount = std::min(failedCellIds.size(), FailureLogLimit);
            for (size_t failureIndex = 0; failureIndex < failureLogCount; ++failureIndex) {
                const uint32_t cell = failedCellIds[failureIndex];
                const glm::vec4& seed = (*input.seeds)[cell];
                std::cerr << "[RVD-Failure] cell=" << cell
                          << " status=" << rvdStatusName(downloadedStatuses[cell])
                          << " seed=(" << seed.x << "," << seed.y << "," << seed.z << ")"
                          << std::endl;
            }
            if (failedCellIds.size() > failureLogCount) {
                std::cerr << "[RVD-Failure] omitted="
                          << failedCellIds.size() - failureLogCount << std::endl;
            }
            for (const uint32_t cell : failedCellIds) {
                if ((ReferenceDiscardMask & rvdStatusBit(downloadedStatuses[cell])) == 0u)
                    return false;
            }
        }
        return true;
    }

    RVDGeometry geometry;
    int cudaDevice = -1;
    cudaStream_t stream = nullptr;
    bool initialized = false;
};

RVD::RVD() : implementation(std::make_unique<Implementation>()) {}
RVD::~RVD() = default;
bool RVD::initialize(VulkanDevice& vulkanDevice) { return implementation->initialize(vulkanDevice); }
bool RVD::prepareMeshGeometry(
    const std::vector<std::array<glm::vec4, 3>>& meshTriangles,
    const VoxelGrid& voxelGrid,
    const std::vector<glm::vec4>& seeds,
    const glm::vec3& sdfGridMin,
    const glm::ivec3& sdfGridDim,
    float nominalCellSize,
    std::vector<uint32_t>& seedFlags) {
    return implementation->prepareMeshGeometry(
        meshTriangles, voxelGrid, seeds, sdfGridMin, sdfGridDim,
        nominalCellSize, seedFlags);
}
bool RVD::hasPreparedMeshGeometry() const {
    return implementation->hasPreparedMeshGeometry();
}
void RVD::clearMeshGeometry() { implementation->clearMeshGeometry(); }
bool RVD::build(const RVDInput& input, const RVDConfig& config, RVDResult& result) {
    return implementation->build(input, config, result);
}
void RVD::cleanup() { implementation->cleanup(); }

static const char* rvdStatusName(RVDStatus status) {
    switch (status) {
    case RVDStatus::Success: return "success";
    case RVDStatus::RestrictedPending: return "restricted_pending";
    case RVDStatus::Ghost: return "ghost";
    case RVDStatus::EmptyCell: return "empty_cell";
    case RVDStatus::VertexOverflow: return "vertex_overflow";
    case RVDStatus::PlaneOverflow: return "plane_overflow";
    case RVDStatus::TriangleOverflow: return "triangle_overflow";
    case RVDStatus::InterfaceOverflow: return "interface_overflow";
    case RVDStatus::SecurityRadiusNotReached: return "security_radius_not_reached";
    case RVDStatus::NeedsRobustPredicate: return "needs_robust_predicate";
    case RVDStatus::InconsistentBoundary: return "inconsistent_boundary";
    case RVDStatus::FindAnotherBeginningVertex: return "find_another_beginning_vertex";
    case RVDStatus::NonFiniteGeometry: return "non_finite_geometry";
    case RVDStatus::InvalidIndex: return "invalid_index";
    default: return "unknown";
    }
}

} // namespace voronoi
