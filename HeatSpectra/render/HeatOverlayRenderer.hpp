#pragma once

#include "renderers/HeatReceiverRenderer.hpp"
#include "renderers/HeatSourceRenderer.hpp"
#include "runtime/HeatDisplayController.hpp"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

class MemoryAllocator;
class UniformBufferManager;
class VulkanDevice;

namespace render {

class HeatOverlayRenderer {
public:
    HeatOverlayRenderer(
        VulkanDevice& device,
        MemoryAllocator& allocator,
        UniformBufferManager& uniformBufferManager);
    ~HeatOverlayRenderer();

    void initialize(VkRenderPass renderPass, uint32_t maxFramesInFlight);
    void apply(uint64_t socketKey, const HeatDisplayController::Config& config);
    void remove(uint64_t socketKey);
    void render(VkCommandBuffer commandBuffer, uint32_t frameIndex);
    void cleanup();

private:
    void rebuildBindings();

    MemoryAllocator& memoryAllocator;
    std::unique_ptr<HeatSourceRenderer> sourceRenderer;
    std::unique_ptr<HeatReceiverRenderer> receiverRenderer;
    std::unordered_map<uint64_t, HeatDisplayController::Config> configsBySocket;
    std::vector<HeatSourceRenderer::SourceRenderBinding> sourceBindings;
    std::vector<HeatReceiverRenderer::ReceiverRenderBinding> receiverBindings;
    uint32_t maxFramesInFlight = 0;
    bool initialized = false;
};

}
