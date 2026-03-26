#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

class CameraController;
class ModelUploader;
class ResourceManager;
class RuntimePayloadController;
class VulkanDevice;
class FrameSync;

class SceneController {
public:
    SceneController(
        VulkanDevice& vulkanDevice,
        ResourceManager& resourceManager,
        ModelUploader& modelUploader,
        RuntimePayloadController& runtimePayloadController,
        FrameSync& frameSync,
        CameraController& cameraController,
        std::atomic<bool>& isOperating);

    uint32_t loadModel(const std::string& modelPath, uint32_t preferredModelId = 0);
    bool removeModelByID(uint32_t modelId);
    uint32_t materializeModelSink(uint32_t nodeModelId, const std::string& modelPath);
    bool removeNodeModelSink(uint32_t nodeModelId);
    bool tryGetNodeModelRuntimeId(uint32_t nodeModelId, uint32_t& outRuntimeModelId) const;
    bool tryGetRuntimeModelNodeId(uint32_t runtimeModelId, uint32_t& outNodeModelId) const;
    void focusOnVisibleModel();

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
    ResourceManager& resourceManager;
    ModelUploader& modelUploader;
    RuntimePayloadController& runtimePayloadController;
    FrameSync& frameSync;
    CameraController& cameraController;
    std::atomic<bool>& isOperating;
    mutable std::mutex nodeBindingsMutex;
    std::unordered_map<uint32_t, uint32_t> runtimeModelIdByNodeModelId;
    std::unordered_map<uint32_t, uint32_t> nodeModelIdByRuntimeModelId;
    std::unordered_map<uint32_t, std::string> modelPathByNodeModelId;
};

