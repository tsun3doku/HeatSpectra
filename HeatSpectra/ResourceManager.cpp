#include <vulkan/vulkan.h>

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
}

void ResourceManager::performRemeshing(int iterations) {
    std::cout << "[ResourceManager] Initializing fresh remesher..." << std::endl;
    remesher = std::make_unique<iODT>(*visModel, *signpostMesh);

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

    remesher->saveCommonSubdivisionOBJ("remeshedIntrinsic.obj", *commonSubdivision);

    // Debug untraced intrinsic mesh
    //visModel->saveOBJ("remeshed.obj");
}

void ResourceManager::reloadModels(const std::string& modelPath) {
    std::cout << "[ResourceManager] Reloading models from: " << modelPath << std::endl;
    
    // Clean up old buffers
    simModel->cleanup();
    visModel->cleanup();
    commonSubdivision->cleanup();
    
    // Clear old geometry data
    simModel->setVertices({});
    simModel->setIndices({});
    visModel->setVertices({});
    visModel->setIndices({});
    commonSubdivision->setVertices({});
    commonSubdivision->setIndices({});
    
    // Reload models with new path
    simModel->init(modelPath);
    visModel->init(modelPath);
    commonSubdivision->init(modelPath);
      
    // Reset remesher and signpost mesh
    signpostMesh = std::make_unique<SignpostMesh>();
    remesher.reset();
}

void ResourceManager::cleanup() {
    simModel->cleanup();
    visModel->cleanup();
    heatModel->cleanup();
    commonSubdivision->cleanup();
    grid->cleanup(vulkanDevice);
}