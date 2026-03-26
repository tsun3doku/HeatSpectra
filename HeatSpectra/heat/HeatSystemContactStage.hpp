#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "HeatContactRuntime.hpp"
#include "HeatSystemStageContext.hpp"

struct HeatPackage;
class HeatReceiverRuntime;
class VoronoiModelRuntime;

class HeatSystemContactStage {
public:
    explicit HeatSystemContactStage(const HeatSystemStageContext& stageContext);

    void refreshCouplings();
    bool createDescriptorPool(uint32_t maxFramesInFlight);
    bool createDescriptorSetLayout();
    bool createPipeline();
    void updateCouplingDescriptors(
        HeatContactRuntime::ContactCoupling& coupling,
        const class HeatSystemSimRuntime& simRuntime,
        const HeatPackage& heatPackage,
        const std::vector<std::unique_ptr<HeatReceiverRuntime>>& receivers,
        const std::vector<std::unique_ptr<VoronoiModelRuntime>>& voronoiModelRuntimes);
    void dispatchCoupling(
        VkCommandBuffer commandBuffer,
        const HeatContactRuntime::ContactCoupling& coupling,
        bool evenSubstep) const;
    void insertInjectionBarrier(
        VkCommandBuffer commandBuffer,
        const class HeatSystemSimRuntime& simRuntime) const;

private:
    bool ensureParamsBuffer(HeatContactRuntime::ContactCoupling& coupling);
    const HeatReceiverRuntime* findReceiverRuntime(
        const std::vector<std::unique_ptr<HeatReceiverRuntime>>& receivers,
        uint32_t runtimeModelId) const;
    const VoronoiModelRuntime* findVoronoiModelRuntime(
        const std::vector<std::unique_ptr<VoronoiModelRuntime>>& voronoiModelRuntimes,
        uint32_t runtimeModelId) const;
    HeatSystemStageContext context;
};
