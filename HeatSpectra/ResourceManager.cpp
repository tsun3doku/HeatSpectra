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
#include "SurfelRenderer.hpp"
#include "UniformBufferManager.hpp"
#include "MemoryAllocator.hpp"
#include "VulkanDevice.hpp"
#include "ResourceManager.hpp"
#include "TimingOverlay.hpp"

ResourceManager::ResourceManager(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, UniformBufferManager& uniformBufferManager, 
    VkRenderPass renderPass, Camera& camera, uint32_t maxFramesInFlight, uint32_t overlaySubpass, CommandPool* asyncCommandPool, CommandPool* renderCommandPool)
    : vulkanDevice(vulkanDevice), memoryAllocator(memoryAllocator), uniformBufferManager(uniformBufferManager), camera(camera) {
    (void)asyncCommandPool;

    visModel            = std::make_unique<Model>(vulkanDevice, memoryAllocator, camera, *renderCommandPool);
    commonSubdivision   = std::make_unique<Model>(vulkanDevice, memoryAllocator, camera, *renderCommandPool);
    heatModel           = std::make_unique<Model>(vulkanDevice, memoryAllocator, camera, *renderCommandPool);
    grid                = std::make_unique<Grid>(vulkanDevice, memoryAllocator, uniformBufferManager, maxFramesInFlight, renderPass, *renderCommandPool);
    timingOverlay       = std::make_unique<TimingOverlay>(vulkanDevice, maxFramesInFlight, renderPass, overlaySubpass, *renderCommandPool);
}

ResourceManager::~ResourceManager() {
}

void ResourceManager::initialize() {
    visModel            ->init(MODEL_PATH);
    commonSubdivision   ->init(MODEL_PATH);
    heatModel           ->init(HEATSOURCE_PATH);
}

void ResourceManager::performRemeshing(Model* targetModel, int iterations, double minAngleDegrees, double maxEdgeLength, double stepSize,
                                      CommandPool& cmdPool, uint32_t maxFramesInFlight) {
    (void)cmdPool;
    (void)maxFramesInFlight;
    if (!targetModel) {
        std::cerr << "[ResourceManager] Cannot remesh null model" << std::endl;
        return;
    }
        
    // Get or create remesh data for this model
    RemeshData& remeshData = modelRemeshData[targetModel];
    
    remeshData.remesher = std::make_unique<iODT>(*targetModel, vulkanDevice, memoryAllocator);
    
    // Perform remeshing
    bool success = remeshData.remesher->optimalDelaunayTriangulation(iterations, minAngleDegrees, maxEdgeLength, stepSize);
    if (!success) {
        std::cerr << "[ResourceManager] Remeshing failed for model" << std::endl;
        return;
    }
    
    remeshData.isRemeshed = true;
    
    // Create surfel renderer for this remeshed model
    auto* supportingHalfedge = remeshData.remesher->getSupportingHalfedge();
    if (supportingHalfedge) {
        remeshData.surfel = std::make_unique<SurfelRenderer>(vulkanDevice, memoryAllocator, uniformBufferManager);
        std::cout << "[ResourceManager] Created surfel renderer for model" << std::endl;
    }
}

void ResourceManager::performRemeshingOnSelected(int iterations, double minAngleDegrees, double maxEdgeLength, double stepSize,
                                                 CommandPool& cmdPool, uint32_t maxFramesInFlight) {
    if (selectedModel) {
        performRemeshing(selectedModel, iterations, minAngleDegrees, maxEdgeLength, stepSize, cmdPool, maxFramesInFlight);
    } else {
        std::cerr << "[ResourceManager] No model selected for remeshing" << std::endl;
    }
}

bool ResourceManager::isModelRemeshed(Model* model) const {
    if (!model) return false;
    auto it = modelRemeshData.find(model);
    return it != modelRemeshData.end() && it->second.isRemeshed;
}

bool ResourceManager::areRequiredModelsRemeshed() const {
    // Heat system requires both heatModel and visModel to be remeshed
    return isModelRemeshed(heatModel.get()) && isModelRemeshed(visModel.get());
}

iODT* ResourceManager::getRemesherForModel(Model* model) {
    if (!model) return nullptr;
    
    auto it = modelRemeshData.find(model);
    if (it != modelRemeshData.end()) {
        return it->second.remesher.get();
    }
    return nullptr;
}

SurfelRenderer* ResourceManager::getSurfelPerModel(Model* model) {
    if (!model) return nullptr;
    
    auto it = modelRemeshData.find(model);
    if (it != modelRemeshData.end()) {
        return it->second.surfel.get();
    }
    return nullptr;
}

Model* ResourceManager::getModelByID(uint32_t modelID) {
    // Map model IDs to Model pointers
    // ID 1 = visModel, ID 2 = heatModel
    switch (modelID) {
        case 1: return visModel.get();
        case 2: return heatModel.get();
        default: return nullptr;
    }
}

uint32_t ResourceManager::getModelID(Model* model) const {
    // Map Model pointers to IDs
    if (model == visModel.get()) return 1;
    if (model == heatModel.get()) return 2;
    return 0; // Invalid/unknown model
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
      
    // Clear all remeshing data
    modelRemeshData.clear();
    selectedModel = nullptr;
}

void ResourceManager::buildCommonSubdivision(bool enable) {
    if (!enable) {
        return;
    }
    if (!isModelRemeshed(visModel.get())) {
        std::cerr << "[ResourceManager] Cannot build common subdivision: VisModel not remeshed" << std::endl;
        return;
    }

    iODT* remesher = getRemesherForModel(visModel.get());
    if (!remesher) return;

    // Use iODT to create the common subdivision and populate intrinsic triangles
    remesher->createCommonSubdivision(*commonSubdivision, intrinsicTriangles);
    
    std::cout << "[ResourceManager] Built Common Subdivision with " << intrinsicTriangles.size() << " intrinsic triangles." << std::endl;
}

void ResourceManager::cleanup() {
    // Cleanup models 
    visModel->cleanup();
    heatModel->cleanup();
    commonSubdivision->cleanup();
    
    // Cleanup grid
    grid->cleanup(vulkanDevice);
    if (timingOverlay) {
        timingOverlay->cleanup();
    }
    
    // Cleanup all remeshers
    for (auto& [model, remeshData] : modelRemeshData) {
        if (remeshData.remesher) {
            remeshData.remesher->cleanup();
        }
    }
    modelRemeshData.clear();
}

void ResourceManager::updateTimingOverlayText(const std::vector<std::string>& lines) {
    if (timingOverlay) {
        timingOverlay->setLines(lines);
    }
}

void ResourceManager::renderTimingOverlay(VkCommandBuffer commandBuffer, uint32_t currentFrame, VkExtent2D extent) {
    if (timingOverlay) {
        timingOverlay->render(commandBuffer, currentFrame, extent);
    }
}

glm::vec3 ResourceManager::calculateMaxBoundingBoxSize() const {
    glm::vec3 globalMin = glm::vec3(std::numeric_limits<float>::max());
    glm::vec3 globalMax = glm::vec3(std::numeric_limits<float>::lowest());

    // Check visModel
    if (visModel && visModel->getVertexCount() > 0) {
        glm::vec3 minBound = visModel->getBoundingBoxMin();
        glm::vec3 maxBound = visModel->getBoundingBoxMax();
        globalMin = glm::min(globalMin, minBound);
        globalMax = glm::max(globalMax, maxBound);
    }

    // Check heatModel
    if (heatModel && heatModel->getVertexCount() > 0) {
        glm::vec3 minBound = heatModel->getBoundingBoxMin();
        glm::vec3 maxBound = heatModel->getBoundingBoxMax();
        globalMin = glm::min(globalMin, minBound);
        globalMax = glm::max(globalMax, maxBound);
    }

    // Calculate the size of the bounding box
    glm::vec3 size = globalMax - globalMin;
    
    // Snap to multiples of 1 units to keep greater lines aligned
    float greaterLineInterval = 1.0f;  // 10 grid cells = 1.0 world unit
    float snappedWidth = std::ceil(size.x / greaterLineInterval) * greaterLineInterval;
    float snappedDepth = std::ceil(size.z / greaterLineInterval) * greaterLineInterval;
    float snappedHeight = std::ceil(size.y / greaterLineInterval) * greaterLineInterval;
    
    // Minimum of 1 unit
    float minSize = 1.0f;
    snappedWidth = glm::max(snappedWidth, minSize);
    snappedDepth = glm::max(snappedDepth, minSize);
    snappedHeight = glm::max(snappedHeight, minSize);
    
    // Add 1 unit padding if model meets or exceeds the grid bounds
    float padding = 1.0f;
    if (size.x >= snappedWidth) snappedWidth += padding;
    if (size.z >= snappedDepth) snappedDepth += padding;
    if (size.y >= snappedHeight) snappedHeight += padding;
      
    return glm::vec3(snappedWidth, snappedDepth, snappedHeight);
}
