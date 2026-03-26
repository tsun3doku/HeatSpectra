#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <vulkan/vulkan.h>

#include "HeatSystemStageContext.hpp"

class HeatReceiverRuntime;

class HeatSystemSurfaceStage {
public:
    explicit HeatSystemSurfaceStage(const HeatSystemStageContext& stageContext);

    void refreshSurfaceDescriptors(uint32_t nodeCount);
    bool createDescriptorPool(uint32_t maxFramesInFlight);
    bool createDescriptorSetLayout();
    bool createPipeline();
    void dispatchSurfaceTemperatureUpdates(
        VkCommandBuffer commandBuffer,
        uint32_t nodeCount,
        const std::vector<std::unique_ptr<HeatReceiverRuntime>>& receivers,
        bool finalWritesBufferB) const;

private:
    HeatSystemStageContext context;
};
