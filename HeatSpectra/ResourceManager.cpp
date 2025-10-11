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

    simModel            = std::make_unique<Model>(vulkanDevice, memoryAllocator, camera);
    visModel            = std::make_unique<Model>(vulkanDevice, memoryAllocator, camera);
    commonSubdivision   = std::make_unique<Model>(vulkanDevice, memoryAllocator, camera);
    heatModel           = std::make_unique<Model>(vulkanDevice, memoryAllocator, camera);
    grid                = std::make_unique<Grid>(vulkanDevice, uniformBufferManager, maxFramesInFlight, renderPass);
    signpostMesh        = std::make_unique<SignpostMesh>();
}

ResourceManager::~ResourceManager() {
}

void ResourceManager::initialize() {
    simModel            ->init(MODEL_PATH);
    visModel            ->init(MODEL_PATH);
    commonSubdivision   ->init(MODEL_PATH);
    heatModel           ->init(HEATSOURCE_PATH);

    performRemeshing(3);
}

void ResourceManager::initializeRemesher() {
    remesher = std::make_unique<iODT>(*visModel, *signpostMesh);
}

void ResourceManager::performRemeshing(int iterations) {
    if (!remesher) {
        initializeRemesher();
    }

    // Print original model stats
    std::cout << "Original model: " << visModel->getVertexCount() << " vertices, " << visModel->getIndices().size() / 3 << " triangles" << std::endl;

    // Run ODT 
    bool success = remesher->optimalDelaunayTriangulation(iterations);
    if (!success) {
        std::cerr << "[ResourceManager] Remeshing failed.\n";
        return;
    }
    
    // Generate intrinsic overlay with common subdivision
    std::cout << "\nCreating common subdivision..." << std::endl;
    remesher->createCommonSubdivision(*commonSubdivision);

    // Save the intrinsic overlay as OBJ for analysis
    remesher->saveCommonSubdivisionOBJ("remeshedIntrinsic.obj", *commonSubdivision);
    //visModel->saveOBJ("remeshed.obj");
}

void ResourceManager::cleanup() {
    simModel->cleanup();
    visModel->cleanup();
    heatModel->cleanup();
}