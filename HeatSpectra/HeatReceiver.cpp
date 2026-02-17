#include "HeatReceiver.hpp"
#include "VulkanDevice.hpp"
#include "MemoryAllocator.hpp"
#include "Model.hpp"
#include "CommandBufferManager.hpp"
#include "ResourceManager.hpp"
#include "iODT.hpp"
#include "VulkanBuffer.hpp"
#include "HeatSource.hpp"
#include "SurfelRenderer.hpp"
#include "Structs.hpp"
#include "GeometryUtils.hpp"

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
	const size_t pairCount = pairs.empty() ? 1ull : pairs.size();
	VkDeviceSize bufferSize = sizeof(ContactPairGPU) * pairCount;

	if (contactPairBuffer != VK_NULL_HANDLE) {
		memoryAllocator.free(contactPairBuffer, contactPairBufferOffset);
		contactPairBuffer = VK_NULL_HANDLE;
		contactPairBufferOffset = 0;
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
	if (pairs.empty()) {
		std::memset(stagingData, 0, static_cast<size_t>(bufferSize));
	} else {
		std::memcpy(stagingData, pairs.data(), static_cast<size_t>(bufferSize));
	}

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
    intrinsicSurfacePositions.clear();
    intrinsicSurfacePositions.reserve(vertexCount);
    for (const auto& v : intrinsicMeshData.vertices) {
        intrinsicSurfacePositions.push_back(v.position);
    }

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
        
    std::vector<glm::vec3> positions(vertexCount);
    for (size_t i = 0; i < vertexCount; ++i) {
        positions[i] = intrinsicMeshData.vertices[i].position;
    }
    std::vector<float> vertexAreas = computeVertexAreas(positions, intrinsicMeshData.indices);

    std::vector<SurfacePoint> surfacePoints(vertexCount);
    for (size_t i = 0; i < vertexCount; i++) {
        surfacePoints[i].position = intrinsicMeshData.vertices[i].position;
        surfacePoints[i].temperature = AMBIENT_TEMPERATURE;
        surfacePoints[i].normal = intrinsicMeshData.vertices[i].normal;
        surfacePoints[i].area = vertexAreas[i];
        surfacePoints[i].color = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    if (triangleIndicesBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(triangleIndicesBuffer, triangleIndicesBufferOffset);
        triangleIndicesBuffer = VK_NULL_HANDLE;
        triangleIndicesBufferOffset = 0;
    }
    auto [triIdxHandle, triIdxOffset] = memoryAllocator.allocate(
        triangleIndicesBufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        storageAlignment
    );
    triangleIndicesBuffer = triIdxHandle;
    triangleIndicesBufferOffset = triIdxOffset;

    VkBuffer triIdxStagingBuffer = VK_NULL_HANDLE;
    VkDeviceSize triIdxStagingOffset = 0;
    void* triIdxStagingData = nullptr;
    createStagingBuffer(
        memoryAllocator,
        triangleIndicesBufferSize,
        triIdxStagingBuffer,
        triIdxStagingOffset,
        &triIdxStagingData
    );
    memcpy(triIdxStagingData, intrinsicMeshData.indices.data(), static_cast<size_t>(triangleIndicesBufferSize));

    if (initStagingBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(initStagingBuffer, initStagingOffset);
        initStagingBuffer = VK_NULL_HANDLE;
        initStagingOffset = 0;
        initBufferSize = 0;
    }
    initBufferSize = sizeof(SurfacePoint) * vertexCount;
    void* initStagingData = nullptr;
    createStagingBuffer(
        memoryAllocator,
        initBufferSize,
        initStagingBuffer,
        initStagingOffset,
        &initStagingData
    );
    memcpy(initStagingData, surfacePoints.data(), static_cast<size_t>(initBufferSize));
    
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

    // Create Voronoi candidate buffer
    if (voronoiCandidateBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(voronoiCandidateBuffer, voronoiCandidateBufferOffset);
        voronoiCandidateBuffer = VK_NULL_HANDLE;
        voronoiCandidateBufferOffset = 0;
    }
    VkBuffer candStagingBuffer = VK_NULL_HANDLE;
    VkDeviceSize candStagingOffset = 0;
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

        void* stagingData;
        createStagingBuffer(
            memoryAllocator,
            candidateBufferSize,
            candStagingBuffer,
            candStagingOffset,
            &stagingData
        );
        memcpy(stagingData, initData.data(), static_cast<size_t>(candidateBufferSize));
    }

    // Batch transfer uploads into one submit 
    VkCommandBuffer cmd = renderCommandPool.beginCommands();
    {
        VkBufferCopy region{};
        region.srcOffset = triIdxStagingOffset;
        region.dstOffset = triangleIndicesBufferOffset;
        region.size = triangleIndicesBufferSize;
        vkCmdCopyBuffer(cmd, triIdxStagingBuffer, triangleIndicesBuffer, 1, &region);
    }
    if (candStagingBuffer != VK_NULL_HANDLE) {
        VkBufferCopy region{};
        region.srcOffset = candStagingOffset;
        region.dstOffset = voronoiCandidateBufferOffset;
        region.size = candidateBufferSize;
        vkCmdCopyBuffer(cmd, candStagingBuffer, voronoiCandidateBuffer, 1, &region);
    }
    renderCommandPool.endCommands(cmd);

    memoryAllocator.free(triIdxStagingBuffer, triIdxStagingOffset);
    if (candStagingBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(candStagingBuffer, candStagingOffset);
    }
    
    std::cout << "[HeatReceiver] Created buffers for " << vertexCount << " vertices, "
              << triangleCount << " triangles" << std::endl;
}

void HeatReceiver::initializeReceiverBuffer() {
    if (initStagingBuffer != VK_NULL_HANDLE && initBufferSize > 0) {
        return;
    }

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

    std::vector<glm::vec3> positions(vertexCount);
    for (size_t i = 0; i < vertexCount; ++i) {
        positions[i] = intrinsicMesh.vertices[i].position;
    }
    std::vector<float> vertexAreas = computeVertexAreas(positions, intrinsicMesh.indices);
    
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

void HeatReceiver::updateDescriptors(
    VkDescriptorSetLayout surfaceLayout,
    VkDescriptorPool surfacePool,
    VkBuffer tempBufferA, VkDeviceSize tempBufferAOffset,
    VkBuffer tempBufferB, VkDeviceSize tempBufferBOffset,
    VkBuffer timeBuffer, VkDeviceSize timeBufferOffset,
    uint32_t nodeCount) {

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

    VkDescriptorBufferInfo surfaceInfo{ surfaceBuffer, surfaceBufferOffset, VK_WHOLE_SIZE };
    VkDescriptorBufferInfo timeInfo{ timeBuffer, timeBufferOffset, sizeof(TimeUniform) };
    VkDescriptorBufferInfo mappingInfo{ voronoiMappingBuffer, voronoiMappingBufferOffset, sizeof(VoronoiSurfaceMapping) * intrinsicVertexCount };

    VkDescriptorSet sets[2] = { surfaceComputeSetA, surfaceComputeSetB };
    VkBuffer tempBuffers[2] = { tempBufferA, tempBufferB };
    VkDeviceSize tempOffsets[2] = { tempBufferAOffset, tempBufferBOffset };

    for (uint32_t pass = 0; pass < 2; pass++) {
        VkDescriptorBufferInfo nodeTempInfo{ tempBuffers[pass], tempOffsets[pass], sizeof(float) * nodeCount };
        std::array<VkDescriptorBufferInfo*, 4> infos = { &nodeTempInfo, &surfaceInfo, &timeInfo, &mappingInfo };
        std::array<uint32_t, 4> bindings = { 0u, 1u, 3u, 9u };
        std::array<VkDescriptorType, 4> types = {
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
        };
        std::array<VkWriteDescriptorSet, 4> writes{};
        for (size_t i = 0; i < writes.size(); i++) {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = sets[pass];
            writes[i].dstBinding = bindings[i];
            writes[i].dstArrayElement = 0;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = types[i];
            writes[i].pBufferInfo = infos[i];
        }
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

    auto* surfel = resourceManager.getSurfelPerModel(receiverModelPtr);
    if (!surfel) {
        return;
    }

    VkDescriptorSet contactSets[2] = { contactComputeSetA, contactComputeSetB };
    VkBuffer tempBuffers[2] = { tempBufferA, tempBufferB };
    VkDeviceSize tempOffsets[2] = { tempBufferAOffset, tempBufferBOffset };

    for (uint32_t pass = 0; pass < 2; pass++) {
        std::array<VkDescriptorBufferInfo, 10> infos = {
            VkDescriptorBufferInfo{tempBuffers[pass], tempOffsets[pass], sizeof(float) * nodeCount},
            VkDescriptorBufferInfo{triangleIndicesBuffer, triangleIndicesBufferOffset, VK_WHOLE_SIZE},
            VkDescriptorBufferInfo{triangleIndicesBuffer, triangleIndicesBufferOffset, VK_WHOLE_SIZE},
            VkDescriptorBufferInfo{heatSource.getSourceBuffer(), heatSource.getSourceBufferOffset(), sizeof(SurfacePoint) * heatSource.getVertexCount()},
            VkDescriptorBufferInfo{heatSource.getTriangleGeometryBuffer(), heatSource.getTriangleGeometryBufferOffset(), VK_WHOLE_SIZE},
            VkDescriptorBufferInfo{surfel->getSurfelParamsBuffer(), surfel->getSurfelParamsBufferOffset(), sizeof(SurfelParams)},
            VkDescriptorBufferInfo{voronoiMappingBuffer, voronoiMappingBufferOffset, sizeof(VoronoiSurfaceMapping) * intrinsicVertexCount},
            VkDescriptorBufferInfo{injectionKBuffer, injectionKBufferOffset, sizeof(uint32_t) * nodeCount},
            VkDescriptorBufferInfo{injectionKTBuffer, injectionKTBufferOffset, sizeof(uint32_t) * nodeCount},
            VkDescriptorBufferInfo{contactPairBuffer, contactPairBufferOffset, VK_WHOLE_SIZE},
        };
        std::array<VkWriteDescriptorSet, 10> writes{};
        for (uint32_t i = 0; i < static_cast<uint32_t>(writes.size()); i++) {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = contactSets[pass];
            writes[i].dstBinding = i;
            writes[i].dstArrayElement = 0;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = (i == 5u) ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[i].pBufferInfo = &infos[i];
        }
        vkUpdateDescriptorSets(vulkanDevice.getDevice(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
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
    if (voronoiMappingBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(voronoiMappingBuffer, voronoiMappingBufferOffset);
        voronoiMappingBuffer = VK_NULL_HANDLE;
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
}
