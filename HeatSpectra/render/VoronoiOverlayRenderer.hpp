#pragma once

#include "runtime/VoronoiDisplayController.hpp"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

class PointRenderer;
class UniformBufferManager;
class VulkanDevice;
class VoronoiRenderer;
class CommandPool;

namespace render {

class VoronoiOverlayRenderer {
public:
    VoronoiOverlayRenderer(VulkanDevice& device, UniformBufferManager& uniformBufferManager, CommandPool& renderCommandPool);
    ~VoronoiOverlayRenderer();

    void initialize(VkRenderPass renderPass, uint32_t subpass, uint32_t maxFramesInFlight);
    void apply(uint64_t socketKey, const VoronoiDisplayController::Config& config);
    void remove(uint64_t socketKey);
    void renderSurface(VkCommandBuffer commandBuffer, uint32_t frameIndex);
    void renderPoints(VkCommandBuffer commandBuffer, uint32_t frameIndex, VkExtent2D extent);
    void cleanup();

private:
    std::unique_ptr<VoronoiRenderer> voronoiRenderer;
    std::unique_ptr<PointRenderer> pointRenderer;
    std::unordered_map<uint64_t, VoronoiDisplayController::Config> configsBySocket;
    bool initialized = false;
};

} 
