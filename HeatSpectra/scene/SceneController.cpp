#include "SceneController.hpp"

#include "CameraController.hpp"
#include "app/SwapchainManager.hpp"
#include "heat/HeatSystemController.hpp"
#include "mesh/MeshModifiers.hpp"
#include "Model.hpp"
#include "render/RenderRuntime.hpp"
#include "vulkan/ResourceManager.hpp"
#include "render/SceneRenderer.hpp"
#include "framegraph/VkFrameGraphRuntime.hpp"
#include "vulkan/VulkanDevice.hpp"

#include <iostream>

SceneController::SceneController(
    VulkanDevice& vulkanDevice,
    SwapchainManager& swapchainManager,
    ResourceManager& resourceManager,
    MeshModifiers& meshModifiers,
    RenderRuntime& renderRuntime,
    HeatSystemController& heatSystemController,
    CameraController& cameraController,
    std::atomic<bool>& isOperating)
    : vulkanDevice(vulkanDevice),
      swapchainManager(swapchainManager),
      resourceManager(resourceManager),
      meshModifiers(meshModifiers),
      renderRuntime(renderRuntime),
      heatSystemController(heatSystemController),
      cameraController(cameraController),
      isOperating(isOperating) {
}

uint32_t SceneController::loadModel(const std::string& modelPath, uint32_t preferredModelId) {
    OperatingScope operatingScope(isOperating);
    const uint32_t modelId = heatSystemController.loadModel(modelPath, preferredModelId);
    auto* currentSimulation = heatSystemController.getHeatSystem();
    renderRuntime.setSimulation(currentSimulation);
    if (Model* loadedModel = resourceManager.getModelByID(modelId)) {
        cameraController.focusOn(loadedModel->getBoundingBoxCenter());
    } else {
        focusOnVisibleModel();
    }
    return modelId;
}

bool SceneController::removeModelByID(uint32_t modelId) {
    OperatingScope operatingScope(isOperating);
    const bool removed = heatSystemController.removeModelByID(modelId);
    auto* currentSimulation = heatSystemController.getHeatSystem();
    renderRuntime.setSimulation(currentSimulation);
    if (removed) {
        focusOnVisibleModel();
    }
    return removed;
}

void SceneController::performRemeshing(int iterations, double minAngleDegrees, double maxEdgeLength, double stepSize, uint32_t targetModelId) {
    vkDeviceWaitIdle(vulkanDevice.getDevice());
    OperatingScope operatingScope(isOperating);

    Model* remeshedModel = meshModifiers.performRemeshing(
        renderRuntime.getModelSelection(),
        iterations,
        minAngleDegrees,
        maxEdgeLength,
        stepSize,
        targetModelId);

    if (!remeshedModel) {
        std::cerr << "[SceneController] Remeshing failed" << std::endl;
        return;
    }

    renderRuntime.getSceneRenderer().updateModelDescriptors(remeshedModel, meshModifiers.getRemesher());

    auto* currentSimulation = heatSystemController.getHeatSystem();
    if (currentSimulation) {
        heatSystemController.recreateHeatSystem(
            swapchainManager.getExtent(),
            renderRuntime.getFrameGraphRuntime().getRenderPass());
        currentSimulation = heatSystemController.getHeatSystem();
        renderRuntime.setSimulation(currentSimulation);
        std::cout << "[SceneController] HeatSystem recreated after remeshing" << std::endl;
    }

    focusOnVisibleModel();
}

void SceneController::focusOnVisibleModel() {
    for (uint32_t modelId : resourceManager.getRenderableModelIds()) {
        if (Model* model = resourceManager.getModelByID(modelId)) {
            cameraController.focusOn(model->getBoundingBoxCenter());
            return;
        }
    }
}

SceneController::OperatingScope::OperatingScope(std::atomic<bool>& isOperating)
    : isOperating(isOperating) {
    isOperating.store(true, std::memory_order_release);
}

SceneController::OperatingScope::~OperatingScope() {
    isOperating.store(false, std::memory_order_release);
}
