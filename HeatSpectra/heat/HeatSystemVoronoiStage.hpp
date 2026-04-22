#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.h>

#include "HeatSystemPresets.hpp"
#include "HeatSystemStageContext.hpp"
#include "util/Structs.hpp"
#include "voronoi/VoronoiDomain.hpp"

class PointRenderer;
class HeatSystemSimRuntime;

class HeatSystemVoronoiStage {
public:
    explicit HeatSystemVoronoiStage(const HeatSystemStageContext& stageContext);

    void dispatchDiffusionSubstep(
        VkCommandBuffer commandBuffer,
        uint32_t currentFrame,
        const HeatSystemSimRuntime& simRuntime,
        const HeatSourcePushConstant& basePushConstant,
        int substepIndex,
        uint32_t workGroupCount) const;
    void insertInterSubstepBarrier(
        VkCommandBuffer commandBuffer,
        const HeatSystemSimRuntime& simRuntime,
        int substepIndex,
        uint32_t numSubsteps) const;
    void insertFinalTemperatureBarrier(
        VkCommandBuffer commandBuffer,
        const HeatSystemSimRuntime& simRuntime,
        uint32_t numSubsteps) const;
    bool finalSubstepWritesBufferB(uint32_t numSubsteps) const;
    bool createDescriptorPool(uint32_t maxFramesInFlight);
    bool createDescriptorSetLayout();
    bool createDescriptorSets(uint32_t maxFramesInFlight, const HeatSystemSimRuntime& simRuntime);
    bool createPipeline();

    HeatSystemStageContext context;
};
