#include "SceneController.hpp"

#include "CameraController.hpp"
#include "runtime/ModelRuntime.hpp"
#include "vulkan/ModelRegistry.hpp"

#include <glm/mat4x4.hpp>

SceneController::SceneController(
    VulkanDevice& vulkanDevice,
    ModelRegistry& resourceManager,
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

void SceneController::setModelRuntime(ModelRuntime* updatedModelRuntime) {
    modelRuntime = updatedModelRuntime;
}

uint32_t SceneController::loadModel(const std::string& modelPath, uint32_t preferredModelId) {
    if (!modelRuntime) {
        return 0;
    }

    return modelRuntime->loadModel(modelPath, preferredModelId);
}

bool SceneController::removeModelByID(uint32_t modelId) {
    if (!modelRuntime) {
        return false;
    }

    return modelRuntime->removeModelByID(modelId);
}

uint32_t SceneController::materializeModelSink(uint32_t nodeModelId, const std::string& modelPath) {
    if (!modelRuntime) {
        return 0;
    }

    modelRuntime->queueApplySink(nodeModelId, modelPath, glm::mat4(1.0f));
    modelRuntime->flush();

    uint32_t runtimeModelId = 0;
    if (!modelRuntime->tryGetRuntimeModelId(nodeModelId, runtimeModelId)) {
        return 0;
    }

    return runtimeModelId;
}

bool SceneController::removeNodeModelSink(uint32_t nodeModelId) {
    if (!modelRuntime) {
        return false;
    }

    modelRuntime->queueRemoveSink(nodeModelId);
    modelRuntime->flush();
    return true;
}

bool SceneController::tryGetNodeModelRuntimeId(uint32_t nodeModelId, uint32_t& outRuntimeModelId) const {
    outRuntimeModelId = 0;
    if (!modelRuntime) {
        return false;
    }

    return modelRuntime->tryGetRuntimeModelId(nodeModelId, outRuntimeModelId);
}

bool SceneController::tryGetRuntimeModelNodeId(uint32_t runtimeModelId, uint32_t& outNodeModelId) const {
    outNodeModelId = 0;
    if (!modelRuntime) {
        return false;
    }

    return modelRuntime->tryGetNodeModelId(runtimeModelId, outNodeModelId);
}

void SceneController::focusOnVisibleModel() {
    for (uint32_t modelId : resourceManager.getRenderableModelIds()) {
        glm::vec3 localCenter(0.0f);
        if (resourceManager.tryGetBoundingBoxCenter(modelId, localCenter)) {
            cameraController.focusOn(localCenter);
            return;
        }
    }
}

void SceneController::focusCameraOn(const glm::vec3& localCenter) {
    cameraController.focusOn(localCenter);
}

SceneController::OperatingScope::OperatingScope(std::atomic<bool>& isOperating)
    : isOperating(isOperating) {
    previousState = isOperating.exchange(true, std::memory_order_acq_rel);
}

SceneController::OperatingScope::~OperatingScope() {
    isOperating.store(previousState, std::memory_order_release);
}

