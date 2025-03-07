#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <array>
#include <vector>

#include "GBuffer.hpp"
#include "VulkanImage.hpp"
#include "VulkanDevice.hpp"
#include "DeferredRenderer.hpp"

DeferredRenderer::DeferredRenderer(VulkanDevice& vulkanDevice, VkFormat swapchainImageFormat, VkExtent2D swapchainExtent, uint32_t maxFramesInFlight)
    : vulkanDevice(vulkanDevice) {
    createRenderPass(vulkanDevice, swapchainImageFormat);
    createImageViews(vulkanDevice, swapchainImageFormat, swapchainExtent, maxFramesInFlight);
}

DeferredRenderer::~DeferredRenderer() {
}

void DeferredRenderer::createRenderPass(const VulkanDevice& vulkanDevice, VkFormat swapchainImageFormat) {
    VkAttachmentDescription2 attachments[11] = {};
    // Albedo attachment
    attachments[0].sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;
    attachments[0].format = VK_FORMAT_R8G8B8A8_UNORM;
    attachments[0].samples = VK_SAMPLE_COUNT_4_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    // Normal attachment
    attachments[1].sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;
    attachments[1].format = VK_FORMAT_R16G16B16A16_SFLOAT;
    attachments[1].samples = VK_SAMPLE_COUNT_4_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    // Position attachment
    attachments[2].sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;
    attachments[2].format = VK_FORMAT_R16G16B16A16_SFLOAT;
    attachments[2].samples = VK_SAMPLE_COUNT_4_BIT;
    attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[2].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    // Depth attachment
    attachments[3].sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;
    attachments[3].format = VK_FORMAT_D32_SFLOAT_S8_UINT;
    attachments[3].samples = VK_SAMPLE_COUNT_4_BIT;
    attachments[3].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[3].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[3].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[3].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[3].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[3].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    // Albedo resolve
    attachments[4].sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;
    attachments[4].format = VK_FORMAT_R8G8B8A8_UNORM;
    attachments[4].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[4].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[4].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[4].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[4].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[4].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[4].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    // Normal resolve
    attachments[5] = attachments[4];
    attachments[5] = attachments[4];
    attachments[5].format = VK_FORMAT_R16G16B16A16_SFLOAT;
    // Position resolve
    attachments[6] = attachments[4];
    attachments[6].format = VK_FORMAT_R16G16B16A16_SFLOAT;
    // Depth resolve
    attachments[7] = attachments[4];
    attachments[7].format = VK_FORMAT_D32_SFLOAT_S8_UINT;
    attachments[7].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    // Lighting presenting attachment
    attachments[8].sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;
    attachments[8].format = swapchainImageFormat;
    attachments[8].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[8].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[8].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[8].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[8].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[8].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[8].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    // Grid MSAA color attachment 
    attachments[9].sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;
    attachments[9].format = swapchainImageFormat;
    attachments[9].samples = VK_SAMPLE_COUNT_4_BIT;
    attachments[9].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[9].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[9].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[9].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[9].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[9].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    // Grid resolve attachment 
    attachments[10] = attachments[9];
    attachments[10].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[10].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[10].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[10].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[10].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // --- Setup Attachment References ---
    // Resolve attachments for color (indices 4, 5, 6)
    VkAttachmentReference2 resolveRefs[3] = {};
    for (int i = 0; i < 3; i++) {
        resolveRefs[i].sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
        resolveRefs[i].pNext = nullptr;
        resolveRefs[i].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        resolveRefs[i].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    }
    resolveRefs[0].attachment = 4;
    resolveRefs[1].attachment = 5;
    resolveRefs[2].attachment = 6;

    // Depth resolve reference
    VkAttachmentReference2 depthResolveReference = {};
    depthResolveReference.sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
    depthResolveReference.pNext = nullptr;
    depthResolveReference.attachment = 7; // Resolve attachment index
    depthResolveReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthResolveReference.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;

    VkSubpassDescriptionDepthStencilResolveKHR depthResolve = {};
    depthResolve.sType = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_DEPTH_STENCIL_RESOLVE;
    depthResolve.pNext = nullptr;
    depthResolve.depthResolveMode = vulkanDevice.getDepthResolveMode();
    depthResolve.stencilResolveMode = vulkanDevice.getDepthResolveMode();
    depthResolve.pDepthStencilResolveAttachment = &depthResolveReference;

    // Geometry subpass attachment references (color and depth)
    VkAttachmentReference2 geometryColorRefs[3] = {};
    for (int i = 0; i < 3; i++) {
        geometryColorRefs[i].sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
        geometryColorRefs[i].pNext = nullptr;
        geometryColorRefs[i].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        geometryColorRefs[i].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    }
    geometryColorRefs[0].attachment = 0;
    geometryColorRefs[1].attachment = 1;
    geometryColorRefs[2].attachment = 2;

    VkAttachmentReference2 depthRef = {};
    depthRef.sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
    depthRef.pNext = nullptr;
    depthRef.attachment = 3;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthRef.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;

    // --- Create Subpass Descriptions ---
    // Geometry Subpass with depth resolve chain
    VkSubpassDescription2 geometrySubpass = {};
    geometrySubpass.sType = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2;
    geometrySubpass.pNext = &depthResolve;
    geometrySubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    geometrySubpass.colorAttachmentCount = 3;
    geometrySubpass.pColorAttachments = geometryColorRefs;
    geometrySubpass.pResolveAttachments = resolveRefs;
    geometrySubpass.pDepthStencilAttachment = &depthRef;

    // Lighting Subpass (reads resolve attachments as input)
    VkAttachmentReference2 inputRefs[3] = {};
    for (int i = 0; i < 3; i++) {
        inputRefs[i].sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
        inputRefs[i].pNext = nullptr;
        inputRefs[i].layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        inputRefs[i].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    }
    inputRefs[0].attachment = 4;
    inputRefs[1].attachment = 5;
    inputRefs[2].attachment = 6;

    VkAttachmentReference2 lightingColorRef = {};
    lightingColorRef.sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
    lightingColorRef.pNext = nullptr;
    lightingColorRef.attachment = 8;
    lightingColorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    lightingColorRef.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

    VkSubpassDescription2 lightingSubpass = {};
    lightingSubpass.sType = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2;
    lightingSubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    lightingSubpass.inputAttachmentCount = 3;
    lightingSubpass.pInputAttachments = inputRefs;
    lightingSubpass.colorAttachmentCount = 1;
    lightingSubpass.pColorAttachments = &lightingColorRef;

    // Grid Subpass
    VkAttachmentReference2 gridColorRef = {};
    gridColorRef.sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
    gridColorRef.pNext = nullptr;
    gridColorRef.attachment = 9;
    gridColorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    gridColorRef.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

    VkAttachmentReference2 gridResolveRef{};
    gridResolveRef.sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
    gridResolveRef.attachment = 10;
    gridResolveRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference2 gridDepthRef = {};
    gridDepthRef.sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
    gridDepthRef.pNext = nullptr;
    gridDepthRef.attachment = 3; // Depth attachment index
    gridDepthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    gridDepthRef.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;

    VkSubpassDescription2 gridSubpass = {};
    gridSubpass.sType = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2;
    gridSubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    gridSubpass.colorAttachmentCount = 1;
    gridSubpass.pColorAttachments = &gridColorRef;
    gridSubpass.pDepthStencilAttachment = &gridDepthRef;
    gridSubpass.pResolveAttachments = &gridResolveRef;

    VkAttachmentReference2 blendInputRef{};
    blendInputRef.sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
    blendInputRef.attachment = 10;
    blendInputRef.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    blendInputRef.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

    VkAttachmentReference2 blendOutputRef{};
    blendOutputRef.sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
    blendOutputRef.attachment = 8; // Swapchain attachment index
    blendOutputRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    blendOutputRef.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

    VkSubpassDescription2 blendSubpass{};
    blendSubpass.sType = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2;
    blendSubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    blendSubpass.inputAttachmentCount = 1;
    blendSubpass.pInputAttachments = &blendInputRef;
    blendSubpass.colorAttachmentCount = 1;
    blendSubpass.pColorAttachments = &blendOutputRef;

    // --- Subpass Dependencies (using VkSubpassDependency2) ---
    std::array<VkSubpassDependency2, 5> dependencies = {};
    dependencies[0].sType = VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2;
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0; // Geomtry subpass
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].srcAccessMask = 0;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    dependencies[1].sType = VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2;
    dependencies[1].srcSubpass = 0; // Geometry subpass
    dependencies[1].dstSubpass = 1; // Lighting subpass
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    dependencies[2].sType = VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2;
    dependencies[2].srcSubpass = 1; // Lighting subpass
    dependencies[2].dstSubpass = 2; // Grid subpass
    dependencies[2].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[2].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[2].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[2].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[2].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    dependencies[3].sType = VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2;
    dependencies[3].srcSubpass = 0; // Geometry subpass
    dependencies[3].dstSubpass = 2; // Grid subpass
    dependencies[3].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[3].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[3].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[3].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    dependencies[3].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    dependencies[4].sType = VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2;
    dependencies[4].srcSubpass = 1; // Lighting subpass
    dependencies[4].dstSubpass = 3; // Blend subpass
    dependencies[4].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[4].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;;
    dependencies[4].srcAccessMask = 0;
    dependencies[4].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    std::array<VkSubpassDescription2, 4> subpasses = { geometrySubpass, lightingSubpass, gridSubpass, blendSubpass };

    // --- Create Render Pass using VkRenderPassCreateInfo2 and vkCreateRenderPass2 ---
    VkRenderPassCreateInfo2 renderPassInfo2 = {};
    renderPassInfo2.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2;
    renderPassInfo2.attachmentCount = 11;
    renderPassInfo2.pAttachments = attachments;
    renderPassInfo2.subpassCount = static_cast<uint32_t>(subpasses.size());
    renderPassInfo2.pSubpasses = subpasses.data();
    renderPassInfo2.dependencyCount = static_cast<uint32_t>(dependencies.size());
    renderPassInfo2.pDependencies = dependencies.data();

    if (vkCreateRenderPass2(vulkanDevice.getDevice(), &renderPassInfo2, nullptr, &renderPass) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create render pass!");
    }
}

void DeferredRenderer::createImageViews(const VulkanDevice& vulkanDevice, VkFormat swapchainImageFormat, VkExtent2D extent, uint32_t maxFramesInFlight) {
    // Albedo resources
    albedoViews.resize(maxFramesInFlight);
    albedoImages.resize(maxFramesInFlight);
    albedoMemories.resize(maxFramesInFlight);
    albedoResolveViews.resize(maxFramesInFlight);
    albedoResolveImages.resize(maxFramesInFlight);
    albedoResolveMemories.resize(maxFramesInFlight);

    // Normal resources
    normalViews.resize(maxFramesInFlight);
    normalImages.resize(maxFramesInFlight);
    normalMemories.resize(maxFramesInFlight);
    normalResolveViews.resize(maxFramesInFlight);
    normalResolveImages.resize(maxFramesInFlight);
    normalResolveMemories.resize(maxFramesInFlight);

    // Position resources
    positionViews.resize(maxFramesInFlight);
    positionImages.resize(maxFramesInFlight);
    positionMemories.resize(maxFramesInFlight);
    positionResolveViews.resize(maxFramesInFlight);
    positionResolveImages.resize(maxFramesInFlight);
    positionResolveMemories.resize(maxFramesInFlight);

    // Depth resources
    depthViews.resize(maxFramesInFlight);
    depthImages.resize(maxFramesInFlight);
    depthMemories.resize(maxFramesInFlight);
    depthResolveViews.resize(maxFramesInFlight);
    depthResolveImages.resize(maxFramesInFlight);
    depthResolveMemories.resize(maxFramesInFlight);

    // Grid resources
    gridViews.resize(maxFramesInFlight);
    gridImages.resize(maxFramesInFlight);
    gridMemories.resize(maxFramesInFlight);
    gridResolveViews.resize(maxFramesInFlight);
    gridResolveImages.resize(maxFramesInFlight);
    gridResolveMemories.resize(maxFramesInFlight);

    for (size_t i = 0; i < maxFramesInFlight; i++) {
        // Albedo image creation (multisampled)
        VkFormat albedoFormat = VK_FORMAT_R8G8B8A8_UNORM;
        albedoImageInfo = createImageCreateInfo(extent.width, extent.height, albedoFormat,
            VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
            VK_SAMPLE_COUNT_4_BIT);
        createImage(vulkanDevice, extent.width, extent.height, albedoFormat,
            VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, albedoImages[i], albedoMemories[i],
            VK_SAMPLE_COUNT_4_BIT);
        albedoViews[i] = createImageView(vulkanDevice, albedoImages[i], albedoFormat, VK_IMAGE_ASPECT_COLOR_BIT);

        // Albedo resolve image
        createImage(vulkanDevice, extent.width, extent.height, albedoFormat,
            VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, albedoResolveImages[i], albedoResolveMemories[i],
            VK_SAMPLE_COUNT_1_BIT);
        albedoResolveViews[i] = createImageView(vulkanDevice, albedoResolveImages[i], albedoFormat, VK_IMAGE_ASPECT_COLOR_BIT);

        // Normal image creation (multisampled)
        VkFormat normalFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
        normalImageInfo = createImageCreateInfo(extent.width, extent.height, normalFormat,
            VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
            VK_SAMPLE_COUNT_4_BIT);
        createImage(vulkanDevice, extent.width, extent.height, normalFormat,
            VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, normalImages[i], normalMemories[i],
            VK_SAMPLE_COUNT_4_BIT);
        normalViews[i] = createImageView(vulkanDevice, normalImages[i], normalFormat, VK_IMAGE_ASPECT_COLOR_BIT);

        // Normal resolve image
        createImage(vulkanDevice, extent.width, extent.height, normalFormat,
            VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, normalResolveImages[i], normalResolveMemories[i],
            VK_SAMPLE_COUNT_1_BIT);
        normalResolveViews[i] = createImageView(vulkanDevice, normalResolveImages[i], normalFormat, VK_IMAGE_ASPECT_COLOR_BIT);

        // Position image creation (multisampled)
        VkFormat positionFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
        positionImageInfo = createImageCreateInfo(extent.width, extent.height, positionFormat,
            VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
            VK_SAMPLE_COUNT_4_BIT);
        createImage(vulkanDevice, extent.width, extent.height, positionFormat,
            VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, positionImages[i], positionMemories[i],
            VK_SAMPLE_COUNT_4_BIT);
        positionViews[i] = createImageView(vulkanDevice, positionImages[i], positionFormat, VK_IMAGE_ASPECT_COLOR_BIT);

        // Position resolve image
        createImage(vulkanDevice, extent.width, extent.height, positionFormat,
            VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, positionResolveImages[i], positionResolveMemories[i],
            VK_SAMPLE_COUNT_1_BIT);
        positionResolveViews[i] = createImageView(vulkanDevice, positionResolveImages[i], positionFormat, VK_IMAGE_ASPECT_COLOR_BIT);

        // Depth image creation (multisampled)
        VkFormat depthFormat = VK_FORMAT_D32_SFLOAT_S8_UINT;
        depthImageInfo = createImageCreateInfo(extent.width, extent.height, depthFormat,
            VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
            VK_SAMPLE_COUNT_4_BIT);
        createImage(vulkanDevice, extent.width, extent.height, depthFormat,
            VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, depthImages[i], depthMemories[i],
            VK_SAMPLE_COUNT_4_BIT);
        depthViews[i] = createImageView(vulkanDevice, depthImages[i], depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);

        // Depth resolve image
        createImage(vulkanDevice, extent.width, extent.height, depthFormat, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, depthResolveImages[i], depthResolveMemories[i],
            VK_SAMPLE_COUNT_1_BIT);
        depthResolveViews[i] = createImageView(vulkanDevice, depthResolveImages[i], depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);

        // Grid image creation (multisampled)
        createImage(vulkanDevice, extent.width, extent.height, swapchainImageFormat,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, gridImages[i], gridMemories[i],
            VK_SAMPLE_COUNT_4_BIT);
        gridViews[i] = createImageView(vulkanDevice, gridImages[i], swapchainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT);

        // Grid Resolve Image
        createImage(vulkanDevice, extent.width, extent.height, swapchainImageFormat,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, gridResolveImages[i], gridResolveMemories[i],
            VK_SAMPLE_COUNT_1_BIT);
        gridResolveViews[i] = createImageView(vulkanDevice, gridResolveImages[i], swapchainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT);
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
            vkDestroyImageView(vulkanDevice.getDevice(), gridViews[i], nullptr);
            vkDestroyImageView(vulkanDevice.getDevice(), albedoResolveViews[i], nullptr);
            vkDestroyImageView(vulkanDevice.getDevice(), normalResolveViews[i], nullptr);
            vkDestroyImageView(vulkanDevice.getDevice(), positionResolveViews[i], nullptr);
            vkDestroyImageView(vulkanDevice.getDevice(), depthResolveViews[i], nullptr);
            vkDestroyImageView(vulkanDevice.getDevice(), gridResolveViews[i], nullptr);

        }
    }

    // Cleanup images
    for (size_t i = 0; i < maxFramesInFlight; ++i) {
        if (albedoImages[i] != VK_NULL_HANDLE) {
            vkDestroyImage(vulkanDevice.getDevice(), albedoImages[i], nullptr);
            vkDestroyImage(vulkanDevice.getDevice(), normalImages[i], nullptr);
            vkDestroyImage(vulkanDevice.getDevice(), positionImages[i], nullptr);
            vkDestroyImage(vulkanDevice.getDevice(), depthImages[i], nullptr);
            vkDestroyImage(vulkanDevice.getDevice(), gridImages[i], nullptr);
            vkDestroyImage(vulkanDevice.getDevice(), albedoResolveImages[i], nullptr);
            vkDestroyImage(vulkanDevice.getDevice(), normalResolveImages[i], nullptr);
            vkDestroyImage(vulkanDevice.getDevice(), positionResolveImages[i], nullptr);
            vkDestroyImage(vulkanDevice.getDevice(), depthResolveImages[i], nullptr);
            vkDestroyImage(vulkanDevice.getDevice(), gridResolveImages[i], nullptr);

        }
    }

    // Cleanup image memories
    for (size_t i = 0; i < maxFramesInFlight; ++i) {
        if (albedoMemories[i] != VK_NULL_HANDLE) {
            vkFreeMemory(vulkanDevice.getDevice(), albedoMemories[i], nullptr);
            vkFreeMemory(vulkanDevice.getDevice(), normalMemories[i], nullptr);
            vkFreeMemory(vulkanDevice.getDevice(), positionMemories[i], nullptr);
            vkFreeMemory(vulkanDevice.getDevice(), depthMemories[i], nullptr);
            vkFreeMemory(vulkanDevice.getDevice(), gridMemories[i], nullptr);
            vkFreeMemory(vulkanDevice.getDevice(), albedoResolveMemories[i], nullptr);
            vkFreeMemory(vulkanDevice.getDevice(), normalResolveMemories[i], nullptr);
            vkFreeMemory(vulkanDevice.getDevice(), positionResolveMemories[i], nullptr);
            vkFreeMemory(vulkanDevice.getDevice(), depthResolveMemories[i], nullptr);
            vkFreeMemory(vulkanDevice.getDevice(), gridResolveMemories[i], nullptr);

        }
    }
}

void DeferredRenderer::cleanup(VulkanDevice& vulkanDevice) {
    vkDestroyRenderPass(vulkanDevice.getDevice(), renderPass, nullptr);
}