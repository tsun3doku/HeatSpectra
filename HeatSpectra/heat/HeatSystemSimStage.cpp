#include "HeatSystemSimStage.hpp"

#include "HeatReceiverRuntime.hpp"
#include "HeatSourceRuntime.hpp"
#include "HeatSystemResources.hpp"
#include "HeatSystemSurfaceStage.hpp"
#include "HeatSystemVoronoiStage.hpp"
#include <glm/mat4x4.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <limits>

HeatSystemSimStage::HeatSystemSimStage(const HeatSystemStageContext& stageContext)
    : context(stageContext) {
}

void HeatSystemSimStage::recordComputeCommands(
    VkCommandBuffer commandBuffer,
    uint32_t currentFrame,
    const HeatSystemSimRuntime& simRuntime,
    const heat::SourcePushConstant& basePushConstant,
    const std::vector<std::unique_ptr<HeatReceiverRuntime>>& receivers,
    const HeatSystemVoronoiStage& voronoiStage,
    const HeatSystemSurfaceStage& surfaceStage,
    uint32_t maxNodeNeighbors,
    uint32_t numSubsteps) const {
    (void)currentFrame;

    const uint32_t nodeCount = simRuntime.getNodeCount();
    if (nodeCount == 0 || numSubsteps == 0) {
        return;
    }

    heat::SourcePushConstant pushConstant = basePushConstant;
    pushConstant.maxNodeNeighbors = maxNodeNeighbors;

    const uint32_t workGroupSize = 256;
    const uint32_t workGroupCount = (nodeCount + workGroupSize - 1) / workGroupSize;
    for (uint32_t substepIndex = 0; substepIndex < numSubsteps; ++substepIndex) {
        pushConstant.substepIndex = static_cast<uint32_t>(substepIndex);
        voronoiStage.dispatchDiffusionSubstep(
            commandBuffer,
            currentFrame,
            simRuntime,
            pushConstant,
            static_cast<int>(substepIndex),
            workGroupCount);
        voronoiStage.insertInterSubstepBarrier(
            commandBuffer,
            simRuntime,
            static_cast<int>(substepIndex),
            numSubsteps);
    }

    voronoiStage.insertFinalTemperatureBarrier(commandBuffer, simRuntime, numSubsteps);

    pushConstant.substepIndex = 0;
    surfaceStage.dispatchSurfaceTemperatureUpdates(
        commandBuffer,
        nodeCount,
        receivers,
        voronoiStage.finalSubstepWritesBufferB(numSubsteps));
}
