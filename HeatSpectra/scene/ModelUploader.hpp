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
    static constexpr const char* DefaultSceneModelPath = "models/teapot.obj";
    static constexpr const char* DefaultHeatModelPath = "models/heatsource_tube.obj";

    ModelUploader(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, Camera& camera, CommandPool& commandPool);

    void uploadInitialModels(ModelRegistry& resourceManager);
    uint32_t addModel(ModelRegistry& resourceManager, const std::string& modelPath, uint32_t preferredModelId = 0);

private:
    std::unique_ptr<Model> createModel(const std::string& modelPath) const;

    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    Camera& camera;
    CommandPool& commandPool;
};

