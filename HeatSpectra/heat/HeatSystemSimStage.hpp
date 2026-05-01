#pragma once

#include "HeatSystemSimRuntime.hpp"
#include "HeatSystemStageContext.hpp"
#include "heat/HeatGpuStructs.hpp"
#include "util/Structs.hpp"

#include <memory>
#include <vector>

#include <vulkan/vulkan.h>

class HeatReceiverRuntime;
class HeatSystemSurfaceStage;
class HeatSystemVoronoiStage;

class HeatSystemSimStage {
public:
    explicit HeatSystemSimStage(const HeatSystemStageContext& stageContext);

    void recordComputeCommands(
        VkCommandBuffer commandBuffer,
        uint32_t currentFrame,
        const HeatSystemSimRuntime& simRuntime,
        const heat::SourcePushConstant& basePushConstant,
        const std::vector<std::unique_ptr<HeatReceiverRuntime>>& receivers,
        const HeatSystemVoronoiStage& voronoiStage,
        const HeatSystemSurfaceStage& surfaceStage,
        uint32_t maxNodeNeighbors,
        uint32_t numSubsteps) const;

private:
    HeatSystemStageContext context;
};
