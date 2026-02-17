#include <utility>

#include "FrameGraph.hpp"
#include "VulkanDevice.hpp"

FrameGraph::FrameGraph(VulkanDevice& vulkanDevice, VkFormat swapchainImageFormat, VkExtent2D swapchainExtent, uint32_t maxFramesInFlight)
    : vulkanDevice(vulkanDevice) {
    (void)swapchainImageFormat;
    (void)swapchainExtent;
    (void)maxFramesInFlight;
}

FrameGraph::~FrameGraph() {
}

void FrameGraph::clearGraphDesc() {
    registeredResourceDecls.clear();
    registeredPassDecls.clear();
    resourceDecls.clear();
    passDecls.clear();
    resourceIdByName.clear();
    subpassIndexByName.clear();
    attachmentResourceOrder.clear();
    resourceStorages.clear();
    graphBuilt = false;
}

uint32_t FrameGraph::addResourceDesc(fg::ResourceDesc desc) {
    desc.id = static_cast<uint32_t>(registeredResourceDecls.size());
    registeredResourceDecls.push_back(std::move(desc));
    graphBuilt = false;
    return static_cast<uint32_t>(registeredResourceDecls.size() - 1);
}

void FrameGraph::addPassDesc(fg::PassDesc passDesc) {
    passDesc.id = static_cast<uint32_t>(registeredPassDecls.size());
    registeredPassDecls.push_back(std::move(passDesc));
    graphBuilt = false;
}

void FrameGraph::addPass(std::unique_ptr<render::Pass> pass) {
    passes.push_back(std::move(pass));
    rebuildOrderedPasses();
}

void FrameGraph::createPasses() {
    rebuildOrderedPasses();
    for (render::Pass* pass : orderedPasses) {
        pass->create();
    }
}

void FrameGraph::resizePasses(VkExtent2D extent) {
    for (render::Pass* pass : orderedPasses) {
        pass->resize(extent);
    }
}

void FrameGraph::updatePassDescriptors() {
    for (render::Pass* pass : orderedPasses) {
        pass->updateDescriptors();
    }
}

void FrameGraph::recordPasses(
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

void FrameGraph::destroyPasses() {
    for (auto it = orderedPasses.rbegin(); it != orderedPasses.rend(); ++it) {
        (*it)->destroy();
    }
    orderedPasses.clear();
    passes.clear();
}

uint32_t FrameGraph::getSubpassIndex(const char* passName) const {
    if (!passName) {
        return 0;
    }

    auto it = subpassIndexByName.find(passName);
    if (it == subpassIndexByName.end()) {
        return 0;
    }
    return it->second;
}

void FrameGraph::setComputeSyncEnabled(bool enabled) {
    computeSyncEnabled = enabled;
}

VkPipelineStageFlags FrameGraph::getComputeWaitDstStageMask() const {
    if (!computeSyncEnabled) {
        return VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    }

    return VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT |
        VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
}

void FrameGraph::recordComputeToGraphicsBarrier(VkCommandBuffer commandBuffer) const {
    if (!computeSyncEnabled || commandBuffer == VK_NULL_HANDLE) {
        return;
    }

    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT |
        VK_ACCESS_INPUT_ATTACHMENT_READ_BIT |
        VK_ACCESS_INDIRECT_COMMAND_READ_BIT;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT |
            VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0,
        1, &barrier,
        0, nullptr,
        0, nullptr);
}

void FrameGraph::rebuildOrderedPasses() {
    orderedPasses.clear();

    if (passes.empty()) {
        return;
    }

    std::unordered_map<std::string, render::Pass*> passByName;
    passByName.reserve(passes.size());
    for (auto& pass : passes) {
        if (pass) {
            passByName.emplace(pass->name(), pass.get());
        }
    }

    for (const fg::PassDesc& passDesc : passDecls) {
        auto it = passByName.find(passDesc.name);
        if (it == passByName.end()) {
            continue;
        }
        orderedPasses.push_back(it->second);
        passByName.erase(it);
    }

}

const std::vector<VkImage>& FrameGraph::emptyImages() {
    static const std::vector<VkImage> kEmpty;
    return kEmpty;
}

const std::vector<VkBuffer>& FrameGraph::emptyBuffers() {
    static const std::vector<VkBuffer> kEmpty;
    return kEmpty;
}

const std::vector<VkImageView>& FrameGraph::emptyViews() {
    static const std::vector<VkImageView> kEmpty;
    return kEmpty;
}

const FrameGraph::ResourceStorage* FrameGraph::findResourceStorage(const char* resourceName) const {
    if (!resourceName) {
        return nullptr;
    }

    auto it = resourceIdByName.find(resourceName);
    if (it == resourceIdByName.end()) {
        return nullptr;
    }

    const uint32_t resourceId = it->second;
    if (resourceId >= resourceStorages.size()) {
        return nullptr;
    }
    return &resourceStorages[resourceId];
}

const std::vector<VkImage>& FrameGraph::getResourceImages(const char* resourceName) const {
    const ResourceStorage* storage = findResourceStorage(resourceName);
    return storage ? storage->images : emptyImages();
}

const std::vector<VkBuffer>& FrameGraph::getResourceBuffers(const char* resourceName) const {
    const ResourceStorage* storage = findResourceStorage(resourceName);
    return storage ? storage->buffers : emptyBuffers();
}

const std::vector<VkImageView>& FrameGraph::getResourceViews(const char* resourceName) const {
    const ResourceStorage* storage = findResourceStorage(resourceName);
    return storage ? storage->views : emptyViews();
}

const std::vector<VkImageView>& FrameGraph::getResourceDepthSamplerViews(const char* resourceName) const {
    const ResourceStorage* storage = findResourceStorage(resourceName);
    return storage ? storage->depthSamplerViews : emptyViews();
}

const std::vector<VkImageView>& FrameGraph::getResourceStencilSamplerViews(const char* resourceName) const {
    const ResourceStorage* storage = findResourceStorage(resourceName);
    return storage ? storage->stencilSamplerViews : emptyViews();
}

const std::vector<VkImage>& FrameGraph::getAlbedoImages() const {
    return getResourceImages("AlbedoMSAA");
}

const std::vector<VkImage>& FrameGraph::getNormalImages() const {
    return getResourceImages("NormalMSAA");
}

const std::vector<VkImage>& FrameGraph::getPositionImages() const {
    return getResourceImages("PositionMSAA");
}

const std::vector<VkImage>& FrameGraph::getDepthImages() const {
    return getResourceImages("DepthMSAA");
}

const std::vector<VkImage>& FrameGraph::getLineOverlayImages() const {
    return getResourceImages("LineMSAA");
}

const std::vector<VkImage>& FrameGraph::getSurfaceOverlayImages() const {
    return getResourceImages("SurfaceMSAA");
}

const std::vector<VkImageView>& FrameGraph::getAlbedoViews() const {
    return getResourceViews("AlbedoMSAA");
}

const std::vector<VkImageView>& FrameGraph::getNormalViews() const {
    return getResourceViews("NormalMSAA");
}

const std::vector<VkImageView>& FrameGraph::getPositionViews() const {
    return getResourceViews("PositionMSAA");
}

const std::vector<VkImageView>& FrameGraph::getDepthViews() const {
    return getResourceViews("DepthMSAA");
}

const std::vector<VkImageView>& FrameGraph::getLineOverlayViews() const {
    return getResourceViews("LineMSAA");
}

const std::vector<VkImageView>& FrameGraph::getSurfaceOverlayViews() const {
    return getResourceViews("SurfaceMSAA");
}

const std::vector<VkImageView>& FrameGraph::getAlbedoResolveViews() const {
    return getResourceViews("AlbedoResolve");
}

const std::vector<VkImageView>& FrameGraph::getNormalResolveViews() const {
    return getResourceViews("NormalResolve");
}

const std::vector<VkImageView>& FrameGraph::getPositionResolveViews() const {
    return getResourceViews("PositionResolve");
}

const std::vector<VkImageView>& FrameGraph::getDepthResolveViews() const {
    return getResourceViews("DepthResolve");
}

const std::vector<VkImageView>& FrameGraph::getDepthResolveSamplerViews() const {
    return getResourceDepthSamplerViews("DepthResolve");
}

const std::vector<VkImageView>& FrameGraph::getStencilMSAASamplerViews() const {
    return getResourceStencilSamplerViews("DepthMSAA");
}

const std::vector<VkImageView>& FrameGraph::getLineOverlayResolveViews() const {
    return getResourceViews("LineResolve");
}

const std::vector<VkImageView>& FrameGraph::getSurfaceOverlayResolveViews() const {
    return getResourceViews("SurfaceResolve");
}

const std::vector<VkImageView>& FrameGraph::getLightingViews() const {
    return getResourceViews("LightingMSAA");
}

const std::vector<VkImageView>& FrameGraph::getLightingResolveViews() const {
    return getResourceViews("LightingResolve");
}

const std::vector<VkImage>& FrameGraph::getDepthResolveImages() const {
    return getResourceImages("DepthResolve");
}

std::vector<std::string> FrameGraph::getPassNames() const {
    std::vector<std::string> names;
    names.reserve(passDecls.size());
    for (const fg::PassDesc& passDesc : passDecls) {
        if (passDesc.name) {
            names.emplace_back(passDesc.name);
        }
    }
    return names;
}
