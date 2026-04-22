#pragma once

class VulkanDevice;
class MemoryAllocator;
class ModelRegistry;
class CommandPool;
class VoronoiSystemResources;

struct VoronoiStageContext {
    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    ModelRegistry& resourceManager;
    CommandPool& renderCommandPool;
    VoronoiSystemResources& resources;
};