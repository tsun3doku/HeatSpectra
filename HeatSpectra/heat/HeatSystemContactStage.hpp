#pragma once

#include <cstdint>

#include "HeatSystemStageContext.hpp"
#include "HeatSystemRuntime.hpp"

class HeatSystemContactStage {
public:
    explicit HeatSystemContactStage(const HeatSystemStageContext& stageContext);

    void refreshCouplings();
    bool createDescriptorPool(uint32_t maxFramesInFlight);
    bool createDescriptorSetLayout();
    bool createPipeline();
    void updateCouplingDescriptors(HeatSystemRuntime::ContactCoupling& coupling, uint32_t nodeCount);

private:
    bool ensureParamsBuffer(HeatSystemRuntime::ContactCoupling& coupling);
    HeatSystemStageContext context;
};
