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


ResourceManager::ResourceManager(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, UniformBufferManager& uniformBufferManager, VkRenderPass renderPass, uint32_t maxFramesInFlight)
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
    //visModel->setSubdivisionLevel(2);
    //visModel->subdivide();
    //visModel->voronoiTessellate(2);
    //visModel->midpointSubdivide(2, true);
    //visModel->uniformSubdivide(2, 0.25f);
    visModel->isotropicRemesh(0.005f, 5);

    heatModel->init(vulkanDevice, memoryAllocator, HEATSOURCE_PATH);
    //heatModel->setSubdivisionLevel(2);
    //heatModel->subdivide();
    //heatModel->voronoiTessellate(2);
    //heatModel->midpointSubdivide(2, true);
    //heatModel->uniformSubdivide(2, 0.25f);
    heatModel->isotropicRemesh(0.005f, 5);
}

void ResourceManager::cleanup() {
    simModel->cleanup();
    visModel->cleanup();
    heatModel->cleanup();
}