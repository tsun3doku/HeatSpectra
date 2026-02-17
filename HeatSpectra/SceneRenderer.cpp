#include <array>
#include <stdexcept>
#include <vector>

#include "FrameGraph.hpp"
#include "CommandBufferManager.hpp"
#include "Gizmo.hpp"
#include "HeatSystem.hpp"
#include "ModelSelection.hpp"
#include "ResourceManager.hpp"
#include "SceneRenderer.hpp"
#include "UniformBufferManager.hpp"
#include "VulkanDevice.hpp"
#include "WireframeRenderer.hpp"

#include "GeometryPass.hpp"
#include "LightingPass.hpp"
#include "OverlayPass.hpp"
#include "BlendPass.hpp"

SceneRenderer::SceneRenderer(
    VulkanDevice& vulkanDevice,
    FrameGraph& frameGraph,
    ResourceManager& resourceManager,
    UniformBufferManager& uniformBufferManager,
    uint32_t width,
    uint32_t height,
    VkExtent2D swapchainExtent,
    const std::vector<VkImageView> swapChainImageViews,
    VkFormat swapchainImageFormat,
    uint32_t maxFramesInFlight,
    CommandPool& cmdPool,
    bool drawWireframe)
    : vulkanDevice(vulkanDevice),
      frameGraph(frameGraph),
      resourceManager(resourceManager),
      uniformBufferManager(uniformBufferManager),
      renderCommandPool(cmdPool),
      maxFramesInFlight(maxFramesInFlight),
      renderExtent(swapchainExtent) {
    (void)width;
    (void)height;
    (void)swapchainImageFormat;
    (void)swapChainImageViews;
    (void)drawWireframe;

    initializePasses();
    frameGraph.createPasses();
    createCommandBuffers(maxFramesInFlight);
    createGpuTimingQueryPool();
}

SceneRenderer::~SceneRenderer() {
    destroyGpuTimingQueryPool();
}

void SceneRenderer::initializePasses() {

    auto geometry = std::make_unique<render::GeometryPass>(*this);
    geometryPass = geometry.get();
    frameGraph.addPass(std::move(geometry));

    auto lighting = std::make_unique<render::LightingPass>(*this);
    lightingPass = lighting.get();
    frameGraph.addPass(std::move(lighting));

    auto overlay = std::make_unique<render::OverlayPass>(*this, *geometryPass);
    overlayPass = overlay.get();
    frameGraph.addPass(std::move(overlay));

    auto blend = std::make_unique<render::BlendPass>(*this);
    blendPass = blend.get();
    frameGraph.addPass(std::move(blend));
}

void SceneRenderer::createCommandBuffers(uint32_t maxFramesInFlight) {
    gbufferCommandBuffers.resize(maxFramesInFlight);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = renderCommandPool.getHandle();
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(gbufferCommandBuffers.size());

    if (vkAllocateCommandBuffers(vulkanDevice.getDevice(), &allocInfo, gbufferCommandBuffers.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate command buffers for scene renderer");
    }
}

void SceneRenderer::freeCommandBuffers() {
    vkFreeCommandBuffers(
        vulkanDevice.getDevice(),
        renderCommandPool.getHandle(),
        static_cast<uint32_t>(gbufferCommandBuffers.size()),
        gbufferCommandBuffers.data());
    gbufferCommandBuffers.clear();
}

void SceneRenderer::resize(VkExtent2D extent) {
    renderExtent = extent;
    frameGraph.resizePasses(extent);
}

void SceneRenderer::updateDescriptorSets(uint32_t maxFramesInFlight) {
    (void)maxFramesInFlight;
    frameGraph.updatePassDescriptors();
}

void SceneRenderer::allocateDescriptorSetsForModel(Model* model, uint32_t maxFramesInFlight) {
    if (overlayPass) {
        overlayPass->allocateDescriptorSetsForModel(model, maxFramesInFlight);
    }
}

void SceneRenderer::updateDescriptorSetsForModel(Model* model, iODT* remesher, UniformBufferManager& uniformBufferManager, uint32_t maxFramesInFlight) {
    if (overlayPass) {
        overlayPass->updateDescriptorSetsForModel(model, remesher, uniformBufferManager, maxFramesInFlight);
    }
}

void SceneRenderer::allocateNormalsDescriptorSetsForModel(Model* model, uint32_t maxFramesInFlight) {
    if (overlayPass) {
        overlayPass->allocateNormalsDescriptorSetsForModel(model, maxFramesInFlight);
    }
}

void SceneRenderer::updateNormalsDescriptorSetsForModel(Model* model, iODT* remesher, UniformBufferManager& uniformBufferManager, uint32_t maxFramesInFlight) {
    if (overlayPass) {
        overlayPass->updateNormalsDescriptorSetsForModel(model, remesher, uniformBufferManager, maxFramesInFlight);
    }
}

void SceneRenderer::allocateVertexNormalsDescriptorSetsForModel(Model* model, uint32_t maxFramesInFlight) {
    if (overlayPass) {
        overlayPass->allocateVertexNormalsDescriptorSetsForModel(model, maxFramesInFlight);
    }
}

void SceneRenderer::updateVertexNormalsDescriptorSetsForModel(Model* model, iODT* remesher, UniformBufferManager& uniformBufferManager, uint32_t maxFramesInFlight) {
    if (overlayPass) {
        overlayPass->updateVertexNormalsDescriptorSetsForModel(model, remesher, uniformBufferManager, maxFramesInFlight);
    }
}

VkDescriptorSetLayout SceneRenderer::getGbufferDescriptorSetLayout() const {
    return geometryPass ? geometryPass->getDescriptorSetLayout() : VK_NULL_HANDLE;
}

VkDescriptorSet SceneRenderer::getGeometryDescriptorSet(uint32_t frameIndex) const {
    if (!geometryPass) {
        return VK_NULL_HANDLE;
    }
    return geometryPass->getDescriptorSet(frameIndex);
}

void SceneRenderer::recordCommandBuffer(
    ResourceManager& resourceManager,
    HeatSystem& heatSystem,
    ModelSelection& modelSelection,
    Gizmo& gizmo,
    WireframeRenderer& wireframeRenderer,
    std::vector<VkImageView> swapChainImageViews,
    uint32_t currentFrame,
    uint32_t imageIndex,
    uint32_t maxFramesInFlight,
    VkExtent2D extent,
    int wireframeMode,
    bool drawIntrinsicOverlay,
    bool drawHeatOverlay,
    bool drawIntrinsicNormals,
    bool drawIntrinsicVertexNormals,
    float normalLength,
    bool drawSurfels,
    bool drawVoronoi,
    bool drawPoints,
    bool drawContactLines) {
    VkCommandBuffer commandBuffer = gbufferCommandBuffers[currentFrame];

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("Failed to begin recording scene renderer command buffer");
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

    frameGraph.recordComputeToGraphicsBarrier(commandBuffer);

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = frameGraph.getRenderPass();
    renderPassInfo.framebuffer = frameGraph.getFramebuffer(currentFrame, imageIndex);
    if (renderPassInfo.framebuffer == VK_NULL_HANDLE) {
        throw std::runtime_error("Invalid framebuffer for scene renderer");
    }
    renderPassInfo.renderArea.offset = { 0, 0 };
    renderPassInfo.renderArea.extent = extent;

    VkSubpassBeginInfo subpassBeginInfo{};
    subpassBeginInfo.sType = VK_STRUCTURE_TYPE_SUBPASS_BEGIN_INFO;
    subpassBeginInfo.contents = VK_SUBPASS_CONTENTS_INLINE;

    std::array<VkClearValue, 15> clearValues{};
    clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
    clearValues[1].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
    clearValues[2].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
    clearValues[3].depthStencil = { 1.0f, 0 };
    clearValues[4].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
    clearValues[5].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
    clearValues[6].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
    clearValues[7].depthStencil = { 1.0f, 0 };
    clearValues[8].color = { { clearColorValues[0], clearColorValues[1], clearColorValues[2], 1.0f } };
    clearValues[9].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
    clearValues[10].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
    clearValues[11].color = { { clearColorValues[0], clearColorValues[1], clearColorValues[2], 1.0f } };
    clearValues[12].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
    clearValues[13].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
    clearValues[14].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };

    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

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
    frameContext.imageIndex = imageIndex;
    frameContext.maxFramesInFlight = maxFramesInFlight;
    frameContext.extent = extent;
    frameContext.swapChainImageViews = &swapChainImageViews;

    render::SceneView sceneView{};

    render::RenderFlags flags{};
    flags.wireframeMode = wireframeMode;
    flags.drawIntrinsicOverlay = drawIntrinsicOverlay;
    flags.drawHeatOverlay = drawHeatOverlay;
    flags.drawSurfels = drawSurfels;
    flags.drawVoronoi = drawVoronoi;
    flags.drawPoints = drawPoints;
    flags.drawContactLines = drawContactLines;

    render::OverlayParams overlayParams{};
    overlayParams.drawIntrinsicNormals = drawIntrinsicNormals;
    overlayParams.drawIntrinsicVertexNormals = drawIntrinsicVertexNormals;
    overlayParams.normalLength = normalLength;

    render::RenderServices services{};
    services.resourceManager = &resourceManager;
    services.heatSystem = &heatSystem;
    services.modelSelection = &modelSelection;
    services.gizmo = &gizmo;
    services.wireframeRenderer = &wireframeRenderer;

    frameGraph.recordPasses(
        frameContext,
        sceneView,
        flags,
        overlayParams,
        services,
        gpuTimingQueryPool,
        timingQueryBase + 2);

    vkCmdEndRenderPass(commandBuffer);

    if (gpuTimingQueryPool != VK_NULL_HANDLE) {
        vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, gpuTimingQueryPool, timingQueryBase + 1);
        if (currentFrame < gpuTimingValidFrames.size()) {
            gpuTimingValidFrames[currentFrame] = 1;
        }
    }

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to record scene renderer command buffer");
    }
}

void SceneRenderer::cleanup(uint32_t maxFramesInFlight) {
    (void)maxFramesInFlight;
    destroyGpuTimingQueryPool();
    frameGraph.destroyPasses();
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
    if (!tryGetGpuTimingStats(frameIndex, stats)) {
        outGpuMs = 0.0f;
        return false;
    }
    outGpuMs = stats.totalMs;
    return true;
}

bool SceneRenderer::tryGetGpuTimingStats(uint32_t frameIndex, GpuTimingStats& outStats) const {
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

