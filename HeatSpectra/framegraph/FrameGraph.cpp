#include <utility>
#include <iostream>
#include <string>

#include "FrameGraph.hpp"

FrameGraph::FrameGraph() {
}

FrameGraph::~FrameGraph() {
}

void FrameGraph::clearGraphDesc() {
    registeredResourceDecls.clear();
    registeredPassDecls.clear();
    resourceDecls.clear();
    passDecls.clear();
    resourceIdByName.clear();
    passIdByName.clear();
    attachmentResourceOrder.clear();
    passSyncEdges.clear();
    frameGraphResult = {};
    transientNoAliasBytes = 0;
    transientAliasedBytes = 0;
}

framegraph::ResourceId FrameGraph::addImageResource(framegraph::ImageResourceCreateInfo createInfo) {
    if (createInfo.name.empty()) {
        std::cerr << "[FrameGraph] Image resource has empty name" << std::endl;
        return framegraph::ResourceId{};
    }

    ResourceDescription desc{};
    desc.name = createInfo.name;
    desc.lifetime = createInfo.lifetime;
    desc.isAttachment = createInfo.isAttachment;
    desc.useSwapchainFormat = createInfo.useSwapchainFormat;
    desc.isGraphOutput = (createInfo.lifetime == framegraph::ResourceLifetime::External);
    desc.format = createInfo.format;
    desc.samples = createInfo.samples;
    desc.imageUsage = createInfo.imageUsage;
    desc.memoryProperties = createInfo.memoryProperties;
    desc.viewAspect = createInfo.viewAspect;
    desc.loadOp = createInfo.ops.loadOp;
    desc.storeOp = createInfo.ops.storeOp;
    desc.stencilLoadOp = createInfo.ops.stencilLoadOp;
    desc.stencilStoreOp = createInfo.ops.stencilStoreOp;
    desc.initialLayout = createInfo.initialLayout;
    desc.finalLayout = createInfo.finalLayout;
    return addResourceDesc(std::move(desc));
}

framegraph::ResourceId FrameGraph::addResourceDesc(ResourceDescription desc) {
    frameGraphResult = {};
    desc.id = framegraph::ResourceId(static_cast<uint32_t>(registeredResourceDecls.size()));
    registeredResourceDecls.push_back(std::move(desc));
    return framegraph::ResourceId(static_cast<uint32_t>(registeredResourceDecls.size() - 1));
}

void FrameGraph::addPassDesc(framegraph::PassDescription passDesc) {
    frameGraphResult = {};
    passDesc.id = framegraph::PassId(static_cast<uint32_t>(registeredPassDecls.size()));
    registeredPassDecls.push_back(std::move(passDesc));
}

framegraph::ResourceId FrameGraph::getResourceId(std::string_view resourceName) const {
    if (resourceName.empty()) {
        return framegraph::ResourceId{};
    }

    auto it = resourceIdByName.find(std::string(resourceName));
    if (it == resourceIdByName.end()) {
        return framegraph::ResourceId{};
    }
    return it->second;
}

framegraph::PassId FrameGraph::getPassId(std::string_view passName) const {
    if (passName.empty()) {
        return framegraph::PassId{};
    }

    auto it = passIdByName.find(std::string(passName));
    if (it == passIdByName.end()) {
        return framegraph::PassId{};
    }
    return it->second;
}

bool FrameGraph::compile(framegraph::ImageFormat swapchainImageFormat, framegraph::Extent2D extent) {
    frameGraphResult = {};
    if (!buildGraph(swapchainImageFormat, extent)) {
        return false;
    }

    rebuildFrameGraphResult();
    return true;
}

const framegraph::FrameGraphResult& FrameGraph::getFrameGraphResult() const {
    return frameGraphResult;
}

void FrameGraph::rebuildFrameGraphResult() {
    frameGraphResult = {};
    frameGraphResult.orderedPasses = passDecls;
    frameGraphResult.passSyncEdges = passSyncEdges;
    frameGraphResult.resources = resourceDecls;
    frameGraphResult.attachmentResourceOrder = attachmentResourceOrder;
    frameGraphResult.aliasGroupByResource = aliasGroupByResource;
    frameGraphResult.aliasGroups = aliasGroups;
    frameGraphResult.transientNoAliasBytes = transientNoAliasBytes;
    frameGraphResult.transientAliasedBytes = transientAliasedBytes;
}

std::vector<std::string> FrameGraph::getPassNames() const {
    std::vector<std::string> names;
    const std::vector<framegraph::PassDescription>& orderedPassesDesc =
        frameGraphResult.orderedPasses.empty() ? passDecls : frameGraphResult.orderedPasses;
    names.reserve(orderedPassesDesc.size());
    for (const framegraph::PassDescription& passDesc : orderedPassesDesc) {
        if (!passDesc.name.empty()) {
            names.emplace_back(passDesc.name);
        }
    }
    return names;
}


