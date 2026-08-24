#include "GlobalContactRegionKernels.cuh"

#include <cuda_runtime.h>
#include <cmath>

namespace heat::contactregion {

static __device__ __forceinline__ float3 normalizeSafe(float3 v, float3 fallback) {
    float len = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
    return (len > 1e-6f) ? make_float3(v.x / len, v.y / len, v.z / len) : fallback;
}

static __device__ __forceinline__ float3 crossProduct(float3 a, float3 b) {
    return make_float3(
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    );
}

static __device__ __forceinline__ void buildBasis(float3 normal, float3& u, float3& v) {
    float3 up = (fabsf(normal.z) < 0.9f) ? make_float3(0.0f, 0.0f, 1.0f) : make_float3(1.0f, 0.0f, 0.0f);
    u = normalizeSafe(crossProduct(normal, up), make_float3(1.0f, 0.0f, 0.0f));
    v = crossProduct(normal, u);
}

// 1. Generate 64-bit spatial key per face based on model pair and coarse spatial tile
__global__ void generateRegionKeysKernel(
    const heat::ContactFace* faces,
    uint32_t faceCount,
    float3 gridMin,
    float tileSize,
    uint3 tileDim,
    uint32_t channelCount,
    uint64_t* keys,
    uint32_t* faceIndices) {
    uint32_t idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= faceCount) return;

    const heat::ContactFace& f = faces[idx];
    float3 c = make_float3(f.centroidArea.x, f.centroidArea.y, f.centroidArea.z);

    int tx = max(0, min(__float2int_rd((c.x - gridMin.x) / tileSize), int(tileDim.x) - 1));
    int ty = max(0, min(__float2int_rd((c.y - gridMin.y) / tileSize), int(tileDim.y) - 1));
    int tz = max(0, min(__float2int_rd((c.z - gridMin.z) / tileSize), int(tileDim.z) - 1));

    uint64_t tileCount = uint64_t(tileDim.x) * tileDim.y * tileDim.z;
    uint64_t tileIndex = uint64_t(tx) + uint64_t(ty) * tileDim.x + uint64_t(tz) * tileDim.x * tileDim.y;
    uint64_t pairIndex = uint64_t(f.channelA) * channelCount + uint64_t(f.channelB);

    keys[idx] = pairIndex * tileCount + tileIndex;
    faceIndices[idx] = idx;
}

// 2. Mark boundary flags between adjacent unique keys
__global__ void markRegionBoundariesKernel(
    const uint64_t* sortedKeys,
    uint32_t faceCount,
    uint32_t* flags) {
    uint32_t idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= faceCount) return;
    flags[idx] = (idx == 0 || sortedKeys[idx] != sortedKeys[idx - 1]) ? 1 : 0;
}

// 3. Set sentinel end index for the last region
__global__ void finalizeRegionStartsKernel(
    const uint32_t* actualRegionCount,
    uint32_t* regionStarts,
    uint32_t faceCount) {
    if (threadIdx.x == 0 && blockIdx.x == 0) {
        regionStarts[*actualRegionCount] = faceCount;
    }
}

// 4. Reduce each region's analytic disks into an oriented bounding box (OBB)
__global__ void reduceRegionBoundsKernel(
    const heat::ContactFace* faces,
    const uint32_t* sortedFaceIndices,
    const uint32_t* regionStarts,
    const uint32_t* actualRegionCount,
    uint32_t channelCount,
    const uint32_t* pairToPsiChannel,
    heat::ContactRegion* outRegions) {
    uint32_t r = blockIdx.x;
    if (r >= *actualRegionCount) return;

    uint32_t begin = regionStarts[r];
    uint32_t end = regionStarts[r + 1];
    if (begin >= end) return;

    __shared__ uint32_t regChannelA, regChannelB;
    if (threadIdx.x == 0) {
        const heat::ContactFace& firstFace = faces[sortedFaceIndices[begin]];
        regChannelA = firstFace.channelA;
        regChannelB = firstFace.channelB;
    }

    // Step A: Parallel block reduction of area-weighted normal
    float3 localNormalSum = make_float3(0.0f, 0.0f, 0.0f);
    for (uint32_t i = begin + threadIdx.x; i < end; i += blockDim.x) {
        const heat::ContactFace& f = faces[sortedFaceIndices[i]];
        float area = f.centroidArea.w;
        localNormalSum.x += f.normalGap.x * area;
        localNormalSum.y += f.normalGap.y * area;
        localNormalSum.z += f.normalGap.z * area;
    }

    __shared__ float s_nx[256], s_ny[256], s_nz[256];
    s_nx[threadIdx.x] = localNormalSum.x;
    s_ny[threadIdx.x] = localNormalSum.y;
    s_nz[threadIdx.x] = localNormalSum.z;
    __syncthreads();

    for (int stride = blockDim.x / 2; stride > 0; stride >>= 1) {
        if (threadIdx.x < stride) {
            s_nx[threadIdx.x] += s_nx[threadIdx.x + stride];
            s_ny[threadIdx.x] += s_ny[threadIdx.x + stride];
            s_nz[threadIdx.x] += s_nz[threadIdx.x + stride];
        }
        __syncthreads();
    }

    __shared__ float3 regNormal, regU, regV, regCenterRef;
    if (threadIdx.x == 0) {
        regNormal = normalizeSafe(make_float3(s_nx[0], s_ny[0], s_nz[0]), make_float3(0.0f, 0.0f, 1.0f));
        buildBasis(regNormal, regU, regV);

        const heat::ContactFace& firstFace = faces[sortedFaceIndices[begin]];
        regCenterRef = make_float3(firstFace.centroidArea.x, firstFace.centroidArea.y, firstFace.centroidArea.z);
    }
    __syncthreads();

    // Step B: Parallel block reduction of exact disk projection extrema along (U, V, W)
    float minU = 1e30f, maxU = -1e30f;
    float minV = 1e30f, maxV = -1e30f;
    float minW = 1e30f, maxW = -1e30f;

    for (uint32_t i = begin + threadIdx.x; i < end; i += blockDim.x) {
        const heat::ContactFace& f = faces[sortedFaceIndices[i]];
        float3 c = make_float3(f.centroidArea.x, f.centroidArea.y, f.centroidArea.z);
        float3 d = make_float3(c.x - regCenterRef.x, c.y - regCenterRef.y, c.z - regCenterRef.z);
        float uCenter = d.x * regU.x + d.y * regU.y + d.z * regU.z;
        float vCenter = d.x * regV.x + d.y * regV.y + d.z * regV.z;
        float wCenter = d.x * regNormal.x + d.y * regNormal.y + d.z * regNormal.z;
        float radius = sqrtf(f.centroidArea.w * (1.0f / 3.14159265358979323846f));
        float halfGap = 0.5f * f.normalGap.w;
        minU = fminf(minU, uCenter - radius); maxU = fmaxf(maxU, uCenter + radius);
        minV = fminf(minV, vCenter - radius); maxV = fmaxf(maxV, vCenter + radius);
        minW = fminf(minW, wCenter - halfGap); maxW = fmaxf(maxW, wCenter + halfGap);
    }

    __shared__ float s_minU[256], s_maxU[256], s_minV[256], s_maxV[256], s_minW[256], s_maxW[256];
    s_minU[threadIdx.x] = minU; s_maxU[threadIdx.x] = maxU;
    s_minV[threadIdx.x] = minV; s_maxV[threadIdx.x] = maxV;
    s_minW[threadIdx.x] = minW; s_maxW[threadIdx.x] = maxW;
    __syncthreads();

    for (int stride = blockDim.x / 2; stride > 0; stride >>= 1) {
        if (threadIdx.x < stride) {
            s_minU[threadIdx.x] = fminf(s_minU[threadIdx.x], s_minU[threadIdx.x + stride]);
            s_maxU[threadIdx.x] = fmaxf(s_maxU[threadIdx.x], s_maxU[threadIdx.x + stride]);
            s_minV[threadIdx.x] = fminf(s_minV[threadIdx.x], s_minV[threadIdx.x + stride]);
            s_maxV[threadIdx.x] = fmaxf(s_maxV[threadIdx.x], s_maxV[threadIdx.x + stride]);
            s_minW[threadIdx.x] = fminf(s_minW[threadIdx.x], s_minW[threadIdx.x + stride]);
            s_maxW[threadIdx.x] = fmaxf(s_maxW[threadIdx.x], s_maxW[threadIdx.x + stride]);
        }
        __syncthreads();
    }

    if (threadIdx.x == 0) {
        float midU = 0.5f * (s_minU[0] + s_maxU[0]);
        float midV = 0.5f * (s_minV[0] + s_maxV[0]);
        float midW = 0.5f * (s_minW[0] + s_maxW[0]);

        float extentU = 0.5f * (s_maxU[0] - s_minU[0]);
        float extentV = 0.5f * (s_maxV[0] - s_minV[0]);
        float extentW = 0.5f * (s_maxW[0] - s_minW[0]);

        float3 center = make_float3(
            regCenterRef.x + midU * regU.x + midV * regV.x + midW * regNormal.x,
            regCenterRef.y + midU * regU.y + midV * regV.y + midW * regNormal.y,
            regCenterRef.z + midU * regU.z + midV * regV.z + midW * regNormal.z
        );

        uint32_t psiCh = (pairToPsiChannel && channelCount > 0) ? pairToPsiChannel[regChannelA * channelCount + regChannelB] : 0;

        heat::ContactRegion reg{};
        reg.centerExtentW = glm::vec4(center.x, center.y, center.z, extentW);
        reg.normalExtentU = glm::vec4(regNormal.x, regNormal.y, regNormal.z, extentU);
        reg.uAxisExtentV = glm::vec4(regU.x, regU.y, regU.z, extentV);
        reg.vAxis = glm::vec4(regV.x, regV.y, regV.z, 0.0f);
        reg.channelA = regChannelA;
        reg.channelB = regChannelB;
        reg.psiChannel = psiCh;
        outRegions[r] = reg;
    }
}

// 5. Emit single indirect instanced draw command
__global__ void writeIndirectCommandKernel(
    const uint32_t* actualRegionCount,
    VkDrawIndirectCommand* indirectCmd) {
    if (threadIdx.x == 0 && blockIdx.x == 0) {
        *indirectCmd = { 36, *actualRegionCount, 0, 0 };
    }
}

} // namespace heat::contactregion
