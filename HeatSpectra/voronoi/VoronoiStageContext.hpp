#pragma once

class VulkanDevice;
class MemoryAllocator;
class ModelRegistry;
class UniformBufferManager;
class CommandPool;
class VoronoiSystemResources;

struct VoronoiStageContext {
    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    ModelRegistry& resourceManager;
    UniformBufferManager& uniformBufferManager;
    CommandPool& renderCommandPool;
    VoronoiSystemResources& resources;
};

