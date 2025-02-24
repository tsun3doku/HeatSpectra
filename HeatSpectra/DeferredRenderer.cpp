#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <array>
#include <vector>

#include "GBuffer.hpp"
#include "VulkanImage.hpp"
#include "VulkanDevice.hpp"
#include "DeferredRenderer.hpp"

DeferredRenderer::DeferredRenderer(VulkanDevice& vulkanDevice, VkFormat swapchainImageFormat, VkExtent2D swapchainExtent, uint32_t maxFramesInFlight)
	: vulkanDevice(&vulkanDevice) {
	createRenderPass(vulkanDevice, swapchainImageFormat);
	createImageViews(vulkanDevice, swapchainExtent, maxFramesInFlight);
}

DeferredRenderer::~DeferredRenderer() {
}

void DeferredRenderer::createRenderPass(const VulkanDevice& vulkanDevice, VkFormat swapchainImageFormat) {
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
    attachments[3].storeOp = VK_ATTACHMENT_STORE_OP_STORE; 
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

    VkSubpassDescription subpasses[3] = { geometrySubpass, lightingSubpass, gridSubpass };

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

void DeferredRenderer::createImageViews(const VulkanDevice& vulkanDevice, VkExtent2D extent, uint32_t maxFramesInFlight) {
    // Resize the vectors for multiple frames
    albedoViews.resize(maxFramesInFlight);
    normalViews.resize(maxFramesInFlight);
    positionViews.resize(maxFramesInFlight);
    depthViews.resize(maxFramesInFlight);

    albedoImages.resize(maxFramesInFlight);
    albedoMemories.resize(maxFramesInFlight);

    normalImages.resize(maxFramesInFlight);
    normalMemories.resize(maxFramesInFlight);

    positionImages.resize(maxFramesInFlight);
    positionMemories.resize(maxFramesInFlight);

    depthImages.resize(maxFramesInFlight);
    depthMemories.resize(maxFramesInFlight);

    for (size_t i = 0; i < maxFramesInFlight; i++) {
        // Albedo image creation for each frame
        VkFormat albedoFormat = VK_FORMAT_R8G8B8A8_UNORM;
        albedoImageInfo = createImageCreateInfo(extent.width, extent.height, albedoFormat,
            VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT);
        createImage(vulkanDevice, extent.width, extent.height, albedoFormat,
            VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, albedoImages[i], albedoMemories[i]);
        albedoViews[i] = createImageView(vulkanDevice, albedoImages[i], albedoFormat, VK_IMAGE_ASPECT_COLOR_BIT);

        // Normal image creation for each frame
        VkFormat normalFormat = VK_FORMAT_R16G16B16A16_SFLOAT; //A16 remains unused
        normalImageInfo = createImageCreateInfo(extent.width, extent.height, normalFormat,
            VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT);
        createImage(vulkanDevice, extent.width, extent.height, normalFormat,
            VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, normalImages[i], normalMemories[i]);
        normalViews[i] = createImageView(vulkanDevice, normalImages[i], normalFormat, VK_IMAGE_ASPECT_COLOR_BIT);

        // Position image creation for each frame
        VkFormat positionFormat = VK_FORMAT_R16G16B16A16_SFLOAT; //A16 remains unused
        positionImageInfo = createImageCreateInfo(extent.width, extent.height, positionFormat,
            VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT);
        createImage(vulkanDevice, extent.width, extent.height, positionFormat,
            VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, positionImages[i], positionMemories[i]);
        positionViews[i] = createImageView(vulkanDevice, positionImages[i], positionFormat, VK_IMAGE_ASPECT_COLOR_BIT);

        // Depth image creation for each frame
        VkFormat depthFormat = VK_FORMAT_D32_SFLOAT_S8_UINT;
        depthImageInfo = createImageCreateInfo(extent.width, extent.height, depthFormat,
            VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT);
        createImage(vulkanDevice, extent.width, extent.height, depthFormat,
            VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, depthImages[i], depthMemories[i]);
        depthViews[i] = createImageView(vulkanDevice, depthImages[i], depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
    }
}

void DeferredRenderer::cleanupImages(VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight) {
    vkDeviceWaitIdle(vulkanDevice.getDevice());

    // Cleanup image views
    for (size_t i = 0; i < maxFramesInFlight; ++i) {
        if (albedoViews[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(vulkanDevice.getDevice(), albedoViews[i], nullptr);
            vkDestroyImageView(vulkanDevice.getDevice(), normalViews[i], nullptr);
            vkDestroyImageView(vulkanDevice.getDevice(), positionViews[i], nullptr);
            vkDestroyImageView(vulkanDevice.getDevice(), depthViews[i], nullptr);
        }
    }

    // Cleanup images
    for (size_t i = 0; i < maxFramesInFlight; ++i) {
        if (albedoImages[i] != VK_NULL_HANDLE) {
            vkDestroyImage(vulkanDevice.getDevice(), albedoImages[i], nullptr);
            vkDestroyImage(vulkanDevice.getDevice(), normalImages[i], nullptr);
            vkDestroyImage(vulkanDevice.getDevice(), positionImages[i], nullptr);
            vkDestroyImage(vulkanDevice.getDevice(), depthImages[i], nullptr);
        }
    }

    // Cleanup image memories
    for (size_t i = 0; i < maxFramesInFlight; ++i) {
        if (albedoMemories[i] != VK_NULL_HANDLE) {
            vkFreeMemory(vulkanDevice.getDevice(), albedoMemories[i], nullptr);
            vkFreeMemory(vulkanDevice.getDevice(), normalMemories[i], nullptr);
            vkFreeMemory(vulkanDevice.getDevice(), positionMemories[i], nullptr);
            vkFreeMemory(vulkanDevice.getDevice(), depthMemories[i], nullptr);
        }
    }
}

void DeferredRenderer::cleanup(VulkanDevice& vulkanDevice) {
    vkDestroyRenderPass(vulkanDevice.getDevice(), renderPass, nullptr);
}
