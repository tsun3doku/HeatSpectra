#include "SceneController.hpp"

#include "CameraController.hpp"
#include "framegraph/FrameSync.hpp"
#include "Model.hpp"
#include "ModelUploader.hpp"
#include "render/RenderConfig.hpp"
#include "vulkan/ResourceManager.hpp"
#include "vulkan/VulkanDevice.hpp"

#include <chrono>
#include <iostream>
#include <thread>

SceneController::SceneController(
    VulkanDevice& vulkanDevice,
    ResourceManager& resourceManager,
    ModelUploader& modelUploader,
    FrameSync& frameSync,
    CameraController& cameraController,
    std::atomic<bool>& isOperating)
    : vulkanDevice(vulkanDevice),
      resourceManager(resourceManager),
      modelUploader(modelUploader),
      frameSync(frameSync),
      cameraController(cameraController),
      isOperating(isOperating) {
}

uint32_t SceneController::loadModel(const std::string& modelPath, uint32_t preferredModelId) {
    OperatingScope operatingScope(isOperating);
    std::cout << "[SceneController] Adding model: " << modelPath << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(renderconfig::ModelLoadPauseMs));

    frameSync.waitForAllFrameFences();
    vkDeviceWaitIdle(vulkanDevice.getDevice());

    if (preferredModelId != 0) {
        resourceManager.removeModelByID(preferredModelId);
    }

    const uint32_t modelId = modelUploader.addModel(resourceManager, modelPath, preferredModelId);

    if (Model* loadedModel = resourceManager.getModelByID(modelId)) {
        cameraController.focusOn(loadedModel->getBoundingBoxCenter());
    } else {
        focusOnVisibleModel();
    }

    std::cout << "[SceneController] Added model with runtime ID: " << modelId << std::endl;
    return modelId;
}

bool SceneController::removeModelByID(uint32_t modelId) {
    OperatingScope operatingScope(isOperating);
    if (modelId == 0) {
        return false;
    }

    std::cout << "[SceneController] Removing model ID: " << modelId << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(renderconfig::ModelLoadPauseMs));

    frameSync.waitForAllFrameFences();
    vkDeviceWaitIdle(vulkanDevice.getDevice());

    const bool removed = resourceManager.removeModelByID(modelId);
    if (removed) {
        focusOnVisibleModel();
    }

    if (removed) {
        std::cout << "[SceneController] Removed model ID: " << modelId << std::endl;
    }
    return removed;
}

uint32_t SceneController::materializeModelSink(uint32_t nodeModelId, const std::string& modelPath) {
    if (nodeModelId == 0 || modelPath.empty()) {
        return 0;
    }

    uint32_t existingRuntimeModelId = 0;
    std::string existingModelPath;
    {
        std::lock_guard<std::mutex> lock(nodeBindingsMutex);
        const auto runtimeIt = runtimeModelIdByNodeModelId.find(nodeModelId);
        if (runtimeIt != runtimeModelIdByNodeModelId.end()) {
            existingRuntimeModelId = runtimeIt->second;
        }

        const auto pathIt = modelPathByNodeModelId.find(nodeModelId);
        if (pathIt != modelPathByNodeModelId.end()) {
            existingModelPath = pathIt->second;
        }
    }

    if (existingRuntimeModelId != 0 && existingModelPath == modelPath) {
        return existingRuntimeModelId;
    }

    const uint32_t runtimeModelId = loadModel(modelPath, existingRuntimeModelId);
    std::lock_guard<std::mutex> lock(nodeBindingsMutex);
    if (existingRuntimeModelId != 0 && existingRuntimeModelId != runtimeModelId) {
        nodeModelIdByRuntimeModelId.erase(existingRuntimeModelId);
    }
    if (runtimeModelId != 0) {
        runtimeModelIdByNodeModelId[nodeModelId] = runtimeModelId;
        nodeModelIdByRuntimeModelId[runtimeModelId] = nodeModelId;
        modelPathByNodeModelId[nodeModelId] = modelPath;
    } else {
        runtimeModelIdByNodeModelId.erase(nodeModelId);
        modelPathByNodeModelId.erase(nodeModelId);
    }

    return runtimeModelId;
}

bool SceneController::removeNodeModelSink(uint32_t nodeModelId) {
    if (nodeModelId == 0) {
        return false;
    }

    uint32_t runtimeModelId = 0;
    {
        std::lock_guard<std::mutex> lock(nodeBindingsMutex);
        const auto bindingIt = runtimeModelIdByNodeModelId.find(nodeModelId);
        if (bindingIt == runtimeModelIdByNodeModelId.end() || bindingIt->second == 0) {
            return false;
        }

        runtimeModelId = bindingIt->second;
        runtimeModelIdByNodeModelId.erase(nodeModelId);
        nodeModelIdByRuntimeModelId.erase(runtimeModelId);
        modelPathByNodeModelId.erase(nodeModelId);
    }

    return removeModelByID(runtimeModelId);
}

bool SceneController::tryGetNodeModelRuntimeId(uint32_t nodeModelId, uint32_t& outRuntimeModelId) const {
    outRuntimeModelId = 0;
    if (nodeModelId == 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(nodeBindingsMutex);
    const auto bindingIt = runtimeModelIdByNodeModelId.find(nodeModelId);
    if (bindingIt == runtimeModelIdByNodeModelId.end() || bindingIt->second == 0) {
        return false;
    }

    outRuntimeModelId = bindingIt->second;
    return true;
}

bool SceneController::tryGetRuntimeModelNodeId(uint32_t runtimeModelId, uint32_t& outNodeModelId) const {
    outNodeModelId = 0;
    if (runtimeModelId == 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(nodeBindingsMutex);
    const auto bindingIt = nodeModelIdByRuntimeModelId.find(runtimeModelId);
    if (bindingIt == nodeModelIdByRuntimeModelId.end() || bindingIt->second == 0) {
        return false;
    }

    outNodeModelId = bindingIt->second;
    return true;
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
    previousState = isOperating.exchange(true, std::memory_order_acq_rel);
}

SceneController::OperatingScope::~OperatingScope() {
    isOperating.store(previousState, std::memory_order_release);
}
