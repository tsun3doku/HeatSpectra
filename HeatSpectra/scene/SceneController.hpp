#pragma once

#include <atomic>
#include <cstdint>
#include <string>

#include <glm/vec3.hpp>

class CameraController;
class ModelUploader;
class ModelComputeRuntime;
class ModelRegistry;
class VulkanDevice;
class FrameSync;

class SceneController {
public:
    SceneController(
        VulkanDevice& vulkanDevice,
        ModelRegistry& resourceManager,
        ModelUploader& modelUploader,
        FrameSync& frameSync,
        CameraController& cameraController,
        std::atomic<bool>& isOperating);

    void setModelComputeRuntime(ModelComputeRuntime* updatedModelComputeRuntime);
    uint32_t loadModel(const std::string& modelPath, uint32_t preferredModelId = 0);
    bool removeModelByID(uint32_t modelId);
    bool tryGetRuntimeModelSocketKey(uint32_t runtimeModelId, uint64_t& outSocketKey) const;
    bool tryGetSocketRuntimeModelId(uint64_t socketKey, uint32_t& outRuntimeModelId) const;
    void focusOnVisibleModel();
    void focusCameraOn(const glm::vec3& localCenter);

private:
    class OperatingScope {
    public:
        explicit OperatingScope(std::atomic<bool>& isOperating);
        ~OperatingScope();

    private:
        std::atomic<bool>& isOperating;
        bool previousState = false;
    };

    VulkanDevice& vulkanDevice;
    ModelRegistry& resourceManager;
    ModelUploader& modelUploader;
    FrameSync& frameSync;
    CameraController& cameraController;
    std::atomic<bool>& isOperating;
    ModelComputeRuntime* modelComputeRuntime = nullptr;
};


