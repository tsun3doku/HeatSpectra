#include "MeshModifiers.hpp"

#include "vulkan/MemoryAllocator.hpp"
#include "scene/Model.hpp"
#include "scene/ModelSelection.hpp"
#include "vulkan/ResourceManager.hpp"
#include "vulkan/UniformBufferManager.hpp"
#include "vulkan/VulkanDevice.hpp"

#include <iostream>

MeshModifiers::MeshModifiers(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, ResourceManager& resourceManager, UniformBufferManager& uniformBufferManager)
    : resourceManager(resourceManager),
      remesher(vulkanDevice, memoryAllocator, uniformBufferManager) {
}

Model* MeshModifiers::performRemeshing(
    ModelSelection& modelSelection,
    int iterations,
    double minAngleDegrees,
    double maxEdgeLength,
    double stepSize,
    uint32_t targetModelId) {
    const uint32_t selectedModelID = (targetModelId != 0) ? targetModelId : modelSelection.getSelectedModelID();
    Model* targetModel = resourceManager.getModelByID(selectedModelID);

    if (!targetModel) {
        for (uint32_t modelId : resourceManager.getRenderableModelIds()) {
            targetModel = resourceManager.getModelByID(modelId);
            if (targetModel) {
                std::cout << "[MeshModifiers] No explicit remesh target; defaulting to first renderable model ID: "
                          << modelId << std::endl;
                break;
            }
        }
    } else {
        std::cout << "[MeshModifiers] Remeshing model ID: " << selectedModelID << std::endl;
    }

    if (!targetModel) {
        std::cout << "[MeshModifiers] No renderable model available for remeshing" << std::endl;
        return nullptr;
    } else {
        std::cout << "[MeshModifiers] Remeshing model runtime ID: " << targetModel->getRuntimeModelId() << std::endl;
    }

    const bool remeshSuccess = remesher.performRemeshing(targetModel, iterations, minAngleDegrees, maxEdgeLength, stepSize);

    if (!remeshSuccess) {
        return nullptr;
    }

    return targetModel;
}

bool MeshModifiers::areAllModelsRemeshed() const {
    bool hasRenderableModel = false;
    for (uint32_t modelId : resourceManager.getRenderableModelIds()) {
        Model* model = resourceManager.getModelByID(modelId);
        if (!model) {
            continue;
        }

        hasRenderableModel = true;
        if (!remesher.isModelRemeshed(model)) {
            return false;
        }
    }

    return hasRenderableModel;
}

void MeshModifiers::cleanup() {
    remesher.cleanup();
}
