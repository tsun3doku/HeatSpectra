#include "CudaVulkanDevice.hpp"

#include <cuda_runtime.h>

#include <cstring>

namespace cudaVulkan {

int findDevice(VkPhysicalDevice physicalDevice) {
    if (physicalDevice == VK_NULL_HANDLE) return -1;

    VkPhysicalDeviceIDProperties idProperties{};
    idProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
    VkPhysicalDeviceProperties2 properties{};
    properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    properties.pNext = &idProperties;
    vkGetPhysicalDeviceProperties2(physicalDevice, &properties);

    int count = 0;
    if (cudaGetDeviceCount(&count) != cudaSuccess) return -1;
    for (int device = 0; device < count; ++device) {
        cudaDeviceProp cudaProperties{};
        if (cudaGetDeviceProperties(&cudaProperties, device) != cudaSuccess) continue;
        if (std::memcmp(cudaProperties.uuid.bytes, idProperties.deviceUUID, VK_UUID_SIZE) == 0) {
            return device;
        }
    }
    return -1;
}

} // namespace cudaVulkan
