#pragma once

#include "../HeatGpuStructs.hpp"

#include <cuda_runtime.h>
#include <vulkan/vulkan.h>
#include <cstdint>

namespace heat::contactregion {

__global__ void generateRegionKeysKernel(
    const heat::ContactFace* faces,
    uint32_t faceCount,
    float3 gridMin,
    float tileSize,
    uint3 tileDim,
    uint32_t channelCount,
    uint64_t* keys,
    uint32_t* faceIndices);

__global__ void markRegionBoundariesKernel(
    const uint64_t* sortedKeys,
    uint32_t faceCount,
    uint32_t* flags);

__global__ void finalizeRegionStartsKernel(
    const uint32_t* actualRegionCount,
    uint32_t* regionStarts,
    uint32_t faceCount);

__global__ void reduceRegionBoundsKernel(
    const heat::ContactFace* faces,
    const uint32_t* sortedFaceIndices,
    const uint32_t* regionStarts,
    const uint32_t* actualRegionCount,
    uint32_t channelCount,
    const uint32_t* pairToPsiChannel,
    heat::ContactRegion* outRegions);

__global__ void writeIndirectCommandKernel(
    const uint32_t* actualRegionCount,
    VkDrawIndirectCommand* indirectCmd);

} // namespace heat::contactregion
