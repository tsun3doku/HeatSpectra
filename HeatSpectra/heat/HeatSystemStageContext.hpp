#pragma once

class VulkanDevice;
class MemoryAllocator;
class ModelRegistry;
class CommandPool;
class HeatSystemResources;

struct HeatSystemStageContext {
    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    ModelRegistry& resourceManager;
    CommandPool& renderCommandPool;
    HeatSystemResources& resources;
};