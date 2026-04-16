#include "HeatSystemSimStage.hpp"

#include "HeatSystemContactStage.hpp"
#include "HeatReceiverRuntime.hpp"
#include "HeatSourceRuntime.hpp"
#include "HeatSystemResources.hpp"
#include "HeatSystemSurfaceStage.hpp"
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
    const HeatSourcePushConstant& basePushConstant,
    const std::vector<HeatContactRuntime::ContactCoupling>& contactCouplings,
    const std::vector<HeatSystemRuntime::SourceBinding>& sourceBindings,
    const std::vector<std::unique_ptr<HeatReceiverRuntime>>& receivers,
    const HeatSystemContactStage& contactStage,
    const HeatSystemVoronoiStage& voronoiStage,
    const HeatSystemSurfaceStage& surfaceStage,
    uint32_t maxNodeNeighbors,
    uint32_t numSubsteps) const {
    (void)currentFrame;

    const uint32_t nodeCount = simRuntime.getNodeCount();
    if (nodeCount == 0 || numSubsteps == 0) {
        return;
    }

    HeatSourcePushConstant pushConstant = basePushConstant;
    pushConstant.maxNodeNeighbors = maxNodeNeighbors;

    const uint32_t workGroupSize = 256;
    const uint32_t workGroupCount = (nodeCount + workGroupSize - 1) / workGroupSize;
    for (uint32_t substepIndex = 0; substepIndex < numSubsteps; ++substepIndex) {
        const bool evenSubstep = ((substepIndex % 2) == 0);
        for (const HeatContactRuntime::ContactCoupling& coupling : contactCouplings) {
            contactStage.dispatchCoupling(commandBuffer, coupling, sourceBindings, evenSubstep);
        }
        if (!contactCouplings.empty()) {
            contactStage.insertInjectionBarrier(commandBuffer, simRuntime);
        }

        pushConstant.substepIndex = substepIndex;
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
