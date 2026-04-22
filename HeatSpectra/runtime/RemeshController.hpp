#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "mesh/remesher/Remesher.hpp"
#include "runtime/RemeshSystem.hpp"
#include "runtime/RuntimeProducts.hpp"

class MemoryAllocator;
class ModelRegistry;
class VulkanDevice;

class RemeshController {
public:
    struct Config {
        uint64_t socketKey = 0;
        std::vector<float> pointPositions;
        std::vector<uint32_t> triangleIndices;
        int iterations = 1;
        float minAngleDegrees = 30.0f;
        float maxEdgeLength = 0.1f;
        float stepSize = 0.25f;
        uint32_t runtimeModelId = 0;
        uint64_t computeHash = 0;
    };

    RemeshController(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, ModelRegistry& resourceManager, std::atomic<bool>& isOperating);

    void configure(const Config& config);
    void disable(uint64_t socketKey);
    void disable();
    bool exportProduct(uint64_t socketKey, RemeshProduct& outProduct) const;

    Remesher& getRemesher() { return remesher; }
    const Remesher& getRemesher() const { return remesher; }

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
    std::atomic<bool>& isOperating;
    Remesher remesher;
    std::unordered_map<uint64_t, std::unique_ptr<RemeshSystem>> activeSystems;
    std::unordered_map<uint64_t, Config> configuredConfigs;
};

inline uint64_t buildComputeHash(const RemeshController::Config& config) {
    uint64_t hash = 1469598103934665603ull;
    hash = RuntimeProductHash::mixPod(hash, config.socketKey);
    hash = RuntimeProductHash::mixPodVector(hash, config.pointPositions);
    hash = RuntimeProductHash::mixPodVector(hash, config.triangleIndices);
    hash = RuntimeProductHash::mixPod(hash, config.iterations);
    hash = RuntimeProductHash::mixPod(hash, config.minAngleDegrees);
    hash = RuntimeProductHash::mixPod(hash, config.maxEdgeLength);
    hash = RuntimeProductHash::mixPod(hash, config.stepSize);
    hash = RuntimeProductHash::mixPod(hash, config.runtimeModelId);
    return hash;
}
