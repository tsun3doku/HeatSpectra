#pragma once

#include "HeatContactRuntime.hpp"
#include "HeatSystemSimRuntime.hpp"
#include "HeatSystemRuntime.hpp"
#include "HeatSystemStageContext.hpp"
#include "HeatSystemVoronoiStage.hpp"

#include <memory>
#include <vector>

#include <vulkan/vulkan.h>

class HeatReceiverRuntime;
class HeatSystemContactStage;
class HeatSystemSurfaceStage;
struct HeatPackage;

class HeatSystemSimStage {
public:
    explicit HeatSystemSimStage(const HeatSystemStageContext& stageContext);

    void recordComputeCommands(
        VkCommandBuffer commandBuffer,
        uint32_t currentFrame,
        const HeatSystemSimRuntime& simRuntime,
        const HeatSourcePushConstant& basePushConstant,
        const std::vector<HeatContactRuntime::ContactCoupling>& contactCouplings,
        const std::vector<std::unique_ptr<HeatReceiverRuntime>>& receivers,
        const HeatPackage& heatPackage,
        const HeatSystemContactStage& contactStage,
        const HeatSystemVoronoiStage& voronoiStage,
        const HeatSystemSurfaceStage& surfaceStage,
        uint32_t maxNodeNeighbors,
        uint32_t numSubsteps) const;

private:
    HeatSystemStageContext context;
};
