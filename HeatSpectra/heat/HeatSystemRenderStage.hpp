#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include <vulkan/vulkan.h>

#include "HeatSystemRuntime.hpp"
#include "HeatSystemStageContext.hpp"

class ContactLineRenderer;
class HeatReceiverRuntime;
class HeatReceiverRenderer;
class HeatSourceRenderer;

class HeatSystemRenderStage {
public:
    explicit HeatSystemRenderStage(const HeatSystemStageContext& stageContext);

    void renderContactLines(VkCommandBuffer cmdBuffer, uint32_t frameIndex, VkExtent2D extent, ContactLineRenderer* contactLineRenderer) const;
    void renderHeatOverlay(
        VkCommandBuffer cmdBuffer,
        uint32_t frameIndex,
        HeatSourceRenderer* heatSourceRenderer,
        HeatReceiverRenderer* heatReceiverRenderer,
        const std::vector<HeatSystemRuntime::SourceBinding>& sourceBindings,
        const std::vector<std::unique_ptr<HeatReceiverRuntime>>& receivers,
        bool isActive,
        bool isPaused) const;
    void renderSurfels(
        VkCommandBuffer cmdBuffer,
        uint32_t frameIndex,
        float radius,
        const std::vector<HeatSystemRuntime::SourceBinding>& sourceBindings,
        const std::vector<std::unique_ptr<HeatReceiverRuntime>>& receivers) const;

private:
    HeatSystemStageContext context;
};
