
#include <array>
#include <algorithm>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <queue>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "VulkanImage.hpp"
#include "VulkanDevice.hpp"
#include "FrameGraphVk.hpp"
#include "FrameGraph.hpp"

namespace {

struct SyncState {
    VkPipelineStageFlags stageMask = 0;
    VkAccessFlags accessMask = 0;
    bool isWrite = false;
};

SyncState usageToSync(const fg::ResourceUse& use) {
    SyncState sync{};
    switch (use.usage) {
    case fg::UsageType::ColorAttachment:
    case fg::UsageType::Present:
        sync.stageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        sync.accessMask = use.write ? VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT : VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
        sync.isWrite = use.write;
        break;
    case fg::UsageType::DepthStencilAttachment:
        sync.stageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        sync.accessMask = use.write ? VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT : VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        sync.isWrite = use.write;
        break;
    case fg::UsageType::InputAttachment:
        sync.stageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        sync.accessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
        sync.isWrite = false;
        break;
    case fg::UsageType::Sampled:
    case fg::UsageType::StorageRead:
        sync.stageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        sync.accessMask = VK_ACCESS_SHADER_READ_BIT;
        sync.isWrite = false;
        break;
    case fg::UsageType::StorageWrite:
        sync.stageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        sync.accessMask = VK_ACCESS_SHADER_WRITE_BIT;
        sync.isWrite = true;
        break;
    case fg::UsageType::TransferSrc:
        sync.stageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
        sync.accessMask = VK_ACCESS_TRANSFER_READ_BIT;
        sync.isWrite = false;
        break;
    case fg::UsageType::TransferDst:
        sync.stageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
        sync.accessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sync.isWrite = true;
        break;
    }
    return sync;
}

bool hasDepthOrStencilAspect(VkImageAspectFlags aspectMask) {
    return (aspectMask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) != 0;
}

void destroyImageViewAt(VkDevice device, std::vector<VkImageView>& views, uint32_t frameIndex) {
    if (frameIndex >= views.size()) {
        return;
    }
    VkImageView& view = views[frameIndex];
    if (view != VK_NULL_HANDLE) {
        vkDestroyImageView(device, view, nullptr);
        view = VK_NULL_HANDLE;
    }
}

void destroyImageAt(VkDevice device, std::vector<VkImage>& images, uint32_t frameIndex) {
    if (frameIndex >= images.size()) {
        return;
    }
    VkImage& image = images[frameIndex];
    if (image != VK_NULL_HANDLE) {
        vkDestroyImage(device, image, nullptr);
        image = VK_NULL_HANDLE;
    }
}

void destroyBufferAt(VkDevice device, std::vector<VkBuffer>& buffers, uint32_t frameIndex) {
    if (frameIndex >= buffers.size()) {
        return;
    }
    VkBuffer& buffer = buffers[frameIndex];
    if (buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
    }
}

} // namespace

void FrameGraph::buildGraph(VkFormat swapchainImageFormat, VkExtent2D extent) {
    resourceDecls.clear();
    passDecls.clear();
    resourceIdByName.clear();
    resourceStorages.clear();
    attachmentResourceOrder.clear();

    if (registeredResourceDecls.empty() || registeredPassDecls.empty()) {
        throw std::runtime_error("Frame graph has no registered resources/passes");
    }

    resourceDecls.reserve(registeredResourceDecls.size());
    for (size_t index = 0; index < registeredResourceDecls.size(); ++index) {
        fg::ResourceDesc desc = registeredResourceDecls[index];
        desc.id = static_cast<uint32_t>(index);
        if (desc.memoryProperties == 0) {
            desc.memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        }
        if (desc.type == fg::ResourceType::Image2D) {
            desc.extent = extent;
            if (desc.useSwapchainFormat) {
                desc.format = swapchainImageFormat;
            }
        }
        else if (desc.type == fg::ResourceType::Buffer) {
            desc.renderPassAttachment = false;
            if (desc.bufferSize == 0) {
                throw std::runtime_error("Frame graph buffer resource has zero size");
            }
            if (desc.bufferUsage == 0) {
                throw std::runtime_error("Frame graph buffer resource has no usage flags");
            }
        }
        if (!desc.name || desc.name[0] == '\0') {
            throw std::runtime_error("Frame graph resource has empty name");
        }
        resourceDecls.push_back(desc);
    }

    passDecls = registeredPassDecls;
    for (size_t passIndex = 0; passIndex < passDecls.size(); ++passIndex) {
        passDecls[passIndex].id = static_cast<uint32_t>(passIndex);
    }

    cullUnusedGraph();
    compilePassDag();
    compileTransientAliasingPlan();

    resourceStorages.assign(resourceDecls.size(), {});
    for (uint32_t resourceId = 0; resourceId < resourceDecls.size(); ++resourceId) {
        const fg::ResourceDesc& resource = resourceDecls[resourceId];
        if (resource.type != fg::ResourceType::Image2D) {
            continue;
        }
        resourceStorages[resourceId].createDepthSamplerViews = (resource.viewAspect & VK_IMAGE_ASPECT_DEPTH_BIT) != 0;
        resourceStorages[resourceId].createStencilSamplerViews = (resource.viewAspect & VK_IMAGE_ASPECT_STENCIL_BIT) != 0;
    }
    rebuildOrderedPasses();
    graphBuilt = true;
}

void FrameGraph::cullUnusedGraph() {
    if (resourceDecls.empty() || passDecls.empty()) {
        return;
    }

    const uint32_t resourceCount = static_cast<uint32_t>(resourceDecls.size());
    const uint32_t passCount = static_cast<uint32_t>(passDecls.size());

    std::vector<std::vector<fg::ResourceUse>> passUses(passCount);
    std::vector<std::vector<uint32_t>> writersByResource(resourceCount);
    for (uint32_t passIndex = 0; passIndex < passCount; ++passIndex) {
        passUses[passIndex] = deriveUses(passDecls[passIndex]);
        for (const fg::ResourceUse& use : passUses[passIndex]) {
            if (use.resourceId >= resourceCount || !use.write) {
                continue;
            }
            writersByResource[use.resourceId].push_back(passIndex);
        }
    }

    std::vector<uint8_t> liveResources(resourceCount, 0);
    std::vector<uint8_t> livePasses(passCount, 0);
    std::deque<uint32_t> resourceQueue;

    for (uint32_t resourceId = 0; resourceId < resourceCount; ++resourceId) {
        if (!resourceDecls[resourceId].finalOutput) {
            continue;
        }
        liveResources[resourceId] = 1;
        resourceQueue.push_back(resourceId);
    }

    if (resourceQueue.empty()) {
        for (uint32_t resourceId = 0; resourceId < resourceCount; ++resourceId) {
            if (resourceDecls[resourceId].lifetime != fg::ResourceLifetime::External) {
                continue;
            }
            liveResources[resourceId] = 1;
            resourceQueue.push_back(resourceId);
        }
    }

    while (!resourceQueue.empty()) {
        const uint32_t requiredResource = resourceQueue.front();
        resourceQueue.pop_front();

        for (uint32_t writerPass : writersByResource[requiredResource]) {
            if (writerPass >= passCount || livePasses[writerPass]) {
                continue;
            }

            livePasses[writerPass] = 1;
            for (const fg::ResourceUse& use : passUses[writerPass]) {
                if (use.resourceId >= resourceCount || liveResources[use.resourceId]) {
                    continue;
                }
                liveResources[use.resourceId] = 1;
                resourceQueue.push_back(use.resourceId);
            }
        }
    }

    if (std::none_of(livePasses.begin(), livePasses.end(), [](uint8_t live) { return live != 0; })) {
        std::fill(livePasses.begin(), livePasses.end(), 1);
        std::fill(liveResources.begin(), liveResources.end(), 1);
    }

    std::vector<int32_t> resourceRemap(resourceCount, -1);
    std::vector<fg::ResourceDesc> culledResources;
    culledResources.reserve(resourceCount);
    for (uint32_t resourceId = 0; resourceId < resourceCount; ++resourceId) {
        if (!liveResources[resourceId]) {
            continue;
        }
        fg::ResourceDesc desc = resourceDecls[resourceId];
        const uint32_t newId = static_cast<uint32_t>(culledResources.size());
        resourceRemap[resourceId] = static_cast<int32_t>(newId);
        desc.id = newId;
        culledResources.push_back(std::move(desc));
    }

    auto remapAttachmentRef = [&](fg::AttachmentRef& ref) {
        if (ref.resourceId >= resourceRemap.size()) {
            return false;
        }
        const int32_t mapped = resourceRemap[ref.resourceId];
        if (mapped < 0) {
            return false;
        }
        ref.resourceId = static_cast<uint32_t>(mapped);
        return true;
    };

    std::vector<fg::PassDesc> culledPasses;
    culledPasses.reserve(passCount);
    for (uint32_t passIndex = 0; passIndex < passCount; ++passIndex) {
        if (!livePasses[passIndex]) {
            continue;
        }

        fg::PassDesc pass = passDecls[passIndex];

        std::vector<fg::AttachmentRef> remappedColors;
        remappedColors.reserve(pass.colors.size());
        for (fg::AttachmentRef ref : pass.colors) {
            if (remapAttachmentRef(ref)) {
                remappedColors.push_back(std::move(ref));
            }
        }
        pass.colors = std::move(remappedColors);

        std::vector<fg::AttachmentRef> remappedResolves;
        remappedResolves.reserve(pass.resolves.size());
        for (fg::AttachmentRef ref : pass.resolves) {
            if (remapAttachmentRef(ref)) {
                remappedResolves.push_back(std::move(ref));
            }
        }
        pass.resolves = std::move(remappedResolves);

        std::vector<fg::AttachmentRef> remappedInputs;
        remappedInputs.reserve(pass.inputs.size());
        for (fg::AttachmentRef ref : pass.inputs) {
            if (remapAttachmentRef(ref)) {
                remappedInputs.push_back(std::move(ref));
            }
        }
        pass.inputs = std::move(remappedInputs);

        if (pass.depthStencil.has_value()) {
            fg::AttachmentRef ref = pass.depthStencil.value();
            if (remapAttachmentRef(ref)) {
                pass.depthStencil = std::move(ref);
            }
            else {
                pass.depthStencil.reset();
            }
        }

        if (pass.depthResolve.has_value()) {
            fg::AttachmentRef ref = pass.depthResolve.value();
            if (remapAttachmentRef(ref)) {
                pass.depthResolve = std::move(ref);
            }
            else {
                pass.depthResolve.reset();
            }
        }

        std::vector<fg::ResourceUse> remappedUses;
        remappedUses.reserve(pass.uses.size());
        for (fg::ResourceUse use : pass.uses) {
            if (use.resourceId >= resourceRemap.size()) {
                continue;
            }
            const int32_t mapped = resourceRemap[use.resourceId];
            if (mapped < 0) {
                continue;
            }
            use.resourceId = static_cast<uint32_t>(mapped);
            remappedUses.push_back(use);
        }
        pass.uses = std::move(remappedUses);

        pass.id = static_cast<uint32_t>(culledPasses.size());
        culledPasses.push_back(std::move(pass));
    }

    resourceDecls = std::move(culledResources);
    passDecls = std::move(culledPasses);

    attachmentResourceOrder.clear();
    attachmentResourceOrder.reserve(resourceDecls.size());
    resourceIdByName.clear();
    for (uint32_t resourceId = 0; resourceId < resourceDecls.size(); ++resourceId) {
        fg::ResourceDesc& desc = resourceDecls[resourceId];
        if (!desc.name || desc.name[0] == '\0') {
            throw std::runtime_error("Frame graph resource has empty name");
        }
        desc.id = resourceId;
        if (!resourceIdByName.emplace(desc.name, resourceId).second) {
            throw std::runtime_error("Frame graph resource names must be unique");
        }
        if (desc.renderPassAttachment) {
            attachmentResourceOrder.push_back(resourceId);
        }
    }

    if (passDecls.empty()) {
        throw std::runtime_error("Frame graph culling removed all passes");
    }
}

void FrameGraph::compilePassDag() {
    subpassIndexByName.clear();

    const uint32_t passCount = static_cast<uint32_t>(passDecls.size());
    if (passCount == 0) {
        return;
    }

    auto addEdge = [](std::vector<std::unordered_set<uint32_t>>& adjacency,
        std::vector<uint32_t>& indegree,
        uint32_t src,
        uint32_t dst) {
            if (src == dst) {
                return;
            }
            if (adjacency[src].insert(dst).second) {
                ++indegree[dst];
            }
        };

    std::vector<std::unordered_set<uint32_t>> adjacency(passCount);
    std::vector<uint32_t> indegree(passCount, 0);

    struct PassAccess {
        bool read = false;
        bool write = false;
    };

    for (uint32_t resourceId = 0; resourceId < resourceDecls.size(); ++resourceId) {
        int32_t lastWriter = -1;
        std::vector<uint32_t> readersSinceLastWriter;

        for (uint32_t passIndex = 0; passIndex < passCount; ++passIndex) {
            PassAccess access{};
            const std::vector<fg::ResourceUse> uses = deriveUses(passDecls[passIndex]);
            for (const fg::ResourceUse& use : uses) {
                if (use.resourceId != resourceId) {
                    continue;
                }
                access.write = access.write || use.write;
                access.read = access.read || !use.write;
            }

            if (!access.read && !access.write) {
                continue;
            }

            if (access.read && lastWriter >= 0) {
                addEdge(adjacency, indegree, static_cast<uint32_t>(lastWriter), passIndex);
            }

            if (access.write) {
                if (lastWriter >= 0) {
                    addEdge(adjacency, indegree, static_cast<uint32_t>(lastWriter), passIndex);
                }
                for (uint32_t readerPass : readersSinceLastWriter) {
                    addEdge(adjacency, indegree, readerPass, passIndex);
                }
                lastWriter = static_cast<int32_t>(passIndex);
                readersSinceLastWriter.clear();
            }

            if (access.read) {
                if (readersSinceLastWriter.empty() || readersSinceLastWriter.back() != passIndex) {
                    readersSinceLastWriter.push_back(passIndex);
                }
            }
        }
    }

    std::priority_queue<uint32_t, std::vector<uint32_t>, std::greater<uint32_t>> ready;
    for (uint32_t passIndex = 0; passIndex < passCount; ++passIndex) {
        if (indegree[passIndex] == 0) {
            ready.push(passIndex);
        }
    }

    std::vector<uint32_t> sortedOrder;
    sortedOrder.reserve(passCount);
    while (!ready.empty()) {
        const uint32_t passIndex = ready.top();
        ready.pop();
        sortedOrder.push_back(passIndex);

        for (uint32_t dstPass : adjacency[passIndex]) {
            if (--indegree[dstPass] == 0) {
                ready.push(dstPass);
            }
        }
    }

    if (sortedOrder.size() != passCount) {
        throw std::runtime_error("Frame graph pass DAG has a cycle");
    }

    std::vector<fg::PassDesc> sortedPasses;
    sortedPasses.reserve(passCount);
    for (uint32_t sortedIndex = 0; sortedIndex < passCount; ++sortedIndex) {
        fg::PassDesc passDesc = std::move(passDecls[sortedOrder[sortedIndex]]);
        passDesc.id = sortedIndex;
        if (!subpassIndexByName.emplace(passDesc.name, sortedIndex).second) {
            throw std::runtime_error("Frame graph pass names must be unique");
        }
        sortedPasses.push_back(std::move(passDesc));
    }

    passDecls = std::move(sortedPasses);
}

bool FrameGraph::canAliasResources(uint32_t resourceA, uint32_t resourceB) const {
    if (resourceA >= resourceDecls.size() || resourceB >= resourceDecls.size()) {
        return false;
    }

    const fg::ResourceDesc& a = resourceDecls[resourceA];
    const fg::ResourceDesc& b = resourceDecls[resourceB];

    if (a.type != b.type) {
        return false;
    }
    if (a.lifetime != fg::ResourceLifetime::Transient || b.lifetime != fg::ResourceLifetime::Transient) {
        return false;
    }
    if (a.memoryProperties != b.memoryProperties) {
        return false;
    }

    if (a.type == fg::ResourceType::Image2D) {
        // Keep image aliasing conservative around sample count and format.
        // Size/usage compatibility is resolved from actual VkMemoryRequirements.
        return a.format == b.format &&
            a.samples == b.samples;
    }

    if (a.type == fg::ResourceType::Buffer) {
        return true;
    }

    return false;
}

bool FrameGraph::hasLifetimeOverlap(uint32_t resourceA, uint32_t resourceB) const {
    if (resourceA >= resourceLifetimes.size() || resourceB >= resourceLifetimes.size()) {
        return false;
    }

    const ResourceLifetimeRange& a = resourceLifetimes[resourceA];
    const ResourceLifetimeRange& b = resourceLifetimes[resourceB];
    if (!a.isValid() || !b.isValid()) {
        return false;
    }

    return !(a.lastPass < b.firstPass || b.lastPass < a.firstPass);
}

void FrameGraph::compileTransientAliasingPlan() {
    resourceLifetimes.assign(resourceDecls.size(), {});
    aliasGroupByResource.assign(resourceDecls.size(), -1);
    aliasGroups.clear();

    for (size_t passIndex = 0; passIndex < passDecls.size(); ++passIndex) {
        const std::vector<fg::ResourceUse> uses = deriveUses(passDecls[passIndex]);
        for (const fg::ResourceUse& use : uses) {
            if (use.resourceId >= resourceDecls.size()) {
                continue;
            }
            if (resourceDecls[use.resourceId].lifetime != fg::ResourceLifetime::Transient) {
                continue;
            }

            ResourceLifetimeRange& range = resourceLifetimes[use.resourceId];
            if (range.firstPass < 0) {
                range.firstPass = static_cast<int32_t>(passIndex);
            }
            range.lastPass = static_cast<int32_t>(passIndex);
        }
    }

    std::vector<uint32_t> candidates;
    candidates.reserve(resourceDecls.size());
    for (uint32_t resourceId = 0; resourceId < resourceDecls.size(); ++resourceId) {
        const fg::ResourceDesc& resource = resourceDecls[resourceId];
        if (resource.lifetime != fg::ResourceLifetime::Transient) {
            continue;
        }
        if (resource.type != fg::ResourceType::Image2D &&
            resource.type != fg::ResourceType::Buffer) {
            continue;
        }
        if (!resourceLifetimes[resourceId].isValid()) {
            continue;
        }
        candidates.push_back(resourceId);
    }

    std::sort(candidates.begin(), candidates.end(), [&](uint32_t a, uint32_t b) {
        const ResourceLifetimeRange& ra = resourceLifetimes[a];
        const ResourceLifetimeRange& rb = resourceLifetimes[b];
        if (ra.firstPass != rb.firstPass) {
            return ra.firstPass < rb.firstPass;
        }
        return ra.lastPass < rb.lastPass;
    });

    for (uint32_t resourceId : candidates) {
        int32_t selectedGroup = -1;

        for (size_t groupIndex = 0; groupIndex < aliasGroups.size(); ++groupIndex) {
            const std::vector<uint32_t>& group = aliasGroups[groupIndex];
            if (group.empty()) {
                continue;
            }
            if (!canAliasResources(resourceId, group.front())) {
                continue;
            }

            bool overlap = false;
            for (uint32_t groupMember : group) {
                if (hasLifetimeOverlap(resourceId, groupMember)) {
                    overlap = true;
                    break;
                }
            }
            if (!overlap) {
                selectedGroup = static_cast<int32_t>(groupIndex);
                break;
            }
        }

        if (selectedGroup < 0) {
            aliasGroups.push_back({ resourceId });
            selectedGroup = static_cast<int32_t>(aliasGroups.size() - 1);
        }
        else {
            aliasGroups[selectedGroup].push_back(resourceId);
        }

        aliasGroupByResource[resourceId] = selectedGroup;
    }
}

std::vector<fg::ResourceUse> FrameGraph::deriveUses(const fg::PassDesc& passDesc) const {
    if (!passDesc.uses.empty()) {
        return passDesc.uses;
    }

    std::vector<fg::ResourceUse> uses;
    uses.reserve(passDesc.colors.size() + passDesc.resolves.size() + passDesc.inputs.size() + 2);

    for (const fg::AttachmentRef& ref : passDesc.colors) {
        uses.push_back({ ref.resourceId, fg::UsageType::ColorAttachment, true });
    }
    for (const fg::AttachmentRef& ref : passDesc.resolves) {
        uses.push_back({ ref.resourceId, fg::UsageType::ColorAttachment, true });
    }
    for (const fg::AttachmentRef& ref : passDesc.inputs) {
        uses.push_back({ ref.resourceId, fg::UsageType::InputAttachment, false });
    }
    if (passDesc.depthStencil.has_value()) {
        uses.push_back({ passDesc.depthStencil->resourceId, fg::UsageType::DepthStencilAttachment, !passDesc.depthReadOnly });
    }
    if (passDesc.depthResolve.has_value()) {
        uses.push_back({ passDesc.depthResolve->resourceId, fg::UsageType::DepthStencilAttachment, true });
    }

    return uses;
}
void FrameGraph::createRenderPass(const VulkanDevice& vulkanDevice, VkFormat swapchainImageFormat) {
    if (renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(vulkanDevice.getDevice(), renderPass, nullptr);
        renderPass = VK_NULL_HANDLE;
    }

    buildGraph(swapchainImageFormat, {});

    std::vector<uint32_t> attachmentIndexByResource(resourceDecls.size(), VK_ATTACHMENT_UNUSED);
    std::vector<VkAttachmentDescription2> attachments(attachmentResourceOrder.size());
    for (size_t attachmentIndex = 0; attachmentIndex < attachmentResourceOrder.size(); ++attachmentIndex) {
        const uint32_t resourceId = attachmentResourceOrder[attachmentIndex];
        attachmentIndexByResource[resourceId] = static_cast<uint32_t>(attachmentIndex);

        const fg::ResourceDesc& resource = resourceDecls[resourceId];
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
        desc.format = resource.format;
        desc.samples = resource.samples;
        desc.loadOp = resource.loadOp;
        desc.storeOp = resource.storeOp;
        desc.stencilLoadOp = resource.stencilLoadOp;
        desc.stencilStoreOp = resource.stencilStoreOp;
        desc.initialLayout = resource.initialLayout;
        desc.finalLayout = resource.finalLayout;
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

    std::vector<SubpassData> subpassData(passDecls.size());
    std::vector<VkSubpassDescription2> subpasses(passDecls.size());

    for (size_t passIndex = 0; passIndex < passDecls.size(); ++passIndex) {
        const fg::PassDesc& passDesc = passDecls[passIndex];
        SubpassData& data = subpassData[passIndex];

        auto makeRef = [&](const fg::AttachmentRef& attachmentRef,
            VkImageLayout defaultLayout,
            VkImageAspectFlags defaultAspect) {
            VkAttachmentReference2 ref{};
            ref.sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
            ref.attachment = attachmentIndexByResource.at(attachmentRef.resourceId);
            ref.layout = attachmentRef.layout.value_or(defaultLayout);
            ref.aspectMask = attachmentRef.aspectMask.value_or(defaultAspect);
            return ref;
        };

        data.colors.reserve(passDesc.colors.size());
        for (const fg::AttachmentRef& ref : passDesc.colors) {
            data.colors.push_back(makeRef(ref, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT));
        }

        data.resolves.reserve(passDesc.resolves.size());
        for (const fg::AttachmentRef& ref : passDesc.resolves) {
            data.resolves.push_back(makeRef(ref, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT));
        }

        data.inputs.reserve(passDesc.inputs.size());
        for (const fg::AttachmentRef& ref : passDesc.inputs) {
            const fg::ResourceDesc& resource = resourceDecls.at(ref.resourceId);
            const VkImageLayout layout = hasDepthOrStencilAspect(resource.viewAspect)
                ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            data.inputs.push_back(makeRef(ref, layout, resource.viewAspect));
        }

        if (passDesc.depthStencil.has_value()) {
            const fg::AttachmentRef& ref = passDesc.depthStencil.value();
            const VkImageLayout layout = passDesc.depthReadOnly
                ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            data.depth = makeRef(ref, layout, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
        }

        if (passDesc.depthResolve.has_value()) {
            const fg::AttachmentRef& ref = passDesc.depthResolve.value();
            data.depthResolve = makeRef(ref, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);

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

    auto addDependency = [&](uint32_t srcPass, uint32_t dstPass, const SyncState& srcSync, const SyncState& dstSync) {
        const uint64_t key = (static_cast<uint64_t>(srcPass) << 32) | dstPass;
        auto it = dependencyMap.find(key);
        if (it == dependencyMap.end()) {
            VkSubpassDependency2 dependency{};
            dependency.sType = VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2;
            dependency.srcSubpass = srcPass;
            dependency.dstSubpass = dstPass;
            dependency.srcStageMask = srcSync.stageMask;
            dependency.dstStageMask = dstSync.stageMask;
            dependency.srcAccessMask = srcSync.accessMask;
            dependency.dstAccessMask = dstSync.accessMask;
            dependency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
            dependencyMap.emplace(key, dependency);
        }
        else {
            it->second.srcStageMask |= srcSync.stageMask;
            it->second.dstStageMask |= dstSync.stageMask;
            it->second.srcAccessMask |= srcSync.accessMask;
            it->second.dstAccessMask |= dstSync.accessMask;
        }
    };
    std::vector<std::unordered_map<uint32_t, SyncState>> passUseStates(passDecls.size());
    for (uint32_t passIndex = 0; passIndex < passDecls.size(); ++passIndex) {
        std::unordered_map<uint32_t, SyncState> passUseState;
        const std::vector<fg::ResourceUse> uses = deriveUses(passDecls[passIndex]);
        for (const fg::ResourceUse& use : uses) {
            SyncState sync = usageToSync(use);
            auto [it, inserted] = passUseState.emplace(use.resourceId, sync);
            if (!inserted) {
                it->second.stageMask |= sync.stageMask;
                it->second.accessMask |= sync.accessMask;
                it->second.isWrite = it->second.isWrite || sync.isWrite;
            }
        }
        passUseStates[passIndex] = std::move(passUseState);
    }

    struct ReaderUse {
        uint32_t passIndex = 0;
        SyncState sync{};
    };

    for (uint32_t resourceId = 0; resourceId < resourceDecls.size(); ++resourceId) {
        int32_t lastWriterPass = -1;
        SyncState lastWriterSync{};
        std::vector<ReaderUse> readersSinceLastWriter;

        for (uint32_t passIndex = 0; passIndex < passDecls.size(); ++passIndex) {
            const auto it = passUseStates[passIndex].find(resourceId);
            if (it == passUseStates[passIndex].end()) {
                continue;
            }

            const SyncState& currentSync = it->second;
            const bool currentReads = (currentSync.accessMask & (VK_ACCESS_INPUT_ATTACHMENT_READ_BIT |
                VK_ACCESS_SHADER_READ_BIT |
                VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                VK_ACCESS_TRANSFER_READ_BIT)) != 0;
            const bool currentWrites = currentSync.isWrite;

            if (currentReads && lastWriterPass >= 0) {
                addDependency(static_cast<uint32_t>(lastWriterPass), passIndex, lastWriterSync, currentSync);
            }

            if (currentWrites) {
                if (lastWriterPass >= 0) {
                    addDependency(static_cast<uint32_t>(lastWriterPass), passIndex, lastWriterSync, currentSync);
                }
                for (const ReaderUse& reader : readersSinceLastWriter) {
                    addDependency(reader.passIndex, passIndex, reader.sync, currentSync);
                }

                lastWriterPass = static_cast<int32_t>(passIndex);
                lastWriterSync = currentSync;
                readersSinceLastWriter.clear();
            }

            if (currentReads) {
                bool alreadyTracked = false;
                for (const ReaderUse& reader : readersSinceLastWriter) {
                    if (reader.passIndex == passIndex) {
                        alreadyTracked = true;
                        break;
                    }
                }
                if (!alreadyTracked) {
                    readersSinceLastWriter.push_back(ReaderUse{ passIndex, currentSync });
                }
            }
        }
    }

    std::vector<VkSubpassDependency2> dependencies;
    dependencies.reserve(dependencyMap.size() + 2);

    if (!passDecls.empty()) {
        VkSubpassDependency2 externalBegin{};
        externalBegin.sType = VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2;
        externalBegin.srcSubpass = VK_SUBPASS_EXTERNAL;
        externalBegin.dstSubpass = 0;
        externalBegin.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        externalBegin.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        externalBegin.srcAccessMask = 0;
        externalBegin.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        externalBegin.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        dependencies.push_back(externalBegin);
    }

    for (const auto& [_, dependency] : dependencyMap) {
        dependencies.push_back(dependency);
    }

    if (!passDecls.empty()) {
        VkSubpassDependency2 externalEnd{};
        externalEnd.sType = VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2;
        externalEnd.srcSubpass = static_cast<uint32_t>(passDecls.size() - 1);
        externalEnd.dstSubpass = VK_SUBPASS_EXTERNAL;
        externalEnd.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        externalEnd.dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        externalEnd.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        externalEnd.dstAccessMask = 0;
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
        throw std::runtime_error("Failed to create frame graph render pass");
    }
}

void FrameGraph::createImageViews(const VulkanDevice& vulkanDevice, VkFormat swapchainImageFormat, VkExtent2D extent, uint32_t maxFramesInFlight) {
    buildGraph(swapchainImageFormat, extent);

    transientNoAliasBytes = 0;
    transientAliasedBytes = 0;

    for (uint32_t resourceId = 0; resourceId < resourceDecls.size(); ++resourceId) {
        const fg::ResourceDesc& resource = resourceDecls[resourceId];
        if (resource.lifetime == fg::ResourceLifetime::External) {
            continue;
        }

        if (resourceId >= resourceStorages.size()) {
            throw std::runtime_error("Resource storage index out of range");
        }

        ResourceStorage& storage = resourceStorages[resourceId];
        if (resource.type == fg::ResourceType::Image2D) {
            storage.images.assign(maxFramesInFlight, VK_NULL_HANDLE);
            storage.imageMemories.assign(maxFramesInFlight, VK_NULL_HANDLE);
            storage.views.assign(maxFramesInFlight, VK_NULL_HANDLE);
            storage.depthSamplerViews.assign(maxFramesInFlight, VK_NULL_HANDLE);
            storage.stencilSamplerViews.assign(maxFramesInFlight, VK_NULL_HANDLE);
            storage.buffers.clear();
            storage.bufferMemories.clear();
        }
        else if (resource.type == fg::ResourceType::Buffer) {
            storage.buffers.assign(maxFramesInFlight, VK_NULL_HANDLE);
            storage.bufferMemories.assign(maxFramesInFlight, VK_NULL_HANDLE);
            storage.images.clear();
            storage.imageMemories.clear();
            storage.views.clear();
            storage.depthSamplerViews.clear();
            storage.stencilSamplerViews.clear();
        }
    }

    for (uint32_t frameIndex = 0; frameIndex < maxFramesInFlight; ++frameIndex) {
        std::vector<VkMemoryRequirements> imageMemoryRequirements(resourceDecls.size());
        std::vector<VkMemoryRequirements> bufferMemoryRequirements(resourceDecls.size());
        std::vector<bool> imageCreated(resourceDecls.size(), false);
        std::vector<bool> imageMemoryBound(resourceDecls.size(), false);
        std::vector<bool> bufferCreated(resourceDecls.size(), false);
        std::vector<bool> bufferMemoryBound(resourceDecls.size(), false);

        for (uint32_t resourceId = 0; resourceId < resourceDecls.size(); ++resourceId) {
            const fg::ResourceDesc& resource = resourceDecls[resourceId];
            if (resource.lifetime == fg::ResourceLifetime::External) {
                continue;
            }

            ResourceStorage& storage = resourceStorages[resourceId];

            if (resource.type == fg::ResourceType::Image2D) {
                VkImageCreateInfo imageInfo = createImageCreateInfo(
                    extent.width,
                    extent.height,
                    resource.format,
                    VK_IMAGE_TILING_OPTIMAL,
                    resource.imageUsage,
                    resource.samples);

                if (resourceId < aliasGroupByResource.size()) {
                    const int32_t groupIndex = aliasGroupByResource[resourceId];
                    if (groupIndex >= 0 &&
                        static_cast<size_t>(groupIndex) < aliasGroups.size() &&
                        aliasGroups[groupIndex].size() > 1) {
                        imageInfo.flags |= VK_IMAGE_CREATE_ALIAS_BIT;
                    }
                }

                if (vkCreateImage(vulkanDevice.getDevice(), &imageInfo, nullptr, &storage.images[frameIndex]) != VK_SUCCESS) {
                    throw std::runtime_error("Failed to create frame graph image");
                }

                vkGetImageMemoryRequirements(vulkanDevice.getDevice(), storage.images[frameIndex], &imageMemoryRequirements[resourceId]);
                imageCreated[resourceId] = true;
            }
            else if (resource.type == fg::ResourceType::Buffer) {
                VkBufferCreateInfo bufferInfo{};
                bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
                bufferInfo.size = resource.bufferSize;
                bufferInfo.usage = resource.bufferUsage;
                bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

                if (vkCreateBuffer(vulkanDevice.getDevice(), &bufferInfo, nullptr, &storage.buffers[frameIndex]) != VK_SUCCESS) {
                    throw std::runtime_error("Failed to create frame graph buffer");
                }

                vkGetBufferMemoryRequirements(vulkanDevice.getDevice(), storage.buffers[frameIndex], &bufferMemoryRequirements[resourceId]);
                bufferCreated[resourceId] = true;
            }
        }

        for (const std::vector<uint32_t>& group : aliasGroups) {
            if (group.empty()) {
                continue;
            }

            const uint32_t firstResource = group.front();
            if (firstResource >= resourceDecls.size()) {
                continue;
            }

            if (resourceDecls[firstResource].type == fg::ResourceType::Image2D) {
                VkDeviceSize maxSize = 0;
                VkDeviceSize noAliasSize = 0;
                uint32_t compatibleTypeBits = UINT32_MAX;
                bool canAliasGroup = group.size() > 1;
                VkMemoryPropertyFlags memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

                for (uint32_t resourceId : group) {
                    if (!imageCreated[resourceId]) {
                        canAliasGroup = false;
                        continue;
                    }
                    compatibleTypeBits &= imageMemoryRequirements[resourceId].memoryTypeBits;
                    maxSize = std::max(maxSize, imageMemoryRequirements[resourceId].size);
                    noAliasSize += imageMemoryRequirements[resourceId].size;
                    memoryProperties = resourceDecls[resourceId].memoryProperties;
                }

                if (compatibleTypeBits == 0) {
                    canAliasGroup = false;
                }

                if (frameIndex == 0) {
                    transientNoAliasBytes += noAliasSize;
                }

                if (canAliasGroup) {
                    VkMemoryAllocateInfo allocInfo{};
                    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
                    allocInfo.allocationSize = maxSize;
                    allocInfo.memoryTypeIndex = vulkanDevice.findMemoryType(compatibleTypeBits, memoryProperties);

                    VkDeviceMemory sharedMemory = VK_NULL_HANDLE;
                    if (vkAllocateMemory(vulkanDevice.getDevice(), &allocInfo, nullptr, &sharedMemory) != VK_SUCCESS) {
                        throw std::runtime_error("Failed to allocate aliased frame graph image memory");
                    }

                    for (uint32_t resourceId : group) {
                        if (!imageCreated[resourceId]) {
                            continue;
                        }
                        ResourceStorage& storage = resourceStorages[resourceId];
                        storage.imageMemories[frameIndex] = sharedMemory;
                        if (vkBindImageMemory(vulkanDevice.getDevice(), storage.images[frameIndex], sharedMemory, 0) != VK_SUCCESS) {
                            throw std::runtime_error("Failed to bind aliased frame graph image memory");
                        }
                        imageMemoryBound[resourceId] = true;
                    }

                    if (frameIndex == 0) {
                        transientAliasedBytes += maxSize;
                    }
                }
                else {
                    for (uint32_t resourceId : group) {
                        if (!imageCreated[resourceId]) {
                            continue;
                        }
                        ResourceStorage& storage = resourceStorages[resourceId];

                        VkMemoryAllocateInfo allocInfo{};
                        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
                        allocInfo.allocationSize = imageMemoryRequirements[resourceId].size;
                        allocInfo.memoryTypeIndex = vulkanDevice.findMemoryType(
                            imageMemoryRequirements[resourceId].memoryTypeBits,
                            resourceDecls[resourceId].memoryProperties);

                        if (vkAllocateMemory(vulkanDevice.getDevice(), &allocInfo, nullptr, &storage.imageMemories[frameIndex]) != VK_SUCCESS) {
                            throw std::runtime_error("Failed to allocate frame graph image memory");
                        }
                        if (vkBindImageMemory(vulkanDevice.getDevice(), storage.images[frameIndex], storage.imageMemories[frameIndex], 0) != VK_SUCCESS) {
                            throw std::runtime_error("Failed to bind frame graph image memory");
                        }
                        imageMemoryBound[resourceId] = true;

                        if (frameIndex == 0) {
                            transientAliasedBytes += imageMemoryRequirements[resourceId].size;
                        }
                    }
                }
            }
            else if (resourceDecls[firstResource].type == fg::ResourceType::Buffer) {
                VkDeviceSize maxSize = 0;
                VkDeviceSize noAliasSize = 0;
                uint32_t compatibleTypeBits = UINT32_MAX;
                bool canAliasGroup = group.size() > 1;
                VkMemoryPropertyFlags memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

                for (uint32_t resourceId : group) {
                    if (!bufferCreated[resourceId]) {
                        canAliasGroup = false;
                        continue;
                    }
                    compatibleTypeBits &= bufferMemoryRequirements[resourceId].memoryTypeBits;
                    maxSize = std::max(maxSize, bufferMemoryRequirements[resourceId].size);
                    noAliasSize += bufferMemoryRequirements[resourceId].size;
                    memoryProperties = resourceDecls[resourceId].memoryProperties;
                }

                if (compatibleTypeBits == 0) {
                    canAliasGroup = false;
                }

                if (frameIndex == 0) {
                    transientNoAliasBytes += noAliasSize;
                }

                if (canAliasGroup) {
                    VkMemoryAllocateInfo allocInfo{};
                    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
                    allocInfo.allocationSize = maxSize;
                    allocInfo.memoryTypeIndex = vulkanDevice.findMemoryType(compatibleTypeBits, memoryProperties);

                    VkDeviceMemory sharedMemory = VK_NULL_HANDLE;
                    if (vkAllocateMemory(vulkanDevice.getDevice(), &allocInfo, nullptr, &sharedMemory) != VK_SUCCESS) {
                        throw std::runtime_error("Failed to allocate aliased frame graph buffer memory");
                    }

                    for (uint32_t resourceId : group) {
                        if (!bufferCreated[resourceId]) {
                            continue;
                        }
                        ResourceStorage& storage = resourceStorages[resourceId];
                        storage.bufferMemories[frameIndex] = sharedMemory;
                        if (vkBindBufferMemory(vulkanDevice.getDevice(), storage.buffers[frameIndex], sharedMemory, 0) != VK_SUCCESS) {
                            throw std::runtime_error("Failed to bind aliased frame graph buffer memory");
                        }
                        bufferMemoryBound[resourceId] = true;
                    }

                    if (frameIndex == 0) {
                        transientAliasedBytes += maxSize;
                    }
                }
                else {
                    for (uint32_t resourceId : group) {
                        if (!bufferCreated[resourceId]) {
                            continue;
                        }
                        ResourceStorage& storage = resourceStorages[resourceId];

                        VkMemoryAllocateInfo allocInfo{};
                        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
                        allocInfo.allocationSize = bufferMemoryRequirements[resourceId].size;
                        allocInfo.memoryTypeIndex = vulkanDevice.findMemoryType(
                            bufferMemoryRequirements[resourceId].memoryTypeBits,
                            resourceDecls[resourceId].memoryProperties);

                        if (vkAllocateMemory(vulkanDevice.getDevice(), &allocInfo, nullptr, &storage.bufferMemories[frameIndex]) != VK_SUCCESS) {
                            throw std::runtime_error("Failed to allocate frame graph buffer memory");
                        }
                        if (vkBindBufferMemory(vulkanDevice.getDevice(), storage.buffers[frameIndex], storage.bufferMemories[frameIndex], 0) != VK_SUCCESS) {
                            throw std::runtime_error("Failed to bind frame graph buffer memory");
                        }
                        bufferMemoryBound[resourceId] = true;

                        if (frameIndex == 0) {
                            transientAliasedBytes += bufferMemoryRequirements[resourceId].size;
                        }
                    }
                }
            }
        }

        for (uint32_t resourceId = 0; resourceId < resourceDecls.size(); ++resourceId) {
            const fg::ResourceDesc& resource = resourceDecls[resourceId];
            if (resource.lifetime == fg::ResourceLifetime::External) {
                continue;
            }
            ResourceStorage& storage = resourceStorages[resourceId];

            if (resource.type == fg::ResourceType::Image2D) {
                if (!imageCreated[resourceId] || imageMemoryBound[resourceId]) {
                    continue;
                }

                VkMemoryAllocateInfo allocInfo{};
                allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
                allocInfo.allocationSize = imageMemoryRequirements[resourceId].size;
                allocInfo.memoryTypeIndex = vulkanDevice.findMemoryType(
                    imageMemoryRequirements[resourceId].memoryTypeBits,
                    resource.memoryProperties);

                if (vkAllocateMemory(vulkanDevice.getDevice(), &allocInfo, nullptr, &storage.imageMemories[frameIndex]) != VK_SUCCESS) {
                    throw std::runtime_error("Failed to allocate frame graph fallback image memory");
                }
                if (vkBindImageMemory(vulkanDevice.getDevice(), storage.images[frameIndex], storage.imageMemories[frameIndex], 0) != VK_SUCCESS) {
                    throw std::runtime_error("Failed to bind frame graph fallback image memory");
                }
                imageMemoryBound[resourceId] = true;

                if (frameIndex == 0) {
                    transientNoAliasBytes += imageMemoryRequirements[resourceId].size;
                    transientAliasedBytes += imageMemoryRequirements[resourceId].size;
                }
            }
            else if (resource.type == fg::ResourceType::Buffer) {
                if (!bufferCreated[resourceId] || bufferMemoryBound[resourceId]) {
                    continue;
                }

                VkMemoryAllocateInfo allocInfo{};
                allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
                allocInfo.allocationSize = bufferMemoryRequirements[resourceId].size;
                allocInfo.memoryTypeIndex = vulkanDevice.findMemoryType(
                    bufferMemoryRequirements[resourceId].memoryTypeBits,
                    resource.memoryProperties);

                if (vkAllocateMemory(vulkanDevice.getDevice(), &allocInfo, nullptr, &storage.bufferMemories[frameIndex]) != VK_SUCCESS) {
                    throw std::runtime_error("Failed to allocate frame graph fallback buffer memory");
                }
                if (vkBindBufferMemory(vulkanDevice.getDevice(), storage.buffers[frameIndex], storage.bufferMemories[frameIndex], 0) != VK_SUCCESS) {
                    throw std::runtime_error("Failed to bind frame graph fallback buffer memory");
                }
                bufferMemoryBound[resourceId] = true;

                if (frameIndex == 0) {
                    transientNoAliasBytes += bufferMemoryRequirements[resourceId].size;
                    transientAliasedBytes += bufferMemoryRequirements[resourceId].size;
                }
            }
        }

        for (uint32_t resourceId = 0; resourceId < resourceDecls.size(); ++resourceId) {
            const fg::ResourceDesc& resource = resourceDecls[resourceId];
            if (resource.lifetime == fg::ResourceLifetime::External || resource.type != fg::ResourceType::Image2D) {
                continue;
            }
            if (!imageCreated[resourceId]) {
                continue;
            }

            ResourceStorage& storage = resourceStorages[resourceId];
            storage.views[frameIndex] = createImageView(
                vulkanDevice,
                storage.images[frameIndex],
                resource.format,
                resource.viewAspect);

            if (storage.createDepthSamplerViews) {
                storage.depthSamplerViews[frameIndex] = createImageView(
                    vulkanDevice,
                    storage.images[frameIndex],
                    resource.format,
                    VK_IMAGE_ASPECT_DEPTH_BIT);
            }

            if (storage.createStencilSamplerViews) {
                storage.stencilSamplerViews[frameIndex] = createImageView(
                    vulkanDevice,
                    storage.images[frameIndex],
                    resource.format,
                    VK_IMAGE_ASPECT_STENCIL_BIT);
            }
        }
    }
}
void FrameGraph::createFramebuffers(const std::vector<VkImageView>& swapChainImageViews, VkExtent2D extent, uint32_t maxFramesInFlight) {
    cleanupFramebuffers();

    if (swapChainImageViews.empty()) {
        throw std::runtime_error("Swapchain image views array is empty");
    }
    if (attachmentResourceOrder.empty()) {
        throw std::runtime_error("Frame graph attachment order is not initialized");
    }

    swapchainImageCount = static_cast<uint32_t>(swapChainImageViews.size());
    framebuffers.resize(static_cast<size_t>(maxFramesInFlight) * swapchainImageCount, VK_NULL_HANDLE);

    for (uint32_t frameIndex = 0; frameIndex < maxFramesInFlight; ++frameIndex) {
        for (uint32_t swapchainIndex = 0; swapchainIndex < swapchainImageCount; ++swapchainIndex) {
            std::vector<VkImageView> attachments;
            attachments.resize(attachmentResourceOrder.size(), VK_NULL_HANDLE);

            for (size_t attachmentIndex = 0; attachmentIndex < attachmentResourceOrder.size(); ++attachmentIndex) {
                const uint32_t resourceId = attachmentResourceOrder[attachmentIndex];
                const fg::ResourceDesc& resource = resourceDecls.at(resourceId);

                if (resource.lifetime == fg::ResourceLifetime::External) {
                    attachments[attachmentIndex] = swapChainImageViews[swapchainIndex];
                    continue;
                }

                if (resourceId >= resourceStorages.size()) {
                    throw std::runtime_error("Invalid frame graph attachment storage index");
                }
                const ResourceStorage& storage = resourceStorages[resourceId];
                if (frameIndex >= storage.views.size()) {
                    throw std::runtime_error("Invalid frame graph attachment view binding");
                }

                attachments[attachmentIndex] = storage.views[frameIndex];
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
                throw std::runtime_error("Failed to create frame graph framebuffer");
            }
        }
    }
}

void FrameGraph::cleanupFramebuffers() {
    for (VkFramebuffer& framebuffer : framebuffers) {
        if (framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(vulkanDevice.getDevice(), framebuffer, nullptr);
            framebuffer = VK_NULL_HANDLE;
        }
    }
    framebuffers.clear();
    swapchainImageCount = 0;
}

VkFramebuffer FrameGraph::getFramebuffer(uint32_t currentFrame, uint32_t imageIndex) const {
    if (swapchainImageCount == 0) {
        return VK_NULL_HANDLE;
    }

    const size_t framebufferIndex = static_cast<size_t>(currentFrame) * swapchainImageCount + imageIndex;
    if (framebufferIndex >= framebuffers.size()) {
        return VK_NULL_HANDLE;
    }

    return framebuffers[framebufferIndex];
}

void FrameGraph::cleanupImages(VulkanDevice& vulkanDevice, uint32_t maxFramesInFlight) {
    const VkDevice device = vulkanDevice.getDevice();
    vkDeviceWaitIdle(device);
    cleanupFramebuffers();

    for (uint32_t frameIndex = 0; frameIndex < maxFramesInFlight; ++frameIndex) {
        std::unordered_set<VkDeviceMemory> frameMemoriesToFree;

        for (uint32_t resourceId = 0; resourceId < resourceDecls.size(); ++resourceId) {
            const fg::ResourceDesc& resource = resourceDecls[resourceId];
            if (resource.lifetime == fg::ResourceLifetime::External) {
                continue;
            }

            if (resourceId >= resourceStorages.size()) {
                continue;
            }

            ResourceStorage& storage = resourceStorages[resourceId];
            destroyImageViewAt(device, storage.views, frameIndex);
            destroyImageViewAt(device, storage.depthSamplerViews, frameIndex);
            destroyImageViewAt(device, storage.stencilSamplerViews, frameIndex);
            destroyImageAt(device, storage.images, frameIndex);
            destroyBufferAt(device, storage.buffers, frameIndex);

            if (frameIndex < storage.imageMemories.size()) {
                VkDeviceMemory& imageMemory = storage.imageMemories[frameIndex];
                if (imageMemory != VK_NULL_HANDLE) {
                    frameMemoriesToFree.insert(imageMemory);
                    imageMemory = VK_NULL_HANDLE;
                }
            }
            if (frameIndex < storage.bufferMemories.size()) {
                VkDeviceMemory& bufferMemory = storage.bufferMemories[frameIndex];
                if (bufferMemory != VK_NULL_HANDLE) {
                    frameMemoriesToFree.insert(bufferMemory);
                    bufferMemory = VK_NULL_HANDLE;
                }
            }
        }

        for (VkDeviceMemory memory : frameMemoriesToFree) {
            vkFreeMemory(device, memory, nullptr);
        }
    }

    for (uint32_t resourceId = 0; resourceId < resourceDecls.size(); ++resourceId) {
        if (resourceId >= resourceStorages.size()) {
            continue;
        }
        ResourceStorage& storage = resourceStorages[resourceId];
        storage.views.clear();
        storage.depthSamplerViews.clear();
        storage.stencilSamplerViews.clear();
        storage.images.clear();
        storage.imageMemories.clear();
        storage.buffers.clear();
        storage.bufferMemories.clear();
    }
}

void FrameGraph::cleanup(VulkanDevice& vulkanDevice) {
    cleanupFramebuffers();
    if (renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(vulkanDevice.getDevice(), renderPass, nullptr);
        renderPass = VK_NULL_HANDLE;
    }
}

