#include "VkFrameGraphBackend.hpp"

#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/VulkanDevice.hpp"

VkFrameGraphBackend::VkFrameGraphBackend(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator)
    : vulkanDevice(vulkanDevice),
      memoryAllocator(memoryAllocator) {
}

bool VkFrameGraphBackend::rebuild(const framegraph::FrameGraphResult& result, const std::vector<VkImageView>& swapChainImageViews, VkExtent2D extent, uint32_t maxFramesInFlight) {
    return runtime.rebuild(vulkanDevice, memoryAllocator, result, swapChainImageViews, extent, maxFramesInFlight);
}

void VkFrameGraphBackend::cleanup(uint32_t maxFramesInFlight) {
    runtime.cleanup(vulkanDevice, memoryAllocator, maxFramesInFlight);
}
