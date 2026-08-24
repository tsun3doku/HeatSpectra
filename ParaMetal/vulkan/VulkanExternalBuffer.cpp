#define NOMINMAX
#include <Windows.h>

#include "VulkanExternalBuffer.hpp"

#include "VulkanDevice.hpp"

#include <vulkan/vulkan_win32.h>

#include <iostream>

VulkanExternalBuffer::~VulkanExternalBuffer() {
    cleanup();
}

bool VulkanExternalBuffer::initialize(
    VulkanDevice& vulkanDevice,
    VkDeviceSize requestedSize,
    VkBufferUsageFlags usage) {
    cleanup();
    if (requestedSize == 0) return false;

    device = vulkanDevice.getDevice();
    bufferSize = requestedSize;

    VkExternalMemoryBufferCreateInfo externalInfo{};
    externalInfo.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO;
    externalInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.pNext = &externalInfo;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        cleanup();
        return false;
    }

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device, buffer, &requirements);
    allocationSize = requirements.size;

    VkExportMemoryAllocateInfo exportInfo{};
    exportInfo.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO;
    exportInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;

    VkMemoryAllocateInfo allocationInfo{};
    allocationInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocationInfo.pNext = &exportInfo;
    allocationInfo.allocationSize = allocationSize;
    allocationInfo.memoryTypeIndex = vulkanDevice.findMemoryType(
        requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(device, &allocationInfo, nullptr, &memory) != VK_SUCCESS ||
        vkBindBufferMemory(device, buffer, memory, 0) != VK_SUCCESS) {
        cleanup();
        return false;
    }

    return true;
}

void VulkanExternalBuffer::cleanup() {
    if (device != VK_NULL_HANDLE && buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, buffer, nullptr);
    }
    if (device != VK_NULL_HANDLE && memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, memory, nullptr);
    }
    buffer = VK_NULL_HANDLE;
    memory = VK_NULL_HANDLE;
    device = VK_NULL_HANDLE;
    bufferSize = 0;
    allocationSize = 0;
}

void* VulkanExternalBuffer::exportWin32Handle() const {
    if (device == VK_NULL_HANDLE || memory == VK_NULL_HANDLE) return nullptr;

    const auto getMemoryHandle = reinterpret_cast<PFN_vkGetMemoryWin32HandleKHR>(
        vkGetDeviceProcAddr(device, "vkGetMemoryWin32HandleKHR"));
    if (!getMemoryHandle) return nullptr;

    VkMemoryGetWin32HandleInfoKHR handleInfo{};
    handleInfo.sType = VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR;
    handleInfo.memory = memory;
    handleInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;

    HANDLE handle = nullptr;
    if (getMemoryHandle(device, &handleInfo, &handle) != VK_SUCCESS) {
        return nullptr;
    }
    return handle;
}
