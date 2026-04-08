#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/mat4x4.hpp>

#include "runtime/RuntimeProducts.hpp"

class FrameSync;
class ModelUploader;
class ModelRegistry;
class SceneController;
class VulkanDevice;

class ModelRuntime {
public:
    ModelRuntime(
        VulkanDevice& vulkanDevice,
        ModelRegistry& resourceManager,
        ModelUploader& modelUploader,
        FrameSync& frameSync,
        std::atomic<bool>& isOperating);

    void setSceneController(SceneController* updatedSceneController);

    uint32_t loadModel(const std::string& modelPath, uint32_t preferredModelId = 0);
    bool removeModelByID(uint32_t runtimeModelId);
    void queueApplySink(uint32_t nodeModelId, const std::string& modelPath, const glm::mat4& matrix);
    void queueRemoveSink(uint32_t nodeModelId);
    void queueFocusVisibleModel();
    void flush();

    bool tryGetRuntimeModelId(uint32_t nodeModelId, uint32_t& outRuntimeModelId) const;
    bool tryGetNodeModelId(uint32_t runtimeModelId, uint32_t& outNodeModelId) const;
    bool exportProduct(uint32_t runtimeModelId, ModelProduct& outProduct) const;
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

    struct PendingSinkOperation {
        enum class Type {
            ApplySink,
            RemoveSink,
            FocusVisibleModel,
        };

        Type type = Type::ApplySink;
        uint32_t nodeModelId = 0;
        std::string modelPath;
        glm::mat4 matrix{1.0f};
    };

    uint32_t materializeSinkImmediate(uint32_t nodeModelId, const std::string& modelPath);
    uint32_t resolveRuntimeModelIdImmediate(uint32_t nodeModelId, const std::string& modelPath);
    bool removeSinkImmediate(uint32_t nodeModelId);
    bool setModelMatrixImmediate(uint32_t runtimeModelId, const glm::mat4& matrix);
    uint32_t loadModelImmediate(const std::string& modelPath, uint32_t preferredModelId);
    bool removeModelByIDImmediate(uint32_t modelId);

    VulkanDevice& vulkanDevice;
    ModelRegistry& resourceManager;
    ModelUploader& modelUploader;
    FrameSync& frameSync;
    std::atomic<bool>& isOperating;
    SceneController* sceneController = nullptr;
    mutable std::mutex executionMutex;
    mutable std::mutex pendingSinkOperationsMutex;
    mutable std::mutex nodeBindingsMutex;
    std::vector<PendingSinkOperation> pendingSinkOperations;
    std::unordered_map<uint32_t, uint32_t> runtimeModelIdByNodeModelId;
    std::unordered_map<uint32_t, uint32_t> nodeModelIdByRuntimeModelId;
    std::unordered_map<uint32_t, std::string> modelPathByNodeModelId;
};

