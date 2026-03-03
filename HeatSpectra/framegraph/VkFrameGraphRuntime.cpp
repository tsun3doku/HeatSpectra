#include "VkFrameGraphRuntime.hpp"

#include <optional>
#include <iostream>

#include "FrameGraphVkTypes.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/VulkanDevice.hpp"
#include "vulkan/VulkanImage.hpp"

bool VkFrameGraphRuntime::rebuild(
    const VulkanDevice& vulkanDevice,
    MemoryAllocator& memoryAllocator,
    const framegraph::FrameGraphResult& result,
    const std::vector<VkImageView>& swapChainImageViews,
    VkExtent2D extent,
    uint32_t maxFramesInFlight) {
    cleanup(vulkanDevice, memoryAllocator, maxFramesInFlight);
    loadResult(result);
    if (!createRenderPass(vulkanDevice)) {
        cleanup(vulkanDevice, memoryAllocator, maxFramesInFlight);
        return false;
    }
    if (!createImageViews(vulkanDevice, memoryAllocator, maxFramesInFlight)) {
        cleanup(vulkanDevice, memoryAllocator, maxFramesInFlight);
        return false;
    }
    if (!createFramebuffers(swapChainImageViews, extent, maxFramesInFlight, vulkanDevice)) {
        cleanup(vulkanDevice, memoryAllocator, maxFramesInFlight);
        return false;
    }
    return true;
}

void VkFrameGraphRuntime::cleanup(const VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, uint32_t maxFramesInFlight) {
    cleanupImages(vulkanDevice, memoryAllocator, maxFramesInFlight);
    if (renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(vulkanDevice.getDevice(), renderPass, nullptr);
        renderPass = VK_NULL_HANDLE;
    }
    resourceStorages.clear();
    frameGraphResult = {};
    computeToGraphicsWaitDstStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    isLoaded = false;
}

void VkFrameGraphRuntime::loadResult(const framegraph::FrameGraphResult& result) {
    frameGraphResult = result;
    const auto& resourceDecls = frameGraphResult.resources;
    const auto& passSyncEdges = frameGraphResult.passSyncEdges;

    resourceStorages.assign(resourceDecls.size(), {});
    for (uint32_t resourceId = 0; resourceId < resourceDecls.size(); ++resourceId) {
        const framegraph::ResourceDefinition& resource = resourceDecls[resourceId];
        resourceStorages[resourceId].createDepthSamplerViews = framegraph::hasAny(resource.viewAspect, framegraph::ImageAspect::Depth);
        resourceStorages[resourceId].createStencilSamplerViews = framegraph::hasAny(resource.viewAspect, framegraph::ImageAspect::Stencil);
    }

    VkPipelineStageFlags consumerMask = 0;
    for (const framegraph::PassSyncEdge& edge : passSyncEdges) {
        framegraph::ResourceUse dstUse{};
        dstUse.resourceId = edge.resourceId;
        dstUse.usage = edge.dstUsage;
        dstUse.write = edge.dstWrite;
        consumerMask |= usageToSync(dstUse).stageMask;
    }

    computeToGraphicsWaitDstStageMask = consumerMask != 0
        ? consumerMask
        : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

    isLoaded = true;
}

VkFrameGraphRuntime::SyncState VkFrameGraphRuntime::usageToSync(const framegraph::ResourceUse& use) const {
    SyncState sync{};
    switch (use.usage) {
    case framegraph::UsageType::ColorAttachment:
    case framegraph::UsageType::Present:
        sync.stageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        sync.accessMask = use.write ? VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT : VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
        sync.isWrite = use.write;
        break;
    case framegraph::UsageType::DepthStencilAttachment:
        sync.stageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        sync.accessMask = use.write ? VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT : VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        sync.isWrite = use.write;
        break;
    case framegraph::UsageType::InputAttachment:
        sync.stageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        sync.accessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
        sync.isWrite = false;
        break;
    case framegraph::UsageType::Sampled:
    case framegraph::UsageType::StorageRead:
        sync.stageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        sync.accessMask = VK_ACCESS_SHADER_READ_BIT;
        sync.isWrite = false;
        break;
    case framegraph::UsageType::StorageWrite:
        sync.stageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        sync.accessMask = VK_ACCESS_SHADER_WRITE_BIT;
        sync.isWrite = true;
        break;
    case framegraph::UsageType::TransferSrc:
        sync.stageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
        sync.accessMask = VK_ACCESS_TRANSFER_READ_BIT;
        sync.isWrite = false;
        break;
    case framegraph::UsageType::TransferDst:
        sync.stageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
        sync.accessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sync.isWrite = true;
        break;
    }
    return sync;
}

bool VkFrameGraphRuntime::hasDepthOrStencilAspect(framegraph::ImageAspect aspectMask) {
    return framegraph::hasAny(aspectMask, framegraph::ImageAspect::Depth | framegraph::ImageAspect::Stencil);
}

void VkFrameGraphRuntime::destroyImageViewAt(VkDevice device, std::vector<VkImageView>& views, uint32_t frameIndex) {
    if (frameIndex >= views.size()) {
        return;
    }
    VkImageView& view = views[frameIndex];
    if (view != VK_NULL_HANDLE) {
        vkDestroyImageView(device, view, nullptr);
        view = VK_NULL_HANDLE;
    }
}

void VkFrameGraphRuntime::destroyImageAt(VkDevice device, std::vector<VkImage>& images, uint32_t frameIndex) {
    if (frameIndex >= images.size()) {
        return;
    }
    VkImage& image = images[frameIndex];
    if (image != VK_NULL_HANDLE) {
        vkDestroyImage(device, image, nullptr);
        image = VK_NULL_HANDLE;
    }
}

bool VkFrameGraphRuntime::buildVkAttachmentRef(
    const framegraph::AttachmentReference& attachmentRef,
    const AttachmentRefBuildContext& buildContext,
    VkAttachmentReference2& outRef) const {
    if (!buildContext.attachmentIndexByResource) {
        std::cerr << "[VkFrameGraphRuntime] Missing attachment index mapping" << std::endl;
        return false;
    }
    if (attachmentRef.resourceId >= buildContext.attachmentIndexByResource->size()) {
        std::cerr << "[VkFrameGraphRuntime] Attachment resource index out of range" << std::endl;
        return false;
    }

    outRef = {};
    outRef.sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
    outRef.attachment = (*buildContext.attachmentIndexByResource)[attachmentRef.resourceId];
    outRef.layout = framegraph::vk::toVkImageLayout(attachmentRef.layout.value_or(buildContext.defaultLayout));
    outRef.aspectMask = framegraph::vk::toVkImageAspect(attachmentRef.aspectMask.value_or(buildContext.defaultAspect));
    return true;
}

void VkFrameGraphRuntime::addOrMergeSubpassDependency(
    std::map<uint64_t, VkSubpassDependency2>& dependencyMap,
    uint32_t srcPass,
    uint32_t dstPass,
    const SyncState& src,
    const SyncState& dst) const {
    const uint64_t key = (static_cast<uint64_t>(srcPass) << 32) | dstPass;
    auto it = dependencyMap.find(key);
    if (it == dependencyMap.end()) {
        VkSubpassDependency2 dependency{};
        dependency.sType = VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2;
        dependency.srcSubpass = srcPass;
        dependency.dstSubpass = dstPass;
        dependency.srcStageMask = src.stageMask;
        dependency.dstStageMask = dst.stageMask;
        dependency.srcAccessMask = src.accessMask;
        dependency.dstAccessMask = dst.accessMask;
        dependency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        dependencyMap.emplace(key, dependency);
        return;
    }

    it->second.srcStageMask |= src.stageMask;
    it->second.dstStageMask |= dst.stageMask;
    it->second.srcAccessMask |= src.accessMask;
    it->second.dstAccessMask |= dst.accessMask;
}

bool VkFrameGraphRuntime::createRenderPass(const VulkanDevice& vulkanDevice) {
    if (!isLoaded) {
        std::cerr << "[VkFrameGraphRuntime] Missing compiled frame graph result" << std::endl;
        return false;
    }

    if (renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(vulkanDevice.getDevice(), renderPass, nullptr);
        renderPass = VK_NULL_HANDLE;
    }

    const auto& resourceDecls = frameGraphResult.resources;
    const auto& passSyncEdges = frameGraphResult.passSyncEdges;
    const auto& attachmentResourceOrder = frameGraphResult.attachmentResourceOrder;
    const auto& aliasGroupByResource = frameGraphResult.aliasGroupByResource;
    const auto& aliasGroups = frameGraphResult.aliasGroups;
    const std::vector<framegraph::PassDescription>& planPasses = frameGraphResult.orderedPasses;

    std::vector<uint32_t> attachmentIndexByResource(resourceDecls.size(), VK_ATTACHMENT_UNUSED);
    std::vector<VkAttachmentDescription2> attachments(attachmentResourceOrder.size());
    for (size_t attachmentIndex = 0; attachmentIndex < attachmentResourceOrder.size(); ++attachmentIndex) {
        const uint32_t resourceId = attachmentResourceOrder[attachmentIndex];
        attachmentIndexByResource[resourceId] = static_cast<uint32_t>(attachmentIndex);

        const framegraph::ResourceDefinition& resource = resourceDecls[resourceId];
        VkAttachmentDescription2 desc{};
        desc.sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;
        if (resourceId < aliasGroupByResource.size()) {
            const int32_t groupIndex = aliasGroupByResource[resourceId];
            if (groupIndex >= 0 &&
                static_cast<size_t>(groupIndex) < aliasGroups.size() &&
                aliasGroups[groupIndex].size() > 1) {
                desc.flags |= VK_ATTACHMENT_DESCRIPTION_MAY_ALIAS_BIT;
            }
        }
        desc.format = framegraph::vk::toVkFormat(resource.format);
        desc.samples = framegraph::vk::toVkSampleCount(resource.samples);
        desc.loadOp = framegraph::vk::toVkAttachmentLoadOp(resource.loadOp);
        desc.storeOp = framegraph::vk::toVkAttachmentStoreOp(resource.storeOp);
        desc.stencilLoadOp = framegraph::vk::toVkAttachmentLoadOp(resource.stencilLoadOp);
        desc.stencilStoreOp = framegraph::vk::toVkAttachmentStoreOp(resource.stencilStoreOp);
        desc.initialLayout = framegraph::vk::toVkImageLayout(resource.initialLayout);
        desc.finalLayout = framegraph::vk::toVkImageLayout(resource.finalLayout);
        attachments[attachmentIndex] = desc;
    }

    struct SubpassData {
        std::vector<VkAttachmentReference2> colors;
        std::vector<VkAttachmentReference2> resolves;
        std::vector<VkAttachmentReference2> inputs;
        std::optional<VkAttachmentReference2> depth;
        std::optional<VkAttachmentReference2> depthResolve;
        VkSubpassDescriptionDepthStencilResolveKHR depthResolveInfo{};
    };

    std::vector<SubpassData> subpassData(planPasses.size());
    std::vector<VkSubpassDescription2> subpasses(planPasses.size());

    for (size_t passIndex = 0; passIndex < planPasses.size(); ++passIndex) {
        const framegraph::PassDescription& passDesc = planPasses[passIndex];
        SubpassData& data = subpassData[passIndex];

        data.colors.reserve(passDesc.colors.size());
        for (const framegraph::AttachmentReference& ref : passDesc.colors) {
            VkAttachmentReference2 attachmentRef{};
            if (!buildVkAttachmentRef(ref, { &attachmentIndexByResource, framegraph::ResourceLayout::ColorAttachment, framegraph::ImageAspect::Color }, attachmentRef)) {
                return false;
            }
            data.colors.push_back(attachmentRef);
        }

        data.resolves.reserve(passDesc.resolves.size());
        for (const framegraph::AttachmentReference& ref : passDesc.resolves) {
            VkAttachmentReference2 attachmentRef{};
            if (!buildVkAttachmentRef(ref, { &attachmentIndexByResource, framegraph::ResourceLayout::ColorAttachment, framegraph::ImageAspect::Color }, attachmentRef)) {
                return false;
            }
            data.resolves.push_back(attachmentRef);
        }

        data.inputs.reserve(passDesc.inputs.size());
        for (const framegraph::AttachmentReference& ref : passDesc.inputs) {
            if (ref.resourceId >= resourceDecls.size()) {
                std::cerr << "[VkFrameGraphRuntime] Input attachment resource index out of range" << std::endl;
                return false;
            }
            const framegraph::ResourceDefinition& resource = resourceDecls[ref.resourceId];
            const framegraph::ResourceLayout layout = hasDepthOrStencilAspect(resource.viewAspect)
                ? framegraph::ResourceLayout::DepthStencilReadOnly
                : framegraph::ResourceLayout::ShaderReadOnly;
            VkAttachmentReference2 attachmentRef{};
            if (!buildVkAttachmentRef(ref, { &attachmentIndexByResource, layout, resource.viewAspect }, attachmentRef)) {
                return false;
            }
            data.inputs.push_back(attachmentRef);
        }

        if (passDesc.depthStencil.has_value()) {
            const framegraph::ResourceLayout layout = passDesc.depthReadOnly
                ? framegraph::ResourceLayout::DepthStencilReadOnly
                : framegraph::ResourceLayout::DepthStencilAttachment;
            VkAttachmentReference2 depthRef{};
            if (!buildVkAttachmentRef(
                passDesc.depthStencil.value(),
                { &attachmentIndexByResource, layout, framegraph::ImageAspect::Depth | framegraph::ImageAspect::Stencil },
                depthRef)) {
                return false;
            }
            data.depth = depthRef;
        }

        if (passDesc.depthResolve.has_value()) {
            VkAttachmentReference2 depthResolveRef{};
            if (!buildVkAttachmentRef(
                passDesc.depthResolve.value(),
                { &attachmentIndexByResource, framegraph::ResourceLayout::General, framegraph::ImageAspect::Depth | framegraph::ImageAspect::Stencil },
                depthResolveRef)) {
                return false;
            }
            data.depthResolve = depthResolveRef;
            data.depthResolveInfo.sType = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_DEPTH_STENCIL_RESOLVE;
            data.depthResolveInfo.depthResolveMode = vulkanDevice.getDepthResolveMode();
            data.depthResolveInfo.stencilResolveMode = vulkanDevice.getDepthResolveMode();
            data.depthResolveInfo.pDepthStencilResolveAttachment = &data.depthResolve.value();
        }

        VkSubpassDescription2 subpass{};
        subpass.sType = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2;
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = static_cast<uint32_t>(data.colors.size());
        subpass.pColorAttachments = data.colors.empty() ? nullptr : data.colors.data();
        subpass.pResolveAttachments = data.resolves.empty() ? nullptr : data.resolves.data();
        subpass.inputAttachmentCount = static_cast<uint32_t>(data.inputs.size());
        subpass.pInputAttachments = data.inputs.empty() ? nullptr : data.inputs.data();
        subpass.pDepthStencilAttachment = data.depth.has_value() ? &data.depth.value() : nullptr;
        subpass.pNext = data.depthResolve.has_value() ? &data.depthResolveInfo : nullptr;
        subpasses[passIndex] = subpass;
    }

    std::map<uint64_t, VkSubpassDependency2> dependencyMap;
    for (const framegraph::PassSyncEdge& edge : passSyncEdges) {
        if (!edge.srcPass.isValid() || !edge.dstPass.isValid()) {
            continue;
        }

        const uint32_t srcPass = framegraph::toIndex(edge.srcPass);
        const uint32_t dstPass = framegraph::toIndex(edge.dstPass);
        if (srcPass >= planPasses.size() || dstPass >= planPasses.size() || srcPass == dstPass) {
            continue;
        }

        framegraph::ResourceUse srcUse{};
        srcUse.resourceId = edge.resourceId;
        srcUse.usage = edge.srcUsage;
        srcUse.write = edge.srcWrite;

        framegraph::ResourceUse dstUse{};
        dstUse.resourceId = edge.resourceId;
        dstUse.usage = edge.dstUsage;
        dstUse.write = edge.dstWrite;

        addOrMergeSubpassDependency(
            dependencyMap,
            srcPass,
            dstPass,
            usageToSync(srcUse),
            usageToSync(dstUse));
    }

    std::vector<VkSubpassDependency2> dependencies;
    dependencies.reserve(dependencyMap.size() + 2);

    if (!planPasses.empty()) {
        VkSubpassDependency2 externalBegin{};
        externalBegin.sType = VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2;
        externalBegin.srcSubpass = VK_SUBPASS_EXTERNAL;
        externalBegin.dstSubpass = 0;
        externalBegin.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        externalBegin.dstStageMask = externalBegin.srcStageMask;
        externalBegin.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        externalBegin.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        dependencies.push_back(externalBegin);
    }

    for (const auto& [_, dependency] : dependencyMap) {
        dependencies.push_back(dependency);
    }

    if (!planPasses.empty()) {
        VkSubpassDependency2 externalEnd{};
        externalEnd.sType = VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2;
        externalEnd.srcSubpass = static_cast<uint32_t>(planPasses.size() - 1);
        externalEnd.dstSubpass = VK_SUBPASS_EXTERNAL;
        externalEnd.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        externalEnd.dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        externalEnd.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        externalEnd.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        dependencies.push_back(externalEnd);
    }

    VkRenderPassCreateInfo2 renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = static_cast<uint32_t>(subpasses.size());
    renderPassInfo.pSubpasses = subpasses.data();
    renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
    renderPassInfo.pDependencies = dependencies.data();

    if (vkCreateRenderPass2(vulkanDevice.getDevice(), &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
        std::cerr << "[VkFrameGraphRuntime] Failed to create frame graph render pass" << std::endl;
        return false;
    }
    return true;
}

bool VkFrameGraphRuntime::createImageViews(const VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, uint32_t maxFramesInFlight) {
    if (!isLoaded) {
        std::cerr << "[VkFrameGraphRuntime] Missing compiled frame graph result" << std::endl;
        return false;
    }

    const auto& resourceDecls = frameGraphResult.resources;
    for (uint32_t resourceId = 0; resourceId < resourceDecls.size(); ++resourceId) {
        const framegraph::ResourceDefinition& resource = resourceDecls[resourceId];
        if (resource.lifetime == framegraph::ResourceLifetime::External) {
            continue;
        }

        ResourceStorage& storage = resourceStorages[resourceId];
        storage.images.assign(maxFramesInFlight, VK_NULL_HANDLE);
        storage.imageMemories.assign(maxFramesInFlight, VK_NULL_HANDLE);
        storage.views.assign(maxFramesInFlight, VK_NULL_HANDLE);
        storage.depthSamplerViews.assign(maxFramesInFlight, VK_NULL_HANDLE);
        storage.stencilSamplerViews.assign(maxFramesInFlight, VK_NULL_HANDLE);
    }

    for (uint32_t frameIndex = 0; frameIndex < maxFramesInFlight; ++frameIndex) {
        for (uint32_t resourceId = 0; resourceId < resourceDecls.size(); ++resourceId) {
            const framegraph::ResourceDefinition& resource = resourceDecls[resourceId];
            if (resource.lifetime == framegraph::ResourceLifetime::External) {
                continue;
            }

            ResourceStorage& storage = resourceStorages[resourceId];

            VkImageCreateInfo imageInfo = createImageCreateInfo(
                resource.extent.width,
                resource.extent.height,
                framegraph::vk::toVkFormat(resource.format),
                VK_IMAGE_TILING_OPTIMAL,
                framegraph::vk::toVkImageUsage(resource.imageUsage),
                framegraph::vk::toVkSampleCount(resource.samples));

            if (vkCreateImage(vulkanDevice.getDevice(), &imageInfo, nullptr, &storage.images[frameIndex]) != VK_SUCCESS) {
                std::cerr << "[VkFrameGraphRuntime] Failed to create frame graph image" << std::endl;
                return false;
            }

            storage.imageMemories[frameIndex] = memoryAllocator.allocateImageMemory(
                storage.images[frameIndex],
                framegraph::vk::toVkMemoryProperties(resource.memoryProperties));

            const VkFormat vkFormat = framegraph::vk::toVkFormat(resource.format);
            if (createImageView(
                vulkanDevice,
                storage.images[frameIndex],
                vkFormat,
                framegraph::vk::toVkImageAspect(resource.viewAspect),
                storage.views[frameIndex]) != VK_SUCCESS) {
                std::cerr << "[VkFrameGraphRuntime] Failed to create frame graph image view for resource "
                          << resourceId << std::endl;
                return false;
            }
            if (storage.createDepthSamplerViews) {
                if (createImageView(
                    vulkanDevice,
                    storage.images[frameIndex],
                    vkFormat,
                    VK_IMAGE_ASPECT_DEPTH_BIT,
                    storage.depthSamplerViews[frameIndex]) != VK_SUCCESS) {
                    std::cerr << "[VkFrameGraphRuntime] Failed to create depth sampler view for resource "
                              << resourceId << std::endl;
                    return false;
                }
            }
            if (storage.createStencilSamplerViews) {
                if (createImageView(
                    vulkanDevice,
                    storage.images[frameIndex],
                    vkFormat,
                    VK_IMAGE_ASPECT_STENCIL_BIT,
                    storage.stencilSamplerViews[frameIndex]) != VK_SUCCESS) {
                    std::cerr << "[VkFrameGraphRuntime] Failed to create stencil sampler view for resource "
                              << resourceId << std::endl;
                    return false;
                }
            }
        }
    }
    return true;
}

bool VkFrameGraphRuntime::createFramebuffers(
    const std::vector<VkImageView>& swapChainImageViews,
    VkExtent2D extent,
    uint32_t maxFramesInFlight,
    const VulkanDevice& vulkanDevice) {
    cleanupFramebuffers(vulkanDevice);

    if (swapChainImageViews.empty()) {
        std::cerr << "[VkFrameGraphRuntime] Swapchain image views array is empty" << std::endl;
        return false;
    }
    const auto& resourceDecls = frameGraphResult.resources;
    const auto& attachmentResourceOrder = frameGraphResult.attachmentResourceOrder;

    if (attachmentResourceOrder.empty()) {
        std::cerr << "[VkFrameGraphRuntime] Frame graph attachment order is not initialized" << std::endl;
        return false;
    }

    swapchainImageCount = static_cast<uint32_t>(swapChainImageViews.size());
    framebuffers.resize(static_cast<size_t>(maxFramesInFlight) * swapchainImageCount, VK_NULL_HANDLE);

    for (uint32_t frameIndex = 0; frameIndex < maxFramesInFlight; ++frameIndex) {
        for (uint32_t swapchainIndex = 0; swapchainIndex < swapchainImageCount; ++swapchainIndex) {
            std::vector<VkImageView> attachments;
            attachments.resize(attachmentResourceOrder.size(), VK_NULL_HANDLE);

            for (size_t attachmentIndex = 0; attachmentIndex < attachmentResourceOrder.size(); ++attachmentIndex) {
                const uint32_t resourceId = attachmentResourceOrder[attachmentIndex];
                if (resourceId >= resourceDecls.size() || resourceId >= resourceStorages.size()) {
                    std::cerr << "[VkFrameGraphRuntime] Attachment resource index out of range" << std::endl;
                    return false;
                }
                const framegraph::ResourceDefinition& resource = resourceDecls[resourceId];

                if (resource.lifetime == framegraph::ResourceLifetime::External) {
                    attachments[attachmentIndex] = swapChainImageViews[swapchainIndex];
                    continue;
                }

                attachments[attachmentIndex] = resourceStorages[resourceId].views[frameIndex];
            }

            VkFramebufferCreateInfo framebufferInfo{};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = renderPass;
            framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
            framebufferInfo.pAttachments = attachments.data();
            framebufferInfo.width = extent.width;
            framebufferInfo.height = extent.height;
            framebufferInfo.layers = 1;

            const size_t framebufferIndex = static_cast<size_t>(frameIndex) * swapchainImageCount + swapchainIndex;
            if (vkCreateFramebuffer(vulkanDevice.getDevice(), &framebufferInfo, nullptr, &framebuffers[framebufferIndex]) != VK_SUCCESS) {
                std::cerr << "[VkFrameGraphRuntime] Failed to create frame graph framebuffer" << std::endl;
                return false;
            }
        }
    }
    return true;
}

void VkFrameGraphRuntime::cleanupFramebuffers(const VulkanDevice& vulkanDevice) {
    for (VkFramebuffer& framebuffer : framebuffers) {
        if (framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(vulkanDevice.getDevice(), framebuffer, nullptr);
            framebuffer = VK_NULL_HANDLE;
        }
    }
    framebuffers.clear();
    swapchainImageCount = 0;
}

VkFramebuffer VkFrameGraphRuntime::getFramebuffer(uint32_t currentFrame, uint32_t imageIndex) const {
    if (swapchainImageCount == 0) {
        return VK_NULL_HANDLE;
    }

    const size_t framebufferIndex = static_cast<size_t>(currentFrame) * swapchainImageCount + imageIndex;
    if (framebufferIndex >= framebuffers.size()) {
        return VK_NULL_HANDLE;
    }

    return framebuffers[framebufferIndex];
}

void VkFrameGraphRuntime::cleanupImages(const VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, uint32_t maxFramesInFlight) {
    const VkDevice device = vulkanDevice.getDevice();
    cleanupFramebuffers(vulkanDevice);

    for (uint32_t frameIndex = 0; frameIndex < maxFramesInFlight; ++frameIndex) {
        for (ResourceStorage& storage : resourceStorages) {
            destroyImageViewAt(device, storage.views, frameIndex);
            destroyImageViewAt(device, storage.depthSamplerViews, frameIndex);
            destroyImageViewAt(device, storage.stencilSamplerViews, frameIndex);
            destroyImageAt(device, storage.images, frameIndex);

            if (frameIndex < storage.imageMemories.size()) {
                VkDeviceMemory& imageMemory = storage.imageMemories[frameIndex];
                if (imageMemory != VK_NULL_HANDLE) {
                    memoryAllocator.freeImageMemory(imageMemory);
                    imageMemory = VK_NULL_HANDLE;
                }
            }
        }
    }
}

const std::vector<VkImage>& VkFrameGraphRuntime::emptyImages() {
    static const std::vector<VkImage> kEmpty;
    return kEmpty;
}

const std::vector<VkImageView>& VkFrameGraphRuntime::emptyViews() {
    static const std::vector<VkImageView> kEmpty;
    return kEmpty;
}

const VkFrameGraphRuntime::ResourceStorage* VkFrameGraphRuntime::findResourceStorage(framegraph::ResourceId resourceId) const {
    if (!resourceId.isValid()) {
        return nullptr;
    }

    const uint32_t resourceIndex = framegraph::toIndex(resourceId);
    if (resourceIndex >= resourceStorages.size()) {
        return nullptr;
    }
    return &resourceStorages[resourceIndex];
}

const std::vector<VkImage>& VkFrameGraphRuntime::getResourceImages(framegraph::ResourceId resourceId) const {
    const ResourceStorage* storage = findResourceStorage(resourceId);
    return storage ? storage->images : emptyImages();
}

const std::vector<VkImageView>& VkFrameGraphRuntime::getResourceViews(framegraph::ResourceId resourceId) const {
    const ResourceStorage* storage = findResourceStorage(resourceId);
    return storage ? storage->views : emptyViews();
}

const std::vector<VkImageView>& VkFrameGraphRuntime::getResourceDepthSamplerViews(framegraph::ResourceId resourceId) const {
    const ResourceStorage* storage = findResourceStorage(resourceId);
    return storage ? storage->depthSamplerViews : emptyViews();
}

const std::vector<VkImageView>& VkFrameGraphRuntime::getResourceStencilSamplerViews(framegraph::ResourceId resourceId) const {
    const ResourceStorage* storage = findResourceStorage(resourceId);
    return storage ? storage->stencilSamplerViews : emptyViews();
}
