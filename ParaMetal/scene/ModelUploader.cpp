#include "ModelUploader.hpp"

#include "Camera.hpp"
#include "vulkan/CommandBufferManager.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "Model.hpp"
#include "vulkan/ModelRegistry.hpp"
#include "vulkan/VulkanDevice.hpp"

#include <memory>

ModelUploader::ModelUploader(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, Camera& camera, CommandPool& commandPool)
    : vulkanDevice(vulkanDevice),
      memoryAllocator(memoryAllocator),
      camera(camera),
      commandPool(commandPool) {
}

std::unique_ptr<Model> ModelUploader::createModel() const {
    return std::make_unique<Model>(vulkanDevice, memoryAllocator, camera, commandPool);
}
