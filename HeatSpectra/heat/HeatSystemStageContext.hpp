#pragma once

class VulkanDevice;
class MemoryAllocator;
class ModelRegistry;
class UniformBufferManager;
class CommandPool;
class HeatSystemResources;

struct HeatSystemStageContext {
    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    ModelRegistry& resourceManager;
    UniformBufferManager& uniformBufferManager;
    CommandPool& renderCommandPool;
    HeatSystemResources& resources;
};


