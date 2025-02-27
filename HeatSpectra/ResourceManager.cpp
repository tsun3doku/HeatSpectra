#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <memory>
#include <string>

#include "Model.hpp"
#include "Grid.hpp"
#include "HeatSource.hpp"
#include "HeatSystem.hpp"
#include "UniformBufferManager.hpp"
#include "MemoryAllocator.hpp"
#include "VulkanDevice.hpp"
#include "ResourceManager.hpp"


ResourceManager::ResourceManager(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, UniformBufferManager& uniformBufferManager, VkRenderPass renderPass,  uint32_t maxFramesInFlight)
	: vulkanDevice(vulkanDevice), memoryAllocator(memoryAllocator), uniformBufferManager(uniformBufferManager) {

    simModel = std::make_unique<Model>(vulkanDevice, memoryAllocator);
    visModel = std::make_unique<Model>(vulkanDevice, memoryAllocator);
    heatModel = std::make_unique<Model>(vulkanDevice, memoryAllocator);
    grid = std::make_unique<Grid>(vulkanDevice, uniformBufferManager, maxFramesInFlight, renderPass);
}

ResourceManager::~ResourceManager() {
}

void ResourceManager::initialize() {
    simModel->init(vulkanDevice, memoryAllocator, MODEL_PATH);

    visModel->init(vulkanDevice, memoryAllocator, MODEL_PATH);
    visModel->setSubdivisionLevel(3);
    visModel->subdivide();
    visModel->recreateBuffers();

    heatModel->init(vulkanDevice, memoryAllocator, HEATSOURCE_PATH);
    heatModel->setSubdivisionLevel(2);
    heatModel->subdivide();
    heatModel->recreateBuffers();
}