#pragma once

class VulkanDevice;
class MemoryAllocator;
class ResourceManager;
class UniformBufferManager;
class CommandPool;
class VoronoiSystemResources;

struct VoronoiStageContext {
    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    ResourceManager& resourceManager;
    UniformBufferManager& uniformBufferManager;
    CommandPool& renderCommandPool;
    VoronoiSystemResources& resources;
};
