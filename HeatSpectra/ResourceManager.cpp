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
    VkRenderPass renderPass, Camera& camera, uint32_t maxFramesInFlight, CommandPool* asyncCommandPool, CommandPool* renderCommandPool)
    : vulkanDevice(vulkanDevice), memoryAllocator(memoryAllocator), uniformBufferManager(uniformBufferManager), camera(camera) {

    visModel            = std::make_unique<Model>(vulkanDevice, memoryAllocator, camera, *renderCommandPool);
    commonSubdivision   = std::make_unique<Model>(vulkanDevice, memoryAllocator, camera, *renderCommandPool);
    heatModel           = std::make_unique<Model>(vulkanDevice, memoryAllocator, camera, *renderCommandPool);
    grid                = std::make_unique<Grid>(vulkanDevice, uniformBufferManager, maxFramesInFlight, renderPass);
    signpostMesh        = std::make_unique<SignpostMesh>();
}

ResourceManager::~ResourceManager() {
}

void ResourceManager::initialize() {
    visModel            ->init(MODEL_PATH);
    commonSubdivision   ->init(MODEL_PATH);
    heatModel           ->init(HEATSOURCE_PATH);
}

void ResourceManager::performRemeshing(int iterations, double minAngleDegrees, double maxEdgeLength, double stepSize) {
    remesher = std::make_unique<iODT>(*visModel, *signpostMesh);

    std::cout << "Original model: " << visModel->getVertexCount() << " vertices, " << visModel->getIndices().size() / 3 << " triangles" << std::endl;

    // Run ODT with user-specified parameters
    bool success = remesher->optimalDelaunayTriangulation(iterations, minAngleDegrees, maxEdgeLength, stepSize);
    if (!success) {
        std::cerr << "[ResourceManager] Remeshing failed.\n";
        return;
    }
    
    // Generate intrinsic overlay with common subdivision
    std::cout << "\nCreating common subdivision..." << std::endl;
    remesher->createCommonSubdivision(*commonSubdivision);

    //remesher->saveCommonSubdivisionOBJ("remeshedIntrinsic.obj", *commonSubdivision);

    // Debug untraced intrinsic mesh
    //visModel->saveOBJ("remeshed.obj");
}

void ResourceManager::reloadModels(const std::string& modelPath) {
    std::cout << "[ResourceManager] Reloading models from: " << modelPath << std::endl;
    
    // Clean up old buffers
    visModel->cleanup();
    commonSubdivision->cleanup();
    
    // Clear old geometry data
    visModel->setVertices({});
    visModel->setIndices({});
    commonSubdivision->setVertices({});
    commonSubdivision->setIndices({});
    
    // Reload models with new path
    visModel->init(modelPath);
    commonSubdivision->init(modelPath);
      
    // Reset remesher and signpost mesh
    signpostMesh = std::make_unique<SignpostMesh>();
    remesher.reset();
}

void ResourceManager::cleanup() {
    visModel->cleanup();
    heatModel->cleanup();
    commonSubdivision->cleanup();
    grid->cleanup(vulkanDevice);
}