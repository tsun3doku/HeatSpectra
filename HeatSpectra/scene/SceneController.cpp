#include "SceneController.hpp"

#include "CameraController.hpp"
#include "runtime/ModelComputeRuntime.hpp"
#include "vulkan/ModelRegistry.hpp"

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

void SceneController::setModelComputeRuntime(ModelComputeRuntime* updatedModelComputeRuntime) {
    modelComputeRuntime = updatedModelComputeRuntime;
}

uint32_t SceneController::loadModel(const std::string& modelPath, uint32_t preferredModelId) {
    if (!modelComputeRuntime) {
        return 0;
    }

    return modelComputeRuntime->loadModel(modelPath, preferredModelId);
}

bool SceneController::removeModelByID(uint32_t modelId) {
    if (!modelComputeRuntime) {
        return false;
    }

    return modelComputeRuntime->removeModelByID(modelId);
}

bool SceneController::tryGetRuntimeModelSocketKey(uint32_t runtimeModelId, uint64_t& outSocketKey) const {
    outSocketKey = 0;
    if (!modelComputeRuntime) {
        return false;
    }

    return modelComputeRuntime->tryGetSocketKey(runtimeModelId, outSocketKey);
}

bool SceneController::tryGetSocketRuntimeModelId(uint64_t socketKey, uint32_t& outRuntimeModelId) const {
    outRuntimeModelId = 0;
    if (!modelComputeRuntime) {
        return false;
    }

    return modelComputeRuntime->tryGetRuntimeModelId(socketKey, outRuntimeModelId);
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

