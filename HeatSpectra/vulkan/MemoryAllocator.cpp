#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <limits>
#include "VulkanDevice.hpp"
#include "MemoryAllocator.hpp"

namespace {
VkDeviceSize alignUp(VkDeviceSize value, VkDeviceSize alignment) {
    if (alignment <= 1) {
        return value;
    }
    return ((value + alignment - 1) / alignment) * alignment;
}

VkDeviceSize deriveRequiredAlignment(
    const VulkanDevice& vulkanDevice,
    VkBufferUsageFlags usage,
    VkDeviceSize requestedAlignment) {
    VkDeviceSize requiredAlignment = requestedAlignment > 0 ? requestedAlignment : 1;

    const VkPhysicalDeviceProperties properties = vulkanDevice.getPhysicalDeviceProperties();
    const VkPhysicalDeviceLimits& limits = properties.limits;

    if ((usage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT) != 0) {
        requiredAlignment = std::max(requiredAlignment, limits.minUniformBufferOffsetAlignment);
    }
    if ((usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) != 0) {
        requiredAlignment = std::max(requiredAlignment, limits.minStorageBufferOffsetAlignment);
    }
    if ((usage & (VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT)) != 0) {
        requiredAlignment = std::max(requiredAlignment, limits.minTexelBufferOffsetAlignment);
    }

    return requiredAlignment;
}
}

MemoryPool::MemoryPool(VulkanDevice& vulkanDevice, VkDeviceSize poolSize, VkBufferUsageFlags usage, VkMemoryPropertyFlags memProps)
    : vulkanDevice(vulkanDevice), memProperties(memProps), buffer(VK_NULL_HANDLE), memory(VK_NULL_HANDLE)
{

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = poolSize;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult result = vkCreateBuffer(vulkanDevice.getDevice(), &bufferInfo, nullptr, &buffer);
    if (result != VK_SUCCESS) {
        std::cerr << "[MemoryPool] Failed to create buffer" << std::endl;
        buffer = VK_NULL_HANDLE;
        return;
    }

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(vulkanDevice.getDevice(), buffer, &memReqs);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = vulkanDevice.findMemoryType(memReqs.memoryTypeBits, memProps);

    result = vkAllocateMemory(vulkanDevice.getDevice(), &allocInfo, nullptr, &memory);
    if (result != VK_SUCCESS) {
        std::cerr << "[MemoryPool] Failed to allocate memory" << std::endl;
        vkDestroyBuffer(vulkanDevice.getDevice(), buffer, nullptr);
        buffer = VK_NULL_HANDLE;
        memory = VK_NULL_HANDLE;
        return;
    }

    vkBindBufferMemory(vulkanDevice.getDevice(), buffer, memory, 0);

    if (memProps & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        vkMapMemory(vulkanDevice.getDevice(), memory, 0, poolSize, 0, &mappedPtr);
    }

    blocks.push_back({ 0, poolSize, true });
}

MemoryPool::~MemoryPool() {
    if (buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vulkanDevice.getDevice(), buffer, nullptr);
        buffer = VK_NULL_HANDLE;
    }

    if (memory != VK_NULL_HANDLE) {
        vkFreeMemory(vulkanDevice.getDevice(), memory, nullptr);
        memory = VK_NULL_HANDLE;
    }
}

MemoryAllocator::~MemoryAllocator() {
    std::lock_guard<std::mutex> lock(allocationMutex);

    for (VkDeviceMemory imageMemory : imageMemories) {
        if (imageMemory != VK_NULL_HANDLE) {
            vkFreeMemory(vulkanDevice.getDevice(), imageMemory, nullptr);
        }
    }
    imageMemories.clear();

    for (auto& pair : pools) {
        pair.second.clear();
    }
    pools.clear();
}

std::pair<VkBuffer, VkDeviceSize> MemoryAllocator::allocate(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memProps, VkDeviceSize alignment) {
    std::lock_guard<std::mutex> lock(allocationMutex);

    const VkDeviceSize requiredAlignment = deriveRequiredAlignment(vulkanDevice, usage, alignment);

    auto key = std::make_pair(usage, memProps);

    while (true) {
        auto& poolVector = pools[key];

        // Best fit allocation: find smallest block that fits
        for (auto& poolPtr : poolVector) {
            MemoryPool& pool = *poolPtr;

            VkDeviceSize minRemainingBytes = std::numeric_limits<VkDeviceSize>::max();
            size_t bestIndex = std::numeric_limits<size_t>::max();
            VkDeviceSize bestAlignedOffset = 0;

            for (size_t index = 0; index < pool.blocks.size(); ++index) {
                const Suballocation& block = pool.blocks[index];
                if (!block.isFree) {
                    continue;
                }

                const VkDeviceSize alignedOffset = alignUp(block.offset, requiredAlignment);
                const VkDeviceSize prefixPadding = alignedOffset - block.offset;
                if (prefixPadding > block.size || size > (block.size - prefixPadding)) {
                    continue;
                }

                const VkDeviceSize consumed = prefixPadding + size;
                const VkDeviceSize remainingBytes = block.size - consumed;
                if (remainingBytes < minRemainingBytes) {
                    minRemainingBytes = remainingBytes;
                    bestIndex = index;
                    bestAlignedOffset = alignedOffset;
                }
            }

            if (bestIndex != std::numeric_limits<size_t>::max()) {
                const Suballocation freeBlock = pool.blocks[bestIndex];
                const VkDeviceSize blockEnd = freeBlock.offset + freeBlock.size;
                const VkDeviceSize allocatedOffset = bestAlignedOffset;
                const VkDeviceSize allocatedEnd = allocatedOffset + size;

                std::vector<Suballocation> replacement;
                replacement.reserve(3);

                if (allocatedOffset > freeBlock.offset) {
                    replacement.push_back({
                        freeBlock.offset,
                        allocatedOffset - freeBlock.offset,
                        true
                    });
                }

                replacement.push_back({
                    allocatedOffset,
                    size,
                    false
                });

                if (allocatedEnd < blockEnd) {
                    replacement.push_back({
                        allocatedEnd,
                        blockEnd - allocatedEnd,
                        true
                    });
                }

                pool.blocks.erase(pool.blocks.begin() + static_cast<std::ptrdiff_t>(bestIndex));
                pool.blocks.insert(
                    pool.blocks.begin() + static_cast<std::ptrdiff_t>(bestIndex),
                    replacement.begin(),
                    replacement.end());

                return { pool.buffer, allocatedOffset };
            }
        }
        createNewPool(size, usage, memProps);
    }
}

VkDeviceMemory MemoryAllocator::allocateImageMemory(VkImage image, VkMemoryPropertyFlags memProps) {
    if (image == VK_NULL_HANDLE) {
        std::cerr << "[MemoryAllocator] Cannot allocate memory for null image" << std::endl;
        return VK_NULL_HANDLE;
    }

    VkMemoryRequirements memReqs{};
    vkGetImageMemoryRequirements(vulkanDevice.getDevice(), image, &memReqs);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = vulkanDevice.findMemoryType(memReqs.memoryTypeBits, memProps);

    VkDeviceMemory memory = VK_NULL_HANDLE;
    if (vkAllocateMemory(vulkanDevice.getDevice(), &allocInfo, nullptr, &memory) != VK_SUCCESS) {
        std::cerr << "[MemoryAllocator] Failed to allocate image memory" << std::endl;
        return VK_NULL_HANDLE;
    }

    if (vkBindImageMemory(vulkanDevice.getDevice(), image, memory, 0) != VK_SUCCESS) {
        std::cerr << "[MemoryAllocator] Failed to bind image memory" << std::endl;
        vkFreeMemory(vulkanDevice.getDevice(), memory, nullptr);
        return VK_NULL_HANDLE;
    }

    {
        std::lock_guard<std::mutex> lock(allocationMutex);
        imageMemories.insert(memory);
    }

    return memory;
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
            VkDeviceSize poolSize = 0;
            for (const auto& block : pool.blocks) {
                poolSize += block.size;
            }
            stats.totalAllocated += poolSize;
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

void MemoryAllocator::freeImageMemory(VkDeviceMemory memory) {
    if (memory == VK_NULL_HANDLE) {
        return;
    }

    std::lock_guard<std::mutex> lock(allocationMutex);
    const auto it = imageMemories.find(memory);
    if (it == imageMemories.end()) {
        return;
    }

    vkFreeMemory(vulkanDevice.getDevice(), memory, nullptr);
    imageMemories.erase(it);
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
