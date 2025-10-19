#pragma once

#include "VulkanDevice.hpp" 
#include <mutex>

// Thread-safe command pool wrapper
// Create one per thread that needs to allocate command buffers
class CommandPool {
public:
    CommandPool(VulkanDevice& device, const char* debugName = "CommandPool");
    ~CommandPool();
    
    // Begin a one-time command buffer (mutex-protected for thread safety)
    VkCommandBuffer beginCommands();
    
    // End and submit a one-time command buffer (mutex-protected for thread safety)
    void endCommands(VkCommandBuffer commandBuffer);
    
    // Get the raw Vulkan command pool handle (for direct access if needed)
    VkCommandPool getHandle() const { return pool; }
    
    // Helper: Copy buffer with this pool
    void copyBuffer(VkBuffer srcBuffer, VkDeviceSize srcOffset, 
                    VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize size);
    
    // Helper: Copy buffer to image with this pool
    void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
    
    // Helper: Transition image layout with this pool
    void transitionImageLayout(VkImage image, VkFormat format, 
                               VkImageLayout oldLayout, VkImageLayout newLayout);
    
private:
    VulkanDevice& vulkanDevice;
    VkCommandPool pool;
    std::mutex poolMutex;  // Thread-safe access to this pool
    const char* debugName;
    
    // Global queue submission mutex (shared across all CommandPools)
    static std::mutex queueSubmitMutex;
};

// All legacy functions removed - use CommandPool class methods instead
