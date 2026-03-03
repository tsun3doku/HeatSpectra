#include "VulkanDevice.hpp"
#include <iostream>

void VulkanDevice::importExternal(VkPhysicalDevice physicalDevice, VkDevice device, VkQueue graphicsQueue, uint32_t queueFamilyIndex) {
    this->physicalDevice = physicalDevice;
    this->device = device;
    this->graphicsQueue = graphicsQueue;
    this->computeQueue = graphicsQueue;
    this->presentQueue = graphicsQueue;
    vkGetPhysicalDeviceProperties(physicalDevice, &physicalDeviceProperties);

    VkPhysicalDeviceDepthStencilResolveProperties depthResolveProps{};
    depthResolveProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_STENCIL_RESOLVE_PROPERTIES;

    VkPhysicalDeviceProperties2 props2{};
    props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    props2.pNext = &depthResolveProps;
    vkGetPhysicalDeviceProperties2(physicalDevice, &props2);

    if (depthResolveProps.supportedDepthResolveModes & VK_RESOLVE_MODE_SAMPLE_ZERO_BIT) {
        depthResolveMode = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;
    } else if (depthResolveProps.supportedDepthResolveModes & VK_RESOLVE_MODE_MAX_BIT) {
        depthResolveMode = VK_RESOLVE_MODE_MAX_BIT;
    } else if (depthResolveProps.supportedDepthResolveModes & VK_RESOLVE_MODE_MIN_BIT) {
        depthResolveMode = VK_RESOLVE_MODE_MIN_BIT;
    } else {
        depthResolveMode = VK_RESOLVE_MODE_NONE;
    }

    queueFamilyIndices.graphicsFamily = queueFamilyIndex;
    queueFamilyIndices.graphicsAndComputeFamily = queueFamilyIndex;
    queueFamilyIndices.presentFamily = queueFamilyIndex;
}

void VulkanDevice::cleanup() {
    device = VK_NULL_HANDLE;
    graphicsQueue = VK_NULL_HANDLE;
    computeQueue = VK_NULL_HANDLE;
    presentQueue = VK_NULL_HANDLE;
    physicalDevice = VK_NULL_HANDLE;
    physicalDeviceProperties = {};
    queueFamilyIndices = {};
    depthResolveMode = VK_RESOLVE_MODE_NONE;
}

bool VulkanDevice::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties, uint32_t& outMemoryTypeIndex) const {
    outMemoryTypeIndex = UINT32_MAX;
    if (physicalDevice == VK_NULL_HANDLE) {
        return false;
    }

    VkPhysicalDeviceMemoryProperties memProperties{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            outMemoryTypeIndex = i;
            return true;
        }
    }

    return false;
}

uint32_t VulkanDevice::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const {
    uint32_t memoryTypeIndex = UINT32_MAX;
    (void)findMemoryType(typeFilter, properties, memoryTypeIndex);
    return memoryTypeIndex;
}

VkResult VulkanDevice::createBuffer(
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties,
    VkDeviceMemory& bufferMemory,
    VkBuffer& outBuffer) {
    bufferMemory = VK_NULL_HANDLE;
    outBuffer = VK_NULL_HANDLE;
    if (device == VK_NULL_HANDLE || size == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer buffer = VK_NULL_HANDLE;
    const VkResult createResult = vkCreateBuffer(device, &bufferInfo, nullptr, &buffer);
    if (createResult != VK_SUCCESS) {
        return createResult;
    }

    VkMemoryRequirements memRequirements{};
    vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

    uint32_t memoryTypeIndex = UINT32_MAX;
    if (!findMemoryType(memRequirements.memoryTypeBits, properties, memoryTypeIndex)) {
        vkDestroyBuffer(device, buffer, nullptr);
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = memoryTypeIndex;

    const VkResult allocResult = vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory);
    if (allocResult != VK_SUCCESS) {
        vkDestroyBuffer(device, buffer, nullptr);
        return allocResult;
    }

    const VkResult bindResult = vkBindBufferMemory(device, buffer, bufferMemory, 0);
    if (bindResult != VK_SUCCESS) {
        vkFreeMemory(device, bufferMemory, nullptr);
        vkDestroyBuffer(device, buffer, nullptr);
        bufferMemory = VK_NULL_HANDLE;
        return bindResult;
    }

    outBuffer = buffer;
    return VK_SUCCESS;
}

VkBuffer VulkanDevice::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkDeviceMemory& bufferMemory) {
    VkBuffer buffer = VK_NULL_HANDLE;
    const VkResult result = createBuffer(size, usage, properties, bufferMemory, buffer);
    if (result != VK_SUCCESS) {
        std::cerr << "[VulkanDevice] createBuffer failed with VkResult=" << result << std::endl;
        bufferMemory = VK_NULL_HANDLE;
        return VK_NULL_HANDLE;
    }
    return buffer;
}
