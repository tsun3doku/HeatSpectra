#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "mesh/remesher/Remesher.hpp"
#include "runtime/RemeshSystem.hpp"
#include "util/Units.hpp"

class MemoryAllocator;
class ModelRegistry;
class VulkanDevice;

struct RemeshProduct;

class RemeshController {
public:
    struct Config {
        std::vector<float> pointPositions;
        std::vector<uint32_t> triangleIndices;
        int iterations = 1;
        float minAngleDegrees = 20.0f;
        float maxEdgeLength = 0.1f;
        float stepSize = 0.25f;
        uint32_t runtimeModelId = 0;
        uint64_t computeHash = 0;
    };

    RemeshController(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, ModelRegistry& modelRegistry, std::atomic<bool>& isOperating);

    void apply(uint64_t socketKey, const Config& config);
    bool buildProduct(uint64_t socketKey, RemeshProduct& product);
    void remove(uint64_t socketKey);
    void disableAll();

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
    std::atomic<bool>& isOperating;
    Remesher remesher;
    std::unordered_map<uint64_t, std::unique_ptr<RemeshSystem>> activeSystems;
};
