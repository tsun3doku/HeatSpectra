#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <vulkan/vulkan.h>

#include "voronoi/VoronoiStageContext.hpp"

class VoronoiModelRuntime;

class VoronoiSurfaceStage {
public:
    explicit VoronoiSurfaceStage(const VoronoiStageContext& stageContext);

    bool createDescriptorPool(uint32_t maxFramesInFlight);
    bool createDescriptorSetLayout();
    bool createPipeline();
    void dispatchSurfaceTemperatureUpdates(
        VkCommandBuffer commandBuffer,
        uint32_t nodeCount,
        const std::vector<std::unique_ptr<VoronoiModelRuntime>>& modelRuntimes,
        bool finalWritesBufferB) const;

private:
    VoronoiStageContext context;
};
