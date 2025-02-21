#include <algorithm>
#include "VulkanDevice.hpp"
#include "MemoryAllocator.hpp"

MemoryPool::MemoryPool(VulkanDevice& vulkanDevice, VkDeviceSize poolSize, VkBufferUsageFlags usage, VkMemoryPropertyFlags memProps)
    : vulkanDevice(vulkanDevice),
    memProperties(memProps),
    buffer(VK_NULL_HANDLE),
    memory(VK_NULL_HANDLE)
{

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = poolSize;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult result = vkCreateBuffer(vulkanDevice.getDevice(), &bufferInfo, nullptr, &buffer);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create buffer");
    }

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(vulkanDevice.getDevice(), buffer, &memReqs);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = vulkanDevice.findMemoryType(memReqs.memoryTypeBits, memProps);

    result = vkAllocateMemory(vulkanDevice.getDevice(), &allocInfo, nullptr, &memory);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate memory");
    }

    vkBindBufferMemory(vulkanDevice.getDevice(), buffer, memory, 0);

    if (memProps & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        vkMapMemory(vulkanDevice.getDevice(), memory, 0, poolSize, 0, &mappedPtr);
    }

    blocks.push_back({ 0, poolSize, true });
}

MemoryPool::~MemoryPool() {
    if (mappedPtr) {
        vkUnmapMemory(vulkanDevice.getDevice(), memory);
    }
    if (buffer) vkDestroyBuffer(vulkanDevice.getDevice(), buffer, nullptr);
    if (memory) vkFreeMemory(vulkanDevice.getDevice(), memory, nullptr);
}

MemoryAllocator::~MemoryAllocator() {
    // Pools will self-destroy through their destructors
}

std::pair<VkBuffer, VkDeviceSize> MemoryAllocator::allocate(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memProps, VkDeviceSize alignment) {
    std::lock_guard<std::mutex> lock(allocationMutex);

    // Apply alignment
    if (alignment > 0) {
        size = (size + alignment - 1) & ~(alignment - 1);
    }

    auto key = std::make_pair(usage, memProps);

    while (true) {
        auto& poolVector = pools[key];

        // Iterate through unique_ptrs
        for (auto& poolPtr : poolVector) {
            MemoryPool& pool = *poolPtr;
            for (auto it = pool.blocks.begin(); it != pool.blocks.end(); ++it) {
                if (it->isFree && it->size >= size) {
                    // Save the allocation offset
                    VkDeviceSize allocOffset = it->offset;

                    if (it->size > size) {
                        // Adjust the current free block to account for the allocation
                        it->offset += size;
                        it->size -= size;
                    }
                    else {
                        // If the block exactly fits, mark it as allocated
                        it->isFree = false;
                    }

                    return { pool.buffer, allocOffset };
                }
            }
        }
        createNewPool(size, usage, memProps);
    }
}

MemoryPool& MemoryAllocator::createNewPool(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memProps) {
    auto key = std::make_pair(usage, memProps);
    VkDeviceSize poolSize = std::max(DEFAULT_POOL_SIZE, size);
    pools[key].push_back(std::make_unique<MemoryPool>(vulkanDevice, poolSize, usage, memProps));
    return *pools[key].back();
}

void MemoryAllocator::defragment() {
    for (auto& pair : pools) {
        auto& poolVector = pair.second;
        for (auto& poolPtr : poolVector) {
            MemoryPool& pool = *poolPtr;
            std::sort(pool.blocks.begin(), pool.blocks.end(), [](const Suballocation& a, const Suballocation& b) {
                return a.offset < b.offset;
                });
            mergeFreeBlocks(pool);
        }
    }
}

AllocatorStats MemoryAllocator::getStats() {
    AllocatorStats stats = {};
    for (auto& pair : pools) {
        auto& poolVector = pair.second;
        for (auto& poolPtr : poolVector) {
            MemoryPool& pool = *poolPtr;
            stats.totalAllocated += pool.blocks[0].size; 
            for (const auto& block : pool.blocks) {
                if (!block.isFree) {
                    stats.usedBytes += block.size;
                    stats.allocationCount++;
                }
            }
        }
    }
    return stats;
}

void* MemoryAllocator::getMappedPointer(VkBuffer buffer, VkDeviceSize offset) {
    // Iterate over each pool group
    for (auto& poolPair : pools) {
        // Iterate over each pool in the vector
        for (auto& poolPtr : poolPair.second) {
            MemoryPool& pool = *poolPtr;
            if (pool.buffer == buffer) {
                if (pool.mappedPtr) {
                    return static_cast<char*>(pool.mappedPtr) + offset;
                }
                else {
                    return nullptr;
                }
            }
        }
    }
    return nullptr; 
}

void MemoryAllocator::free(VkBuffer buffer, VkDeviceSize offset) {
    std::lock_guard<std::mutex> lock(allocationMutex); 

    // Find the pool containing this buffer
    for (auto& pair : pools) {
        auto& poolVector = pair.second;
        for (auto& poolPtr : poolVector) {
            MemoryPool& pool = *poolPtr;
            if (pool.buffer == buffer) {
                for (auto& block : pool.blocks) {
                    if (block.offset == offset && !block.isFree) {
                        block.isFree = true;
                        mergeFreeBlocks(pool);
                        return;
                    }
                }
            }
        }
    }
}

void MemoryAllocator::mergeFreeBlocks(MemoryPool& pool) {
    std::vector<Suballocation> mergedBlocks;

    for (auto& block : pool.blocks) {
        if (!mergedBlocks.empty() &&
            mergedBlocks.back().isFree &&
            block.isFree &&
            (mergedBlocks.back().offset + mergedBlocks.back().size) == block.offset) {
            // Merge consecutive free blocks
            mergedBlocks.back().size += block.size;
        }
        else {
            mergedBlocks.push_back(block);
        }
    }

    pool.blocks = mergedBlocks;
}