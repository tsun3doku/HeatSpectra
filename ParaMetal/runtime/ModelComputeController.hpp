#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <glm/mat4x4.hpp>

#include "hash/HashValues.hpp"
#include "runtime/RuntimeProducts.hpp"

class FrameSync;
class ModelUploader;
class ModelRegistry;
class VulkanDevice;

class ModelComputeController {
public:
    struct Config {
        std::vector<float> pointPositions;
        std::vector<uint32_t> triangleIndices;
        std::vector<float> renderPositions;
        std::vector<float> renderNormals;
        std::vector<float> renderTexcoords;
        std::vector<uint32_t> renderIndices;
        uint64_t computeHash = 0;
    };

    ModelComputeController(
        VulkanDevice& vulkanDevice,
        ModelRegistry& modelRegistry,
        ModelUploader& modelUploader,
        FrameSync& frameSync,
        std::atomic<bool>& isOperating);

    void apply(uint64_t socketKey, const Config& config);
    bool buildProduct(uint64_t socketKey, ModelProduct& product) const;
    void remove(uint64_t socketKey);
    void disableAll();
    bool tryGetSocketKey(uint32_t runtimeModelId, uint64_t& outSocketKey) const;

private:
    class OperatingScope {
    public:
        explicit OperatingScope(std::atomic<bool>& isOperating);
        ~OperatingScope();

    private:
        std::atomic<bool>& isOperating;
        bool previousState = false;
    };

    bool rebuildModel(uint64_t socketKey, const Config& config, uint32_t preferredModelId);
    bool removeModel(uint64_t socketKey);

    VulkanDevice& vulkanDevice;
    ModelRegistry& modelRegistry;
    ModelUploader& modelUploader;
    FrameSync& frameSync;
    std::atomic<bool>& isOperating;
    mutable std::mutex executionMutex;
    std::unordered_map<uint64_t, uint32_t> runtimeModelIdBySocketKey;
    std::unordered_map<uint32_t, uint64_t> socketKeyByRuntimeModelId;
    std::unordered_map<uint64_t, uint64_t> configuredComputeHashes;
};
