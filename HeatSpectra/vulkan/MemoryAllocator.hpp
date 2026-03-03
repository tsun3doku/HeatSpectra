#pragma once
#include <vector>
#include <memory>
#include <mutex>
#include <map>
#include <unordered_set>

#include "util/Structs.hpp"

class VulkanDevice;

class MemoryPool {
public:
    MemoryPool(VulkanDevice& vulkanDevice, VkDeviceSize poolSize, VkBufferUsageFlags usage, VkMemoryPropertyFlags memProps);
    ~MemoryPool();

    VkBuffer buffer;
    VkDeviceMemory memory;
    VkMemoryPropertyFlags memProperties;

    std::vector<Suballocation> blocks;
    void* mappedPtr = nullptr;

    // Delete copy operations
    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;

private:
    VulkanDevice& vulkanDevice;
};

class MemoryAllocator {
public:
    explicit MemoryAllocator(VulkanDevice& vulkanDevice) : vulkanDevice(vulkanDevice) {}
    ~MemoryAllocator();

    std::pair<VkBuffer, VkDeviceSize> allocate(VkDeviceSize size,VkBufferUsageFlags usage, VkMemoryPropertyFlags memProps, VkDeviceSize alignment = 1);
    VkDeviceMemory allocateImageMemory(VkImage image, VkMemoryPropertyFlags memProps);
    
    void defragment();
    void free(VkBuffer buffer, VkDeviceSize offset);
    void freeImageMemory(VkDeviceMemory memory);

    AllocatorStats getStats();
    void* getMappedPointer(VkBuffer buffer, VkDeviceSize offset);

private:
    VulkanDevice& vulkanDevice;
    std::map<std::pair<VkBufferUsageFlags, VkMemoryPropertyFlags>, std::vector<std::unique_ptr<MemoryPool>>> pools;
    std::unordered_set<VkDeviceMemory> imageMemories;
    const VkDeviceSize DEFAULT_POOL_SIZE = 256 * 1024 * 1024; // 256MB
    std::mutex allocationMutex;

    MemoryPool& createNewPool(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memProps);
    void mergeFreeBlocks(MemoryPool& pool);
};
