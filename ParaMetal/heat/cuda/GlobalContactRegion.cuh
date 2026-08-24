#pragma once

#include "../HeatGpuStructs.hpp"

#include <cuda_runtime.h>
#include <vulkan/vulkan.h>
#include <cstdint>
#include <memory>

namespace heat {

struct ContactRegionBuildInput {
    const heat::ContactFace* d_faces = nullptr;
    uint32_t faceCount = 0;
    uint32_t channelCount = 0;
    float3 gridMin{};
    float tileSize = 0.0f;
    uint3 tileDim{};
    const uint32_t* d_pairToPsiChannel = nullptr;
};

struct ContactRegionBuildOutput {
    heat::ContactRegion* d_regions = nullptr;
    VkDrawIndirectCommand* d_indirectCmd = nullptr;
};

class GlobalContactRegion {
public:
    GlobalContactRegion();
    ~GlobalContactRegion();

    GlobalContactRegion(const GlobalContactRegion&) = delete;
    GlobalContactRegion& operator=(const GlobalContactRegion&) = delete;

    bool build(const ContactRegionBuildInput& input, const ContactRegionBuildOutput& output, cudaStream_t stream = nullptr);
    void cleanup();

private:
    class Implementation;
    std::unique_ptr<Implementation> implementation;
};

} // namespace heat
