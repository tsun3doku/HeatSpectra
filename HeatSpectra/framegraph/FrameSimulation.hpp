#pragma once

#include <vulkan/vulkan.h>
#include <glm/mat4x4.hpp>

#include <cstdint>
#include <vector>

class FrameSimulation {
public:
    virtual ~FrameSimulation() = default;

    virtual void processResetRequest() = 0;
    virtual void update() = 0;
    virtual void recreateResources(uint32_t maxFramesInFlight, VkExtent2D extent, VkRenderPass renderPass) = 0;

    virtual bool hasDispatchableComputeWork() const = 0;
    virtual const std::vector<VkCommandBuffer>& getComputeCommandBuffers() const = 0;
    virtual void recordComputeCommands(VkCommandBuffer commandBuffer, uint32_t currentFrame, VkQueryPool timingQueryPool, uint32_t timingQueryBase) = 0;

    virtual bool simulationActive() const = 0;
    virtual bool simulationPaused() const = 0;
    virtual bool voronoiReady() const = 0;

    virtual void renderSurfels(VkCommandBuffer cmdBuffer, uint32_t frameIndex, const glm::mat4& heatSourceModel, float radius) = 0;
    virtual void renderVoronoiSurface(VkCommandBuffer cmdBuffer, uint32_t frameIndex) = 0;
    virtual void renderHeatOverlay(VkCommandBuffer cmdBuffer, uint32_t frameIndex) = 0;
    virtual void renderOccupancy(VkCommandBuffer cmdBuffer, uint32_t frameIndex, VkExtent2D extent) = 0;
    virtual void renderContactLines(VkCommandBuffer cmdBuffer, uint32_t frameIndex, VkExtent2D extent) = 0;
};
