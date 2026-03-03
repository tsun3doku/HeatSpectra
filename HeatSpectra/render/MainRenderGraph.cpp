#include "MainRenderGraph.hpp"

#include <optional>
#include <string_view>
#include <utility>

#include "framegraph/FrameGraph.hpp"
#include "framegraph/FrameGraphPasses.hpp"
#include "framegraph/FrameGraphResources.hpp"

static framegraph::AttachmentOps makeAttachmentOps(
    framegraph::AttachmentLoadOp loadOp,
    framegraph::AttachmentStoreOp storeOp,
    framegraph::AttachmentLoadOp stencilLoadOp = framegraph::AttachmentLoadOp::DontCare,
    framegraph::AttachmentStoreOp stencilStoreOp = framegraph::AttachmentStoreOp::DontCare) {
    framegraph::AttachmentOps ops{};
    ops.loadOp = loadOp;
    ops.storeOp = storeOp;
    ops.stencilLoadOp = stencilLoadOp;
    ops.stencilStoreOp = stencilStoreOp;
    return ops;
}

static framegraph::ResourceId addColorImage(
    FrameGraph& frameGraph,
    std::string_view name,
    framegraph::ImageFormat format,
    bool useSwapchainFormat,
    framegraph::SampleCount samples,
    framegraph::ImageUsage usage,
    const framegraph::AttachmentOps& ops,
    framegraph::ResourceLayout finalLayout,
    framegraph::ResourceLifetime lifetime = framegraph::ResourceLifetime::Transient,
    framegraph::ResourceLayout initialLayout = framegraph::ResourceLayout::Undefined) {
    framegraph::ImageResourceCreateInfo createInfo{};
    createInfo.name = name;
    createInfo.lifetime = lifetime;
    createInfo.format = format;
    createInfo.useSwapchainFormat = useSwapchainFormat;
    createInfo.samples = samples;
    createInfo.imageUsage = usage;
    createInfo.viewAspect = framegraph::ImageAspect::Color;
    createInfo.ops = ops;
    createInfo.initialLayout = initialLayout;
    createInfo.finalLayout = finalLayout;
    return frameGraph.addImageResource(std::move(createInfo));
}

static framegraph::ResourceId addDepthStencilImage(
    FrameGraph& frameGraph,
    std::string_view name,
    framegraph::ImageFormat format,
    bool useSwapchainFormat,
    framegraph::SampleCount samples,
    framegraph::ImageUsage usage,
    const framegraph::AttachmentOps& ops,
    framegraph::ResourceLayout finalLayout,
    framegraph::ResourceLifetime lifetime = framegraph::ResourceLifetime::Transient,
    framegraph::ResourceLayout initialLayout = framegraph::ResourceLayout::Undefined) {
    framegraph::ImageResourceCreateInfo createInfo{};
    createInfo.name = name;
    createInfo.lifetime = lifetime;
    createInfo.format = format;
    createInfo.useSwapchainFormat = useSwapchainFormat;
    createInfo.samples = samples;
    createInfo.imageUsage = usage;
    createInfo.viewAspect = framegraph::ImageAspect::Depth | framegraph::ImageAspect::Stencil;
    createInfo.ops = ops;
    createInfo.initialLayout = initialLayout;
    createInfo.finalLayout = finalLayout;
    return frameGraph.addImageResource(std::move(createInfo));
}

static framegraph::ResourceId addSwapchainOutput(FrameGraph& frameGraph, std::string_view name) {
    return addColorImage(
        frameGraph,
        name,
        framegraph::ImageFormat::Undefined,
        true,
        framegraph::SampleCount::Count1,
        framegraph::ImageUsage::ColorAttachment | framegraph::ImageUsage::Sampled,
        makeAttachmentOps(framegraph::AttachmentLoadOp::Clear, framegraph::AttachmentStoreOp::Store),
        framegraph::ResourceLayout::ShaderReadOnly,
        framegraph::ResourceLifetime::External);
}

static framegraph::AttachmentReference makeRef(
    framegraph::ResourceId resourceId,
    std::optional<framegraph::ImageAspect> aspectMask = std::nullopt,
    std::optional<framegraph::ResourceLayout> layout = std::nullopt) {
    framegraph::AttachmentReference ref{ resourceId };
    ref.aspectMask = aspectMask;
    ref.layout = layout;
    return ref;
}

void MainRenderGraph::buildMainRenderGraph(FrameGraph& frameGraph) {
    frameGraph.clearGraphDesc();

    const framegraph::ResourceId resAlbedoMSAA = addColorImage(
        frameGraph,
        framegraph::resources::AlbedoMSAA,
        framegraph::ImageFormat::R8G8B8A8Unorm,
        false,
        framegraph::SampleCount::Count8,
        framegraph::ImageUsage::ColorAttachment | framegraph::ImageUsage::InputAttachment,
        makeAttachmentOps(framegraph::AttachmentLoadOp::Clear, framegraph::AttachmentStoreOp::DontCare),
        framegraph::ResourceLayout::ColorAttachment);

    const framegraph::ResourceId resNormalMSAA = addColorImage(
        frameGraph,
        framegraph::resources::NormalMSAA,
        framegraph::ImageFormat::R16G16B16A16Sfloat,
        false,
        framegraph::SampleCount::Count8,
        framegraph::ImageUsage::ColorAttachment | framegraph::ImageUsage::InputAttachment,
        makeAttachmentOps(framegraph::AttachmentLoadOp::Clear, framegraph::AttachmentStoreOp::DontCare),
        framegraph::ResourceLayout::ColorAttachment);

    const framegraph::ResourceId resPositionMSAA = addColorImage(
        frameGraph,
        framegraph::resources::PositionMSAA,
        framegraph::ImageFormat::R16G16B16A16Sfloat,
        false,
        framegraph::SampleCount::Count8,
        framegraph::ImageUsage::ColorAttachment | framegraph::ImageUsage::InputAttachment,
        makeAttachmentOps(framegraph::AttachmentLoadOp::Clear, framegraph::AttachmentStoreOp::DontCare),
        framegraph::ResourceLayout::ColorAttachment);

    const framegraph::ResourceId resDepthMSAA = addDepthStencilImage(
        frameGraph,
        framegraph::resources::DepthMSAA,
        framegraph::ImageFormat::D32SfloatS8Uint,
        false,
        framegraph::SampleCount::Count8,
        framegraph::ImageUsage::DepthStencilAttachment |
            framegraph::ImageUsage::InputAttachment |
            framegraph::ImageUsage::Sampled,
        makeAttachmentOps(
            framegraph::AttachmentLoadOp::Clear,
            framegraph::AttachmentStoreOp::Store,
            framegraph::AttachmentLoadOp::Clear,
            framegraph::AttachmentStoreOp::Store),
        framegraph::ResourceLayout::General);

    const framegraph::ResourceId resAlbedoResolve = addColorImage(
        frameGraph,
        framegraph::resources::AlbedoResolve,
        framegraph::ImageFormat::R8G8B8A8Unorm,
        false,
        framegraph::SampleCount::Count1,
        framegraph::ImageUsage::ColorAttachment | framegraph::ImageUsage::InputAttachment,
        makeAttachmentOps(framegraph::AttachmentLoadOp::DontCare, framegraph::AttachmentStoreOp::Store),
        framegraph::ResourceLayout::ShaderReadOnly);

    const framegraph::ResourceId resNormalResolve = addColorImage(
        frameGraph,
        framegraph::resources::NormalResolve,
        framegraph::ImageFormat::R16G16B16A16Sfloat,
        false,
        framegraph::SampleCount::Count1,
        framegraph::ImageUsage::ColorAttachment | framegraph::ImageUsage::InputAttachment,
        makeAttachmentOps(framegraph::AttachmentLoadOp::DontCare, framegraph::AttachmentStoreOp::Store),
        framegraph::ResourceLayout::ShaderReadOnly);

    const framegraph::ResourceId resPositionResolve = addColorImage(
        frameGraph,
        framegraph::resources::PositionResolve,
        framegraph::ImageFormat::R16G16B16A16Sfloat,
        false,
        framegraph::SampleCount::Count1,
        framegraph::ImageUsage::ColorAttachment | framegraph::ImageUsage::InputAttachment,
        makeAttachmentOps(framegraph::AttachmentLoadOp::DontCare, framegraph::AttachmentStoreOp::Store),
        framegraph::ResourceLayout::ShaderReadOnly);

    const framegraph::ResourceId resDepthResolve = addDepthStencilImage(
        frameGraph,
        framegraph::resources::DepthResolve,
        framegraph::ImageFormat::D32SfloatS8Uint,
        false,
        framegraph::SampleCount::Count1,
        framegraph::ImageUsage::DepthStencilAttachment |
            framegraph::ImageUsage::Sampled |
            framegraph::ImageUsage::InputAttachment |
            framegraph::ImageUsage::TransferSrc,
        makeAttachmentOps(framegraph::AttachmentLoadOp::DontCare, framegraph::AttachmentStoreOp::Store),
        framegraph::ResourceLayout::General);

    const framegraph::ResourceId resLightingMSAA = addColorImage(
        frameGraph,
        framegraph::resources::LightingMSAA,
        framegraph::ImageFormat::Undefined,
        true,
        framegraph::SampleCount::Count8,
        framegraph::ImageUsage::ColorAttachment | framegraph::ImageUsage::TransientAttachment,
        makeAttachmentOps(framegraph::AttachmentLoadOp::Clear, framegraph::AttachmentStoreOp::Store),
        framegraph::ResourceLayout::ColorAttachment);

    const framegraph::ResourceId resLineMSAA = addColorImage(
        frameGraph,
        framegraph::resources::LineMSAA,
        framegraph::ImageFormat::Undefined,
        true,
        framegraph::SampleCount::Count8,
        framegraph::ImageUsage::ColorAttachment | framegraph::ImageUsage::TransientAttachment,
        makeAttachmentOps(framegraph::AttachmentLoadOp::Clear, framegraph::AttachmentStoreOp::DontCare),
        framegraph::ResourceLayout::ColorAttachment);

    const framegraph::ResourceId resLineResolve = addColorImage(
        frameGraph,
        framegraph::resources::LineResolve,
        framegraph::ImageFormat::Undefined,
        true,
        framegraph::SampleCount::Count1,
        framegraph::ImageUsage::ColorAttachment | framegraph::ImageUsage::InputAttachment,
        makeAttachmentOps(framegraph::AttachmentLoadOp::DontCare, framegraph::AttachmentStoreOp::Store),
        framegraph::ResourceLayout::ShaderReadOnly);

    const framegraph::ResourceId resLightingResolve = addColorImage(
        frameGraph,
        framegraph::resources::LightingResolve,
        framegraph::ImageFormat::Undefined,
        true,
        framegraph::SampleCount::Count1,
        framegraph::ImageUsage::ColorAttachment | framegraph::ImageUsage::InputAttachment,
        makeAttachmentOps(framegraph::AttachmentLoadOp::DontCare, framegraph::AttachmentStoreOp::Store),
        framegraph::ResourceLayout::ShaderReadOnly);

    const framegraph::ResourceId resSwapchain = addSwapchainOutput(frameGraph, framegraph::resources::Swapchain);

    const framegraph::ResourceId resSurfaceMSAA = addColorImage(
        frameGraph,
        framegraph::resources::SurfaceMSAA,
        framegraph::ImageFormat::Undefined,
        true,
        framegraph::SampleCount::Count8,
        framegraph::ImageUsage::ColorAttachment | framegraph::ImageUsage::TransientAttachment,
        makeAttachmentOps(framegraph::AttachmentLoadOp::Clear, framegraph::AttachmentStoreOp::DontCare),
        framegraph::ResourceLayout::ColorAttachment);

    const framegraph::ResourceId resSurfaceResolve = addColorImage(
        frameGraph,
        framegraph::resources::SurfaceResolve,
        framegraph::ImageFormat::Undefined,
        true,
        framegraph::SampleCount::Count1,
        framegraph::ImageUsage::ColorAttachment | framegraph::ImageUsage::InputAttachment,
        makeAttachmentOps(framegraph::AttachmentLoadOp::DontCare, framegraph::AttachmentStoreOp::Store),
        framegraph::ResourceLayout::ShaderReadOnly);

    framegraph::PassDescription geometryPass{};
    geometryPass.name = framegraph::passes::Geometry;
    geometryPass.colors = {
        makeRef(resAlbedoMSAA),
        makeRef(resNormalMSAA),
        makeRef(resPositionMSAA),
    };
    geometryPass.resolves = {
        makeRef(resAlbedoResolve),
        makeRef(resNormalResolve),
        makeRef(resPositionResolve),
    };
    geometryPass.depthStencil = makeRef(resDepthMSAA);
    frameGraph.addPassDesc(std::move(geometryPass));

    framegraph::PassDescription lightingPass{};
    lightingPass.name = framegraph::passes::Lighting;
    lightingPass.inputs = {
        makeRef(resAlbedoResolve),
        makeRef(resNormalResolve),
        makeRef(resPositionResolve),
        makeRef(
            resDepthResolve,
            framegraph::ImageAspect::Depth,
            framegraph::ResourceLayout::DepthStencilReadOnly),
        makeRef(
            resDepthMSAA,
            framegraph::ImageAspect::Stencil,
            framegraph::ResourceLayout::DepthStencilReadOnly),
    };
    lightingPass.colors = { makeRef(resLightingMSAA) };
    lightingPass.resolves = { makeRef(resLightingResolve) };
    lightingPass.depthStencil = makeRef(
        resDepthMSAA,
        framegraph::ImageAspect::Depth | framegraph::ImageAspect::Stencil,
        framegraph::ResourceLayout::DepthStencilReadOnly);
    lightingPass.depthReadOnly = true;
    frameGraph.addPassDesc(std::move(lightingPass));

    framegraph::PassDescription overlayPass{};
    overlayPass.name = framegraph::passes::Overlay;
    overlayPass.colors = {
        makeRef(resSurfaceMSAA),
        makeRef(resLineMSAA),
    };
    overlayPass.resolves = {
        makeRef(resSurfaceResolve),
        makeRef(resLineResolve),
    };
    overlayPass.depthStencil = makeRef(resDepthMSAA, std::nullopt, framegraph::ResourceLayout::General);
    overlayPass.depthResolve = makeRef(
        resDepthResolve,
        framegraph::ImageAspect::Depth | framegraph::ImageAspect::Stencil,
        framegraph::ResourceLayout::General);
    frameGraph.addPassDesc(std::move(overlayPass));

    framegraph::PassDescription blendPass{};
    blendPass.name = framegraph::passes::Blend;
    blendPass.inputs = {
        makeRef(resSurfaceResolve),
        makeRef(resLineResolve),
        makeRef(resLightingResolve),
        makeRef(resAlbedoResolve),
    };
    blendPass.colors = { makeRef(resSwapchain) };
    frameGraph.addPassDesc(std::move(blendPass));
}

