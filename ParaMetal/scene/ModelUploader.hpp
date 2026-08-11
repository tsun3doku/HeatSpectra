#pragma once

#include <memory>
#include <string>
#include <cstdint>

class Camera;
class CommandPool;
class MemoryAllocator;
class Model;
class ModelRegistry;
class VulkanDevice;

class ModelUploader {
public:
    ModelUploader(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, Camera& camera, CommandPool& commandPool);

    std::unique_ptr<Model> createModel() const;

private:
    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    Camera& camera;
    CommandPool& commandPool;
};

