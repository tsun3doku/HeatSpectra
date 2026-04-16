#include "ModelComputeRuntime.hpp"

#include "framegraph/FrameSync.hpp"
#include "scene/ModelUploader.hpp"
#include "render/RenderConfig.hpp"
#include "vulkan/ModelRegistry.hpp"
#include "vulkan/VulkanDevice.hpp"

#include <chrono>
#include <iostream>
#include <thread>

ModelComputeRuntime::ModelComputeRuntime(
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

uint32_t ModelComputeRuntime::loadModel(const std::string& modelPath, uint32_t preferredModelId) {
    std::lock_guard<std::mutex> lock(executionMutex);
    return loadModelImmediate(modelPath, preferredModelId);
}

bool ModelComputeRuntime::removeModelByID(uint32_t runtimeModelId) {
    std::lock_guard<std::mutex> lock(executionMutex);
    return removeModelByIDImmediate(runtimeModelId);
}

void ModelComputeRuntime::queueAcquireSocket(uint64_t socketKey, const std::string& modelPath) {
    if (socketKey == 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(pendingOperationsMutex);
    pendingOperations.push_back(PendingOperation{
        PendingOperation::Type::AcquireSocket,
        socketKey,
        modelPath });
}

void ModelComputeRuntime::queueReleaseSocket(uint64_t socketKey) {
    if (socketKey == 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(pendingOperationsMutex);
    pendingOperations.push_back(PendingOperation{
        PendingOperation::Type::ReleaseSocket,
        socketKey,
        {} });
}

void ModelComputeRuntime::flush() {
    std::vector<PendingOperation> operations;
    {
        std::lock_guard<std::mutex> pendingLock(pendingOperationsMutex);
        operations.swap(pendingOperations);
    }

    if (operations.empty()) {
        return;
    }

    std::lock_guard<std::mutex> executionLock(executionMutex);
    for (const PendingOperation& operation : operations) {
        switch (operation.type) {
        case PendingOperation::Type::AcquireSocket:
            materializeSocketImmediate(operation.socketKey, operation.modelPath);
            break;
        case PendingOperation::Type::ReleaseSocket:
            releaseSocketImmediate(operation.socketKey);
            break;
        }
    }
}

bool ModelComputeRuntime::tryGetRuntimeModelId(uint64_t socketKey, uint32_t& outRuntimeModelId) const {
    std::lock_guard<std::mutex> lock(executionMutex);
    outRuntimeModelId = 0;
    if (socketKey == 0) {
        return false;
    }

    std::lock_guard<std::mutex> bindingsLock(bindingsMutex);
    const auto bindingIt = runtimeModelIdBySocketKey.find(socketKey);
    if (bindingIt == runtimeModelIdBySocketKey.end() || bindingIt->second == 0) {
        return false;
    }

    outRuntimeModelId = bindingIt->second;
    return true;
}

bool ModelComputeRuntime::tryGetSocketKey(uint32_t runtimeModelId, uint64_t& outSocketKey) const {
    std::lock_guard<std::mutex> lock(executionMutex);
    outSocketKey = 0;
    if (runtimeModelId == 0) {
        return false;
    }

    std::lock_guard<std::mutex> bindingsLock(bindingsMutex);
    const auto bindingIt = socketKeyByRuntimeModelId.find(runtimeModelId);
    if (bindingIt == socketKeyByRuntimeModelId.end() || bindingIt->second == 0) {
        return false;
    }

    outSocketKey = bindingIt->second;
    return true;
}

bool ModelComputeRuntime::exportProduct(uint32_t runtimeModelId, ModelProduct& outProduct) const {
    std::lock_guard<std::mutex> lock(executionMutex);
    if (!resourceManager.exportProduct(runtimeModelId, outProduct)) {
        outProduct = {};
        return false;
    }

    outProduct.runtimeModelId = runtimeModelId;
    outProduct.contentHash = computeContentHash(outProduct);
    return outProduct.isValid();
}

uint32_t ModelComputeRuntime::materializeSocketImmediate(uint64_t socketKey, const std::string& modelPath) {
    if (socketKey == 0 || modelPath.empty()) {
        return 0;
    }

    uint32_t existingRuntimeModelId = 0;
    std::string existingModelPath;
    {
        std::lock_guard<std::mutex> bindingsLock(bindingsMutex);
        const auto runtimeIt = runtimeModelIdBySocketKey.find(socketKey);
        if (runtimeIt != runtimeModelIdBySocketKey.end()) {
            existingRuntimeModelId = runtimeIt->second;
        }

        const auto pathIt = modelPathBySocketKey.find(socketKey);
        if (pathIt != modelPathBySocketKey.end()) {
            existingModelPath = pathIt->second;
        }
    }

    if (existingRuntimeModelId != 0 && existingModelPath == modelPath) {
        return existingRuntimeModelId;
    }

    const uint32_t runtimeModelId = loadModelImmediate(modelPath, existingRuntimeModelId);

    std::lock_guard<std::mutex> bindingsLock(bindingsMutex);
    if (existingRuntimeModelId != 0 && existingRuntimeModelId != runtimeModelId) {
        socketKeyByRuntimeModelId.erase(existingRuntimeModelId);
    }
    if (runtimeModelId != 0) {
        runtimeModelIdBySocketKey[socketKey] = runtimeModelId;
        socketKeyByRuntimeModelId[runtimeModelId] = socketKey;
        modelPathBySocketKey[socketKey] = modelPath;
    } else {
        runtimeModelIdBySocketKey.erase(socketKey);
        modelPathBySocketKey.erase(socketKey);
    }

    return runtimeModelId;
}

bool ModelComputeRuntime::releaseSocketImmediate(uint64_t socketKey) {
    if (socketKey == 0) {
        return false;
    }

    uint32_t runtimeModelId = 0;
    {
        std::lock_guard<std::mutex> bindingsLock(bindingsMutex);
        const auto bindingIt = runtimeModelIdBySocketKey.find(socketKey);
        if (bindingIt == runtimeModelIdBySocketKey.end() || bindingIt->second == 0) {
            return false;
        }

        runtimeModelId = bindingIt->second;
        runtimeModelIdBySocketKey.erase(socketKey);
        socketKeyByRuntimeModelId.erase(runtimeModelId);
        modelPathBySocketKey.erase(socketKey);
    }

    return removeModelByIDImmediate(runtimeModelId);
}

uint32_t ModelComputeRuntime::loadModelImmediate(const std::string& modelPath, uint32_t preferredModelId) {
    OperatingScope operatingScope(isOperating);
    std::this_thread::sleep_for(std::chrono::milliseconds(renderconfig::ModelLoadPauseMs));

    frameSync.waitForAllFrameFences();
    vkDeviceWaitIdle(vulkanDevice.getDevice());

    if (preferredModelId != 0) {
        resourceManager.removeModelByID(preferredModelId);
    }

    return modelUploader.addModel(resourceManager, modelPath, preferredModelId);
}

bool ModelComputeRuntime::removeModelByIDImmediate(uint32_t modelId) {
    OperatingScope operatingScope(isOperating);
    if (modelId == 0) {
        return false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(renderconfig::ModelLoadPauseMs));

    frameSync.waitForAllFrameFences();
    vkDeviceWaitIdle(vulkanDevice.getDevice());

    return resourceManager.removeModelByID(modelId);
}

ModelComputeRuntime::OperatingScope::OperatingScope(std::atomic<bool>& isOperating)
    : isOperating(isOperating) {
    previousState = isOperating.exchange(true, std::memory_order_acq_rel);
}

ModelComputeRuntime::OperatingScope::~OperatingScope() {
    isOperating.store(previousState, std::memory_order_release);
}
