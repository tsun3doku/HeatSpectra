#pragma once

class VulkanDevice;
class MemoryAllocator;
class ModelRegistry;
class CommandPool;
class VoronoiResources;

struct VoronoiStageContext {
    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    ModelRegistry& resourceManager;
    CommandPool& renderCommandPool;
    VoronoiResources& resources;
};
