#include "MeshModifiers.hpp"

#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/ResourceManager.hpp"
#include "vulkan/VulkanDevice.hpp"

MeshModifiers::MeshModifiers(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, ResourceManager& resourceManager)
    : resourceManager(resourceManager),
      remesher(vulkanDevice, memoryAllocator) {
}
