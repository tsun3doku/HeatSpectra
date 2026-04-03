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
        HeatContactRuntime::ContactCoupling& coupling,
        const class HeatSystemSimRuntime& simRuntime);
    void dispatchCoupling(
        VkCommandBuffer commandBuffer,
        const HeatContactRuntime::ContactCoupling& coupling,
        const std::vector<HeatSystemRuntime::SourceBinding>& sourceBindings,
        bool evenSubstep) const;
    void insertInjectionBarrier(
        VkCommandBuffer commandBuffer,
        const class HeatSystemSimRuntime& simRuntime) const;

private:
    bool ensureParamsBuffer(HeatContactRuntime::ContactCoupling& coupling);
    const HeatSystemRuntime::SourceBinding* findSourceBindingByRuntimeModelId(
        const std::vector<HeatSystemRuntime::SourceBinding>& sourceBindings,
        uint32_t runtimeModelId) const;
    HeatSystemStageContext context;
};
