#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <memory>
#include <string>

#include "iODT.hpp"
#include "SignPostMesh.hpp"
#include "Model.hpp"
#include "Grid.hpp"
#include "Camera.hpp"
#include "HeatSource.hpp"
#include "HeatSystem.hpp"
#include "UniformBufferManager.hpp"
#include "MemoryAllocator.hpp"
#include "VulkanDevice.hpp"
#include "ResourceManager.hpp"

ResourceManager::ResourceManager(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, UniformBufferManager& uniformBufferManager, 
    VkRenderPass renderPass, Camera& camera, uint32_t maxFramesInFlight)
    : vulkanDevice(vulkanDevice), memoryAllocator(memoryAllocator), uniformBufferManager(uniformBufferManager), camera(camera) {

    simModel         = std::make_unique<Model>(vulkanDevice, memoryAllocator, camera);
    visModel         = std::make_unique<Model>(vulkanDevice, memoryAllocator, camera);
    heatModel        = std::make_unique<Model>(vulkanDevice, memoryAllocator, camera);
    grid             = std::make_unique<Grid>(vulkanDevice, uniformBufferManager, maxFramesInFlight, renderPass);
    signpostMesh     = std::make_unique<SignpostMesh>();
}

ResourceManager::~ResourceManager() {
}

void ResourceManager::initialize() {
    simModel->init(MODEL_PATH);
    visModel->init(MODEL_PATH);
    heatModel->init(HEATSOURCE_PATH);

    //performRemeshing(0.01f, 1);
}

void ResourceManager::initializeRemesher() {
    //remesher = std::make_unique<iODT>(*visModel, *signpostMesh);
}
/*
void ResourceManager::performRemeshing(float targetEdgeLength, int iterations) {
    if (!remesher) {
        initializeRemesher();
    }

    // Print original model stats
    std::cout << "Original model: " << visModel->getVertexCount() << " vertices, "
        << visModel->getIndices().size() / 3 << " triangles" << std::endl;

    for (int iter = 0; iter < iterations; iter++) {
        std::cout << "Iteration " << (iter + 1)
            << "/" << iterations << std::endl;

        signpostMesh->buildFromModel(*visModel);

        bool success = remesher->optimalDelaunayTriangulation(1);
        if (!success) {
            std::cerr << "[ResourceManager] Remeshing iteration "
                << (iter + 1) << " failed.\n";
            break;
        }
    }
    visModel->saveOBJ("remeshed.obj");
}
*/
void ResourceManager::cleanup() {
    simModel->cleanup();
    visModel->cleanup();
    heatModel->cleanup();
}