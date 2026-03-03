#include <array>
#include <iostream>
#include <unordered_map>
#include <vector>

#include "SceneColorSpace.hpp"
#include "framegraph/FrameGraph.hpp"
#include "framegraph/FrameGraphPasses.hpp"
#include "framegraph/FrameGraphResources.hpp"
#include "vulkan/CommandBufferManager.hpp"
#include "mesh/remesher/Remesher.hpp"
#include "vulkan/ResourceManager.hpp"
#include "SceneRenderer.hpp"
#include "vulkan/UniformBufferManager.hpp"
#include "framegraph/VkFrameGraphRuntime.hpp"
#include "vulkan/VulkanDevice.hpp"

#include "GeometryPass.hpp"
#include "LightingPass.hpp"
#include "OverlayPass.hpp"
#include "BlendPass.hpp"

namespace {
void recordComputeToGraphicsBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags dstStageMask) {
    if (commandBuffer == VK_NULL_HANDLE || dstStageMask == 0) {
        return;
    }

    VkAccessFlags dstAccessMask = 0;
    if ((dstStageMask & VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT) != 0) {
        dstAccessMask |= VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
    }
    if ((dstStageMask & (VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
            VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT |
            VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT |
            VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT |
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)) != 0) {
        dstAccessMask |= VK_ACCESS_SHADER_READ_BIT;
    }
    if ((dstStageMask & VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT) != 0) {
        dstAccessMask |= VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
    }
    if ((dstStageMask & VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT) != 0) {
        dstAccessMask |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
    }
    if ((dstStageMask & VK_PIPELINE_STAGE_TRANSFER_BIT) != 0) {
        dstAccessMask |= VK_ACCESS_TRANSFER_READ_BIT;
    }
    if ((dstStageMask & (VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT)) != 0) {
        dstAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    }
    if (dstAccessMask == 0) {
        dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    }

    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = dstAccessMask;
    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        dstStageMask,
        0,
        1, &barrier,
        0, nullptr,
        0, nullptr);
}
}

SceneRenderer::SceneRenderer(VulkanDevice& device, FrameGraph& graph, VkFrameGraphRuntime& runtime, ResourceManager& manager, UniformBufferManager& ubo, uint32_t framesInFlight, CommandPool& commandPool)
    : vulkanDevice(device),
      frameGraph(graph),
      frameGraphRuntime(runtime),
      resourceManager(manager),
      uniformBufferManager(ubo),
      renderCommandPool(commandPool),
      maxFramesInFlight(framesInFlight) {
    ready = initializePasses();
    if (!ready) {
        cleanup();
        std::cerr << "[SceneRenderer] Initialization failed" << std::endl;
        return;
    }
    createPasses();
    if (!createCommandBuffers()) {
        cleanup();
        ready = false;
        std::cerr << "[SceneRenderer] Failed to create command buffers" << std::endl;
        return;
    }
    createGpuTimingQueryPool();
}

SceneRenderer::~SceneRenderer() {
    destroyGpuTimingQueryPool();
}

bool SceneRenderer::initializePasses() {
    const framegraph::PassId geometryPassId = frameGraph.getPassId(framegraph::passes::Geometry);
    const framegraph::PassId lightingPassId = frameGraph.getPassId(framegraph::passes::Lighting);
    const framegraph::PassId overlayPassId = frameGraph.getPassId(framegraph::passes::Overlay);
    const framegraph::PassId blendPassId = frameGraph.getPassId(framegraph::passes::Blend);

    const framegraph::ResourceId albedoResolveId = frameGraph.getResourceId(framegraph::resources::AlbedoResolve);
    const framegraph::ResourceId normalResolveId = frameGraph.getResourceId(framegraph::resources::NormalResolve);
    const framegraph::ResourceId positionResolveId = frameGraph.getResourceId(framegraph::resources::PositionResolve);
    const framegraph::ResourceId depthResolveId = frameGraph.getResourceId(framegraph::resources::DepthResolve);
    const framegraph::ResourceId depthMsaaId = frameGraph.getResourceId(framegraph::resources::DepthMSAA);
    const framegraph::ResourceId lineResolveId = frameGraph.getResourceId(framegraph::resources::LineResolve);
    const framegraph::ResourceId lightingResolveId = frameGraph.getResourceId(framegraph::resources::LightingResolve);
    const framegraph::ResourceId surfaceResolveId = frameGraph.getResourceId(framegraph::resources::SurfaceResolve);

    if (!geometryPassId.isValid() || !lightingPassId.isValid() || !overlayPassId.isValid() || !blendPassId.isValid()) {
        std::cerr << "[SceneRenderer] Failed to resolve pass ids" << std::endl;
        return false;
    }
    if (!albedoResolveId.isValid() ||
        !normalResolveId.isValid() ||
        !positionResolveId.isValid() ||
        !depthResolveId.isValid() ||
        !depthMsaaId.isValid() ||
        !lineResolveId.isValid() ||
        !lightingResolveId.isValid() ||
        !surfaceResolveId.isValid()) {
        std::cerr << "[SceneRenderer] Failed to resolve resource ids" << std::endl;
        return false;
    }

    auto geometry = std::make_unique<render::GeometryPass>(
        vulkanDevice,
        frameGraphRuntime,
        resourceManager,
        uniformBufferManager,
        maxFramesInFlight,
        geometryPassId);
    geometryPass = geometry.get();
    addPass(std::move(geometry));

    auto lighting = std::make_unique<render::LightingPass>(
        vulkanDevice,
        frameGraphRuntime,
        uniformBufferManager,
        maxFramesInFlight,
        lightingPassId,
        albedoResolveId,
        normalResolveId,
        positionResolveId);
    lightingPass = lighting.get();
    addPass(std::move(lighting));

    auto overlay = std::make_unique<render::OverlayPass>(
        vulkanDevice,
        frameGraphRuntime,
        resourceManager,
        uniformBufferManager,
        *geometryPass,
        maxFramesInFlight,
        renderCommandPool,
        overlayPassId,
        depthResolveId,
        depthMsaaId);
    overlayPass = overlay.get();
    addPass(std::move(overlay));

    auto blend = std::make_unique<render::BlendPass>(
        vulkanDevice,
        renderCommandPool,
        frameGraphRuntime,
        maxFramesInFlight,
        blendPassId,
        surfaceResolveId,
        lineResolveId,
        lightingResolveId,
        albedoResolveId);
    blendPass = blend.get();
    addPass(std::move(blend));
    return true;
}

void SceneRenderer::addPass(std::unique_ptr<render::Pass> pass) {
    if (!pass) {
        return;
    }
    passes.push_back(std::move(pass));
}

void SceneRenderer::rebuildOrderedPasses() {
    orderedPasses.clear();
    if (passes.empty()) {
        return;
    }

    std::unordered_map<std::string, render::Pass*> passByName;
    passByName.reserve(passes.size());
    for (const auto& pass : passes) {
        if (pass) {
            passByName.emplace(pass->name(), pass.get());
        }
    }

    const auto& graphPasses = frameGraph.getFrameGraphResult().orderedPasses;
    for (const framegraph::PassDescription& passDesc : graphPasses) {
        auto it = passByName.find(passDesc.name);
        if (it == passByName.end()) {
            continue;
        }
        orderedPasses.push_back(it->second);
        passByName.erase(it);
    }
}

void SceneRenderer::createPasses() {
    rebuildOrderedPasses();
    for (render::Pass* pass : orderedPasses) {
        pass->create();
    }
}

void SceneRenderer::resizePasses(VkExtent2D extent) {
    rebuildOrderedPasses();
    for (render::Pass* pass : orderedPasses) {
        pass->resize(extent);
    }
}

void SceneRenderer::updatePassDescriptors() {
    rebuildOrderedPasses();
    for (render::Pass* pass : orderedPasses) {
        pass->updateDescriptors();
    }
}

void SceneRenderer::recordPasses(
    const render::FrameContext& frameContext,
    const render::SceneView& sceneView,
    const render::RenderFlags& flags,
    const render::OverlayParams& overlayParams,
    render::RenderServices& services,
    VkQueryPool passTimingQueryPool,
    uint32_t passTimingQueryBase) {
    for (size_t passIndex = 0; passIndex < orderedPasses.size(); ++passIndex) {
        render::Pass* pass = orderedPasses[passIndex];
        if (passIndex > 0) {
            VkSubpassBeginInfo nextSubpassBeginInfo{};
            nextSubpassBeginInfo.sType = VK_STRUCTURE_TYPE_SUBPASS_BEGIN_INFO;
            nextSubpassBeginInfo.contents = VK_SUBPASS_CONTENTS_INLINE;

            VkSubpassEndInfo nextSubpassEndInfo{};
            nextSubpassEndInfo.sType = VK_STRUCTURE_TYPE_SUBPASS_END_INFO;
            vkCmdNextSubpass2(frameContext.commandBuffer, &nextSubpassBeginInfo, &nextSubpassEndInfo);
        }

        if (passTimingQueryPool != VK_NULL_HANDLE) {
            const uint32_t startQuery = passTimingQueryBase + static_cast<uint32_t>(passIndex * 2);
            vkCmdWriteTimestamp(
                frameContext.commandBuffer,
                VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
                passTimingQueryPool,
                startQuery);
        }

        pass->record(frameContext, sceneView, flags, overlayParams, services);

        if (passTimingQueryPool != VK_NULL_HANDLE) {
            const uint32_t endQuery = passTimingQueryBase + static_cast<uint32_t>(passIndex * 2 + 1);
            vkCmdWriteTimestamp(
                frameContext.commandBuffer,
                VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
                passTimingQueryPool,
                endQuery);
        }
    }
}

void SceneRenderer::destroyPasses() {
    for (auto it = orderedPasses.rbegin(); it != orderedPasses.rend(); ++it) {
        (*it)->destroy();
    }
    orderedPasses.clear();
    passes.clear();
}

bool SceneRenderer::createCommandBuffers() {
    freeCommandBuffers();
    gbufferCommandBuffers.resize(maxFramesInFlight);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = renderCommandPool.getHandle();
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(gbufferCommandBuffers.size());

    if (vkAllocateCommandBuffers(vulkanDevice.getDevice(), &allocInfo, gbufferCommandBuffers.data()) != VK_SUCCESS) {
        gbufferCommandBuffers.clear();
        std::cerr << "[SceneRenderer] Failed to allocate command buffers" << std::endl;
        return false;
    }
    return true;
}

void SceneRenderer::freeCommandBuffers() {
    vkFreeCommandBuffers(vulkanDevice.getDevice(), renderCommandPool.getHandle(), static_cast<uint32_t>(gbufferCommandBuffers.size()), gbufferCommandBuffers.data());
    gbufferCommandBuffers.clear();
}

void SceneRenderer::resize(VkExtent2D extent) {
    resizePasses(extent);
}

void SceneRenderer::updateDescriptorSets() {
    updatePassDescriptors();
}

void SceneRenderer::allocateDescriptorSetsForModel(Model* model) {
    if (overlayPass) {
        overlayPass->allocateDescriptorSetsForModel(model, maxFramesInFlight);
    }
}

void SceneRenderer::updateDescriptorSetsForModel(Model* model, iODT* remesher) {
    if (overlayPass) {
        overlayPass->updateDescriptorSetsForModel(model, remesher, uniformBufferManager, maxFramesInFlight);
    }
}

void SceneRenderer::allocateNormalsDescriptorSetsForModel(Model* model) {
    if (overlayPass) {
        overlayPass->allocateNormalsDescriptorSetsForModel(model, maxFramesInFlight);
    }
}

void SceneRenderer::updateNormalsDescriptorSetsForModel(Model* model, iODT* remesher) {
    if (overlayPass) {
        overlayPass->updateNormalsDescriptorSetsForModel(model, remesher, uniformBufferManager, maxFramesInFlight);
    }
}

void SceneRenderer::allocateVertexNormalsDescriptorSetsForModel(Model* model) {
    if (overlayPass) {
        overlayPass->allocateVertexNormalsDescriptorSetsForModel(model, maxFramesInFlight);
    }
}

void SceneRenderer::updateVertexNormalsDescriptorSetsForModel(Model* model, iODT* remesher) {
    if (overlayPass) {
        overlayPass->updateVertexNormalsDescriptorSetsForModel(model, remesher, uniformBufferManager, maxFramesInFlight);
    }
}

void SceneRenderer::updateModelDescriptors(Model* model, Remesher& remesher) {
    if (!model) {
        return;
    }

    iODT* modelRemesher = remesher.getRemesherForModel(model);
    if (!modelRemesher) {
        return;
    }

    updateDescriptorSetsForModel(model, modelRemesher);
    updateNormalsDescriptorSetsForModel(model, modelRemesher);
    updateVertexNormalsDescriptorSetsForModel(model, modelRemesher);
}

void SceneRenderer::setTimingOverlayLines(const std::vector<std::string>& lines) {
    if (overlayPass) {
        overlayPass->setTimingOverlayLines(lines);
    }
}

void SceneRenderer::updateGridLabels(const glm::vec3& gridSize) {
    if (overlayPass) {
        overlayPass->updateGridLabels(gridSize);
    }
}

VkDescriptorSetLayout SceneRenderer::getGbufferDescriptorSetLayout() const {
    if (!geometryPass) {
        return VK_NULL_HANDLE;
    }
    return geometryPass->getDescriptorSetLayout();
}

VkDescriptorSet SceneRenderer::getGeometryDescriptorSet(uint32_t frameIndex) const {
    if (!geometryPass) {
        return VK_NULL_HANDLE;
    }
    return geometryPass->getDescriptorSet(frameIndex);
}

bool SceneRenderer::recordCommandBuffer(
    const render::RenderFrameRequest& frameRequest,
    render::RenderServices& services,
    bool insertComputeToGraphicsBarrier,
    VkPipelineStageFlags computeToGraphicsDstStageMask,
    const std::function<void(VkCommandBuffer)>& postRenderCommands) {
    if (!ready) {
        return false;
    }
    const uint32_t currentFrame = frameRequest.frameIndex;
    const uint32_t imageIndex = frameRequest.imageIndex;
    const VkExtent2D extent = frameRequest.extent;
    if (currentFrame >= gbufferCommandBuffers.size()) {
        std::cerr << "[SceneRenderer] Invalid frame index for command buffer recording" << std::endl;
        return false;
    }
    VkCommandBuffer commandBuffer = gbufferCommandBuffers[currentFrame];
    if (commandBuffer == VK_NULL_HANDLE) {
        std::cerr << "[SceneRenderer] Null command buffer" << std::endl;
        return false;
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        std::cerr << "[SceneRenderer] Failed to begin command buffer recording" << std::endl;
        return false;
    }

    uint32_t timingQueryBase = 0;
    if (gpuTimingQueryPool != VK_NULL_HANDLE) {
        if (currentFrame < gpuTimingValidFrames.size()) {
            gpuTimingValidFrames[currentFrame] = 0;
        }
        timingQueryBase = currentFrame * gpuTimingQueriesPerFrame;
        vkCmdResetQueryPool(commandBuffer, gpuTimingQueryPool, timingQueryBase, gpuTimingQueriesPerFrame);
        vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, gpuTimingQueryPool, timingQueryBase);
    }

    recordComputeToGraphicsBarrier(
        commandBuffer,
        insertComputeToGraphicsBarrier ? computeToGraphicsDstStageMask : 0);

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = frameGraphRuntime.getRenderPass();
    renderPassInfo.framebuffer = frameGraphRuntime.getFramebuffer(currentFrame, imageIndex);
    if (renderPassInfo.framebuffer == VK_NULL_HANDLE) {
        std::cerr << "[SceneRenderer] Invalid framebuffer" << std::endl;
        vkEndCommandBuffer(commandBuffer);
        return false;
    }
    renderPassInfo.renderArea.offset = { 0, 0 };
    renderPassInfo.renderArea.extent = extent;

    VkSubpassBeginInfo subpassBeginInfo{};
    subpassBeginInfo.sType = VK_STRUCTURE_TYPE_SUBPASS_BEGIN_INFO;
    subpassBeginInfo.contents = VK_SUBPASS_CONTENTS_INLINE;

    const auto& frameGraphResult = frameGraphRuntime.getFrameGraphResult();
    const auto& resources = frameGraphResult.resources;
    const auto& attachmentOrder = frameGraphResult.attachmentResourceOrder;
    std::vector<VkClearValue> clearValues(attachmentOrder.size());

    for (size_t attachmentIndex = 0; attachmentIndex < attachmentOrder.size(); ++attachmentIndex) {
        VkClearValue clearValue{};

        const uint32_t resourceIndex = framegraph::toIndex(attachmentOrder[attachmentIndex]);
        if (resourceIndex >= resources.size()) {
            clearValues[attachmentIndex] = clearValue;
            continue;
        }

        const framegraph::ResourceDefinition& resource = resources[resourceIndex];
        const bool isDepthStencil = framegraph::hasAny(
            resource.viewAspect,
            framegraph::ImageAspect::Depth | framegraph::ImageAspect::Stencil);

        if (isDepthStencil) {
            clearValue.depthStencil = { 1.0f, 0 };
        } else {
            clearValue.color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
            if (resource.name == framegraph::resources::LightingMSAA) {
                clearValue.color = { { clearColorValues[0], clearColorValues[1], clearColorValues[2], 1.0f } };
            }
        }

        clearValues[attachmentIndex] = clearValue;
    }

    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.empty() ? nullptr : clearValues.data();

    vkCmdBeginRenderPass2(commandBuffer, &renderPassInfo, &subpassBeginInfo);

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

    render::FrameContext frameContext{};
    frameContext.commandBuffer = commandBuffer;
    frameContext.currentFrame = currentFrame;
    frameContext.extent = extent;

    recordPasses(
        frameContext,
        frameRequest.sceneView,
        frameRequest.flags,
        frameRequest.overlay,
        services,
        gpuTimingQueryPool,
        timingQueryBase + 2);

    vkCmdEndRenderPass(commandBuffer);

    if (postRenderCommands) {
        postRenderCommands(commandBuffer);
    }

    if (gpuTimingQueryPool != VK_NULL_HANDLE) {
        vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, gpuTimingQueryPool, timingQueryBase + 1);
        if (currentFrame < gpuTimingValidFrames.size()) {
            gpuTimingValidFrames[currentFrame] = 1;
        }
    }

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        std::cerr << "[SceneRenderer] Failed to finalize command buffer recording" << std::endl;
        return false;
    }
    return true;
}

void SceneRenderer::cleanup() {
    ready = false;
    destroyGpuTimingQueryPool();
    destroyPasses();
    geometryPass = nullptr;
    lightingPass = nullptr;
    overlayPass = nullptr;
    blendPass = nullptr;
}

void SceneRenderer::createGpuTimingQueryPool() {
    destroyGpuTimingQueryPool();

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(vulkanDevice.getPhysicalDevice(), &properties);
    gpuTimestampPeriod = properties.limits.timestampPeriod;
    gpuTimingPassNames = frameGraph.getPassNames();
    gpuTimingQueriesPerFrame = 2 + static_cast<uint32_t>(gpuTimingPassNames.size() * 2);
    gpuTimingValidFrames.assign(maxFramesInFlight, 0);

    if (gpuTimestampPeriod <= 0.0f || maxFramesInFlight == 0 || gpuTimingQueriesPerFrame == 0) {
        return;
    }

    VkQueryPoolCreateInfo queryInfo{};
    queryInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    queryInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    queryInfo.queryCount = maxFramesInFlight * gpuTimingQueriesPerFrame;

    if (vkCreateQueryPool(vulkanDevice.getDevice(), &queryInfo, nullptr, &gpuTimingQueryPool) != VK_SUCCESS) {
        gpuTimingQueryPool = VK_NULL_HANDLE;
        gpuTimingValidFrames.clear();
    }
}

void SceneRenderer::destroyGpuTimingQueryPool() {
    if (gpuTimingQueryPool != VK_NULL_HANDLE) {
        vkDestroyQueryPool(vulkanDevice.getDevice(), gpuTimingQueryPool, nullptr);
        gpuTimingQueryPool = VK_NULL_HANDLE;
    }
    gpuTimingPassNames.clear();
    gpuTimingQueriesPerFrame = 0;
    gpuTimingValidFrames.clear();
}

bool SceneRenderer::tryGetGpuFrameTimeMs(uint32_t frameIndex, float& outGpuMs) const {
    GpuTimingStats stats{};

    if (!getGpuTimingStats(frameIndex, stats)) {
        outGpuMs = 0.0f;
        return false;
    }

    outGpuMs = stats.totalMs;
    return true;
}

bool SceneRenderer::getGpuTimingStats(uint32_t frameIndex, GpuTimingStats& outStats) const {
    outStats.totalMs = 0.0f;
    outStats.passTimings.clear();

    if (gpuTimingQueryPool == VK_NULL_HANDLE ||
        frameIndex >= maxFramesInFlight ||
        gpuTimestampPeriod <= 0.0f ||
        gpuTimingQueriesPerFrame < 2 ||
        frameIndex >= gpuTimingValidFrames.size() ||
        gpuTimingValidFrames[frameIndex] == 0) {
        return false;
    }

    std::vector<uint64_t> timestamps(gpuTimingQueriesPerFrame, 0);
    const VkResult result = vkGetQueryPoolResults(
        vulkanDevice.getDevice(),
        gpuTimingQueryPool,
        frameIndex * gpuTimingQueriesPerFrame,
        gpuTimingQueriesPerFrame,
        sizeof(uint64_t) * timestamps.size(),
        timestamps.data(),
        sizeof(uint64_t),
        VK_QUERY_RESULT_64_BIT);

    if (result != VK_SUCCESS || timestamps[1] <= timestamps[0]) {
        return false;
    }

    outStats.totalMs = static_cast<float>(timestamps[1] - timestamps[0]) * gpuTimestampPeriod * 1e-6f;
    outStats.passTimings.reserve(gpuTimingPassNames.size());

    for (size_t passIndex = 0; passIndex < gpuTimingPassNames.size(); ++passIndex) {
        const uint32_t queryStart = 2 + static_cast<uint32_t>(passIndex * 2);
        const uint32_t queryEnd = queryStart + 1;
        if (queryEnd >= timestamps.size()) {
            break;
        }

        float passMs = 0.0f;
        if (timestamps[queryEnd] > timestamps[queryStart]) {
            passMs = static_cast<float>(timestamps[queryEnd] - timestamps[queryStart]) * gpuTimestampPeriod * 1e-6f;
        }

        outStats.passTimings.push_back({ gpuTimingPassNames[passIndex], passMs });
    }

    return true;
}

