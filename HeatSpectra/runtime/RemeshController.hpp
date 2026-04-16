#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "mesh/remesher/Remesher.hpp"
#include "runtime/RemeshSystem.hpp"

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
    };

    RemeshController(Remesher& remesher, VulkanDevice& vulkanDevice, ModelRegistry& resourceManager, std::atomic<bool>& isOperating);

    void configure(const Config& config);
    void disable(uint64_t socketKey);
    void disable();
    bool exportProduct(uint64_t socketKey, RemeshProduct& outProduct) const;

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
    Remesher& remesher;
    std::unordered_map<uint64_t, std::unique_ptr<RemeshSystem>> remeshSystems;
};
