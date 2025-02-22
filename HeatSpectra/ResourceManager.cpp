#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <memory>
#include <string>

#include "Model.hpp"
#include "HeatSource.hpp"
#include "HeatSystem.hpp"
#include "UniformBufferManager.hpp"
#include "MemoryAllocator.hpp"
#include "VulkanDevice.hpp"
#include "ResourceManager.hpp"


ResourceManager::ResourceManager(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, uint32_t maxFramesInFlight)
	: vulkanDevice(vulkanDevice), memoryAllocator(memoryAllocator) {
    simModel = std::make_unique<Model>(vulkanDevice, memoryAllocator);
    visModel = std::make_unique<Model>(vulkanDevice, memoryAllocator);
    heatModel = std::make_unique<Model>(vulkanDevice, memoryAllocator);
    uniformBufferManager = std::make_unique<UniformBufferManager>();
}

ResourceManager::~ResourceManager() {
}

void ResourceManager::initialize() {
    simModel->init(vulkanDevice, memoryAllocator, MODEL_PATH);

    visModel->init(vulkanDevice, memoryAllocator, MODEL_PATH);
    visModel->setSubdivisionLevel(2);
    visModel->subdivide();
    visModel->recreateBuffers();

    heatModel->init(vulkanDevice, memoryAllocator, HEATSOURCE_PATH);

}