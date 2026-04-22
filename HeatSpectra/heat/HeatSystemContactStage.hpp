#pragma once

#include <cstdint>
#include "HeatContactRuntime.hpp"
#include "HeatSystemStageContext.hpp"

class HeatSystemContactStage {
public:
    explicit HeatSystemContactStage(const HeatSystemStageContext& stageContext);

    bool createDescriptorPool(uint32_t maxFramesInFlight);
    bool createDescriptorSetLayout();
    bool createPipeline();
    void updateCouplingDescriptors(
        HeatContactRuntime::CouplingState& coupling,
        const class HeatSystemSimRuntime& simRuntime);
    void dispatchCoupling(
        VkCommandBuffer commandBuffer,
        const HeatContactRuntime::CouplingState& coupling,
        const std::vector<HeatSystemRuntime::SourceBinding>& sourceBindings,
        bool evenSubstep) const;
    void insertInjectionBarrier(
        VkCommandBuffer commandBuffer,
        const class HeatSystemSimRuntime& simRuntime) const;

private:
    bool ensureParamsBuffer(HeatContactRuntime::CouplingState& coupling);
    const HeatSystemRuntime::SourceBinding* findSourceBindingByRuntimeModelId(
        const std::vector<HeatSystemRuntime::SourceBinding>& sourceBindings,
        uint32_t runtimeModelId) const;
    HeatSystemStageContext context;
};
