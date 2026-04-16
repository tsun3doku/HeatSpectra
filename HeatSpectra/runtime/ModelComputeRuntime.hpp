#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "runtime/RuntimeProducts.hpp"

class FrameSync;
class ModelUploader;
class ModelRegistry;
class VulkanDevice;

class ModelComputeRuntime {
public:
    ModelComputeRuntime(
        VulkanDevice& vulkanDevice,
        ModelRegistry& resourceManager,
        ModelUploader& modelUploader,
        FrameSync& frameSync,
        std::atomic<bool>& isOperating);

    uint32_t loadModel(const std::string& modelPath, uint32_t preferredModelId = 0);
    bool removeModelByID(uint32_t runtimeModelId);
    void queueAcquireSocket(uint64_t socketKey, const std::string& modelPath);
    void queueReleaseSocket(uint64_t socketKey);
    void flush();

    bool tryGetRuntimeModelId(uint64_t socketKey, uint32_t& outRuntimeModelId) const;
    bool tryGetSocketKey(uint32_t runtimeModelId, uint64_t& outSocketKey) const;
    bool exportProduct(uint32_t runtimeModelId, ModelProduct& outProduct) const;

private:
    class OperatingScope {
    public:
        explicit OperatingScope(std::atomic<bool>& isOperating);
        ~OperatingScope();

    private:
        std::atomic<bool>& isOperating;
        bool previousState = false;
    };

    struct PendingOperation {
        enum class Type {
            AcquireSocket,
            ReleaseSocket,
        };

        Type type = Type::AcquireSocket;
        uint64_t socketKey = 0;
        std::string modelPath;
    };

    uint32_t materializeSocketImmediate(uint64_t socketKey, const std::string& modelPath);
    bool releaseSocketImmediate(uint64_t socketKey);
    uint32_t loadModelImmediate(const std::string& modelPath, uint32_t preferredModelId);
    bool removeModelByIDImmediate(uint32_t modelId);

    VulkanDevice& vulkanDevice;
    ModelRegistry& resourceManager;
    ModelUploader& modelUploader;
    FrameSync& frameSync;
    std::atomic<bool>& isOperating;
    mutable std::mutex executionMutex;
    mutable std::mutex pendingOperationsMutex;
    mutable std::mutex bindingsMutex;
    std::vector<PendingOperation> pendingOperations;
    std::unordered_map<uint64_t, uint32_t> runtimeModelIdBySocketKey;
    std::unordered_map<uint32_t, uint64_t> socketKeyByRuntimeModelId;
    std::unordered_map<uint64_t, std::string> modelPathBySocketKey;
};
