#include "MeshModifiers.hpp"

#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/ModelRegistry.hpp"
#include "vulkan/VulkanDevice.hpp"

MeshModifiers::MeshModifiers(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, ModelRegistry& resourceManager)
    : resourceManager(resourceManager),
      remesher(vulkanDevice, memoryAllocator) {
}

