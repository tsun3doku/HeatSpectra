#include "ModelUploader.hpp"

#include "Camera.hpp"
#include "vulkan/CommandBufferManager.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "mesh/MeshModifiers.hpp"
#include "Model.hpp"
#include "vulkan/ResourceManager.hpp"
#include "vulkan/VulkanDevice.hpp"

#include <memory>

ModelUploader::ModelUploader(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, Camera& camera, CommandPool& commandPool)
    : vulkanDevice(vulkanDevice),
      memoryAllocator(memoryAllocator),
      camera(camera),
      commandPool(commandPool) {
}

void ModelUploader::uploadInitialModels(ResourceManager& resourceManager, MeshModifiers& meshModifiers) {
    resourceManager.setModels(nullptr, nullptr, nullptr);
    meshModifiers.cleanup();
}

uint32_t ModelUploader::addModel(ResourceManager& resourceManager, const std::string& modelPath, uint32_t preferredModelId) {
    return resourceManager.addModel(createModel(modelPath), preferredModelId);
}

std::unique_ptr<Model> ModelUploader::createModel(const std::string& modelPath) const {
    auto model = std::make_unique<Model>(vulkanDevice, memoryAllocator, camera, commandPool);
    if (!model->init(modelPath)) {
        return nullptr;
    }
    return model;
}
