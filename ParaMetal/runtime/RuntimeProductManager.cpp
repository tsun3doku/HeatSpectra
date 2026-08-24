#include "RuntimeProductManager.hpp"

#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/VulkanDevice.hpp"
#include "vulkan/VulkanBuffer.hpp"

#include <cassert>
#include <unordered_set>
#include <vulkan/vulkan.h>

RuntimeProductManager::RuntimeProductManager(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator)
    : vulkanDevice(vulkanDevice),
      memoryAllocator(memoryAllocator) {
}

RuntimeProductManager::~RuntimeProductManager() {
    assert(modelProducts.empty() && remeshProducts.empty() && voronoiProducts.empty() &&
           pointProducts.empty() && heatProducts.empty());
}

void RuntimeProductManager::destroyAll() {
    vkDeviceWaitIdle(vulkanDevice.getDevice());

    destroyAllProducts(modelProducts);
    destroyAllProducts(remeshProducts);
    destroyAllProducts(voronoiProducts);
    destroyAllProducts(pointProducts);
    destroyAllProducts(heatProducts);
}

void RuntimeProductManager::destroy(ModelProduct& product) {
    product = {};
}

void RuntimeProductManager::destroy(RemeshProduct& product) {
    freeBufferView(vulkanDevice, product.supportingHalfedgeView);
    freeBufferView(vulkanDevice, product.supportingAngleView);
    freeBufferView(vulkanDevice, product.halfedgeView);
    freeBufferView(vulkanDevice, product.edgeView);
    freeBufferView(vulkanDevice, product.triangleView);
    freeBufferView(vulkanDevice, product.lengthView);
    freeBufferView(vulkanDevice, product.inputHalfedgeView);
    freeBufferView(vulkanDevice, product.inputEdgeView);
    freeBufferView(vulkanDevice, product.inputTriangleView);
    freeBufferView(vulkanDevice, product.inputLengthView);

    freeBuffer(memoryAllocator, product.intrinsicTriangleBuffer, product.intrinsicTriangleBufferOffset);
    freeBuffer(memoryAllocator, product.intrinsicVertexBuffer, product.intrinsicVertexBufferOffset);

    freeBuffer(memoryAllocator, product.supportingHalfedgeBuffer, product.supportingHalfedgeOffset);
    freeBuffer(memoryAllocator, product.supportingAngleBuffer, product.supportingAngleOffset);
    freeBuffer(memoryAllocator, product.halfedgeBuffer, product.halfedgeOffset);
    freeBuffer(memoryAllocator, product.edgeBuffer, product.edgeOffset);
    freeBuffer(memoryAllocator, product.triangleBuffer, product.triangleOffset);
    freeBuffer(memoryAllocator, product.lengthBuffer, product.lengthOffset);
    freeBuffer(memoryAllocator, product.inputHalfedgeBuffer, product.inputHalfedgeOffset);
    freeBuffer(memoryAllocator, product.inputEdgeBuffer, product.inputEdgeOffset);
    freeBuffer(memoryAllocator, product.inputTriangleBuffer, product.inputTriangleOffset);
    freeBuffer(memoryAllocator, product.inputLengthBuffer, product.inputLengthOffset);

    product = {};
}

void RuntimeProductManager::destroy(VoronoiProduct& product) {
    for (size_t i = 0; i < product.globalDisplayRuntimeModelIds.size(); ++i) {
        freeBuffer(memoryAllocator, product.globalDisplayVertexBuffers[i], product.globalDisplayVertexBufferOffsets[i]);
        freeBuffer(memoryAllocator, product.globalDisplayFaceIndexBuffers[i], product.globalDisplayFaceIndexBufferOffsets[i]);
        freeBuffer(memoryAllocator, product.globalDisplayCandidateBuffers[i], product.globalDisplayCandidateBufferOffsets[i]);
    }
    freeBuffer(memoryAllocator, product.candidateNodeBuffer, product.candidateNodeBufferOffset);
    freeBuffer(memoryAllocator, product.candidateNeighborIndicesBuffer, product.candidateNeighborIndicesBufferOffset);
    freeBuffer(memoryAllocator, product.nodeBuffer, product.nodeBufferOffset);
    freeBuffer(memoryAllocator, product.couplingBuffer, product.couplingBufferOffset);
    freeBuffer(memoryAllocator, product.seedPositionBuffer, product.seedPositionBufferOffset);
    freeBuffer(memoryAllocator, product.occupancyPointBuffer, product.occupancyPointBufferOffset);
    freeBuffer(memoryAllocator, product.candidateBuffer, product.candidateBufferOffset);
    freeBuffer(memoryAllocator, product.gmlsSurfaceStencilBuffer, product.gmlsSurfaceStencilBufferOffset);
    freeBuffer(memoryAllocator, product.gmlsSurfaceWeightBuffer, product.gmlsSurfaceWeightBufferOffset);
    freeBuffer(memoryAllocator, product.gmlsSurfaceGradientWeightBuffer, product.gmlsSurfaceGradientWeightBufferOffset);

    product = {};
}

void RuntimeProductManager::destroy(PointProduct& product) {
    freeBuffer(memoryAllocator, product.positionBuffer, product.positionBufferOffset);
    product = {};
}

void RuntimeProductManager::destroy(HeatProduct& product) {
    for (size_t i = 0; i < product.modelSurfaceBuffers.size(); ++i) {
        VkDeviceSize offset = i < product.modelSurfaceBufferOffsets.size()
            ? product.modelSurfaceBufferOffsets[i] : 0;
        freeBuffer(memoryAllocator, product.modelSurfaceBuffers[i], offset);
    }
    for (size_t i = 0; i < product.modelSurfaceGradientBuffers.size(); ++i) {
        VkDeviceSize offset = i < product.modelSurfaceGradientBufferOffsets.size()
            ? product.modelSurfaceGradientBufferOffsets[i] : 0;
        freeBuffer(memoryAllocator, product.modelSurfaceGradientBuffers[i], offset);
    }
    product = {};
}
