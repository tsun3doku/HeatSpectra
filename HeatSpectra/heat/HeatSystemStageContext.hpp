#pragma once

class VulkanDevice;
class MemoryAllocator;
class ResourceManager;
class UniformBufferManager;
class CommandPool;
class HeatSystemResources;

struct HeatSystemStageContext {
    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    ResourceManager& resourceManager;
    UniformBufferManager& uniformBufferManager;
    CommandPool& renderCommandPool;
    HeatSystemResources& resources;
};

