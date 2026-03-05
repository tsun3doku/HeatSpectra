#pragma once

#include <cstdint>

#include "HeatSystemStageContext.hpp"

class HeatSystemSurfaceStage {
public:
    explicit HeatSystemSurfaceStage(const HeatSystemStageContext& stageContext);

    void refreshSurfaceDescriptors(uint32_t nodeCount);
    bool createDescriptorPool(uint32_t maxFramesInFlight);
    bool createDescriptorSetLayout();
    bool createPipeline();

private:
    HeatSystemStageContext context;
};
