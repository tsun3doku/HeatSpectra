#pragma once

#include <atomic>
#include <cstdint>
#include <string>

#include <glm/vec3.hpp>

class CameraController;
class ModelUploader;
class ModelComputeController;
class ModelRegistry;
class VulkanDevice;
class FrameSync;

class SceneController {
public:
    SceneController(
        VulkanDevice& vulkanDevice,
        ModelRegistry& modelRegistry,
        ModelUploader& modelUploader,
        FrameSync& frameSync,
        CameraController& cameraController,
        std::atomic<bool>& isOperating);

    void setModelComputeController(ModelComputeController* updatedModelComputeController);
    bool tryGetRuntimeModelSocketKey(uint32_t runtimeModelId, uint64_t& outSocketKey) const;
    void focusOnVisibleModel();
    void focusCameraOn(const glm::vec3& worldCenter);

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
    ModelRegistry& modelRegistry;
    ModelUploader& modelUploader;
    FrameSync& frameSync;
    CameraController& cameraController;
    std::atomic<bool>& isOperating;
    ModelComputeController* modelComputeController = nullptr;
};


