#include "HeatReceiver.hpp"
#include "VulkanDevice.hpp"
#include "MemoryAllocator.hpp"
#include "Model.hpp"
#include "CommandBufferManager.hpp"
#include "ResourceManager.hpp"
#include "iODT.hpp"
#include "VulkanBuffer.hpp"
#include "HeatSource.hpp"
#include "HashGrid.hpp"
#include "SurfelRenderer.hpp"
#include "Structs.hpp"

#include <iostream>
#include <algorithm>
#include <array>
#include <cstring>

HeatReceiver::HeatReceiver(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, Model& receiverModel, 
    ResourceManager& resourceManager, CommandPool& renderCommandPool)
    : vulkanDevice(vulkanDevice), memoryAllocator(memoryAllocator), receiverModel(receiverModel), 
    resourceManager(resourceManager), renderCommandPool(renderCommandPool) {
}

HeatReceiver::~HeatReceiver() {
}

void HeatReceiver::uploadContactPairs(const std::vector<ContactPairGPU>& pairs) {
	VkDeviceSize storageAlignment = vulkanDevice.getPhysicalDeviceProperties().limits.minStorageBufferOffsetAlignment;
	VkDeviceSize bufferSize = sizeof(ContactPairGPU) * pairs.size();

	if (contactPairBuffer != VK_NULL_HANDLE) {
		memoryAllocator.free(contactPairBuffer, contactPairBufferOffset);
		contactPairBuffer = VK_NULL_HANDLE;
		contactPairBufferOffset = 0;
	}

	if (bufferSize == 0) {
		return;
	}

	VkBuffer stagingBuffer;
	VkDeviceSize stagingOffset;
	void* stagingData;
	createStagingBuffer(
		memoryAllocator,
		bufferSize,
		stagingBuffer,
		stagingOffset,
		&stagingData
	);
	memcpy(stagingData, pairs.data(), static_cast<size_t>(bufferSize));

	auto [pairHandle, pairOffset] = memoryAllocator.allocate(
		bufferSize,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		storageAlignment
	);
	contactPairBuffer = pairHandle;
	contactPairBufferOffset = pairOffset;

	VkCommandBuffer cmd = renderCommandPool.beginCommands();
	VkBufferCopy region{};
	region.srcOffset = stagingOffset;
	region.dstOffset = contactPairBufferOffset;
	region.size = bufferSize;
	vkCmdCopyBuffer(cmd, stagingBuffer, contactPairBuffer, 1, &region);
	renderCommandPool.endCommands(cmd);

	memoryAllocator.free(stagingBuffer, stagingOffset);
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

    constexpr uint32_t K_CANDIDATES = 64;
    VkDeviceSize vertexBufferSize = sizeof(SurfacePoint) * vertexCount;
    VkDeviceSize triangleIndicesBufferSize = sizeof(uint32_t) * intrinsicMeshData.indices.size();
    VkDeviceSize candidateBufferSize = sizeof(uint32_t) * triangleCount * K_CANDIDATES;

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
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
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

    // Create Voronoi triangle mapping buffer
    VkDeviceSize voronoiTriangleMappingBufferSize = sizeof(VoronoiSurfaceMapping) * triangleCount;
    void* voronoiTriMappedPtr;
    createStorageBuffer(
        memoryAllocator, vulkanDevice,
        nullptr, voronoiTriangleMappingBufferSize,
        voronoiTriangleMappingBuffer, voronoiTriangleMappingBufferOffset, &voronoiTriMappedPtr,
        true,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT
    );

    // Create Voronoi candidate buffer
    if (voronoiCandidateBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(voronoiCandidateBuffer, voronoiCandidateBufferOffset);
        voronoiCandidateBuffer = VK_NULL_HANDLE;
        voronoiCandidateBufferOffset = 0;
    }
    if (candidateBufferSize > 0) {
        auto [candHandle, candOffset] = memoryAllocator.allocate(
            candidateBufferSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            storageAlignment
        );
        voronoiCandidateBuffer = candHandle;
        voronoiCandidateBufferOffset = candOffset;

        std::vector<uint32_t> initData(static_cast<size_t>(triangleCount) * K_CANDIDATES, 0xFFFFFFFFu);

        VkBuffer stagingBuffer;
        VkDeviceSize stagingOffset;
        void* stagingData;
        createStagingBuffer(
            memoryAllocator,
            candidateBufferSize,
            stagingBuffer,
            stagingOffset,
            &stagingData
        );
        memcpy(stagingData, initData.data(), static_cast<size_t>(candidateBufferSize));

        VkCommandBuffer cmd = renderCommandPool.beginCommands();
        VkBufferCopy region{};
        region.srcOffset = stagingOffset;
        region.dstOffset = voronoiCandidateBufferOffset;
        region.size = candidateBufferSize;
        vkCmdCopyBuffer(cmd, stagingBuffer, voronoiCandidateBuffer, 1, &region);
        renderCommandPool.endCommands(cmd);

        memoryAllocator.free(stagingBuffer, stagingOffset);
    }
    
    std::cout << "[HeatReceiver] Created buffers for " << vertexCount << " vertices, "
              << triangleCount << " triangles" << std::endl;
}

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
    initBufferSize = bufferSize;

}

void HeatReceiver::stageVoronoiSurfaceMapping(const std::vector<uint32_t>& cellIndices) {
	if (cellIndices.empty()) {
		return;
	}
	if (intrinsicVertexCount != 0 && cellIndices.size() != intrinsicVertexCount) {
		return;
	}

	std::vector<VoronoiSurfaceMapping> mappingData(cellIndices.size());
	for (size_t i = 0; i < cellIndices.size(); ++i) {
		mappingData[i].cellIndex = cellIndices[i];
	}

	// Upload to staging buffer
	VkDeviceSize bufferSize = sizeof(VoronoiSurfaceMapping) * cellIndices.size();
	
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
	voronoiMappingBufferSize = bufferSize;
}

void HeatReceiver::stageVoronoiTriangleMapping(const std::vector<uint32_t>& cellIndices) {
    if (cellIndices.empty()) {
        return;
    }
    if (intrinsicTriangleCount != 0 && cellIndices.size() != intrinsicTriangleCount) {
        return;
    }

    std::vector<VoronoiSurfaceMapping> mappingData(cellIndices.size());
    for (size_t i = 0; i < cellIndices.size(); ++i) {
        mappingData[i].cellIndex = cellIndices[i];
    }

    VkDeviceSize bufferSize = sizeof(VoronoiSurfaceMapping) * cellIndices.size();

    VkBuffer stagingBuffer;
    VkDeviceSize stagingOffset;
    void* stagingData;
    createStagingBuffer(
        memoryAllocator,
        bufferSize,
        stagingBuffer, stagingOffset, &stagingData
    );

    memcpy(stagingData, mappingData.data(), bufferSize);

    voronoiTriangleMappingStagingBuffer = stagingBuffer;
    voronoiTriangleMappingStagingOffset = stagingOffset;
    voronoiTriangleMappingBufferSize = bufferSize;
}

void HeatReceiver::executeBufferTransfers(VkCommandBuffer commandBuffer) {
    if (initStagingBuffer != VK_NULL_HANDLE) {
        VkBufferCopy surfaceCopyRegion = { initStagingOffset, surfaceBufferOffset, initBufferSize };
        vkCmdCopyBuffer(commandBuffer, initStagingBuffer, surfaceBuffer, 1, &surfaceCopyRegion);
        
        VkBufferCopy vertexCopyRegion = { initStagingOffset, surfaceVertexBufferOffset, initBufferSize };
        vkCmdCopyBuffer(commandBuffer, initStagingBuffer, surfaceVertexBuffer, 1, &vertexCopyRegion);
    }
        
    if (voronoiMappingStagingBuffer != VK_NULL_HANDLE) {
        VkBufferCopy voronoiMappingCopyRegion = { voronoiMappingStagingOffset, voronoiMappingBufferOffset, voronoiMappingBufferSize };
        vkCmdCopyBuffer(commandBuffer, voronoiMappingStagingBuffer, voronoiMappingBuffer, 1, &voronoiMappingCopyRegion);
    }
    if (voronoiTriangleMappingStagingBuffer != VK_NULL_HANDLE) {
        VkBufferCopy voronoiTriMappingCopyRegion = { voronoiTriangleMappingStagingOffset, voronoiTriangleMappingBufferOffset, voronoiTriangleMappingBufferSize };
        vkCmdCopyBuffer(commandBuffer, voronoiTriangleMappingStagingBuffer, voronoiTriangleMappingBuffer, 1, &voronoiTriMappingCopyRegion);
    }
    
    VkMemoryBarrier memBarrier{};
    memBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    memBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 
        0,
        1, &memBarrier,
        0, nullptr,
        0, nullptr
    );
}

void HeatReceiver::updateDescriptors(VkDescriptorSetLayout surfaceLayout, VkDescriptorPool surfacePool, VkBuffer tempBufferA, VkDeviceSize tempBufferAOffset,
    VkBuffer tempBufferB, VkDeviceSize tempBufferBOffset, VkBuffer timeBuffer, VkDeviceSize timeBufferOffset, uint32_t nodeCount) {
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
            VkDescriptorBufferInfo{contactPairBuffer, contactPairBufferOffset, VK_WHOLE_SIZE},
        };

        std::vector<VkWriteDescriptorSet> writes(13);
        for (int j = 0; j < 13; j++) {
            writes[j].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[j].dstSet = contactComputeSetA;
            writes[j].dstBinding = j;
            writes[j].dstArrayElement = 0;
            writes[j].descriptorCount = 1;
            writes[j].descriptorType = (j == 7 || j == 8) ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[j].pBufferInfo = &bufferInfos[j];
        }

        vkUpdateDescriptorSets(vulkanDevice.getDevice(), 13, writes.data(), 0, nullptr);
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
            VkDescriptorBufferInfo{contactPairBuffer, contactPairBufferOffset, VK_WHOLE_SIZE},
        };

        std::vector<VkWriteDescriptorSet> writes(13);
        for (int j = 0; j < 13; j++) {
            writes[j].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[j].dstSet = contactComputeSetB;
            writes[j].dstBinding = j;
            writes[j].dstArrayElement = 0;
            writes[j].descriptorCount = 1;
            writes[j].descriptorType = (j == 7 || j == 8) ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[j].pBufferInfo = &bufferInfos[j];
        }

        vkUpdateDescriptorSets(vulkanDevice.getDevice(), 13, writes.data(), 0, nullptr);
    }
}

void HeatReceiver::recreateDescriptors(
	VkDescriptorSetLayout surfaceLayout,
	VkDescriptorPool surfacePool,
	VkBuffer tempBufferA, VkDeviceSize tempBufferAOffset,
	VkBuffer tempBufferB, VkDeviceSize tempBufferBOffset,
	VkBuffer timeBuffer, VkDeviceSize timeBufferOffset,
	uint32_t nodeCount) {
	
	updateDescriptors(
		surfaceLayout,
		surfacePool,
		tempBufferA,
		tempBufferAOffset,
		tempBufferB,
		tempBufferBOffset,
		timeBuffer,
		timeBufferOffset,
		nodeCount);
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
    if (voronoiTriangleMappingBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(voronoiTriangleMappingBuffer, voronoiTriangleMappingBufferOffset);
        voronoiTriangleMappingBuffer = VK_NULL_HANDLE;
    }
    if (voronoiCandidateBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(voronoiCandidateBuffer, voronoiCandidateBufferOffset);
        voronoiCandidateBuffer = VK_NULL_HANDLE;
        voronoiCandidateBufferOffset = 0;
    }
	if (contactPairBuffer != VK_NULL_HANDLE) {
		memoryAllocator.free(contactPairBuffer, contactPairBufferOffset);
		contactPairBuffer = VK_NULL_HANDLE;
		contactPairBufferOffset = 0;
	}

    cleanupStagingBuffers();
}

void HeatReceiver::cleanupStagingBuffers() {
    if (initStagingBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(initStagingBuffer, initStagingOffset);
        initStagingBuffer = VK_NULL_HANDLE;
    }
    if (voronoiMappingStagingBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(voronoiMappingStagingBuffer, voronoiMappingStagingOffset);
        voronoiMappingStagingBuffer = VK_NULL_HANDLE;
    }
    if (voronoiTriangleMappingStagingBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(voronoiTriangleMappingStagingBuffer, voronoiTriangleMappingStagingOffset);
        voronoiTriangleMappingStagingBuffer = VK_NULL_HANDLE;
    }
}
