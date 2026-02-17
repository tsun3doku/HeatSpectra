#include "OverlayPass.hpp"

#include <array>
#include <stdexcept>
#include <vector>

#include "FrameGraph.hpp"
#include "File_utils.h"
#include "GeometryPass.hpp"
#include "Gizmo.hpp"
#include "Grid.hpp"
#include "HeatSystem.hpp"
#include "Model.hpp"
#include "ModelSelection.hpp"
#include "ResourceManager.hpp"
#include "SceneRenderer.hpp"
#include "Structs.hpp"
#include "UniformBufferManager.hpp"
#include "VulkanDevice.hpp"
#include "VulkanImage.hpp"
#include "WireframeRenderer.hpp"
#include "iODT.hpp"

namespace render {

OverlayPass::OverlayPass(SceneRenderer& sceneRenderer, GeometryPass& geometryPass)
    : sceneRenderer(sceneRenderer),
      geometryPass(geometryPass),
      vulkanDevice(sceneRenderer.getVulkanDevice()),
      frameGraph(sceneRenderer.getFrameGraph()) {
}

const char* OverlayPass::name() const {
    return "OverlayPass";
}

void OverlayPass::create() {
    createDepthSampler();
    createOutlineDescriptorPool(sceneRenderer.getMaxFramesInFlight());
    createOutlineDescriptorSetLayout();
    createOutlineDescriptorSets(sceneRenderer.getMaxFramesInFlight());

    createSupportingHalfedgeDescriptorPool(sceneRenderer.getMaxFramesInFlight());
    createSupportingHalfedgeDescriptorSetLayout();

    createIntrinsicNormalsDescriptorPool(sceneRenderer.getMaxFramesInFlight());
    createIntrinsicNormalsDescriptorSetLayout();

    createIntrinsicVertexNormalsDescriptorPool(sceneRenderer.getMaxFramesInFlight());
    createIntrinsicVertexNormalsDescriptorSetLayout();

    createOutlinePipeline(sceneRenderer.getRenderExtent());
    createSupportingHalfedgePipeline(sceneRenderer.getRenderExtent());
    createIntrinsicNormalsPipeline(sceneRenderer.getRenderExtent());
    createIntrinsicVertexNormalsPipeline(sceneRenderer.getRenderExtent());
}

void OverlayPass::resize(VkExtent2D extent) {
    (void)extent;
}

void OverlayPass::updateDescriptors() {
    for (size_t i = 0; i < sceneRenderer.getMaxFramesInFlight(); i++) {
        std::array<VkDescriptorImageInfo, 2> imageInfos{};

        imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageInfos[0].imageView = frameGraph.getDepthResolveSamplerViews()[i];
        imageInfos[0].sampler = depthSampler;

        imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageInfos[1].imageView = frameGraph.getStencilMSAASamplerViews()[i];
        imageInfos[1].sampler = depthSampler;

        std::array<VkWriteDescriptorSet, 2> descriptorWrites{};

        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = outlineDescriptorSets[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pImageInfo = &imageInfos[0];

        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = outlineDescriptorSets[i];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pImageInfo = &imageInfos[1];

        vkUpdateDescriptorSets(vulkanDevice.getDevice(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }
}

void OverlayPass::createOutlineDescriptorPool(uint32_t maxFramesInFlight) {
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = maxFramesInFlight * 2;  // (depth + stencil)

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = maxFramesInFlight;

    if (vkCreateDescriptorPool(vulkanDevice.getDevice(), &poolInfo, nullptr, &outlineDescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create outline descriptor pool");
    }
}

void OverlayPass::createOutlineDescriptorSetLayout() {
    std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
    
    // Depth texture
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    // Stencil texture
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(vulkanDevice.getDevice(), &layoutInfo, nullptr, &outlineDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create outline descriptor set layout");
    }
}

void OverlayPass::createOutlineDescriptorSets(uint32_t maxFramesInFlight) {
    std::vector<VkDescriptorSetLayout> layouts(maxFramesInFlight, outlineDescriptorSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = outlineDescriptorPool;
    allocInfo.descriptorSetCount = maxFramesInFlight;
    allocInfo.pSetLayouts = layouts.data();

    outlineDescriptorSets.resize(maxFramesInFlight);
    if (vkAllocateDescriptorSets(vulkanDevice.getDevice(), &allocInfo, outlineDescriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate outline descriptor sets");
    }

    for (size_t i = 0; i < maxFramesInFlight; i++) {
        std::array<VkDescriptorImageInfo, 2> imageInfos{};
        
        // Depth texture 
        imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageInfos[0].imageView = frameGraph.getDepthResolveSamplerViews()[i];
        imageInfos[0].sampler = depthSampler;
        
        // Stencil texture 
        imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageInfos[1].imageView = frameGraph.getStencilMSAASamplerViews()[i];
        imageInfos[1].sampler = depthSampler;

        std::array<VkWriteDescriptorSet, 2> descriptorWrites{};
        
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = outlineDescriptorSets[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pImageInfo = &imageInfos[0];
        
        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = outlineDescriptorSets[i];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pImageInfo = &imageInfos[1];

        vkUpdateDescriptorSets(vulkanDevice.getDevice(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }
}

void OverlayPass::createDepthSampler() {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST; 
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

    if (vkCreateSampler(vulkanDevice.getDevice(), &samplerInfo, nullptr, &depthSampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create depth sampler");
    }
}

void OverlayPass::createSupportingHalfedgeDescriptorPool(uint32_t maxFramesInFlight) {
    const uint32_t maxModels = 10;
    
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    
    // UBO 
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = maxFramesInFlight * maxModels;
    
    // Texture buffers (S, A, H, E, T, L, H_input, E_input, T_input, L_input)
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    poolSizes[1].descriptorCount = maxFramesInFlight * maxModels * 10;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = maxFramesInFlight * maxModels;  
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;  

    if (vkCreateDescriptorPool(vulkanDevice.getDevice(), &poolInfo, nullptr, &supportingHalfedgeDescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create supporting halfedge descriptor pool");
    }
}

void OverlayPass::createSupportingHalfedgeDescriptorSetLayout() {
    std::array<VkDescriptorSetLayoutBinding, 11> bindings{};  
    
    // Binding 0: Main UBO 
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    
    // Binding 1: S buffer (supporting halfedge indices)
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    // Binding 2: A buffer (supporting angles)
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    // Binding 3: H buffer (intrinsic halfedge data)
    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    // Binding 4: E buffer (intrinsic edge data)
    bindings[4].binding = 4;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    // Binding 5: T buffer (intrinsic triangle data)
    bindings[5].binding = 5;
    bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    bindings[5].descriptorCount = 1;
    bindings[5].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    // Binding 6: L buffer (intrinsic edge lengths)
    bindings[6].binding = 6;
    bindings[6].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    bindings[6].descriptorCount = 1;
    bindings[6].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    // Binding 7: H_input buffer (input halfedge data)
    bindings[7].binding = 7;
    bindings[7].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    bindings[7].descriptorCount = 1;
    bindings[7].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    // Binding 8: E_input buffer (input edge data)
    bindings[8].binding = 8;
    bindings[8].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    bindings[8].descriptorCount = 1;
    bindings[8].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    // Binding 9: T_input buffer (input triangle data)
    bindings[9].binding = 9;
    bindings[9].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    bindings[9].descriptorCount = 1;
    bindings[9].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    // Binding 10: L_input buffer (input edge lengths)
    bindings[10].binding = 10;
    bindings[10].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    bindings[10].descriptorCount = 1;
    bindings[10].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(vulkanDevice.getDevice(), &layoutInfo, nullptr, &supportingHalfedgeDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create supporting halfedge descriptor set layout");
    }
}

void OverlayPass::allocateDescriptorSetsForModel(Model* model, uint32_t maxFramesInFlight) {
    if (!model) 
        return;
    
    // Check if already allocated
    if (perModelSupportingHalfedgeDescriptorSets.find(model) != perModelSupportingHalfedgeDescriptorSets.end()) {
        return;  
    }
    
    std::vector<VkDescriptorSetLayout> layouts(maxFramesInFlight, supportingHalfedgeDescriptorSetLayout);
    
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = supportingHalfedgeDescriptorPool;
    allocInfo.descriptorSetCount = maxFramesInFlight;
    allocInfo.pSetLayouts = layouts.data();
    
    std::vector<VkDescriptorSet> descriptorSets(maxFramesInFlight);
    if (vkAllocateDescriptorSets(vulkanDevice.getDevice(), &allocInfo, descriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate descriptor sets");
    }
    
    perModelSupportingHalfedgeDescriptorSets[model] = descriptorSets;    
}

void OverlayPass::updateDescriptorSetsForModel(Model* model, iODT* remesher, UniformBufferManager& uniformBufferManager, uint32_t maxFramesInFlight) {
    if (!model || !remesher) 
        return;
    
    if (perModelSupportingHalfedgeDescriptorSets.find(model) == perModelSupportingHalfedgeDescriptorSets.end()) {
        allocateDescriptorSetsForModel(model, maxFramesInFlight);
    }
    
    auto* supportingHalfedge = remesher->getSupportingHalfedge();
    if (!supportingHalfedge || !supportingHalfedge->isUploadedToGPU()) {
        return;
    }
    
    const auto& descriptorSets = perModelSupportingHalfedgeDescriptorSets[model];
    
    // Update descriptor sets for all frames
    for (size_t i = 0; i < maxFramesInFlight; i++) {
        std::array<VkWriteDescriptorSet, 11> descriptorWrites{};
        
        // Binding 0: UBO 
        VkDescriptorBufferInfo uboBufferInfo{};
        uboBufferInfo.buffer = uniformBufferManager.getUniformBuffers()[i];
        uboBufferInfo.offset = uniformBufferManager.getUniformBufferOffsets()[i];
        uboBufferInfo.range = sizeof(UniformBufferObject);
        
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = descriptorSets[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &uboBufferInfo;
        
        // Bindings 1-10: Texel buffers
        VkBufferView bufferViews[10] = {
            supportingHalfedge->getSupportingHalfedgeView(),
            supportingHalfedge->getSupportingAngleView(),
            supportingHalfedge->getHalfedgeView(),
            supportingHalfedge->getEdgeView(),
            supportingHalfedge->getTriangleView(),
            supportingHalfedge->getLengthView(),
            supportingHalfedge->getInputHalfedgeView(),
            supportingHalfedge->getInputEdgeView(),
            supportingHalfedge->getInputTriangleView(),
            supportingHalfedge->getInputLengthView()
        };
        
        for (int j = 0; j < 10; j++) {
            descriptorWrites[j + 1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[j + 1].dstSet = descriptorSets[i];
            descriptorWrites[j + 1].dstBinding = 1 + j;
            descriptorWrites[j + 1].dstArrayElement = 0;
            descriptorWrites[j + 1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
            descriptorWrites[j + 1].descriptorCount = 1;
            descriptorWrites[j + 1].pTexelBufferView = &bufferViews[j];
        }
        
        vkUpdateDescriptorSets(vulkanDevice.getDevice(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }    
}

void OverlayPass::createIntrinsicNormalsDescriptorPool(uint32_t maxFramesInFlight) {
    const uint32_t maxModels = 10;
    
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    
    // UBO 
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = maxFramesInFlight * maxModels;
    
    // Storage buffer (intrinsic triangle data)
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[1].descriptorCount = maxFramesInFlight * maxModels;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = maxFramesInFlight * maxModels;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

    if (vkCreateDescriptorPool(vulkanDevice.getDevice(), &poolInfo, nullptr, &intrinsicNormalsDescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create intrinsic normals descriptor pool");
    }
}

void OverlayPass::createIntrinsicNormalsDescriptorSetLayout() {
    std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
    
    // Binding 0: IntrinsicTriangleData storage buffer
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    
    // Binding 1: UBO 
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_GEOMETRY_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(vulkanDevice.getDevice(), &layoutInfo, nullptr, &intrinsicNormalsDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create intrinsic normals descriptor set layout");
    }
}

void OverlayPass::allocateNormalsDescriptorSetsForModel(Model* model, uint32_t maxFramesInFlight) {
    if (!model) 
        return;
    
    // Check if already allocated
    if (perModelIntrinsicNormalsDescriptorSets.find(model) != perModelIntrinsicNormalsDescriptorSets.end()) {
        return;  
    }
    
    std::vector<VkDescriptorSetLayout> layouts(maxFramesInFlight, intrinsicNormalsDescriptorSetLayout);
    
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = intrinsicNormalsDescriptorPool;
    allocInfo.descriptorSetCount = maxFramesInFlight;
    allocInfo.pSetLayouts = layouts.data();
    
    std::vector<VkDescriptorSet> descriptorSets(maxFramesInFlight);
    if (vkAllocateDescriptorSets(vulkanDevice.getDevice(), &allocInfo, descriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate intrinsic normals descriptor sets");
    }
    
    perModelIntrinsicNormalsDescriptorSets[model] = descriptorSets;    
}

void OverlayPass::updateNormalsDescriptorSetsForModel(Model* model, iODT* remesher, UniformBufferManager& uniformBufferManager, uint32_t maxFramesInFlight) {
    if (!model || !remesher) 
        return;
    
    if (perModelIntrinsicNormalsDescriptorSets.find(model) == perModelIntrinsicNormalsDescriptorSets.end()) {
        allocateNormalsDescriptorSetsForModel(model, maxFramesInFlight);
    }
    
    auto* supportingHalfedge = remesher->getSupportingHalfedge();
    if (!supportingHalfedge || !supportingHalfedge->isUploadedToGPU()) {
        return;
    }
    
    VkBuffer triangleBuffer = supportingHalfedge->getIntrinsicTriangleBuffer();
    VkDeviceSize triangleOffset = supportingHalfedge->getTriangleGeometryOffset();
    size_t triangleCount = supportingHalfedge->getTriangleCount();
    
    if (triangleBuffer == VK_NULL_HANDLE || triangleCount == 0) {
        return;
    }
    
    const auto& descriptorSets = perModelIntrinsicNormalsDescriptorSets[model];
    
    // Update descriptor sets for all frames
    for (size_t i = 0; i < maxFramesInFlight; i++) {
        std::array<VkWriteDescriptorSet, 2> descriptorWrites{};
        
        // Binding 0: IntrinsicTriangleData storage buffer
        VkDescriptorBufferInfo triangleBufferInfo{};
        triangleBufferInfo.buffer = triangleBuffer;
        triangleBufferInfo.offset = triangleOffset;
        triangleBufferInfo.range = triangleCount * sizeof(IntrinsicTriangleData);
        
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = descriptorSets[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &triangleBufferInfo;
        
        // Binding 1: UBO
        VkDescriptorBufferInfo uboBufferInfo{};
        uboBufferInfo.buffer = uniformBufferManager.getUniformBuffers()[i];
        uboBufferInfo.offset = uniformBufferManager.getUniformBufferOffsets()[i];
        uboBufferInfo.range = sizeof(UniformBufferObject);
        
        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = descriptorSets[i];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pBufferInfo = &uboBufferInfo;
        
        vkUpdateDescriptorSets(vulkanDevice.getDevice(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }    
}

void OverlayPass::createIntrinsicVertexNormalsDescriptorPool(uint32_t maxFramesInFlight) {
    const uint32_t maxModels = 10;
    
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    
    // UBO 
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = maxFramesInFlight * maxModels;
    
    // Storage buffer (intrinsic vertex data)
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[1].descriptorCount = maxFramesInFlight * maxModels;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = maxFramesInFlight * maxModels;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

    if (vkCreateDescriptorPool(vulkanDevice.getDevice(), &poolInfo, nullptr, &intrinsicVertexNormalsDescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create intrinsic vertex normals descriptor pool");
    }
}

void OverlayPass::createIntrinsicVertexNormalsDescriptorSetLayout() {
    std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
    
    // Binding 0: IntrinsicVertexData storage buffer
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    
    // Binding 1: UBO 
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_GEOMETRY_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(vulkanDevice.getDevice(), &layoutInfo, nullptr, &intrinsicVertexNormalsDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create intrinsic vertex normals descriptor set layout");
    }
}

void OverlayPass::allocateVertexNormalsDescriptorSetsForModel(Model* model, uint32_t maxFramesInFlight) {
    if (!model) 
        return;
    
    // Check if already allocated
    if (perModelIntrinsicVertexNormalsDescriptorSets.find(model) != perModelIntrinsicVertexNormalsDescriptorSets.end()) {
        return;  
    }
    
    std::vector<VkDescriptorSetLayout> layouts(maxFramesInFlight, intrinsicVertexNormalsDescriptorSetLayout);
    
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = intrinsicVertexNormalsDescriptorPool;
    allocInfo.descriptorSetCount = maxFramesInFlight;
    allocInfo.pSetLayouts = layouts.data();
    
    std::vector<VkDescriptorSet> descriptorSets(maxFramesInFlight);
    if (vkAllocateDescriptorSets(vulkanDevice.getDevice(), &allocInfo, descriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate intrinsic vertex normals descriptor sets");
    }
    
    perModelIntrinsicVertexNormalsDescriptorSets[model] = descriptorSets;    
}

void OverlayPass::updateVertexNormalsDescriptorSetsForModel(Model* model, iODT* remesher, UniformBufferManager& uniformBufferManager, uint32_t maxFramesInFlight) {
    if (!model || !remesher) 
        return;
    
    if (perModelIntrinsicVertexNormalsDescriptorSets.find(model) == perModelIntrinsicVertexNormalsDescriptorSets.end()) {
        allocateVertexNormalsDescriptorSetsForModel(model, maxFramesInFlight);
    }
    
    auto* supportingHalfedge = remesher->getSupportingHalfedge();
    if (!supportingHalfedge || !supportingHalfedge->isUploadedToGPU()) {
        return;
    }
    
    VkBuffer vertexBuffer = supportingHalfedge->getIntrinsicVertexBuffer();
    VkDeviceSize vertexOffset = supportingHalfedge->getVertexGeometryOffset();
    size_t vertexCount = supportingHalfedge->getVertexCount();
    
    if (vertexBuffer == VK_NULL_HANDLE || vertexCount == 0) {
        return;
    }
    
    const auto& descriptorSets = perModelIntrinsicVertexNormalsDescriptorSets[model];
    
    // Update descriptor sets for all frames
    for (size_t i = 0; i < maxFramesInFlight; i++) {
        std::array<VkWriteDescriptorSet, 2> descriptorWrites{};
        
        // Binding 0: IntrinsicVertexData storage buffer
        VkDescriptorBufferInfo vertexBufferInfo{};
        vertexBufferInfo.buffer = vertexBuffer;
        vertexBufferInfo.offset = vertexOffset;
        vertexBufferInfo.range = vertexCount * sizeof(IntrinsicVertexData);
        
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = descriptorSets[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &vertexBufferInfo;
        
        // Binding 1: UBO
        VkDescriptorBufferInfo uboBufferInfo{};
        uboBufferInfo.buffer = uniformBufferManager.getUniformBuffers()[i];
        uboBufferInfo.offset = uniformBufferManager.getUniformBufferOffsets()[i];
        uboBufferInfo.range = sizeof(UniformBufferObject);
        
        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = descriptorSets[i];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pBufferInfo = &uboBufferInfo;
        
        vkUpdateDescriptorSets(vulkanDevice.getDevice(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }    
}

void OverlayPass::createOutlinePipeline(VkExtent2D extent) {
    auto vertCode = readFile("shaders/outline_vert.spv");
    auto fragCode = readFile("shaders/outline_frag.spv");

    VkShaderModule vertModule = createShaderModule(vulkanDevice, vertCode);
    VkShaderModule fragModule = createShaderModule(vulkanDevice, fragCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 0;
    vertexInputInfo.pVertexBindingDescriptions = nullptr;
    vertexInputInfo.vertexAttributeDescriptionCount = 0;
    vertexInputInfo.pVertexAttributeDescriptions = nullptr;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;  
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE; 

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_8_BIT;  
    multisampling.minSampleShading = 1.0f;
    multisampling.pSampleMask = nullptr;
    multisampling.alphaToCoverageEnable = VK_FALSE;
    multisampling.alphaToOneEnable = VK_FALSE;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachments[2] = {};
    // Surface overlay target disabled for line-only pipeline.
    colorBlendAttachments[0].colorWriteMask = 0;
    colorBlendAttachments[0].blendEnable = VK_FALSE;
    // Line overlay target.
    colorBlendAttachments[1].colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
        VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT |
        VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachments[1].blendEnable = VK_TRUE;
    colorBlendAttachments[1].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachments[1].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachments[1].colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachments[1].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachments[1].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachments[1].alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 2;
    colorBlending.pAttachments = colorBlendAttachments;

    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_STENCIL_REFERENCE
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // Push constant for outline thickness and color
    VkPushConstantRange outlinePushConstantRange{};
    outlinePushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    outlinePushConstantRange.offset = 0;
    outlinePushConstantRange.size = sizeof(OutlinePushConstant);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &outlineDescriptorSetLayout; 
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &outlinePushConstantRange;

    if (vkCreatePipelineLayout(vulkanDevice.getDevice(), &pipelineLayoutInfo, nullptr, &outlinePipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create outline pipeline layout");
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = outlinePipelineLayout;
    pipelineInfo.renderPass = frameGraph.getRenderPass();
    pipelineInfo.subpass = frameGraph.getSubpassIndex(name());

    if (vkCreateGraphicsPipelines(vulkanDevice.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &outlinePipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create outline pipeline");
    }

    vkDestroyShaderModule(vulkanDevice.getDevice(), vertModule, nullptr);
    vkDestroyShaderModule(vulkanDevice.getDevice(), fragModule, nullptr);
}

void OverlayPass::createSupportingHalfedgePipeline(VkExtent2D extent) {

    auto vertShaderCode = readFile("shaders/intrinsic_supporting_vert.spv"); 
    auto geomShaderCode = readFile("shaders/intrinsic_supporting_geom.spv");  
    auto fragShaderCode = readFile("shaders/intrinsic_supporting_frag.spv");

    VkShaderModule vertShaderModule = createShaderModule(vulkanDevice, vertShaderCode);
    VkShaderModule geomShaderModule = createShaderModule(vulkanDevice, geomShaderCode);  
    VkShaderModule fragShaderModule = createShaderModule(vulkanDevice, fragShaderCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo geomShaderStageInfo{};
    geomShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    geomShaderStageInfo.stage = VK_SHADER_STAGE_GEOMETRY_BIT;
    geomShaderStageInfo.module = geomShaderModule;
    geomShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, geomShaderStageInfo, fragShaderStageInfo };

    auto bindingDescriptions = Vertex::getBindingDescriptions();
    auto vertexAttributes = Vertex::getVertexAttributes();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescriptions.size());
    vertexInputInfo.pVertexBindingDescriptions = bindingDescriptions.data();

    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributes.size());
    vertexInputInfo.pVertexAttributeDescriptions = vertexAttributes.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL; 
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;  
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_TRUE;  

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_8_BIT;  // 8x MSAA 

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE;  
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachments[2] = {};
    // Surface overlay target.
    colorBlendAttachments[0].colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachments[0].blendEnable = VK_TRUE;
    colorBlendAttachments[0].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachments[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachments[0].colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachments[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachments[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachments[0].alphaBlendOp = VK_BLEND_OP_ADD;
    // Line overlay target disabled for this pipeline.
    colorBlendAttachments[1].colorWriteMask = 0;
    colorBlendAttachments[1].blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 2;
    colorBlending.pAttachments = colorBlendAttachments;

    std::vector<VkDynamicState> dynamicStates = {
         VK_DYNAMIC_STATE_VIEWPORT,
         VK_DYNAMIC_STATE_SCISSOR,
         VK_DYNAMIC_STATE_DEPTH_BIAS,
         VK_DYNAMIC_STATE_STENCIL_REFERENCE
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // Push constant for model matrix 
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(glm::mat4); 

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &supportingHalfedgeDescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(vulkanDevice.getDevice(), &pipelineLayoutInfo, nullptr, &supportingHalfedgePipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create supporting halfedge pipeline layout!");
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 3; 
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = supportingHalfedgePipelineLayout;
    pipelineInfo.renderPass = frameGraph.getRenderPass();
    pipelineInfo.subpass = frameGraph.getSubpassIndex(name());

    if (vkCreateGraphicsPipelines(vulkanDevice.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &supportingHalfedgePipeline) != VK_SUCCESS) {
        throw std::runtime_error("failed to create supporting halfedge pipeline!");
    }

    vkDestroyShaderModule(vulkanDevice.getDevice(), vertShaderModule, nullptr);
    vkDestroyShaderModule(vulkanDevice.getDevice(), geomShaderModule, nullptr);  
    vkDestroyShaderModule(vulkanDevice.getDevice(), fragShaderModule, nullptr);
}

void OverlayPass::createIntrinsicNormalsPipeline(VkExtent2D extent) {
    auto vertShaderCode = readFile("shaders/intrinsic_normals_vert.spv");
    auto geomShaderCode = readFile("shaders/intrinsic_normals_geom.spv");
    auto fragShaderCode = readFile("shaders/intrinsic_normals_frag.spv");

    VkShaderModule vertShaderModule = createShaderModule(vulkanDevice, vertShaderCode);
    VkShaderModule geomShaderModule = createShaderModule(vulkanDevice, geomShaderCode);
    VkShaderModule fragShaderModule = createShaderModule(vulkanDevice, fragShaderCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo geomShaderStageInfo{};
    geomShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    geomShaderStageInfo.stage = VK_SHADER_STAGE_GEOMETRY_BIT;
    geomShaderStageInfo.module = geomShaderModule;
    geomShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, geomShaderStageInfo, fragShaderStageInfo };

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 0;
    vertexInputInfo.pVertexBindingDescriptions = nullptr;
    vertexInputInfo.vertexAttributeDescriptionCount = 0;
    vertexInputInfo.pVertexAttributeDescriptions = nullptr;

    // Point list topology
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 2.0f;  
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_8_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE; 
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachments[2] = {};
    // Surface overlay target disabled for line-only pipeline.
    colorBlendAttachments[0].colorWriteMask = 0;
    colorBlendAttachments[0].blendEnable = VK_FALSE;
    // Line overlay target.
    colorBlendAttachments[1].colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachments[1].blendEnable = VK_TRUE;
    colorBlendAttachments[1].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachments[1].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachments[1].colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachments[1].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachments[1].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachments[1].alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 2;
    colorBlending.pAttachments = colorBlendAttachments;

    std::vector<VkDynamicState> dynamicStates = {
         VK_DYNAMIC_STATE_VIEWPORT,
         VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // Push constant for model matrix, normal length, and average area
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_GEOMETRY_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(NormalPushConstant);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &intrinsicNormalsDescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(vulkanDevice.getDevice(), &pipelineLayoutInfo, nullptr, &intrinsicNormalsPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create intrinsic normals pipeline layout!");
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 3;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = intrinsicNormalsPipelineLayout;
    pipelineInfo.renderPass = frameGraph.getRenderPass();
    pipelineInfo.subpass = frameGraph.getSubpassIndex(name());

    if (vkCreateGraphicsPipelines(vulkanDevice.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &intrinsicNormalsPipeline) != VK_SUCCESS) {
        throw std::runtime_error("failed to create intrinsic normals pipeline!");
    }

    vkDestroyShaderModule(vulkanDevice.getDevice(), vertShaderModule, nullptr);
    vkDestroyShaderModule(vulkanDevice.getDevice(), geomShaderModule, nullptr);
    vkDestroyShaderModule(vulkanDevice.getDevice(), fragShaderModule, nullptr);
}

void OverlayPass::createIntrinsicVertexNormalsPipeline(VkExtent2D extent) {
    auto vertShaderCode = readFile("shaders/intrinsic_vertex_normals_vert.spv");
    auto geomShaderCode = readFile("shaders/intrinsic_vertex_normals_geom.spv");
    auto fragShaderCode = readFile("shaders/intrinsic_vertex_normals_frag.spv");

    VkShaderModule vertShaderModule = createShaderModule(vulkanDevice, vertShaderCode);
    VkShaderModule geomShaderModule = createShaderModule(vulkanDevice, geomShaderCode);
    VkShaderModule fragShaderModule = createShaderModule(vulkanDevice, fragShaderCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo geomShaderStageInfo{};
    geomShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    geomShaderStageInfo.stage = VK_SHADER_STAGE_GEOMETRY_BIT;
    geomShaderStageInfo.module = geomShaderModule;
    geomShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, geomShaderStageInfo, fragShaderStageInfo };

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 0;
    vertexInputInfo.pVertexBindingDescriptions = nullptr;
    vertexInputInfo.vertexAttributeDescriptionCount = 0;
    vertexInputInfo.pVertexAttributeDescriptions = nullptr;

    // Point list topology 
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 2.0f;  
    rasterizer.cullMode = VK_CULL_MODE_NONE;  
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_8_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE;  
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachments[2] = {};
    // Surface overlay target disabled for line-only pipeline.
    colorBlendAttachments[0].colorWriteMask = 0;
    colorBlendAttachments[0].blendEnable = VK_FALSE;
    // Line overlay target.
    colorBlendAttachments[1].colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachments[1].blendEnable = VK_TRUE;
    colorBlendAttachments[1].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachments[1].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachments[1].colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachments[1].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachments[1].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachments[1].alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 2;
    colorBlending.pAttachments = colorBlendAttachments;

    std::vector<VkDynamicState> dynamicStates = {
         VK_DYNAMIC_STATE_VIEWPORT,
         VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // Push constant for model matrix, normal length, and average area
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_GEOMETRY_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(NormalPushConstant);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &intrinsicVertexNormalsDescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(vulkanDevice.getDevice(), &pipelineLayoutInfo, nullptr, &intrinsicVertexNormalsPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create intrinsic vertex normals pipeline layout!");
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 3;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = intrinsicVertexNormalsPipelineLayout;
    pipelineInfo.renderPass = frameGraph.getRenderPass();
    pipelineInfo.subpass = frameGraph.getSubpassIndex(name());

    if (vkCreateGraphicsPipelines(vulkanDevice.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &intrinsicVertexNormalsPipeline) != VK_SUCCESS) {
        throw std::runtime_error("failed to create intrinsic vertex normals pipeline!");
    }

    vkDestroyShaderModule(vulkanDevice.getDevice(), vertShaderModule, nullptr);
    vkDestroyShaderModule(vulkanDevice.getDevice(), geomShaderModule, nullptr);
    vkDestroyShaderModule(vulkanDevice.getDevice(), fragShaderModule, nullptr);
}


void OverlayPass::record(
    const FrameContext& frameContext,
    const SceneView& sceneView,
    const RenderFlags& flags,
    const OverlayParams& overlayParams,
    RenderServices& services) {
    (void)sceneView;

    if (!services.resourceManager || !services.heatSystem || !services.modelSelection || !services.gizmo || !services.wireframeRenderer) {
        return;
    }

    ResourceManager& resourceManager = *services.resourceManager;
    HeatSystem& heatSystem = *services.heatSystem;
    ModelSelection& modelSelection = *services.modelSelection;
    Gizmo& gizmo = *services.gizmo;
    WireframeRenderer& wireframeRenderer = *services.wireframeRenderer;

    VkCommandBuffer commandBuffer = frameContext.commandBuffer;
    const uint32_t currentFrame = frameContext.currentFrame;
    const VkExtent2D extent = frameContext.extent;

    if (flags.drawHeatOverlay && (heatSystem.getIsActive() || heatSystem.getIsPaused())) {
        heatSystem.renderHeatOverlay(commandBuffer, currentFrame);
    }

    if (flags.drawIntrinsicOverlay) {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, supportingHalfedgePipeline);
        vkCmdSetDepthBias(commandBuffer, 0.1f, 0.0f, 0.1f);

        for (const auto& [model, remeshData] : resourceManager.getModelRemeshData()) {
            if (!remeshData.isRemeshed || !remeshData.remesher) {
                continue;
            }

            auto* supportingHalfedge = remeshData.remesher->getSupportingHalfedge();
            if (!supportingHalfedge || !supportingHalfedge->isUploadedToGPU()) {
                continue;
            }

            auto it = perModelSupportingHalfedgeDescriptorSets.find(model);
            if (it == perModelSupportingHalfedgeDescriptorSets.end()) {
                continue;
            }

            const auto& modelDescriptorSets = it->second;
            if (currentFrame >= modelDescriptorSets.size()) {
                continue;
            }

            glm::mat4 modelMatrix = model->getModelMatrix();
            vkCmdPushConstants(commandBuffer, supportingHalfedgePipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &modelMatrix);

            VkBuffer modelVertexBuffer = model->getVertexBuffer();
            VkDeviceSize modelVertexOffset = model->getVertexBufferOffset();
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, &modelVertexBuffer, &modelVertexOffset);
            vkCmdBindIndexBuffer(commandBuffer, model->getIndexBuffer(), model->getIndexBufferOffset(), VK_INDEX_TYPE_UINT32);
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, supportingHalfedgePipelineLayout, 0, 1, &modelDescriptorSets[currentFrame], 0, nullptr);
            vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(model->getIndices().size()), 1, 0, 0, 0);
        }

        vkCmdSetDepthBias(commandBuffer, 0.0f, 0.0f, 0.0f);
    }

    if (overlayParams.drawIntrinsicNormals) {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, intrinsicNormalsPipeline);

        for (const auto& [model, remeshData] : resourceManager.getModelRemeshData()) {
            if (!remeshData.isRemeshed || !remeshData.remesher) {
                continue;
            }

            auto* supportingHalfedge = remeshData.remesher->getSupportingHalfedge();
            if (!supportingHalfedge || !supportingHalfedge->isUploadedToGPU()) {
                continue;
            }

            size_t triangleCount = supportingHalfedge->getTriangleCount();
            if (triangleCount == 0) {
                continue;
            }

            auto it = perModelIntrinsicNormalsDescriptorSets.find(model);
            if (it == perModelIntrinsicNormalsDescriptorSets.end()) {
                continue;
            }

            const auto& modelDescriptorSets = it->second;
            if (currentFrame >= modelDescriptorSets.size()) {
                continue;
            }

            NormalPushConstant pushConstants{};
            pushConstants.modelMatrix = model->getModelMatrix();
            pushConstants.normalLength = overlayParams.normalLength;
            pushConstants.avgArea = supportingHalfedge->getAverageTriangleArea();

            vkCmdPushConstants(commandBuffer, intrinsicNormalsPipelineLayout, VK_SHADER_STAGE_GEOMETRY_BIT, 0, sizeof(NormalPushConstant), &pushConstants);
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, intrinsicNormalsPipelineLayout, 0, 1, &modelDescriptorSets[currentFrame], 0, nullptr);
            vkCmdDraw(commandBuffer, static_cast<uint32_t>(triangleCount), 1, 0, 0);
        }
    }

    if (overlayParams.drawIntrinsicVertexNormals) {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, intrinsicVertexNormalsPipeline);

        for (const auto& [model, remeshData] : resourceManager.getModelRemeshData()) {
            if (!remeshData.isRemeshed || !remeshData.remesher) {
                continue;
            }

            auto* supportingHalfedge = remeshData.remesher->getSupportingHalfedge();
            if (!supportingHalfedge || !supportingHalfedge->isUploadedToGPU()) {
                continue;
            }

            size_t vertexCount = supportingHalfedge->getVertexCount();
            if (vertexCount == 0) {
                continue;
            }

            auto it = perModelIntrinsicVertexNormalsDescriptorSets.find(model);
            if (it == perModelIntrinsicVertexNormalsDescriptorSets.end()) {
                continue;
            }

            const auto& modelDescriptorSets = it->second;
            if (currentFrame >= modelDescriptorSets.size()) {
                continue;
            }

            NormalPushConstant pushConstants{};
            pushConstants.modelMatrix = model->getModelMatrix();
            pushConstants.normalLength = overlayParams.normalLength;
            pushConstants.avgArea = 0.0f;

            vkCmdPushConstants(commandBuffer, intrinsicVertexNormalsPipelineLayout, VK_SHADER_STAGE_GEOMETRY_BIT, 0, sizeof(NormalPushConstant), &pushConstants);
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, intrinsicVertexNormalsPipelineLayout, 0, 1, &modelDescriptorSets[currentFrame], 0, nullptr);
            vkCmdDraw(commandBuffer, static_cast<uint32_t>(vertexCount), 1, 0, 0);
        }
    }

    if (modelSelection.getSelected() && currentFrame < outlineDescriptorSets.size()) {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, outlinePipeline);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, outlinePipelineLayout, 0, 1, &outlineDescriptorSets[currentFrame], 0, nullptr);

        const auto& selectedIDs = modelSelection.getSelectedModelIDsRenderThread();
        for (uint32_t id : selectedIDs) {
            OutlinePushConstant outlinePC{};
            outlinePC.outlineThickness = modelSelection.getOutlineThickness();
            outlinePC.selectedModelID = id;
            outlinePC.outlineColor = modelSelection.getOutlineColor();

            vkCmdPushConstants(commandBuffer, outlinePipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(OutlinePushConstant), &outlinePC);
            vkCmdDraw(commandBuffer, 3, 1, 0, 0);
        }
    }

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, resourceManager.getGrid().getGridPipeline());
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, resourceManager.getGrid().getGridPipelineLayout(), 0, 1, &resourceManager.getGrid().getGridDescriptorSets()[currentFrame], 0, nullptr);
    vkCmdDraw(commandBuffer, resourceManager.getGrid().vertexCount, 1, 0, 0);
    resourceManager.getGrid().renderLabels(commandBuffer, currentFrame);

    if (flags.drawSurfels && heatSystem.getIsActive() && heatSystem.getIsVoronoiReady()) {
        heatSystem.renderSurfels(commandBuffer, currentFrame, resourceManager.getHeatModel().getModelMatrix(), 0.0025f);
    }
    if (flags.drawVoronoi && heatSystem.getIsActive() && heatSystem.getIsVoronoiReady()) {
        heatSystem.renderVoronoiSurface(commandBuffer, currentFrame);
    }
    if (flags.drawPoints && heatSystem.getIsActive() && heatSystem.getIsVoronoiReady()) {
        heatSystem.renderOccupancy(commandBuffer, currentFrame, extent);
    }

    if (flags.wireframeMode > 0) {
        VkDescriptorSet geometryDescriptorSet = geometryPass.getDescriptorSet(currentFrame);
        if (geometryDescriptorSet != VK_NULL_HANDLE) {
            wireframeRenderer.bindPipeline(commandBuffer);
            wireframeRenderer.renderModel(commandBuffer, resourceManager.getVisModel(), geometryDescriptorSet, resourceManager.getVisModel().getModelMatrix());
            wireframeRenderer.renderModel(commandBuffer, resourceManager.getHeatModel(), geometryDescriptorSet, resourceManager.getHeatModel().getModelMatrix());
        }
    }

    if (flags.drawContactLines && heatSystem.getIsActive()) {
        heatSystem.renderContactLines(commandBuffer, currentFrame, extent);
    }

    resourceManager.renderTimingOverlay(commandBuffer, currentFrame, extent);

    if (modelSelection.getSelected()) {
        VkClearAttachment clearAttachment{};
        clearAttachment.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        clearAttachment.clearValue.depthStencil = { 1.0f, 0 };

        VkClearRect clearRect{};
        clearRect.rect.offset = { 0, 0 };
        clearRect.rect.extent = extent;
        clearRect.baseArrayLayer = 0;
        clearRect.layerCount = 1;

        vkCmdClearAttachments(commandBuffer, 1, &clearAttachment, 1, &clearRect);

        glm::vec3 gizmoPosition = gizmo.calculateGizmoPosition(resourceManager, modelSelection);
        float gizmoScale = gizmo.calculateGizmoScale(resourceManager, modelSelection);
        gizmo.render(commandBuffer, currentFrame, gizmoPosition, extent, gizmoScale);
    }
}

void OverlayPass::destroy() {
    if (outlinePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(vulkanDevice.getDevice(), outlinePipeline, nullptr);
        outlinePipeline = VK_NULL_HANDLE;
    }
    if (supportingHalfedgePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(vulkanDevice.getDevice(), supportingHalfedgePipeline, nullptr);
        supportingHalfedgePipeline = VK_NULL_HANDLE;
    }
    if (intrinsicNormalsPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(vulkanDevice.getDevice(), intrinsicNormalsPipeline, nullptr);
        intrinsicNormalsPipeline = VK_NULL_HANDLE;
    }
    if (intrinsicVertexNormalsPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(vulkanDevice.getDevice(), intrinsicVertexNormalsPipeline, nullptr);
        intrinsicVertexNormalsPipeline = VK_NULL_HANDLE;
    }

    if (outlinePipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vulkanDevice.getDevice(), outlinePipelineLayout, nullptr);
        outlinePipelineLayout = VK_NULL_HANDLE;
    }
    if (supportingHalfedgePipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vulkanDevice.getDevice(), supportingHalfedgePipelineLayout, nullptr);
        supportingHalfedgePipelineLayout = VK_NULL_HANDLE;
    }
    if (intrinsicNormalsPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vulkanDevice.getDevice(), intrinsicNormalsPipelineLayout, nullptr);
        intrinsicNormalsPipelineLayout = VK_NULL_HANDLE;
    }
    if (intrinsicVertexNormalsPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vulkanDevice.getDevice(), intrinsicVertexNormalsPipelineLayout, nullptr);
        intrinsicVertexNormalsPipelineLayout = VK_NULL_HANDLE;
    }

    if (outlineDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vulkanDevice.getDevice(), outlineDescriptorSetLayout, nullptr);
        outlineDescriptorSetLayout = VK_NULL_HANDLE;
    }
    if (supportingHalfedgeDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vulkanDevice.getDevice(), supportingHalfedgeDescriptorSetLayout, nullptr);
        supportingHalfedgeDescriptorSetLayout = VK_NULL_HANDLE;
    }
    if (intrinsicNormalsDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vulkanDevice.getDevice(), intrinsicNormalsDescriptorSetLayout, nullptr);
        intrinsicNormalsDescriptorSetLayout = VK_NULL_HANDLE;
    }
    if (intrinsicVertexNormalsDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vulkanDevice.getDevice(), intrinsicVertexNormalsDescriptorSetLayout, nullptr);
        intrinsicVertexNormalsDescriptorSetLayout = VK_NULL_HANDLE;
    }

    if (outlineDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vulkanDevice.getDevice(), outlineDescriptorPool, nullptr);
        outlineDescriptorPool = VK_NULL_HANDLE;
    }
    if (supportingHalfedgeDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vulkanDevice.getDevice(), supportingHalfedgeDescriptorPool, nullptr);
        supportingHalfedgeDescriptorPool = VK_NULL_HANDLE;
    }
    if (intrinsicNormalsDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vulkanDevice.getDevice(), intrinsicNormalsDescriptorPool, nullptr);
        intrinsicNormalsDescriptorPool = VK_NULL_HANDLE;
    }
    if (intrinsicVertexNormalsDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vulkanDevice.getDevice(), intrinsicVertexNormalsDescriptorPool, nullptr);
        intrinsicVertexNormalsDescriptorPool = VK_NULL_HANDLE;
    }

    if (depthSampler != VK_NULL_HANDLE) {
        vkDestroySampler(vulkanDevice.getDevice(), depthSampler, nullptr);
        depthSampler = VK_NULL_HANDLE;
    }

    outlineDescriptorSets.clear();
    perModelSupportingHalfedgeDescriptorSets.clear();
    perModelIntrinsicNormalsDescriptorSets.clear();
    perModelIntrinsicVertexNormalsDescriptorSets.clear();
}


} // namespace render

