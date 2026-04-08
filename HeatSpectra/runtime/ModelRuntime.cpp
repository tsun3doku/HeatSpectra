#include "ModelRuntime.hpp"

#include "framegraph/FrameSync.hpp"
#include "scene/ModelUploader.hpp"
#include "scene/SceneController.hpp"
#include "render/RenderConfig.hpp"
#include "vulkan/ModelRegistry.hpp"
#include "vulkan/VulkanDevice.hpp"

#include <chrono>
#include <iostream>
#include <thread>

ModelRuntime::ModelRuntime(
    VulkanDevice& vulkanDevice,
    ModelRegistry& resourceManager,
    ModelUploader& modelUploader,
    FrameSync& frameSync,
    std::atomic<bool>& isOperating)
    : vulkanDevice(vulkanDevice),
      resourceManager(resourceManager),
      modelUploader(modelUploader),
      frameSync(frameSync),
      isOperating(isOperating) {
}

void ModelRuntime::setSceneController(SceneController* updatedSceneController) {
    std::lock_guard<std::mutex> lock(executionMutex);
    sceneController = updatedSceneController;
}

uint32_t ModelRuntime::loadModel(const std::string& modelPath, uint32_t preferredModelId) {
    std::lock_guard<std::mutex> lock(executionMutex);
    return loadModelImmediate(modelPath, preferredModelId);
}

bool ModelRuntime::removeModelByID(uint32_t runtimeModelId) {
    std::lock_guard<std::mutex> lock(executionMutex);
    return removeModelByIDImmediate(runtimeModelId);
}

void ModelRuntime::queueApplySink(uint32_t nodeModelId, const std::string& modelPath, const glm::mat4& matrix) {
    if (nodeModelId == 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(pendingSinkOperationsMutex);
    pendingSinkOperations.push_back(PendingSinkOperation{
        PendingSinkOperation::Type::ApplySink,
        nodeModelId,
        modelPath,
        matrix});
}

void ModelRuntime::queueRemoveSink(uint32_t nodeModelId) {
    if (nodeModelId == 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(pendingSinkOperationsMutex);
    pendingSinkOperations.push_back(PendingSinkOperation{
        PendingSinkOperation::Type::RemoveSink,
        nodeModelId,
        {},
        glm::mat4(1.0f)});
}

void ModelRuntime::queueFocusVisibleModel() {
    std::lock_guard<std::mutex> lock(pendingSinkOperationsMutex);
    pendingSinkOperations.push_back(PendingSinkOperation{
        PendingSinkOperation::Type::FocusVisibleModel,
        0,
        {},
        glm::mat4(1.0f)});
}

void ModelRuntime::flush() {
    std::vector<PendingSinkOperation> operations;
    {
        std::lock_guard<std::mutex> pendingLock(pendingSinkOperationsMutex);
        operations.swap(pendingSinkOperations);
    }

    if (operations.empty()) {
        return;
    }

    std::lock_guard<std::mutex> executionLock(executionMutex);
    for (const PendingSinkOperation& operation : operations) {
        switch (operation.type) {
        case PendingSinkOperation::Type::ApplySink: {
            const uint32_t runtimeModelId =
                resolveRuntimeModelIdImmediate(operation.nodeModelId, operation.modelPath);
            if (runtimeModelId != 0) {
                setModelMatrixImmediate(runtimeModelId, operation.matrix);
            }
            break;
        }
        case PendingSinkOperation::Type::RemoveSink:
            removeSinkImmediate(operation.nodeModelId);
            break;
        case PendingSinkOperation::Type::FocusVisibleModel:
            if (sceneController) {
                sceneController->focusOnVisibleModel();
            }
            break;
        }
    }
}

bool ModelRuntime::tryGetRuntimeModelId(uint32_t nodeModelId, uint32_t& outRuntimeModelId) const {
    std::lock_guard<std::mutex> lock(executionMutex);
    outRuntimeModelId = 0;
    if (nodeModelId == 0) {
        return false;
    }

    std::lock_guard<std::mutex> bindingsLock(nodeBindingsMutex);
    const auto bindingIt = runtimeModelIdByNodeModelId.find(nodeModelId);
    if (bindingIt == runtimeModelIdByNodeModelId.end() || bindingIt->second == 0) {
        return false;
    }

    outRuntimeModelId = bindingIt->second;
    return true;
}

bool ModelRuntime::tryGetNodeModelId(uint32_t runtimeModelId, uint32_t& outNodeModelId) const {
    std::lock_guard<std::mutex> lock(executionMutex);
    outNodeModelId = 0;
    if (runtimeModelId == 0) {
        return false;
    }

    std::lock_guard<std::mutex> bindingsLock(nodeBindingsMutex);
    const auto bindingIt = nodeModelIdByRuntimeModelId.find(runtimeModelId);
    if (bindingIt == nodeModelIdByRuntimeModelId.end() || bindingIt->second == 0) {
        return false;
    }

    outNodeModelId = bindingIt->second;
    return true;
}

bool ModelRuntime::exportProduct(uint32_t runtimeModelId, ModelProduct& outProduct) const {
    std::lock_guard<std::mutex> lock(executionMutex);
    return resourceManager.exportProduct(runtimeModelId, outProduct);
}

void ModelRuntime::focusOnVisibleModel() {
    std::lock_guard<std::mutex> lock(executionMutex);
    if (sceneController) {
        sceneController->focusOnVisibleModel();
    }
}

uint32_t ModelRuntime::materializeSinkImmediate(uint32_t nodeModelId, const std::string& modelPath) {
    if (nodeModelId == 0 || modelPath.empty()) {
        return 0;
    }

    uint32_t existingRuntimeModelId = 0;
    std::string existingModelPath;
    {
        std::lock_guard<std::mutex> bindingsLock(nodeBindingsMutex);
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

    const uint32_t runtimeModelId = loadModelImmediate(modelPath, existingRuntimeModelId);

    std::lock_guard<std::mutex> bindingsLock(nodeBindingsMutex);
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

uint32_t ModelRuntime::resolveRuntimeModelIdImmediate(uint32_t nodeModelId, const std::string& modelPath) {
    if (nodeModelId == 0) {
        return 0;
    }

    if (!modelPath.empty()) {
        return materializeSinkImmediate(nodeModelId, modelPath);
    }

    std::lock_guard<std::mutex> bindingsLock(nodeBindingsMutex);
    const auto bindingIt = runtimeModelIdByNodeModelId.find(nodeModelId);
    if (bindingIt == runtimeModelIdByNodeModelId.end() || bindingIt->second == 0) {
        return 0;
    }

    return bindingIt->second;
}

bool ModelRuntime::removeSinkImmediate(uint32_t nodeModelId) {
    if (nodeModelId == 0) {
        return false;
    }

    uint32_t runtimeModelId = 0;
    {
        std::lock_guard<std::mutex> bindingsLock(nodeBindingsMutex);
        const auto bindingIt = runtimeModelIdByNodeModelId.find(nodeModelId);
        if (bindingIt == runtimeModelIdByNodeModelId.end() || bindingIt->second == 0) {
            return false;
        }

        runtimeModelId = bindingIt->second;
        runtimeModelIdByNodeModelId.erase(nodeModelId);
        nodeModelIdByRuntimeModelId.erase(runtimeModelId);
        modelPathByNodeModelId.erase(nodeModelId);
    }

    return removeModelByIDImmediate(runtimeModelId);
}

bool ModelRuntime::setModelMatrixImmediate(uint32_t runtimeModelId, const glm::mat4& matrix) {
    if (runtimeModelId == 0) {
        return false;
    }

    return resourceManager.setModelMatrix(runtimeModelId, matrix);
}

uint32_t ModelRuntime::loadModelImmediate(const std::string& modelPath, uint32_t preferredModelId) {
    OperatingScope operatingScope(isOperating);
    std::cout << "[SceneController] Adding model: " << modelPath << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(renderconfig::ModelLoadPauseMs));

    frameSync.waitForAllFrameFences();
    vkDeviceWaitIdle(vulkanDevice.getDevice());

    if (preferredModelId != 0) {
        resourceManager.removeModelByID(preferredModelId);
    }

    const uint32_t modelId = modelUploader.addModel(resourceManager, modelPath, preferredModelId);

    glm::vec3 localCenter(0.0f);
    if (resourceManager.tryGetBoundingBoxCenter(modelId, localCenter)) {
        if (sceneController) {
            sceneController->focusCameraOn(localCenter);
        }
    } else if (sceneController) {
        sceneController->focusOnVisibleModel();
    }

    std::cout << "[SceneController] Added model with runtime ID: " << modelId << std::endl;
    return modelId;
}

bool ModelRuntime::removeModelByIDImmediate(uint32_t modelId) {
    OperatingScope operatingScope(isOperating);
    if (modelId == 0) {
        return false;
    }

    std::cout << "[SceneController] Removing model ID: " << modelId << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(renderconfig::ModelLoadPauseMs));

    frameSync.waitForAllFrameFences();
    vkDeviceWaitIdle(vulkanDevice.getDevice());

    const bool removed = resourceManager.removeModelByID(modelId);
    if (removed && sceneController) {
        sceneController->focusOnVisibleModel();
    }

    if (removed) {
        std::cout << "[SceneController] Removed model ID: " << modelId << std::endl;
    }
    return removed;
}

ModelRuntime::OperatingScope::OperatingScope(std::atomic<bool>& isOperating)
    : isOperating(isOperating) {
    previousState = isOperating.exchange(true, std::memory_order_acq_rel);
}

ModelRuntime::OperatingScope::~OperatingScope() {
    isOperating.store(previousState, std::memory_order_release);
}

