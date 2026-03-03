#include "ComputeTiming.hpp"

#include "vulkan/VulkanDevice.hpp"

void ComputeTiming::initialize(const VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight) {
    shutdown();

    device = vulkanDevice.getDevice();

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(vulkanDevice.getPhysicalDevice(), &properties);
    timestampPeriod = properties.limits.timestampPeriod;
    validFrames.assign(maxFramesInFlight, 0);

    if (timestampPeriod <= 0.0f || maxFramesInFlight == 0) {
        return;
    }

    VkQueryPoolCreateInfo queryInfo{};
    queryInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    queryInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    queryInfo.queryCount = maxFramesInFlight * 2;

    if (vkCreateQueryPool(device, &queryInfo, nullptr, &queryPool) != VK_SUCCESS) {
        queryPool = VK_NULL_HANDLE;
        timestampPeriod = 0.0f;
    }
}

void ComputeTiming::shutdown() {
    validFrames.clear();
    timestampPeriod = 0.0f;

    if (queryPool != VK_NULL_HANDLE && device != VK_NULL_HANDLE) {
        vkDestroyQueryPool(device, queryPool, nullptr);
    }

    queryPool = VK_NULL_HANDLE;
    device = VK_NULL_HANDLE;
}

void ComputeTiming::markFrameValid(uint32_t frameIndex, bool isValid) {
    if (frameIndex >= validFrames.size()) {
        return;
    }

    validFrames[frameIndex] = isValid ? 1 : 0;
}

std::optional<float> ComputeTiming::getGpuTimeMs(uint32_t frameIndex) const {
    if (queryPool == VK_NULL_HANDLE ||
        device == VK_NULL_HANDLE ||
        timestampPeriod <= 0.0f ||
        frameIndex >= validFrames.size() ||
        validFrames[frameIndex] == 0) {
        return std::nullopt;
    }

    uint64_t timestamps[2] = {};
    const VkResult result = vkGetQueryPoolResults(
        device,
        queryPool,
        frameIndex * 2,
        2,
        sizeof(timestamps),
        timestamps,
        sizeof(uint64_t),
        VK_QUERY_RESULT_64_BIT);

    if (result != VK_SUCCESS || timestamps[1] <= timestamps[0]) {
        return std::nullopt;
    }

    return static_cast<float>(timestamps[1] - timestamps[0]) * timestampPeriod * 1e-6f;
}
