#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <stdexcept>
#include <array>
#include <vector>

#include "Structs.hpp"
#include "File_utils.h"
#include "Model.hpp"
#include "Grid.hpp"
#include "HeatSource.hpp"
#include "HeatSystem.hpp"
#include "VulkanImage.hpp"
#include "UniformBufferManager.hpp"
#include "ResourceManager.hpp"
#include "MemoryAllocator.hpp"
#include "VulkanDevice.hpp"
#include "GBuffer.hpp"

GBuffer::GBuffer(VulkanDevice& vulkanDevice, VkFormat swapchainImageFormat)
    : vulkanDevice(&vulkanDevice) {
    createRenderPass(vulkanDevice, swapchainImageFormat);
}

GBuffer::~GBuffer() {
}

void GBuffer::init(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, ResourceManager& resourceManager, HeatSystem& heatSystem,
    uint32_t width, uint32_t height, VkExtent2D swapchainExtent, const std::vector<VkImageView> swapChainImageViews, VkFormat swapchainImageFormat, uint32_t maxFramesInFlight) {
    this->vulkanDevice = &vulkanDevice;
    this->memoryAllocator = &memoryAllocator;
    this->resourceManager = &resourceManager;
    this->heatSystem = &heatSystem;

    createImageViews(vulkanDevice, swapchainExtent, maxFramesInFlight);

    createGeometryDescriptorPool(vulkanDevice, maxFramesInFlight);
    createGeometryDescriptorSetLayout(vulkanDevice);
    createGeometryDescriptorSets(vulkanDevice, resourceManager, maxFramesInFlight);

    createLightingDescriptorPool(vulkanDevice, maxFramesInFlight);
    createLightingDescriptorSetLayout(vulkanDevice);
    createLightingDescriptorSets(vulkanDevice, resourceManager, maxFramesInFlight);

    createFramebuffers(vulkanDevice, swapChainImageViews, swapchainExtent, maxFramesInFlight);

    createGeometryPipeline(vulkanDevice, swapchainExtent);
    createLightingPipeline(vulkanDevice, swapchainExtent);

    createCommandBuffers(vulkanDevice, maxFramesInFlight);
}

void GBuffer::createCommandBuffers(const VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight) {
    gbufferCommandBuffers.resize(maxFramesInFlight);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = vulkanDevice.getCommandPool();
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(gbufferCommandBuffers.size());

    if (vkAllocateCommandBuffers(vulkanDevice.getDevice(), &allocInfo, gbufferCommandBuffers.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate command buffers for G-buffer pass");
    }
}

void GBuffer::freeCommandBuffers(VulkanDevice& vulkanDevice) {
    vkFreeCommandBuffers(vulkanDevice.getDevice(), vulkanDevice.getCommandPool(), static_cast<uint32_t>(gbufferCommandBuffers.size()), gbufferCommandBuffers.data());
    gbufferCommandBuffers.clear();
}
        
void GBuffer::createImageViews(const VulkanDevice& vulkanDevice, VkExtent2D extent, uint32_t maxFramesInFlight) {
    // Resize the vectors for multiple frames
    gAlbedoImageViews.resize(maxFramesInFlight);
    gNormalImageViews.resize(maxFramesInFlight);
    gPositionImageViews.resize(maxFramesInFlight);
    gDepthImageViews.resize(maxFramesInFlight);

    gAlbedoImages.resize(maxFramesInFlight);
    gAlbedoImageMemories.resize(maxFramesInFlight);

    gNormalImages.resize(maxFramesInFlight);
    gNormalImageMemories.resize(maxFramesInFlight);

    gPositionImages.resize(maxFramesInFlight);
    gPositionImageMemories.resize(maxFramesInFlight);

    gDepthImages.resize(maxFramesInFlight);
    gDepthImageMemories.resize(maxFramesInFlight);

    for (size_t i = 0; i < maxFramesInFlight; i++) {
        // Albedo image creation for each frame
        VkFormat albedoFormat = VK_FORMAT_R8G8B8A8_UNORM;
        gAlbedoImageInfo = createImageCreateInfo(extent.width, extent.height, albedoFormat,
            VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT);
        createImage(vulkanDevice, extent.width, extent.height, albedoFormat,
            VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, gAlbedoImages[i], gAlbedoImageMemories[i]);
        gAlbedoImageViews[i] = createImageView(vulkanDevice, gAlbedoImages[i], albedoFormat, VK_IMAGE_ASPECT_COLOR_BIT);

        // Normal image creation for each frame
        VkFormat normalFormat = VK_FORMAT_R16G16B16A16_SFLOAT; //A16 remains unused
        gNormalImageInfo = createImageCreateInfo(extent.width, extent.height, normalFormat,
            VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT);
        createImage(vulkanDevice, extent.width, extent.height, normalFormat,
            VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, gNormalImages[i], gNormalImageMemories[i]);
        gNormalImageViews[i] = createImageView(vulkanDevice, gNormalImages[i], normalFormat, VK_IMAGE_ASPECT_COLOR_BIT);

        // Position image creation for each frame
        VkFormat positionFormat = VK_FORMAT_R16G16B16A16_SFLOAT; //A16 remains unused
        gPositionImageInfo = createImageCreateInfo(extent.width, extent.height, positionFormat,
            VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT);
        createImage(vulkanDevice, extent.width, extent.height, positionFormat,
            VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, gPositionImages[i], gPositionImageMemories[i]);
        gPositionImageViews[i] = createImageView(vulkanDevice, gPositionImages[i], positionFormat, VK_IMAGE_ASPECT_COLOR_BIT);

        // Depth image creation for each frame
        VkFormat depthFormat = VK_FORMAT_D32_SFLOAT_S8_UINT;
        gDepthImageInfo = createImageCreateInfo(extent.width, extent.height, depthFormat,
            VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT);
        createImage(vulkanDevice, extent.width, extent.height, depthFormat,
            VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, gDepthImages[i], gDepthImageMemories[i]);
        gDepthImageViews[i] = createImageView(vulkanDevice, gDepthImages[i], depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
    }
}

void GBuffer::createFramebuffers(const VulkanDevice& vulkanDevice, std::vector<VkImageView> swapChainImageViews, VkExtent2D extent, uint32_t maxFramesInFlight) {
    size_t totalFramebuffers = maxFramesInFlight * swapChainImageViews.size();
    framebuffers.resize(totalFramebuffers);

    if (swapChainImageViews.empty()) {
        throw std::runtime_error("Swapchain image views array is empty");
    }

    for (size_t frameIndex = 0; frameIndex < maxFramesInFlight; frameIndex++) {
        for (size_t swapchainIndex = 0; swapchainIndex < swapChainImageViews.size(); swapchainIndex++) {

            std::array<VkImageView, 5> attachments = {
                gAlbedoImageViews[frameIndex],
                gNormalImageViews[frameIndex],
                gPositionImageViews[frameIndex],
                gDepthImageViews[frameIndex],
                swapChainImageViews[swapchainIndex],
                
            };

            VkFramebufferCreateInfo framebufferInfo{};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = renderPass;
            framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
            framebufferInfo.pAttachments = attachments.data();
            framebufferInfo.width = extent.width;
            framebufferInfo.height = extent.height;
            framebufferInfo.layers = 1;

            size_t framebufferIndex = frameIndex * swapChainImageViews.size() + swapchainIndex;  // Calculate the framebuffer index
            if (vkCreateFramebuffer(vulkanDevice.getDevice(), &framebufferInfo, nullptr, &framebuffers[framebufferIndex]) != VK_SUCCESS) {
                throw std::runtime_error("Failed to create G-buffer framebuffer");
            }
        }
    }
}

void GBuffer::createRenderPass(const VulkanDevice& vulkanDevice, VkFormat swapchainImageFormat) {
    VkAttachmentDescription attachments[6] = {};

    // Albedo attachment
    attachments[0] = {};
    attachments[0].format = VK_FORMAT_R8G8B8A8_UNORM;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    // Normal attachment
    attachments[1] = {};
    attachments[1].format = VK_FORMAT_R16G16B16A16_SFLOAT;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    // Position attachment
    attachments[2] = {};
    attachments[2].format = VK_FORMAT_R16G16B16A16_SFLOAT;
    attachments[2].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[2].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    // Depth attachment
    attachments[3] = {};
    attachments[3].format = VK_FORMAT_D32_SFLOAT_S8_UINT;
    attachments[3].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[3].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[3].storeOp = VK_ATTACHMENT_STORE_OP_STORE; // Store the result
    attachments[3].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[3].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[3].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[3].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    // Lighting presenting attachment 
    attachments[4] = {};
    attachments[4].format = swapchainImageFormat;
    attachments[4].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[4].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[4].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[4].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[4].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[4].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[4].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    // First subpass: Geometry pass (writes to gbuffer)
    VkAttachmentReference geometryReferences[3] = {};
    geometryReferences[0].attachment = 0; // Albedo attachment index attachments array
    geometryReferences[0].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    geometryReferences[1].attachment = 1; // Normal attachment index attachments array
    geometryReferences[1].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    geometryReferences[2].attachment = 2; // Position attachment index attachments array
    geometryReferences[2].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthReference = {};
    depthReference.attachment = 3; // Depth attachment index in attachments array
    depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription geometrySubpass = {};
    geometrySubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    geometrySubpass.colorAttachmentCount = 3;
    geometrySubpass.pColorAttachments = geometryReferences;
    geometrySubpass.pDepthStencilAttachment = &depthReference;

    // Second subpass: Lighting pass (reads gbuffer, writes to swapchain)
    VkAttachmentReference inputReferences[4] = {};
    inputReferences[0].attachment = 0; // Albedo
    inputReferences[0].layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    inputReferences[1].attachment = 1; // Normal
    inputReferences[1].layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    inputReferences[2].attachment = 2; // Position
    inputReferences[2].layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    inputReferences[3].attachment = 3; // Depth
    inputReferences[3].layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    VkAttachmentReference lightingColorReference = {};
    lightingColorReference.attachment = 4; // Lighting attachment index in attachments array;
    lightingColorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription lightingSubpass = {};
    lightingSubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    lightingSubpass.inputAttachmentCount = 4; // Albedo, Normal, Position, Depth/Stencil
    lightingSubpass.pInputAttachments = inputReferences;
    lightingSubpass.colorAttachmentCount = 1;
    lightingSubpass.pColorAttachments = &lightingColorReference;
    lightingSubpass.pDepthStencilAttachment = &depthReference;
    
    // Third subpass: Grid subpass 
    VkAttachmentReference gridColorReference = {};
    gridColorReference.attachment = 4; // Using same attachment as lighting
    gridColorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription gridSubpass = {};
    gridSubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    gridSubpass.colorAttachmentCount = 1; // Writing to the grid attachment
    gridSubpass.pColorAttachments = &gridColorReference;
    gridSubpass.pDepthStencilAttachment = &depthReference;

    // Subpass dependencies
    std::array<VkSubpassDependency, 3> dependencies = {};

    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].srcAccessMask = 0;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    dependencies[1].srcSubpass = 0; // Geometry Pass
    dependencies[1].dstSubpass = 1; // Lighting Pass
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
    dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    dependencies[2].srcSubpass = 1; // Lighting Pass
    dependencies[2].dstSubpass = 2; // Grid Pass
    dependencies[2].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[2].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[2].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[2].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[2].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkSubpassDescription subpasses[3] = { geometrySubpass, lightingSubpass, gridSubpass};

    // Render pass creation
    VkRenderPassCreateInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 5;
    renderPassInfo.pAttachments = attachments;
    renderPassInfo.subpassCount = 3;
    renderPassInfo.pSubpasses = subpasses;
    renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
    renderPassInfo.pDependencies = dependencies.data();

    if (vkCreateRenderPass(vulkanDevice.getDevice(), &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create render pass!");
    }
}

void GBuffer::updateDescriptorSets(const VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight) {
    for (size_t i = 0; i < maxFramesInFlight; i++) {
        // Update G-buffer descriptors (for input attachments)
        VkDescriptorImageInfo albedoImageInfo{};
        albedoImageInfo.imageView = gAlbedoImageViews[i]; // Updated albedo image view
        albedoImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkDescriptorImageInfo normalImageInfo{};
        normalImageInfo.imageView = gNormalImageViews[i]; // Updated normal image view
        normalImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkDescriptorImageInfo positionImageInfo{};
        positionImageInfo.imageView = gPositionImageViews[i]; // Updated position image view
        positionImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkDescriptorImageInfo depthImageInfo{};
        depthImageInfo.imageView = gDepthImageViews[i]; // Updated depth image view
        depthImageInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        std::array<VkWriteDescriptorSet, 3> descriptorWrites{};

        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = lightingDescriptorSets[i];
        descriptorWrites[0].dstBinding = 0; // Albedo input binding
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pImageInfo = &albedoImageInfo;

        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = lightingDescriptorSets[i];
        descriptorWrites[1].dstBinding = 1; // Normal input binding
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pImageInfo = &normalImageInfo;

        descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[2].dstSet = lightingDescriptorSets[i];
        descriptorWrites[2].dstBinding = 2; // Position input binding
        descriptorWrites[2].dstArrayElement = 0;
        descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        descriptorWrites[2].descriptorCount = 1;
        descriptorWrites[2].pImageInfo = &positionImageInfo;

        //descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        //descriptorWrites[3].dstSet = lightingDescriptorSets[i];
        //descriptorWrites[3].dstBinding = 3; // Depth input binding
        //descriptorWrites[3].dstArrayElement = 0;
        //descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        //descriptorWrites[3].descriptorCount = 1;
        //descriptorWrites[3].pImageInfo = &depthImageInfo;

        vkUpdateDescriptorSets(vulkanDevice.getDevice(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }
}

void GBuffer::createGeometryDescriptorPool(const VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight) {
    VkDescriptorPoolSize uboPoolSize{};
    uboPoolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboPoolSize.descriptorCount = static_cast<uint32_t>(maxFramesInFlight);

    std::array<VkDescriptorPoolSize, 1> poolSizes = { uboPoolSize };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = static_cast<uint32_t>(maxFramesInFlight);

    if (vkCreateDescriptorPool(vulkanDevice.getDevice(), &poolInfo, nullptr, &geometryDescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create G-buffer descriptor pool!");
    }
}

void GBuffer::createGeometryDescriptorSetLayout(const VulkanDevice& vulkanDevice) {
    VkDescriptorSetLayoutBinding uboBinding{};
    uboBinding.binding = 0; // UBO binding index
    uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboBinding.descriptorCount = 1;
    uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT; // Used in vertex and frag shader
    uboBinding.pImmutableSamplers = nullptr; 

    std::array<VkDescriptorSetLayoutBinding, 1> bindings = { uboBinding };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(vulkanDevice.getDevice(), &layoutInfo, nullptr, &geometryDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create G-buffer descriptor set layout!");
    }
    std::cout << "Created geometry descriptor set layout: " << geometryDescriptorSetLayout << std::endl;
}

void GBuffer::createGeometryDescriptorSets(const VulkanDevice& vulkanDevice, ResourceManager& resourceManager, uint32_t maxFramesInFlight) {
    std::vector<VkDescriptorSetLayout> layouts(maxFramesInFlight, geometryDescriptorSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = geometryDescriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(maxFramesInFlight);
    allocInfo.pSetLayouts = layouts.data();

    geometryDescriptorSets.resize(maxFramesInFlight);
    if (vkAllocateDescriptorSets(vulkanDevice.getDevice(), &allocInfo, geometryDescriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate descriptor set!");
    }
    for (size_t i = 0; i < maxFramesInFlight; i++) {
        // UBO descriptor
        VkDescriptorBufferInfo uboBufferInfo{};
        uboBufferInfo.buffer = resourceManager.getUniformBufferManager().getUniformBuffers()[i];
        uboBufferInfo.offset = 0;
        uboBufferInfo.range = sizeof(UniformBufferObject);


        std::array<VkWriteDescriptorSet, 1> descriptorWrites{};

        // Write descriptor for the UBO
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = geometryDescriptorSets[i];
        descriptorWrites[0].dstBinding = 0; // Binding for UBO
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &uboBufferInfo;

        vkUpdateDescriptorSets(vulkanDevice.getDevice(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }
}

void GBuffer::createLightingDescriptorPool(const VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight) {
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    poolSizes[0].descriptorCount = static_cast<uint32_t>(maxFramesInFlight) * 3;  // Albedo, Normal, Position
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[1].descriptorCount = static_cast<uint32_t>(maxFramesInFlight) * 2;  // UBO, Light UBO

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = static_cast<uint32_t>(maxFramesInFlight); // One descriptor set per frame in flight

    if (vkCreateDescriptorPool(vulkanDevice.getDevice(), &poolInfo, nullptr, &lightingDescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor pool for lighting pass");
    }
}

void GBuffer::createLightingDescriptorSetLayout(const VulkanDevice& vulkanDevice) {
    std::vector<VkDescriptorSetLayoutBinding> bindings = {
        // Albedo input attachment
        {0, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        // Normal input attachment
        {1, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        // Position input attachment
        {2, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        //Depth input attachment
        //{3, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        // UBO
        {4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        // Light UBO
        {5, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
    };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(vulkanDevice.getDevice(), &layoutInfo, nullptr, &lightingDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create lighting descriptor set layout");
    }
    std::cout << "Created lighting descriptor set layout: " << lightingDescriptorSetLayout << std::endl;
}

void GBuffer::createLightingDescriptorSets(const VulkanDevice& vulkanDevice, ResourceManager& resourceManager, uint32_t maxFramesInFlight) {
    std::vector<VkDescriptorSetLayout> layouts(maxFramesInFlight, lightingDescriptorSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = lightingDescriptorPool;
    allocInfo.descriptorSetCount = maxFramesInFlight;
    allocInfo.pSetLayouts = layouts.data();

    lightingDescriptorSets.resize(maxFramesInFlight);
    if (vkAllocateDescriptorSets(vulkanDevice.getDevice(), &allocInfo, lightingDescriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate descriptor sets for lighting pass");
    }

    for (size_t i = 0; i < maxFramesInFlight; i++) {
        // G-buffer descriptors (for input attachments)
        VkDescriptorImageInfo albedoImageInfo{};
        albedoImageInfo.imageView = gAlbedoImageViews[i]; // Gbuffer albedo image view
        albedoImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkDescriptorImageInfo normalImageInfo{};
        normalImageInfo.imageView = gNormalImageViews[i]; // Gbuffer normal image view
        normalImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkDescriptorImageInfo positionImageInfo{};
        positionImageInfo.imageView = gPositionImageViews[i]; // Gbuffer position image view
        positionImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        //VkDescriptorImageInfo depthImageInfo{};
        //depthImageInfo.imageView = gDepthImageViews[i]; // Gbuffer depth image view
        //depthImageInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        // Main UBO descriptor
        VkDescriptorBufferInfo uboBufferInfo{};
        uboBufferInfo.buffer = resourceManager.getUniformBufferManager().getUniformBuffers()[i];
        uboBufferInfo.offset = 0;
        uboBufferInfo.range = sizeof(UniformBufferObject);

        // Light UBO descriptor
        VkDescriptorBufferInfo lightBufferInfo{};
        lightBufferInfo.buffer = resourceManager.getUniformBufferManager().getLightBuffers()[i];
        lightBufferInfo.offset = 0;
        lightBufferInfo.range = sizeof(LightUniformBufferObject);

        std::array<VkWriteDescriptorSet, 5> descriptorWrites{};
        // Write descriptors for the G-buffer inputs
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = lightingDescriptorSets[i];
        descriptorWrites[0].dstBinding = 0; // Albedo input binding
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pImageInfo = &albedoImageInfo;

        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = lightingDescriptorSets[i];
        descriptorWrites[1].dstBinding = 1; // Normal input binding
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pImageInfo = &normalImageInfo;

        descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[2].dstSet = lightingDescriptorSets[i];
        descriptorWrites[2].dstBinding = 2; // Position input binding
        descriptorWrites[2].dstArrayElement = 0;
        descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        descriptorWrites[2].descriptorCount = 1;
        descriptorWrites[2].pImageInfo = &positionImageInfo;

        /*descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[3].dstSet = lightingDescriptorSets[i];
        descriptorWrites[3].dstBinding = 3; // Depth input binding
        descriptorWrites[3].dstArrayElement = 0;
        descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        descriptorWrites[3].descriptorCount = 1;
        descriptorWrites[3].pImageInfo = &depthImageInfo;
        */
        descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[3].dstSet = lightingDescriptorSets[i];
        descriptorWrites[3].dstBinding = 4; // Main UBO input binding
        descriptorWrites[3].dstArrayElement = 0;
        descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[3].descriptorCount = 1;
        descriptorWrites[3].pBufferInfo = &uboBufferInfo;

        descriptorWrites[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[4].dstSet = lightingDescriptorSets[i];
        descriptorWrites[4].dstBinding = 5; // Light UBO input binding
        descriptorWrites[4].dstArrayElement = 0;
        descriptorWrites[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[4].descriptorCount = 1;
        descriptorWrites[4].pBufferInfo = &lightBufferInfo;

        vkUpdateDescriptorSets(vulkanDevice.getDevice(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }
}

void GBuffer::createGeometryPipeline(const VulkanDevice& vulkanDevice, VkExtent2D extent) {
    auto vertShaderCode = readFile("shaders/gbuffer_vert.spv"); //change
    auto fragShaderCode = readFile("shaders/gbuffer_frag.spv"); //change

    VkShaderModule vertShaderModule = createShaderModule(vulkanDevice, vertShaderCode);
    VkShaderModule fragShaderModule = createShaderModule(vulkanDevice, fragShaderCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    auto bindingDescriptions = Vertex::getBindingDescriptions();
    auto vertexAttributes = Vertex::getVertexAttributes();
    auto surfaceVertexAttributes = Vertex::getSurfaceVertexAttributes();

    // Set vertex binding descriptions
    vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescriptions.size());
    vertexInputInfo.pVertexBindingDescriptions = bindingDescriptions.data();

    // Combine vertex and surface attributes into a single vector
    std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
    attributeDescriptions.insert(attributeDescriptions.end(), vertexAttributes.begin(), vertexAttributes.end());
    attributeDescriptions.insert(attributeDescriptions.end(), surfaceVertexAttributes.begin(), surfaceVertexAttributes.end());

    // Set vertex attribute descriptions
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

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
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT; //debug
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;

    depthStencil.stencilTestEnable = VK_TRUE;

    // Front face stencil operations
    VkStencilOpState stencilOp{};
    stencilOp.passOp = VK_STENCIL_OP_REPLACE;             // Replace stencil value on pass
    stencilOp.failOp = VK_STENCIL_OP_KEEP;                // Keep stencil value on fail
    stencilOp.depthFailOp = VK_STENCIL_OP_KEEP;           // Keep stencil value on depth fail
    stencilOp.compareOp = VK_COMPARE_OP_ALWAYS;           // Always pass stencil test
    stencilOp.compareMask = 0xFF;                         // Compare all bits
    stencilOp.writeMask = 0xFF;                           // Write all bits
    stencilOp.reference = 1;                              // Write value 1 (0x01)

    depthStencil.front = stencilOp;
    depthStencil.back = stencilOp;

    VkPipelineColorBlendAttachmentState colorBlendAttachments[3] = {};
    for (int i = 0; i < 3; ++i) {
        colorBlendAttachments[i].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachments[i].blendEnable = VK_FALSE;
    }

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 3; // Number of gbuffer attachments
    colorBlending.pAttachments = colorBlendAttachments;

    std::vector<VkDynamicState> dynamicStates = {
         VK_DYNAMIC_STATE_VIEWPORT,
         VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // Create pipeline layout
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1; 
    layoutInfo.pSetLayouts = &geometryDescriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 0;
    layoutInfo.pPushConstantRanges = nullptr;

    if (vkCreatePipelineLayout(vulkanDevice.getDevice(), &layoutInfo, nullptr, &geometryPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create pipeline layout");
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
    pipelineInfo.layout = geometryPipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0; // Geometry pass

    if (vkCreateGraphicsPipelines(vulkanDevice.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &geometryPipeline) != VK_SUCCESS) {
        throw std::runtime_error("failed to create graphics pipeline!");
    }

    vkDestroyShaderModule(vulkanDevice.getDevice(), vertShaderModule, nullptr);
    vkDestroyShaderModule(vulkanDevice.getDevice(), fragShaderModule, nullptr);
}

void GBuffer::createLightingPipeline(const VulkanDevice& vulkanDevice, VkExtent2D swapchainExtent) {   
    auto vertShaderCode = readFile("shaders/lighting_vert.spv");
    auto fragShaderCode = readFile("shaders/lighting_frag.spv");
   
    VkShaderModule vertShaderModule = createShaderModule(vulkanDevice, vertShaderCode);
    VkShaderModule fragShaderModule = createShaderModule(vulkanDevice, fragShaderCode);
 
    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";
   
    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
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
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
  
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
   
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_TRUE;

    VkStencilOpState stencilOp{};
    stencilOp.passOp = VK_STENCIL_OP_KEEP;      // Keep stencil value on pass
    stencilOp.failOp = VK_STENCIL_OP_KEEP;      // Keep stencil value on fail
    stencilOp.depthFailOp = VK_STENCIL_OP_KEEP; // Keep stenvil value on depth fail
    stencilOp.compareOp = VK_COMPARE_OP_EQUAL;  // Pass if stencil value equals reference
    stencilOp.compareMask = 0xFF;               // Compare all bits
    stencilOp.writeMask = 0x00;                 // Write no bits
    stencilOp.reference = 1;                    // Read value 1 

    depthStencil.front = stencilOp;
    depthStencil.back = stencilOp;
    
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    
    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();
    
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &lightingDescriptorSetLayout;

    if (vkCreatePipelineLayout(vulkanDevice.getDevice(), &pipelineLayoutInfo, nullptr, &lightingPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create lighting pipeline layout");
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
    pipelineInfo.layout = lightingPipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 1; // Lighting subpass

    if (vkCreateGraphicsPipelines(vulkanDevice.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &lightingPipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create lighting pipeline");
    }
    
    vkDestroyShaderModule(vulkanDevice.getDevice(), fragShaderModule, nullptr);
    vkDestroyShaderModule(vulkanDevice.getDevice(), vertShaderModule, nullptr);
}

VkFormat GBuffer::findDepthFormat(VkPhysicalDevice physicalDevice) {
    const std::vector<VkFormat> candidates = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT
    };

    for (VkFormat format : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(vulkanDevice->getPhysicalDevice(), format, &props);

        if ((props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) ==
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            return format;
        }
    }
    throw std::runtime_error("Failed to find a suitable depth format");
}

bool GBuffer::hasStencilComponent(VkFormat format) {
    return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}

const char* formatToString(VkFormat format) {
    switch (format) {
    case VK_FORMAT_R8G8B8A8_UNORM: return "VK_FORMAT_R8G8B8A8_UNORM";
    case VK_FORMAT_R8G8B8A8_SRGB: return "VK_FORMAT_R8G8B8A8_SRGB";
    case VK_FORMAT_R16G16B16A16_SFLOAT: return "VK_FORMAT_R16G16B16A16_SFLOAT";
    case VK_FORMAT_R32G32B32A32_SFLOAT: return "VK_FORMAT_R32G32B32A32_SFLOAT";
    case VK_FORMAT_D32_SFLOAT_S8_UINT: return "VK_FORMAT_D32_SFLOAT_S8_UINT";
    case VK_FORMAT_D32_SFLOAT: return "VK_FORMAT_D32_SFLOAT_S8_UINT";
    case VK_FORMAT_D24_UNORM_S8_UINT: return "VK_FORMAT_D24_UNORM_S8_UINT";

    default: return "Unknown Format";
    }
}

void logImageDetails(VulkanDevice& vulkanDevice, VkImage image, VkImageCreateInfo imageInfo) {
    // Log image dimension and format
    std::cout << "Image Details:\n";
    std::cout << "  Dimensions: " << imageInfo.extent.width << "x"
        << imageInfo.extent.height << "x" << imageInfo.extent.depth << "\n";
    std::cout << "  Format: " << formatToString(imageInfo.format) << "\n";

    // Retrieve memory requirements
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(vulkanDevice.getDevice(), image, &memRequirements);
    std::cout << "  Memory Requirements:\n";
    std::cout << "    Size: " << memRequirements.size / (1024.0f * 1024.0f) << "MB\n";
}

void GBuffer::recordCommandBuffer(const VulkanDevice& vulkanDevice, ResourceManager& resourceManager, std::vector<VkImageView> swapChainImageViews, uint32_t imageIndex, uint32_t maxFramesInFlight, VkExtent2D extent) {
    VkCommandBuffer commandBuffer = gbufferCommandBuffers[imageIndex];

    // Start recording commands  
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(gbufferCommandBuffers[imageIndex], &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("Failed to begin recording gbuffer command buffer");
    }
    // Log image layouts 
    /*std::cout << "Logging Albedo Image: " << gAlbedoImages[0] << std::endl;
    logImageDetails(vulkanDevice, gAlbedoImages[0], gAlbedoImageInfo);
    std::cout << "Logging Normal Image: " << gNormalImages[0] << std::endl;
    logImageDetails(vulkanDevice, gNormalImages[0], gNormalImageInfo);
    std::cout << "Logging Position Image: " << gPositionImages[0] << std::endl;
    logImageDetails(vulkanDevice, gPositionImages[0], gPositionImageInfo);
    std::cout << "Logging Depth Image: " << gDepthImages[0] << std::endl;
    logImageDetails(vulkanDevice, gDepthImages[0], gDepthImageInfo);
    */
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = framebuffers[imageIndex];
    renderPassInfo.renderArea.offset = { 0, 0 };
    renderPassInfo.renderArea.extent = extent;

    std::array<VkClearValue, 5> clearValues{};
    clearValues[0].color = { { 0.0f, 0.0f, 1.0f, 0.0f } };  // Clear Albedo 
    clearValues[1].color = { { 0.0f, 0.0f, 1.0f, 0.0f } };  // Clear Normal 
    clearValues[2].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };  // Clear Position
    clearValues[3].depthStencil = { 1.0, 0 };               // Clear Depth
    clearValues[4].color = { {clearColorValues[0],clearColorValues[1], clearColorValues[2], 1.0f } };  // Clear Swapchain

    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, geometryPipeline);

    // Viewport and scissor settings
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = extent;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    // Geometry subpass 
    VkBuffer vertexBuffers[] = { resourceManager.getVisModel().getVertexBuffer(), resourceManager.getVisModel().getSurfaceVertexBuffer()};
    VkDeviceSize vertexOffsets[] = { 
        resourceManager.getVisModel().getVertexBufferOffset(),      
        resourceManager.getVisModel().getSurfaceVertexBufferOffset() 
    };
    //std::cout << "GBuffer::recordCommandBuffer - Binding vertex buffer: " << vertexBuffers[0] << std::endl;
    //std::cout << "GBuffer::recordCommandBuffer - Binding color buffer: " << vertexBuffers[1] << std::endl;
    vkCmdBindVertexBuffers(commandBuffer, 0, 2, vertexBuffers, vertexOffsets);
    vkCmdBindIndexBuffer(commandBuffer, resourceManager.getVisModel().getIndexBuffer(), resourceManager.getVisModel().getIndexBufferOffset(), VK_INDEX_TYPE_UINT32);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, geometryPipelineLayout, 0, 1, &geometryDescriptorSets[currentFrame], 0, nullptr);
    // Draw geometry
    vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(resourceManager.getVisModel().getIndices().size()), 1, 0, 0, 0);
    std::cout << "VisModel indices: " << resourceManager.getVisModel().getIndices().size() << std::endl;

    VkBuffer heatSourceVertexBuffers[] = { resourceManager.getHeatModel().getVertexBuffer(),resourceManager.getHeatModel().getSurfaceVertexBuffer()};
    VkDeviceSize heatSourceOffsets[] = {
    resourceManager.getHeatModel().getVertexBufferOffset(),
    resourceManager.getHeatModel().getSurfaceVertexBufferOffset()
    };
    vkCmdBindVertexBuffers(commandBuffer, 0, 2, heatSourceVertexBuffers, heatSourceOffsets);
    vkCmdBindIndexBuffer(commandBuffer, resourceManager.getHeatModel().getIndexBuffer(), resourceManager.getHeatModel().getIndexBufferOffset(), VK_INDEX_TYPE_UINT32);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, geometryPipelineLayout, 0, 1, &geometryDescriptorSets[currentFrame], 0, nullptr);
    // Draw heat source model
    vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(resourceManager.getHeatModel().getIndices().size()), 1, 0, 0, 0);
    std::cout << "HeatSource indices: " << resourceManager.getHeatModel().getIndices().size() << std::endl;

    // Transition to lighting subpass
    vkCmdNextSubpass(commandBuffer, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, lightingPipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, lightingPipelineLayout, 0, 1, &lightingDescriptorSets[currentFrame], 0, nullptr);
    // Draw fullscreen triangle
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);

    // Transition to grid subpass
    vkCmdNextSubpass(commandBuffer, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, resourceManager.getGrid().getGridPipeline());
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, resourceManager.getGrid().getGridPipelineLayout(), 0, 1, &resourceManager.getGrid().getGridDescriptorSets()[currentFrame], 0, nullptr);
    // Draw grid
    vkCmdDraw(commandBuffer, resourceManager.getGrid().vertexCount, 1, 0, 0);

    vkCmdEndRenderPass(commandBuffer);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to record G-buffer command buffer");
    }
}

void GBuffer::cleanupFramebuffers(const VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight) {
    vkDeviceWaitIdle(vulkanDevice.getDevice());
    for (uint32_t i = 0; i < framebuffers.size(); ++i) {
        if (framebuffers[i] != VK_NULL_HANDLE) { // Check if the framebuffer handle is valid
            vkDestroyFramebuffer(vulkanDevice.getDevice(), framebuffers[i], nullptr);
            framebuffers[i] = VK_NULL_HANDLE; // Set the handle to VK_NULL_HANDLE to prevent double destroy
        }
    }
    framebuffers.clear();
}

void GBuffer::cleanupImages(VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight) {
    vkDeviceWaitIdle(vulkanDevice.getDevice());

    for (size_t i = 0; i < maxFramesInFlight; ++i) {
        if (gAlbedoImageViews[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(vulkanDevice.getDevice(), gAlbedoImageViews[i], nullptr);
            vkDestroyImageView(vulkanDevice.getDevice(), gNormalImageViews[i], nullptr);
            vkDestroyImageView(vulkanDevice.getDevice(), gPositionImageViews[i], nullptr);
            vkDestroyImageView(vulkanDevice.getDevice(), gDepthImageViews[i], nullptr);
        }
    }

    for (size_t i = 0; i < maxFramesInFlight; ++i) {
        if (gAlbedoImages[i] != VK_NULL_HANDLE) {
            vkDestroyImage(vulkanDevice.getDevice(), gAlbedoImages[i], nullptr);
            vkDestroyImage(vulkanDevice.getDevice(), gNormalImages[i], nullptr);
            vkDestroyImage(vulkanDevice.getDevice(), gPositionImages[i], nullptr);
            vkDestroyImage(vulkanDevice.getDevice(), gDepthImages[i], nullptr);
        }
    }

    for (size_t i = 0; i < maxFramesInFlight; ++i) {
        if (gAlbedoImageMemories[i] != VK_NULL_HANDLE) {
            vkFreeMemory(vulkanDevice.getDevice(), gAlbedoImageMemories[i], nullptr);
            vkFreeMemory(vulkanDevice.getDevice(), gNormalImageMemories[i], nullptr);
            vkFreeMemory(vulkanDevice.getDevice(), gPositionImageMemories[i], nullptr);
            vkFreeMemory(vulkanDevice.getDevice(), gDepthImageMemories[i], nullptr);
        }
    }
}

void GBuffer::cleanup(VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight) {
    vkDestroyPipeline(vulkanDevice.getDevice(), geometryPipeline, nullptr);
    vkDestroyPipelineLayout(vulkanDevice.getDevice(), geometryPipelineLayout, nullptr);
    vkDestroyPipeline(vulkanDevice.getDevice(), lightingPipeline, nullptr);
    vkDestroyPipelineLayout(vulkanDevice.getDevice(), lightingPipelineLayout, nullptr);

    vkDestroyDescriptorSetLayout(vulkanDevice.getDevice(), geometryDescriptorSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(vulkanDevice.getDevice(), lightingDescriptorSetLayout, nullptr);
    vkDestroyDescriptorPool(vulkanDevice.getDevice(), geometryDescriptorPool, nullptr);
    vkDestroyDescriptorPool(vulkanDevice.getDevice(), lightingDescriptorPool, nullptr);

    resourceManager->getGrid().cleanup(vulkanDevice, maxFramesInFlight);
  

    for (VkFramebuffer framebuffer : framebuffers) {
        vkDestroyFramebuffer(vulkanDevice.getDevice(), framebuffer, nullptr);
    }

    vkDestroyRenderPass(vulkanDevice.getDevice(), renderPass, nullptr);
}
