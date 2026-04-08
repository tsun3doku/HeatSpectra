#pragma once

#include <atomic>
#include <cstdint>
#include <string>

#include <glm/vec3.hpp>

class CameraController;
class ModelUploader;
class ModelRuntime;
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

    void setModelRuntime(ModelRuntime* updatedModelRuntime);
    uint32_t loadModel(const std::string& modelPath, uint32_t preferredModelId = 0);
    bool removeModelByID(uint32_t modelId);
    uint32_t materializeModelSink(uint32_t nodeModelId, const std::string& modelPath);
    bool removeNodeModelSink(uint32_t nodeModelId);
    bool tryGetNodeModelRuntimeId(uint32_t nodeModelId, uint32_t& outRuntimeModelId) const;
    bool tryGetRuntimeModelNodeId(uint32_t runtimeModelId, uint32_t& outNodeModelId) const;
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
    ModelRuntime* modelRuntime = nullptr;
};


