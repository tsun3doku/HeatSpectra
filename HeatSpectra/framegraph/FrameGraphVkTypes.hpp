#pragma once

#include <vulkan/vulkan.h>

#include <iostream>

#include "FrameGraphTypes.hpp"

namespace framegraph::vk {

inline framegraph::ImageFormat toFrameGraphFormat(VkFormat format) {
    switch (format) {
    case VK_FORMAT_UNDEFINED:
        return framegraph::ImageFormat::Undefined;
    case VK_FORMAT_R8G8B8A8_UNORM:
        return framegraph::ImageFormat::R8G8B8A8Unorm;
    case VK_FORMAT_R8G8B8A8_SRGB:
        return framegraph::ImageFormat::R8G8B8A8Srgb;
    case VK_FORMAT_B8G8R8A8_UNORM:
        return framegraph::ImageFormat::B8G8R8A8Unorm;
    case VK_FORMAT_B8G8R8A8_SRGB:
        return framegraph::ImageFormat::B8G8R8A8Srgb;
    case VK_FORMAT_R16G16B16A16_SFLOAT:
        return framegraph::ImageFormat::R16G16B16A16Sfloat;
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        return framegraph::ImageFormat::D32SfloatS8Uint;
    default:
        std::cerr << "[FrameGraphVkTypes] Unsupported Vulkan format for framegraph conversion" << std::endl;
        return framegraph::ImageFormat::Undefined;
    }
}

inline VkFormat toVkFormat(framegraph::ImageFormat format) {
    switch (format) {
    case framegraph::ImageFormat::Undefined:
        return VK_FORMAT_UNDEFINED;
    case framegraph::ImageFormat::R8G8B8A8Unorm:
        return VK_FORMAT_R8G8B8A8_UNORM;
    case framegraph::ImageFormat::R8G8B8A8Srgb:
        return VK_FORMAT_R8G8B8A8_SRGB;
    case framegraph::ImageFormat::B8G8R8A8Unorm:
        return VK_FORMAT_B8G8R8A8_UNORM;
    case framegraph::ImageFormat::B8G8R8A8Srgb:
        return VK_FORMAT_B8G8R8A8_SRGB;
    case framegraph::ImageFormat::R16G16B16A16Sfloat:
        return VK_FORMAT_R16G16B16A16_SFLOAT;
    case framegraph::ImageFormat::D32SfloatS8Uint:
        return VK_FORMAT_D32_SFLOAT_S8_UINT;
    }

    std::cerr << "[FrameGraphVkTypes] Unsupported framegraph format for Vulkan conversion" << std::endl;
    return VK_FORMAT_UNDEFINED;
}

inline framegraph::Extent2D toFrameGraphExtent(VkExtent2D extent) {
    framegraph::Extent2D out{};
    out.width = extent.width;
    out.height = extent.height;
    return out;
}

inline VkExtent2D toVkExtent(framegraph::Extent2D extent) {
    VkExtent2D out{};
    out.width = extent.width;
    out.height = extent.height;
    return out;
}

inline VkSampleCountFlagBits toVkSampleCount(framegraph::SampleCount sampleCount) {
    switch (sampleCount) {
    case framegraph::SampleCount::Count1:
        return VK_SAMPLE_COUNT_1_BIT;
    case framegraph::SampleCount::Count2:
        return VK_SAMPLE_COUNT_2_BIT;
    case framegraph::SampleCount::Count4:
        return VK_SAMPLE_COUNT_4_BIT;
    case framegraph::SampleCount::Count8:
        return VK_SAMPLE_COUNT_8_BIT;
    case framegraph::SampleCount::Count16:
        return VK_SAMPLE_COUNT_16_BIT;
    case framegraph::SampleCount::Count32:
        return VK_SAMPLE_COUNT_32_BIT;
    case framegraph::SampleCount::Count64:
        return VK_SAMPLE_COUNT_64_BIT;
    }

    std::cerr << "[FrameGraphVkTypes] Unsupported sample count for Vulkan conversion" << std::endl;
    return VK_SAMPLE_COUNT_1_BIT;
}

inline VkImageUsageFlags toVkImageUsage(framegraph::ImageUsage usage) {
    VkImageUsageFlags flags = 0;
    if (framegraph::hasAny(usage, framegraph::ImageUsage::ColorAttachment)) {
        flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }
    if (framegraph::hasAny(usage, framegraph::ImageUsage::DepthStencilAttachment)) {
        flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    }
    if (framegraph::hasAny(usage, framegraph::ImageUsage::InputAttachment)) {
        flags |= VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
    }
    if (framegraph::hasAny(usage, framegraph::ImageUsage::Sampled)) {
        flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
    }
    if (framegraph::hasAny(usage, framegraph::ImageUsage::Storage)) {
        flags |= VK_IMAGE_USAGE_STORAGE_BIT;
    }
    if (framegraph::hasAny(usage, framegraph::ImageUsage::TransferSrc)) {
        flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }
    if (framegraph::hasAny(usage, framegraph::ImageUsage::TransferDst)) {
        flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }
    if (framegraph::hasAny(usage, framegraph::ImageUsage::TransientAttachment)) {
        flags |= VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
    }
    return flags;
}

inline VkMemoryPropertyFlags toVkMemoryProperties(framegraph::MemoryProperty properties) {
    VkMemoryPropertyFlags flags = 0;
    if (framegraph::hasAny(properties, framegraph::MemoryProperty::DeviceLocal)) {
        flags |= VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    }
    if (framegraph::hasAny(properties, framegraph::MemoryProperty::HostVisible)) {
        flags |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    }
    if (framegraph::hasAny(properties, framegraph::MemoryProperty::HostCoherent)) {
        flags |= VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    }
    if (framegraph::hasAny(properties, framegraph::MemoryProperty::HostCached)) {
        flags |= VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    }
    if (framegraph::hasAny(properties, framegraph::MemoryProperty::LazilyAllocated)) {
        flags |= VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT;
    }
    return flags;
}

inline VkImageAspectFlags toVkImageAspect(framegraph::ImageAspect aspect) {
    VkImageAspectFlags flags = 0;
    if (framegraph::hasAny(aspect, framegraph::ImageAspect::Color)) {
        flags |= VK_IMAGE_ASPECT_COLOR_BIT;
    }
    if (framegraph::hasAny(aspect, framegraph::ImageAspect::Depth)) {
        flags |= VK_IMAGE_ASPECT_DEPTH_BIT;
    }
    if (framegraph::hasAny(aspect, framegraph::ImageAspect::Stencil)) {
        flags |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    return flags;
}

inline VkAttachmentLoadOp toVkAttachmentLoadOp(framegraph::AttachmentLoadOp op) {
    switch (op) {
    case framegraph::AttachmentLoadOp::Load:
        return VK_ATTACHMENT_LOAD_OP_LOAD;
    case framegraph::AttachmentLoadOp::Clear:
        return VK_ATTACHMENT_LOAD_OP_CLEAR;
    case framegraph::AttachmentLoadOp::DontCare:
        return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    }
    std::cerr << "[FrameGraphVkTypes] Unsupported attachment load op for Vulkan conversion" << std::endl;
    return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
}

inline VkAttachmentStoreOp toVkAttachmentStoreOp(framegraph::AttachmentStoreOp op) {
    switch (op) {
    case framegraph::AttachmentStoreOp::Store:
        return VK_ATTACHMENT_STORE_OP_STORE;
    case framegraph::AttachmentStoreOp::DontCare:
        return VK_ATTACHMENT_STORE_OP_DONT_CARE;
    }
    std::cerr << "[FrameGraphVkTypes] Unsupported attachment store op for Vulkan conversion" << std::endl;
    return VK_ATTACHMENT_STORE_OP_DONT_CARE;
}

inline VkImageLayout toVkImageLayout(framegraph::ResourceLayout layout) {
    switch (layout) {
    case framegraph::ResourceLayout::Undefined:
        return VK_IMAGE_LAYOUT_UNDEFINED;
    case framegraph::ResourceLayout::General:
        return VK_IMAGE_LAYOUT_GENERAL;
    case framegraph::ResourceLayout::ColorAttachment:
        return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    case framegraph::ResourceLayout::DepthStencilAttachment:
        return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    case framegraph::ResourceLayout::DepthStencilReadOnly:
        return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    case framegraph::ResourceLayout::ShaderReadOnly:
        return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    case framegraph::ResourceLayout::TransferSrc:
        return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    case framegraph::ResourceLayout::TransferDst:
        return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    case framegraph::ResourceLayout::PresentSrc:
        return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }

    std::cerr << "[FrameGraphVkTypes] Unsupported layout for Vulkan conversion" << std::endl;
    return VK_IMAGE_LAYOUT_UNDEFINED;
}

}
