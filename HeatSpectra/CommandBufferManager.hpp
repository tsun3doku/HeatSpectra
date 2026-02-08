#pragma once

#include "VulkanDevice.hpp" 
#include <mutex>

class CommandPool {
public:
    CommandPool(VulkanDevice& device, const char* debugName = "CommandPool");
    ~CommandPool();
    
    VkCommandBuffer beginCommands();
    
    void endCommands(VkCommandBuffer commandBuffer);
    
    VkCommandPool getHandle() const { return pool; }
    
    void copyBuffer(VkBuffer srcBuffer, VkDeviceSize srcOffset, 
                    VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize size);
    
    void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
    
    void transitionImageLayout(VkImage image, VkFormat format, 
                               VkImageLayout oldLayout, VkImageLayout newLayout);
    
private:
    VulkanDevice& vulkanDevice;
    VkCommandPool pool;
    std::mutex poolMutex;  
    const char* debugName;
    
    static std::mutex queueSubmitMutex;
};
