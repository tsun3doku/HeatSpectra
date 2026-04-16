#pragma once

#include "renderers/ContactLineRenderer.hpp"
#include "runtime/ContactDisplayController.hpp"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

class MemoryAllocator;
class UniformBufferManager;
class VulkanDevice;

namespace render {

class ContactOverlayRenderer {
public:
    ContactOverlayRenderer(
        VulkanDevice& device,
        MemoryAllocator& allocator,
        UniformBufferManager& uniformBufferManager);
    ~ContactOverlayRenderer();

    void initialize(VkRenderPass renderPass, uint32_t subpass, uint32_t maxFramesInFlight);
    void apply(uint64_t socketKey, const ContactDisplayController::Config& config);
    void remove(uint64_t socketKey);
    void render(VkCommandBuffer commandBuffer, uint32_t frameIndex, VkExtent2D extent);
    void cleanup();

private:
    void rebuildLineBuffers();

    std::unique_ptr<ContactLineRenderer> lineRenderer;
    std::unordered_map<uint64_t, ContactDisplayController::Config> configsBySocket;
    bool initialized = false;
};

} // namespace render
