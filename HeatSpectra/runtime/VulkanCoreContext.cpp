#include "VulkanCoreContext.hpp"

#include "vulkan/CommandBufferManager.hpp"
#include "vulkan/MemoryAllocator.hpp"

VulkanCoreContext::~VulkanCoreContext() = default;

bool VulkanCoreContext::initialize(const AppVulkanContext& vulkanContext) {
    if (initialized) {
        return true;
    }

    if (vulkanContext.physicalDevice == VK_NULL_HANDLE ||
        vulkanContext.device == VK_NULL_HANDLE ||
        vulkanContext.graphicsQueue == VK_NULL_HANDLE) {
        return false;
    }

    vulkanDevice.importExternal(
        vulkanContext.physicalDevice,
        vulkanContext.device,
        vulkanContext.graphicsQueue,
        vulkanContext.queueFamilyIndex);

    memoryAllocator = std::make_unique<MemoryAllocator>(vulkanDevice);
    renderCommandPool = std::make_unique<CommandPool>(vulkanDevice, "Render Command Pool");
    initialized = true;
    return true;
}

void VulkanCoreContext::shutdown() {
    renderCommandPool.reset();
    memoryAllocator.reset();
    vulkanDevice.cleanup();
    initialized = false;
}

bool VulkanCoreContext::isInitialized() const {
    return initialized;
}

VulkanDevice& VulkanCoreContext::device() {
    return vulkanDevice;
}

const VulkanDevice& VulkanCoreContext::device() const {
    return vulkanDevice;
}

MemoryAllocator* VulkanCoreContext::allocator() {
    return memoryAllocator.get();
}

const MemoryAllocator* VulkanCoreContext::allocator() const {
    return memoryAllocator.get();
}

CommandPool* VulkanCoreContext::commandPool() {
    return renderCommandPool.get();
}

const CommandPool* VulkanCoreContext::commandPool() const {
    return renderCommandPool.get();
}
