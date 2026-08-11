#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include "runtime/RuntimeProducts.hpp"
#include "util/Units.hpp"

class VulkanDevice;
class MemoryAllocator;
class CommandPool;

class PointComputeRuntime {
public:
    struct Config {
        uint64_t socketKey = 0;
        std::vector<glm::vec4> positions;
        glm::mat4 modelMatrix = glm::mat4(1.0f);
        uint64_t computeHash = 0;
    };

    PointComputeRuntime(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, CommandPool& commandPool);
    ~PointComputeRuntime();

    bool buildProduct(const Config& config, PointProduct& product);
    void disable(uint64_t socketKey);
    void disableAll();

private:
    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    CommandPool& commandPool;
    std::unordered_map<uint64_t, uint64_t> activeHashes;
};
