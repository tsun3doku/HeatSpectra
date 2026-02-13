#include <vulkan/vulkan.h>

#include "VulkanDevice.hpp"
#include "MemoryAllocator.hpp"
#include "CommandBufferManager.hpp"
#include "Model.hpp"
#include "ResourceManager.hpp"
#include "SupportingHalfedge.hpp"
#include "Structs.hpp"
#include "HeatSource.hpp"

HeatSource::HeatSource(VulkanDevice& device, MemoryAllocator& allocator, Model& model, ResourceManager& resManager, CommandPool& cmdPool)
    : vulkanDevice(device), memoryAllocator(allocator), heatModel(model), resourceManager(resManager), renderCommandPool(cmdPool),
      triangleGeometryBuffer(VK_NULL_HANDLE), triangleGeometryBufferOffset_(0) {

    createSourceBuffer();
}

HeatSource::~HeatSource() {
}

std::vector<float> HeatSource::calculateVertexAreas(size_t vertexCount, const std::vector<uint32_t>& indices, const std::function<glm::vec3(uint32_t)>& getVertexPosition) {   
    std::vector<float> vertexAreas(vertexCount, 0.0f);
    size_t numTriangles = indices.size() / 3;
    
    for (size_t t = 0; t < numTriangles; t++) {
        uint32_t i0 = indices[t * 3 + 0];
        uint32_t i1 = indices[t * 3 + 1];
        uint32_t i2 = indices[t * 3 + 2];
        
        glm::vec3 v0 = getVertexPosition(i0);
        glm::vec3 v1 = getVertexPosition(i1);
        glm::vec3 v2 = getVertexPosition(i2);
        
        // Calculate triangle area
        glm::vec3 edge1 = v1 - v0;
        glm::vec3 edge2 = v2 - v0;
        float triArea = glm::length(glm::cross(edge1, edge2)) * 0.5f;
        
        // Distribute 1/3 to each vertex
        vertexAreas[i0] += triArea / 3.0f;
        vertexAreas[i1] += triArea / 3.0f;
        vertexAreas[i2] += triArea / 3.0f;
    }
    
    return vertexAreas;
}

void HeatSource::createSourceBuffer() {
    // Check if model is remeshed 
    auto* iodt = resourceManager.getRemesherForModel(&heatModel);
    bool isRemeshed = (iodt != nullptr);
    
    std::vector<SurfacePoint> surfacePoints;
    std::vector<uint32_t> indices;
    
    if (isRemeshed) {
        // Use remeshed intrinsic vertices
        auto* supportingHalfedge = iodt->getSupportingHalfedge();
        if (supportingHalfedge) {
            auto intrinsicMesh = supportingHalfedge->buildIntrinsicMesh();
            intrinsicVertexCount = intrinsicMesh.vertices.size();
            indices = intrinsicMesh.indices;
            
            // Calculate vertex areas
            std::vector<float> vertexAreas = calculateVertexAreas(
                intrinsicVertexCount,
                indices,
                [&intrinsicMesh](uint32_t idx) { return intrinsicMesh.vertices[idx].position; }
            );
            
            // Initialize with intrinsic vertices
            surfacePoints.resize(intrinsicVertexCount);
            for (size_t i = 0; i < intrinsicVertexCount; i++) {
                surfacePoints[i].position = intrinsicMesh.vertices[i].position;
                surfacePoints[i].temperature = 100.0f;
                surfacePoints[i].normal = intrinsicMesh.vertices[i].normal;
                surfacePoints[i].area = vertexAreas[i];
                surfacePoints[i].color = glm::vec4(1.0f);
            }           
            std::cout << "[HeatSource] Using remeshed data with " << intrinsicVertexCount << " vertices" << std::endl;
        }
    }
    
    // Return if no vertices to process
    if (surfacePoints.empty()) {
        return;
    }
    
    VkDeviceSize bufferSize = sizeof(SurfacePoint) * surfacePoints.size();

    // Allocate staging buffer
    auto [stagingBufferHandle, stagingBufferOffset] = memoryAllocator.allocate(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    // Copy data to staging buffer
    void* mapped = memoryAllocator.getMappedPointer(stagingBufferHandle, stagingBufferOffset);
    memcpy(mapped, surfacePoints.data(), static_cast<size_t>(bufferSize));

    auto [sourceBufferHandle, sourceBufferOffset] = memoryAllocator.allocate(
        bufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    sourceBuffer = sourceBufferHandle;
    sourceBufferOffset_ = sourceBufferOffset;
    
    // Create buffer view for heat rendering
    VkBufferViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO;
    viewInfo.buffer = sourceBuffer;
    viewInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    viewInfo.offset = sourceBufferOffset_;
    viewInfo.range = bufferSize;
    
    if (vkCreateBufferView(vulkanDevice.getDevice(), &viewInfo, nullptr, &sourceBufferView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create source buffer view!");
    }

    // Copy to sourceBuffer
    VkCommandBuffer cmd = renderCommandPool.beginCommands();
    VkBufferCopy region{};
    region.srcOffset = stagingBufferOffset;
    region.dstOffset = sourceBufferOffset_;
    region.size = bufferSize;
    vkCmdCopyBuffer(cmd, stagingBufferHandle, sourceBuffer, 1, &region);
    renderCommandPool.endCommands(cmd);

    // Free staging buffer
    memoryAllocator.free(stagingBufferHandle, stagingBufferOffset);
    
    // Create triangle geometry buffer
    size_t numTriangles = indices.size() / 3;
    triangleCount_ = static_cast<uint32_t>(numTriangles);
    std::vector<HeatSourceTriangleGPU> triangleData(numTriangles);
    std::vector<SurfacePoint> triangleCentroids(numTriangles);
    
    for (size_t i = 0; i < numTriangles; i++) {
        uint32_t i0 = indices[i * 3 + 0];
        uint32_t i1 = indices[i * 3 + 1];
        uint32_t i2 = indices[i * 3 + 2];
        
        glm::vec3 v0 = glm::vec3(surfacePoints[i0].position);
        glm::vec3 v1 = glm::vec3(surfacePoints[i1].position);
        glm::vec3 v2 = glm::vec3(surfacePoints[i2].position);
        
        glm::vec3 center = (v0 + v1 + v2) / 3.0f;
        glm::vec3 edge1 = v1 - v0;
        glm::vec3 edge2 = v2 - v0;
        glm::vec3 cross = glm::cross(edge1, edge2);
        float area = glm::length(cross) * 0.5f;
        glm::vec3 normal = glm::normalize(cross);

        triangleData[i].centerArea = glm::vec4(center, area);
        triangleData[i].normalPad = glm::vec4(normal, 0.0f);
        triangleData[i].indices = glm::uvec4(i0, i1, i2, 0u);

        triangleCentroids[i].position = center;
        triangleCentroids[i].temperature = 100.0f;
        triangleCentroids[i].normal = normal;
        triangleCentroids[i].area = area;
        triangleCentroids[i].color = glm::vec4(1.0f);
    }
    
    VkDeviceSize triBufferSize = sizeof(HeatSourceTriangleGPU) * numTriangles;
    
    // Allocate triangle staging buffer
    auto [triStagingHandle, triStagingOffset] = memoryAllocator.allocate(
        triBufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    
    void* triMapped = memoryAllocator.getMappedPointer(triStagingHandle, triStagingOffset);
    memcpy(triMapped, triangleData.data(), static_cast<size_t>(triBufferSize));
    
    // Allocate triangle geometry buffer
    auto [triBufferHandle, triBufferOffset] = memoryAllocator.allocate(
        triBufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    triangleGeometryBuffer = triBufferHandle;
    triangleGeometryBufferOffset_ = triBufferOffset;
    
    // Copy triangle buffer
    VkCommandBuffer triCmd = renderCommandPool.beginCommands();
    VkBufferCopy triRegion{};
    triRegion.srcOffset = triStagingOffset;
    triRegion.dstOffset = triangleGeometryBufferOffset_;
    triRegion.size = triBufferSize;
    vkCmdCopyBuffer(triCmd, triStagingHandle, triangleGeometryBuffer, 1, &triRegion);
    renderCommandPool.endCommands(triCmd);
    
    memoryAllocator.free(triStagingHandle, triStagingOffset);

    // Create triangle centroid buffer for hash grid build
    VkDeviceSize centroidBufferSize = sizeof(SurfacePoint) * triangleCentroids.size();

    auto [centroidStagingHandle, centroidStagingOffset] = memoryAllocator.allocate(
        centroidBufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    void* centroidMapped = memoryAllocator.getMappedPointer(centroidStagingHandle, centroidStagingOffset);
    memcpy(centroidMapped, triangleCentroids.data(), static_cast<size_t>(centroidBufferSize));

    if (triangleCentroidBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(triangleCentroidBuffer, triangleCentroidBufferOffset_);
        triangleCentroidBuffer = VK_NULL_HANDLE;
        triangleCentroidBufferOffset_ = 0;
    }

    auto [centroidBufferHandle, centroidBufferOffset] = memoryAllocator.allocate(
        centroidBufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    triangleCentroidBuffer = centroidBufferHandle;
    triangleCentroidBufferOffset_ = centroidBufferOffset;

    VkCommandBuffer centroidCmd = renderCommandPool.beginCommands();
    VkBufferCopy centroidRegion{};
    centroidRegion.srcOffset = centroidStagingOffset;
    centroidRegion.dstOffset = triangleCentroidBufferOffset_;
    centroidRegion.size = centroidBufferSize;
    vkCmdCopyBuffer(centroidCmd, centroidStagingHandle, triangleCentroidBuffer, 1, &centroidRegion);
    renderCommandPool.endCommands(centroidCmd);

    memoryAllocator.free(centroidStagingHandle, centroidStagingOffset);
}

void HeatSource::controller(bool upPressed, bool downPressed, bool leftPressed, bool rightPressed, float deltaTime) {
    float moveSpeed = 0.1f * deltaTime;
    glm::vec3 movement(0.0f);

    if (upPressed)
        movement.y += moveSpeed;
    if (downPressed)
        movement.y -= moveSpeed;
    if (rightPressed)
        movement.x += moveSpeed;
    if (leftPressed)
        movement.x -= moveSpeed;

    if (glm::length(movement) > 0.0f) {
        heatModel.translate(movement);
    }
}


size_t HeatSource::getVertexCount() const {
    return intrinsicVertexCount > 0 ? intrinsicVertexCount : heatModel.getVertexCount();
}

void HeatSource::cleanup() {
    if (sourceBufferView != VK_NULL_HANDLE) {
        vkDestroyBufferView(vulkanDevice.getDevice(), sourceBufferView, nullptr);
        sourceBufferView = VK_NULL_HANDLE;
    }
    if (sourceBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(sourceBuffer, sourceBufferOffset_);
        sourceBuffer = VK_NULL_HANDLE;
    }
    if (triangleGeometryBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(triangleGeometryBuffer, triangleGeometryBufferOffset_);
        triangleGeometryBuffer = VK_NULL_HANDLE;
    }
    if (triangleCentroidBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(triangleCentroidBuffer, triangleCentroidBufferOffset_);
        triangleCentroidBuffer = VK_NULL_HANDLE;
    }
}
