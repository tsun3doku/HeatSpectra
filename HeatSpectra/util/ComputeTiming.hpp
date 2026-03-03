#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <optional>
#include <vector>

class VulkanDevice;

class ComputeTiming {
public:
    void initialize(const VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight);
    void shutdown();

    void markFrameValid(uint32_t frameIndex, bool isValid);
    std::optional<float> getGpuTimeMs(uint32_t frameIndex) const;

    VkQueryPool getQueryPool() const {
        return queryPool;
    }

    uint32_t getQueryBase(uint32_t frameIndex) const {
        return frameIndex * 2;
    }

private:
    VkDevice device = VK_NULL_HANDLE;
    VkQueryPool queryPool = VK_NULL_HANDLE;
    float timestampPeriod = 0.0f;
    std::vector<uint8_t> validFrames;
};
