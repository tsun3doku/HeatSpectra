#include "HeatReceiver.hpp"
#include "VulkanDevice.hpp"
#include "MemoryAllocator.hpp"
#include "Model.hpp"
#include "CommandBufferManager.hpp"
#include "ResourceManager.hpp"
#include "iODT.hpp"
#include "VulkanBuffer.hpp"
#include "UniformBufferManager.hpp"
#include "HeatSource.hpp"
#include "HashGrid.hpp"
#include "SurfelRenderer.hpp"
#include "Structs.hpp"
#include "VoronoiSeeder.hpp"
#include "VoronoiKDTree.hpp"

#include <iostream>
#include <algorithm>
#include <array>
#include <cstring>

HeatReceiver::HeatReceiver(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, Model& receiverModel, 
    ResourceManager& resourceManager, CommandPool& renderCommandPool, uint32_t maxFramesInFlight)
    : vulkanDevice(vulkanDevice), memoryAllocator(memoryAllocator), receiverModel(receiverModel), 
    resourceManager(resourceManager), renderCommandPool(renderCommandPool), maxFramesInFlight(maxFramesInFlight) {
}

HeatReceiver::~HeatReceiver() {
}

void HeatReceiver::createReceiverBuffers() {
    auto* iodt = resourceManager.getRemesherForModel(&receiverModel);
    if (!iodt)
        return;
    
    auto* supportingHalfedge = iodt->getSupportingHalfedge();
    if (!supportingHalfedge || !supportingHalfedge->isUploadedToGPU()) {
        std::cerr << "[HeatReceiver] Supporting halfedge not ready for model" << std::endl;
        return;
    }
    
    auto intrinsicMeshData = supportingHalfedge->buildIntrinsicMesh();
    size_t vertexCount = intrinsicMeshData.vertices.size();

    size_t triangleCount = intrinsicMeshData.indices.size() / 3;
    
    if (vertexCount == 0) {
        std::cerr << "[HeatReceiver] Model has 0 intrinsic vertices" << std::endl;
        return;
    }
    
    
    intrinsicVertexCount = vertexCount;
    intrinsicTriangleCount = triangleCount;

    VkDeviceSize vertexBufferSize = sizeof(SurfacePoint) * vertexCount;
    VkDeviceSize triangleIndicesBufferSize = sizeof(uint32_t) * intrinsicMeshData.indices.size();

    VkDeviceSize storageAlignment = vulkanDevice.getPhysicalDeviceProperties().limits.minStorageBufferOffsetAlignment;

    
    createTexelBuffer(
        memoryAllocator, vulkanDevice,
        nullptr, vertexBufferSize,
        VK_FORMAT_R32G32B32A32_SFLOAT,
        surfaceBuffer, surfaceBufferOffset, surfaceBufferView,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        16 // 16 byte alignment
    );
    
    createVertexBuffer(
        memoryAllocator,
        vertexBufferSize,
        surfaceVertexBuffer, surfaceVertexBufferOffset
    );
    
    // Interpolation buffer logic removed (as per TetGen cleanup)
    
    // Upload triangle indices (device local) via staging
    {
        if (triangleIndicesBuffer != VK_NULL_HANDLE) {
            memoryAllocator.free(triangleIndicesBuffer, triangleIndicesBufferOffset);
            triangleIndicesBuffer = VK_NULL_HANDLE;
            triangleIndicesBufferOffset = 0;
        }

        VkBuffer stagingBuffer;
        VkDeviceSize stagingOffset;
        void* stagingData;
        createStagingBuffer(
            memoryAllocator,
            triangleIndicesBufferSize,
            stagingBuffer,
            stagingOffset,
            &stagingData
        );
        memcpy(stagingData, intrinsicMeshData.indices.data(), static_cast<size_t>(triangleIndicesBufferSize));

        auto [triIdxHandle, triIdxOffset] = memoryAllocator.allocate(
            triangleIndicesBufferSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            storageAlignment
        );
        triangleIndicesBuffer = triIdxHandle;
        triangleIndicesBufferOffset = triIdxOffset;

        VkCommandBuffer cmd = renderCommandPool.beginCommands();
        VkBufferCopy region{};
        region.srcOffset = stagingOffset;
        region.dstOffset = triangleIndicesBufferOffset;
        region.size = triangleIndicesBufferSize;
        vkCmdCopyBuffer(cmd, stagingBuffer, triangleIndicesBuffer, 1, &region);
        renderCommandPool.endCommands(cmd);

        memoryAllocator.free(stagingBuffer, stagingOffset);
    }

    // Upload triangle centroids (device local) via staging
    {
        if (triangleCentroidBuffer != VK_NULL_HANDLE) {
            memoryAllocator.free(triangleCentroidBuffer, triangleCentroidBufferOffset);
            triangleCentroidBuffer = VK_NULL_HANDLE;
            triangleCentroidBufferOffset = 0;
        }

        std::vector<SurfacePoint> triCentroids(triangleCount);
        for (size_t t = 0; t < triangleCount; t++) {
            uint32_t i0 = intrinsicMeshData.indices[t * 3 + 0];
            uint32_t i1 = intrinsicMeshData.indices[t * 3 + 1];
            uint32_t i2 = intrinsicMeshData.indices[t * 3 + 2];

            glm::vec3 v0 = intrinsicMeshData.vertices[i0].position;
            glm::vec3 v1 = intrinsicMeshData.vertices[i1].position;
            glm::vec3 v2 = intrinsicMeshData.vertices[i2].position;

            glm::vec3 center = (v0 + v1 + v2) / 3.0f;
            glm::vec3 edge1 = v1 - v0;
            glm::vec3 edge2 = v2 - v0;
            glm::vec3 cross = glm::cross(edge1, edge2);
            float area = glm::length(cross) * 0.5f;
            glm::vec3 normal = intrinsicMeshData.vertices[i0].normal +
                              intrinsicMeshData.vertices[i1].normal +
                              intrinsicMeshData.vertices[i2].normal;
            float nLen = glm::length(normal);
            if (nLen > 0.0f) {
                normal /= nLen;
            }

            triCentroids[t].position = center;
            triCentroids[t].temperature = AMBIENT_TEMPERATURE;
            triCentroids[t].normal = normal;
            triCentroids[t].area = area;
            triCentroids[t].color = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        }

        VkDeviceSize triCentroidSize = sizeof(SurfacePoint) * triangleCount;

        VkBuffer stagingBuffer;
        VkDeviceSize stagingOffset;
        void* stagingData;
        createStagingBuffer(
            memoryAllocator,
            triCentroidSize,
            stagingBuffer,
            stagingOffset,
            &stagingData
        );
        memcpy(stagingData, triCentroids.data(), static_cast<size_t>(triCentroidSize));

        auto [triCentroidHandle, triCentroidOffset] = memoryAllocator.allocate(
            triCentroidSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            storageAlignment
        );
        triangleCentroidBuffer = triCentroidHandle;
        triangleCentroidBufferOffset = triCentroidOffset;

        VkCommandBuffer cmd = renderCommandPool.beginCommands();
        VkBufferCopy region{};
        region.srcOffset = stagingOffset;
        region.dstOffset = triangleCentroidBufferOffset;
        region.size = triCentroidSize;
        vkCmdCopyBuffer(cmd, stagingBuffer, triangleCentroidBuffer, 1, &region);
        renderCommandPool.endCommands(cmd);

        memoryAllocator.free(stagingBuffer, stagingOffset);
    }
    
    // Create Voronoi mapping buffer
    VkDeviceSize voronoiMappingBufferSize = sizeof(VoronoiSurfaceMapping) * vertexCount;
    void* voronoiMappedPtr;
    createStorageBuffer(
        memoryAllocator, vulkanDevice,
        nullptr, voronoiMappingBufferSize,
        voronoiMappingBuffer, voronoiMappingBufferOffset, &voronoiMappedPtr,
        true,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT
    );
    
    std::cout << "[HeatReceiver] Created buffers for " << vertexCount << " vertices, "
              << triangleCount << " triangles" << std::endl;
}


// Helper to initialize buffer data
void HeatReceiver::initializeReceiverBuffer() {
    auto* iodt = resourceManager.getRemesherForModel(&receiverModel);
    if (!iodt)
        return;

    auto* supportingHalfedge = iodt->getSupportingHalfedge();
    if (!supportingHalfedge)
        return;

    auto intrinsicMesh = supportingHalfedge->buildIntrinsicMesh();
    size_t vertexCount = intrinsicMesh.vertices.size();


    if (vertexCount == 0 || vertexCount != intrinsicVertexCount) {
        std::cerr << "[HeatReceiver] Vertex count mismatch or zero vertices." << std::endl;
        return;
    }

    VkDeviceSize bufferSize = sizeof(SurfacePoint) * vertexCount;

    VkBuffer stagingBuffer;
    VkDeviceSize stagingOffset;
    void* stagingData;
    createStagingBuffer(
        memoryAllocator,
        bufferSize,
        stagingBuffer, stagingOffset, &stagingData
    );

    std::vector<float> vertexAreas(vertexCount, 0.0f);
    size_t numTriangles = intrinsicMesh.indices.size() / 3;
    
    for (size_t t = 0; t < numTriangles; t++) {
        uint32_t i0 = intrinsicMesh.indices[t * 3 + 0];
        uint32_t i1 = intrinsicMesh.indices[t * 3 + 1];
        uint32_t i2 = intrinsicMesh.indices[t * 3 + 2];
        
        glm::vec3 v0 = intrinsicMesh.vertices[i0].position;
        glm::vec3 v1 = intrinsicMesh.vertices[i1].position;
        glm::vec3 v2 = intrinsicMesh.vertices[i2].position;
        
        glm::vec3 edge1 = v1 - v0;
        glm::vec3 edge2 = v2 - v0;
        float triArea = glm::length(glm::cross(edge1, edge2)) * 0.5f;
        
        vertexAreas[i0] += triArea / 3.0f;
        vertexAreas[i1] += triArea / 3.0f;
        vertexAreas[i2] += triArea / 3.0f;
    }
    
    std::vector<SurfacePoint> surfacePoints(vertexCount);
    for (size_t i = 0; i < vertexCount; i++) {
        surfacePoints[i].position = intrinsicMesh.vertices[i].position;
        surfacePoints[i].temperature = AMBIENT_TEMPERATURE;
        surfacePoints[i].normal = intrinsicMesh.vertices[i].normal;
        surfacePoints[i].area = vertexAreas[i];
        surfacePoints[i].color = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    memcpy(stagingData, surfacePoints.data(), bufferSize);

    initStagingBuffer = stagingBuffer;
    initStagingOffset = stagingOffset;
    initStagingData = stagingData;
    initBufferSize = bufferSize;

}

void HeatReceiver::computeVoronoiSurfaceMapping(VoronoiSeeder* seeder) {
    if (!seeder) {
        std::cerr << "[HeatReceiver] Invalid VoronoiSeeder" << std::endl;
        return;
    }
    
    // Get intrinsic mesh vertices (what the shader actually uses)
    auto* iodt = resourceManager.getRemesherForModel(&receiverModel);
    if (!iodt) 
        return;
    
    auto* supportingHalfedge = iodt->getSupportingHalfedge();
    if (!supportingHalfedge) 
        return;
    
    // Rebuild intrinsic mesh locally since we don't store it
    auto intrinsicMesh = supportingHalfedge->buildIntrinsicMesh();
    size_t vertexCount = intrinsicMesh.vertices.size();

    
    if (vertexCount == 0) {
        std::cerr << "[HeatReceiver] Intrinsic mesh has no vertices" << std::endl;
        return;
    }
    
    const auto& allSeeds = seeder->getSeeds();
    
    // Extract only regular seeds AND calculate their correct GPU indices
    std::vector<VoronoiSeeder::Seed> regularSeeds;
    std::vector<uint32_t> regularToBufferIndex;  
    
    regularSeeds.reserve(allSeeds.size());
    regularToBufferIndex.reserve(allSeeds.size());
    
    // CRITICAL: Track GPU index as it appears in HeatSystem's buffers
    // HeatSystem skips surface seeds, so we must not count them
    uint32_t currentGpuIndex = 0;
    
    for (uint32_t i = 0; i < allSeeds.size(); i++) {
        const auto& seed = allSeeds[i];
        
        // HeatSystem skips surface seeds in GPU buffer creation
        if (seed.isSurface) {
            continue; // Don't increment gpuIndex for surface seeds!
        }
        
        // If we reach here, this seed exists in GPU buffer at 'currentGpuIndex'
        
        // CRITICAL: Only MAP to regular (non-ghost) seeds.
        // Some seeds can be outside the mesh but still have restricted Voronoi volume inside the mesh.
        if (!seed.isGhost) {
            regularSeeds.push_back(seed);
            
            // Store the ACTUAL GPU buffer index, not raw allSeeds index!
            regularToBufferIndex.push_back(currentGpuIndex);  
        }
        
        // Increment GPU index for every non-surface seed (Interior + Exterior + Ghost)
        // This keeps us in sync with HeatSystem::generateVoronoiDiagram
        currentGpuIndex++;
    }
    
    if (regularSeeds.empty()) {
        std::cerr << "[HeatReceiver] No regular seeds found" << std::endl;
        return;
    }
    
    std::cout << "[HeatReceiver] Building KDtree from " << regularSeeds.size() << " regular seeds..." << std::endl;
    
    // Build KDtree from regular seeds only
    SeedAdapter adapter{regularSeeds};
    SeedKDTree kdTree(3, adapter, {10});
    
    std::vector<VoronoiSurfaceMapping> mappingData(vertexCount);
    
    std::cout << "[HeatReceiver] Mapping " << vertexCount << " intrinsic vertices to Voronoi cells..." << std::endl;
    
    // Map each intrinsic vertex to nearest regular voronoi cell
    for (size_t i = 0; i < vertexCount; i++) {
        glm::vec3 vertexPos = intrinsicMesh.vertices[i].position; 
        float query[3] = {vertexPos.x, vertexPos.y, vertexPos.z};
        
        // Query KDtree for nearest regular seed 
        uint32_t regularIdx; 
        float distSq;
        kdTree.knnSearch(query, 1, &regularIdx, &distSq);
        
        uint32_t bufferIdx = regularToBufferIndex[regularIdx];
        
        mappingData[i].cellIndex = bufferIdx; 
    }
    
    std::cout << "[HeatReceiver] Mapped " << vertexCount << " intrinsic vertices" << std::endl;
    std::cout << "[HeatReceiver] Regular cell count: " << regularSeeds.size() << std::endl;
    std::cout << "[HeatReceiver] First 10 cell mappings (buffer indices): ";
    for (size_t i = 0; i < std::min<size_t>(10, mappingData.size()); i++) {
        std::cout << mappingData[i].cellIndex << " ";
    }
    std::cout << std::endl;
    
    // Upload to staging buffer
    VkDeviceSize bufferSize = sizeof(VoronoiSurfaceMapping) * vertexCount;
    
    VkBuffer stagingBuffer;
    VkDeviceSize stagingOffset;
    void* stagingData;
    createStagingBuffer(
        memoryAllocator,
        bufferSize,
        stagingBuffer, stagingOffset, &stagingData
    );
    
    memcpy(stagingData, mappingData.data(), bufferSize);
    
    voronoiMappingStagingBuffer = stagingBuffer;
    voronoiMappingStagingOffset = stagingOffset;
    voronoiMappingStagingData = stagingData;
    voronoiMappingBufferSize = bufferSize;
}

void HeatReceiver::executeBufferTransfers(VkCommandBuffer commandBuffer) {
    if (initStagingBuffer != VK_NULL_HANDLE) {
        VkBufferCopy surfaceCopyRegion = { initStagingOffset, surfaceBufferOffset, initBufferSize };
        vkCmdCopyBuffer(commandBuffer, initStagingBuffer, surfaceBuffer, 1, &surfaceCopyRegion);
        
        VkBufferCopy vertexCopyRegion = { initStagingOffset, surfaceVertexBufferOffset, initBufferSize };
        vkCmdCopyBuffer(commandBuffer, initStagingBuffer, surfaceVertexBuffer, 1, &vertexCopyRegion);
    }
    
    // interpStagingBuffer removed
    
    if (voronoiMappingStagingBuffer != VK_NULL_HANDLE) {
        VkBufferCopy voronoiMappingCopyRegion = { voronoiMappingStagingOffset, voronoiMappingBufferOffset, voronoiMappingBufferSize };
        vkCmdCopyBuffer(commandBuffer, voronoiMappingStagingBuffer, voronoiMappingBuffer, 1, &voronoiMappingCopyRegion);
    }
    
    VkMemoryBarrier memBarrier{};
    memBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    memBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,  // Allow both compute and vertex shaders to read
        0,
        1, &memBarrier,
        0, nullptr,
        0, nullptr
    );
}

void HeatReceiver::updateDescriptors(
    VkDescriptorSetLayout surfaceLayout,
    VkDescriptorSetLayout renderLayout,
    VkDescriptorPool surfacePool,
    VkDescriptorPool renderPool,
    UniformBufferManager& uboManager,
    VkBuffer tempBufferA, VkDeviceSize tempBufferAOffset,
    VkBuffer tempBufferB, VkDeviceSize tempBufferBOffset,
    VkBuffer timeBuffer, VkDeviceSize timeBufferOffset,
    uint32_t maxFramesInFlight,
    uint32_t nodeCount) {
    
    auto* iodt = resourceManager.getRemesherForModel(&receiverModel);
    if (!iodt) 
        return;
    
    auto* supportingHalfedge = iodt->getSupportingHalfedge();
    if (!supportingHalfedge || !supportingHalfedge->isUploadedToGPU()) {
        std::cerr << "[HeatReceiver] Supporting halfedge not ready" << std::endl;
        return;
    }
    
    // Only allocate descriptor sets if they don't exist yet
    // CRITICAL: Don't clear and reallocate - that exhausts the pool!
    if (heatRenderDescriptorSets.empty()) {
        heatRenderDescriptorSets.resize(maxFramesInFlight);
        
        std::vector<VkDescriptorSetLayout> layouts(maxFramesInFlight, renderLayout);
        
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = renderPool;
        allocInfo.descriptorSetCount = maxFramesInFlight;
        allocInfo.pSetLayouts = layouts.data();
        
        if (vkAllocateDescriptorSets(vulkanDevice.getDevice(), &allocInfo, heatRenderDescriptorSets.data()) != VK_SUCCESS) {
            std::cerr << "[HeatReceiver] Failed to allocate heat render descriptor sets" << std::endl;
            return;
        }
    }

    
    for (size_t i = 0; i < maxFramesInFlight; i++) {
        std::array<VkWriteDescriptorSet, 12> descriptorWrites{};
        
        VkDescriptorBufferInfo uboBufferInfo{};
        uboBufferInfo.buffer = uboManager.getUniformBuffers()[i];
        uboBufferInfo.offset = uboManager.getUniformBufferOffsets()[i];
        uboBufferInfo.range = sizeof(UniformBufferObject);
        
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = heatRenderDescriptorSets[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &uboBufferInfo;
        
        VkBufferView bufferViews[11] = {
            supportingHalfedge->getSupportingHalfedgeView(),
            supportingHalfedge->getSupportingAngleView(),
            supportingHalfedge->getHalfedgeView(),
            supportingHalfedge->getEdgeView(),
            supportingHalfedge->getTriangleView(),
            supportingHalfedge->getLengthView(),
            supportingHalfedge->getInputHalfedgeView(),
            supportingHalfedge->getInputEdgeView(),
            supportingHalfedge->getInputTriangleView(),
            supportingHalfedge->getInputLengthView(),
            surfaceBufferView
        };
        
        for (int j = 0; j < 11; j++) {
            descriptorWrites[j + 1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[j + 1].dstSet = heatRenderDescriptorSets[i];
            descriptorWrites[j + 1].dstBinding = 1 + j;
            descriptorWrites[j + 1].dstArrayElement = 0;
            descriptorWrites[j + 1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
            descriptorWrites[j + 1].descriptorCount = 1;
            descriptorWrites[j + 1].pTexelBufferView = &bufferViews[j];
        }
        
        vkUpdateDescriptorSets(vulkanDevice.getDevice(), 12, descriptorWrites.data(), 0, nullptr);
    }
    
    // Only allocate surface descriptor sets if they don't exist yet
    if (surfaceComputeSetA == VK_NULL_HANDLE || surfaceComputeSetB == VK_NULL_HANDLE) {
        std::vector<VkDescriptorSetLayout> surfacePingPongLayouts = {surfaceLayout, surfaceLayout};
        
        VkDescriptorSetAllocateInfo surfacePingPongAllocInfo{};
        surfacePingPongAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        surfacePingPongAllocInfo.descriptorPool = surfacePool;

        surfacePingPongAllocInfo.descriptorSetCount = 2;
        surfacePingPongAllocInfo.pSetLayouts = surfacePingPongLayouts.data();
        
        VkDescriptorSet surfacePingPongSets[2];
        if (vkAllocateDescriptorSets(vulkanDevice.getDevice(), &surfacePingPongAllocInfo, surfacePingPongSets) != VK_SUCCESS) {
            std::cerr << "[HeatReceiver] Failed to allocate surface ping-pong descriptor sets" << std::endl;
            return;
        }
        
        surfaceComputeSetA = surfacePingPongSets[0];
        surfaceComputeSetB = surfacePingPongSets[1];
    }

    VkDescriptorBufferInfo nodeTempAInfo{ tempBufferA, tempBufferAOffset, sizeof(float) * nodeCount };
    VkDescriptorBufferInfo nodeTempBInfo{ tempBufferB, tempBufferBOffset, sizeof(float) * nodeCount };
    VkDescriptorBufferInfo surfaceInfo{ surfaceBuffer, surfaceBufferOffset, VK_WHOLE_SIZE };
    VkDescriptorBufferInfo timeInfo{ timeBuffer, timeBufferOffset, sizeof(TimeUniform) };
    VkDescriptorBufferInfo mappingInfo{ voronoiMappingBuffer, voronoiMappingBufferOffset, sizeof(VoronoiSurfaceMapping) * intrinsicVertexCount };

    {
        std::array<VkWriteDescriptorSet, 4> writes{};

        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = surfaceComputeSetA;
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[0].pBufferInfo = &nodeTempAInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = surfaceComputeSetA;
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[1].pBufferInfo = &surfaceInfo;

        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = surfaceComputeSetA;
        writes[2].dstBinding = 3;
        writes[2].descriptorCount = 1;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[2].pBufferInfo = &timeInfo;

        writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet = surfaceComputeSetA;
        writes[3].dstBinding = 9;
        writes[3].descriptorCount = 1;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[3].pBufferInfo = &mappingInfo;

        vkUpdateDescriptorSets(vulkanDevice.getDevice(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    {
        std::array<VkWriteDescriptorSet, 4> writes{};

        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = surfaceComputeSetB;
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[0].pBufferInfo = &nodeTempBInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = surfaceComputeSetB;
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[1].pBufferInfo = &surfaceInfo;

        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = surfaceComputeSetB;
        writes[2].dstBinding = 3;
        writes[2].descriptorCount = 1;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[2].pBufferInfo = &timeInfo;

        writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet = surfaceComputeSetB;
        writes[3].dstBinding = 9;
        writes[3].descriptorCount = 1;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[3].pBufferInfo = &mappingInfo;

        vkUpdateDescriptorSets(vulkanDevice.getDevice(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    std::cout << "[HeatReceiver] Descriptors updated" << std::endl;
    
}

void HeatReceiver::updateContactDescriptors(
    VkDescriptorSetLayout contactLayout,
    VkDescriptorPool contactPool,
    HeatSource& heatSource,
    VkBuffer tempBufferA,
    VkDeviceSize tempBufferAOffset,
    VkBuffer tempBufferB,
    VkDeviceSize tempBufferBOffset,
    VkBuffer injectionKBuffer,
    VkDeviceSize injectionKBufferOffset,
    VkBuffer injectionKTBuffer,
    VkDeviceSize injectionKTBufferOffset,
    uint32_t nodeCount) {

    Model* heatModelPtr = &resourceManager.getHeatModel();
    Model* receiverModelPtr = &receiverModel;

    if (contactComputeSetA == VK_NULL_HANDLE || contactComputeSetB == VK_NULL_HANDLE) {
        std::vector<VkDescriptorSetLayout> layouts = { contactLayout, contactLayout };

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = contactPool;
        allocInfo.descriptorSetCount = 2;
        allocInfo.pSetLayouts = layouts.data();

        VkDescriptorSet sets[2];
        if (vkAllocateDescriptorSets(vulkanDevice.getDevice(), &allocInfo, sets) != VK_SUCCESS) {
            std::cerr << "[HeatReceiver] Failed to allocate contact ping-pong descriptor sets" << std::endl;
            return;
        }

        contactComputeSetA = sets[0];
        contactComputeSetB = sets[1];
    }

    auto* heatGrid = resourceManager.getHashGridForModel(heatModelPtr);
    if (!heatGrid) {
        return;
    }

    auto* surfel = resourceManager.getSurfelPerModel(receiverModelPtr);
    if (!surfel) {
        return;
    }

    // Set A: read tempBufferA
    {
        std::vector<VkDescriptorBufferInfo> bufferInfos = {
            VkDescriptorBufferInfo{tempBufferA, tempBufferAOffset, sizeof(float) * nodeCount},
            VkDescriptorBufferInfo{triangleCentroidBuffer, triangleCentroidBufferOffset, VK_WHOLE_SIZE},
            VkDescriptorBufferInfo{triangleIndicesBuffer, triangleIndicesBufferOffset, VK_WHOLE_SIZE},
            VkDescriptorBufferInfo{heatSource.getSourceBuffer(), heatSource.getSourceBufferOffset(), sizeof(SurfacePoint) * heatSource.getVertexCount()},
            VkDescriptorBufferInfo{heatSource.getTriangleGeometryBuffer(), heatSource.getTriangleGeometryBufferOffset(), VK_WHOLE_SIZE},
            VkDescriptorBufferInfo{heatGrid->getGridBuffer(), heatGrid->getGridBufferOffset(), VK_WHOLE_SIZE},
            VkDescriptorBufferInfo{heatGrid->getCellCountBuffer(), heatGrid->getCellCountBufferOffset(), VK_WHOLE_SIZE},
            VkDescriptorBufferInfo{heatGrid->getParamsBuffer(), heatGrid->getParamsBufferOffset(), sizeof(HashGrid::HashGridParams)},
            VkDescriptorBufferInfo{surfel->getSurfelParamsBuffer(), surfel->getSurfelParamsBufferOffset(), sizeof(SurfelParams)},
            VkDescriptorBufferInfo{voronoiMappingBuffer, voronoiMappingBufferOffset, sizeof(VoronoiSurfaceMapping) * intrinsicVertexCount},
            VkDescriptorBufferInfo{injectionKBuffer, injectionKBufferOffset, sizeof(uint32_t) * nodeCount},
            VkDescriptorBufferInfo{injectionKTBuffer, injectionKTBufferOffset, sizeof(uint32_t) * nodeCount},
        };

        std::vector<VkWriteDescriptorSet> writes(12);
        for (int j = 0; j < 12; j++) {
            writes[j].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[j].dstSet = contactComputeSetA;
            writes[j].dstBinding = j;
            writes[j].dstArrayElement = 0;
            writes[j].descriptorCount = 1;
            writes[j].descriptorType = (j == 7 || j == 8) ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[j].pBufferInfo = &bufferInfos[j];
        }

        vkUpdateDescriptorSets(vulkanDevice.getDevice(), 12, writes.data(), 0, nullptr);
    }

    // Set B: read tempBufferB
    {
        std::vector<VkDescriptorBufferInfo> bufferInfos = {
            VkDescriptorBufferInfo{tempBufferB, tempBufferBOffset, sizeof(float) * nodeCount},
            VkDescriptorBufferInfo{triangleCentroidBuffer, triangleCentroidBufferOffset, VK_WHOLE_SIZE},
            VkDescriptorBufferInfo{triangleIndicesBuffer, triangleIndicesBufferOffset, VK_WHOLE_SIZE},
            VkDescriptorBufferInfo{heatSource.getSourceBuffer(), heatSource.getSourceBufferOffset(), sizeof(SurfacePoint) * heatSource.getVertexCount()},
            VkDescriptorBufferInfo{heatSource.getTriangleGeometryBuffer(), heatSource.getTriangleGeometryBufferOffset(), VK_WHOLE_SIZE},
            VkDescriptorBufferInfo{heatGrid->getGridBuffer(), heatGrid->getGridBufferOffset(), VK_WHOLE_SIZE},
            VkDescriptorBufferInfo{heatGrid->getCellCountBuffer(), heatGrid->getCellCountBufferOffset(), VK_WHOLE_SIZE},
            VkDescriptorBufferInfo{heatGrid->getParamsBuffer(), heatGrid->getParamsBufferOffset(), sizeof(HashGrid::HashGridParams)},
            VkDescriptorBufferInfo{surfel->getSurfelParamsBuffer(), surfel->getSurfelParamsBufferOffset(), sizeof(SurfelParams)},
            VkDescriptorBufferInfo{voronoiMappingBuffer, voronoiMappingBufferOffset, sizeof(VoronoiSurfaceMapping) * intrinsicVertexCount},
            VkDescriptorBufferInfo{injectionKBuffer, injectionKBufferOffset, sizeof(uint32_t) * nodeCount},
            VkDescriptorBufferInfo{injectionKTBuffer, injectionKTBufferOffset, sizeof(uint32_t) * nodeCount},
        };

        std::vector<VkWriteDescriptorSet> writes(12);
        for (int j = 0; j < 12; j++) {
            writes[j].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[j].dstSet = contactComputeSetB;
            writes[j].dstBinding = j;
            writes[j].dstArrayElement = 0;
            writes[j].descriptorCount = 1;
            writes[j].descriptorType = (j == 7 || j == 8) ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[j].pBufferInfo = &bufferInfos[j];
        }

        vkUpdateDescriptorSets(vulkanDevice.getDevice(), 12, writes.data(), 0, nullptr);
    }
}

void HeatReceiver::recreateDescriptors(
    VkDescriptorSetLayout surfaceLayout,
    VkDescriptorPool surfacePool,
    VkDescriptorSetLayout renderLayout,
    VkDescriptorPool renderPool,
    UniformBufferManager& uboManager,
    VkBuffer tempBufferA, VkDeviceSize tempBufferAOffset,
    VkBuffer tempBufferB, VkDeviceSize tempBufferBOffset,
    VkBuffer timeBuffer, VkDeviceSize timeBufferOffset,
    uint32_t maxFramesInFlight,
    uint32_t nodeCount) {
    
    updateDescriptors(
        surfaceLayout,
        renderLayout,
        surfacePool,
        renderPool,
        uboManager,
        tempBufferA,
        tempBufferAOffset,
        tempBufferB,
        tempBufferBOffset,
        timeBuffer,
        timeBufferOffset,
        maxFramesInFlight,
        nodeCount);
}

void HeatReceiver::recreateContactDescriptors(
    VkDescriptorSetLayout contactLayout,
    VkDescriptorPool contactPool,
    HeatSource& heatSource,
    VkBuffer tempBufferA,
    VkDeviceSize tempBufferAOffset,
    VkBuffer tempBufferB,
    VkDeviceSize tempBufferBOffset,
    VkBuffer injectionKBuffer,
    VkDeviceSize injectionKBufferOffset,
    VkBuffer injectionKTBuffer,
    VkDeviceSize injectionKTBufferOffset,
    uint32_t nodeCount) {

    updateContactDescriptors(
        contactLayout,
        contactPool,
        heatSource,
        tempBufferA,
        tempBufferAOffset,
        tempBufferB,
        tempBufferBOffset,
        injectionKBuffer,
        injectionKBufferOffset,
        injectionKTBuffer,
        injectionKTBufferOffset,
        nodeCount
    );
}



void HeatReceiver::cleanup() {
    if (surfaceBufferView != VK_NULL_HANDLE) {
        vkDestroyBufferView(vulkanDevice.getDevice(), surfaceBufferView, nullptr);
        surfaceBufferView = VK_NULL_HANDLE;
    }
    if (surfaceBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(surfaceBuffer, surfaceBufferOffset);
        surfaceBuffer = VK_NULL_HANDLE;
    }
    if (surfaceVertexBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(surfaceVertexBuffer, surfaceVertexBufferOffset);
        surfaceVertexBuffer = VK_NULL_HANDLE;
    }

    // interpolationBuffer REMOVED


    if (triangleIndicesBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(triangleIndicesBuffer, triangleIndicesBufferOffset);
        triangleIndicesBuffer = VK_NULL_HANDLE;
    }
    if (triangleCentroidBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(triangleCentroidBuffer, triangleCentroidBufferOffset);
        triangleCentroidBuffer = VK_NULL_HANDLE;
    }
    if (voronoiMappingBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(voronoiMappingBuffer, voronoiMappingBufferOffset);
        voronoiMappingBuffer = VK_NULL_HANDLE;
    }

    cleanupStagingBuffers();
}

void HeatReceiver::cleanupStagingBuffers() {
    if (initStagingBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(initStagingBuffer, initStagingOffset);
        initStagingBuffer = VK_NULL_HANDLE;
        initStagingData = nullptr;
    }
    if (interpStagingBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(interpStagingBuffer, interpStagingOffset);
        interpStagingBuffer = VK_NULL_HANDLE;
        interpStagingData = nullptr;
    }
    if (voronoiMappingStagingBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(voronoiMappingStagingBuffer, voronoiMappingStagingOffset);
        voronoiMappingStagingBuffer = VK_NULL_HANDLE;
        voronoiMappingStagingData = nullptr;
    }
}