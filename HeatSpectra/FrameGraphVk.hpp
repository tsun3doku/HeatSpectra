#pragma once

#include <vulkan/vulkan.h>

#include <optional>
#include <vector>

namespace fg {

enum class ResourceType {
    Image2D,
    Buffer
};

enum class ResourceLifetime {
    Transient,
    Persistent,
    External
};

enum class UsageType {
    ColorAttachment,
    DepthStencilAttachment,
    InputAttachment,
    Sampled,
    StorageRead,
    StorageWrite,
    TransferSrc,
    TransferDst,
    Present
};

struct ResourceDesc {
    uint32_t id = 0;
    const char* name = "";
    ResourceType type = ResourceType::Image2D;
    ResourceLifetime lifetime = ResourceLifetime::Transient;
    bool renderPassAttachment = true;
    bool useSwapchainFormat = false;
    bool finalOutput = false;

    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    VkImageUsageFlags imageUsage = 0;
    VkBufferUsageFlags bufferUsage = 0;
    VkDeviceSize bufferSize = 0;
    VkMemoryPropertyFlags memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    VkImageAspectFlags viewAspect = VK_IMAGE_ASPECT_COLOR_BIT;

    VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    VkAttachmentStoreOp storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    VkAttachmentLoadOp stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    VkAttachmentStoreOp stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout finalLayout = VK_IMAGE_LAYOUT_UNDEFINED;
};

struct AttachmentRef {
    uint32_t resourceId = 0;
    std::optional<uint32_t> layer;
    std::optional<uint32_t> face;
    std::optional<VkImageAspectFlags> aspectMask;
    std::optional<VkImageLayout> layout;
    std::optional<VkClearValue> clearValue;
};

struct ResourceUse {
    uint32_t resourceId = 0;
    UsageType usage = UsageType::Sampled;
    bool write = false;
};

struct PassDesc {
    uint32_t id = 0;
    const char* name = "";

    std::vector<AttachmentRef> colors;
    std::vector<AttachmentRef> resolves;
    std::vector<AttachmentRef> inputs;
    std::optional<AttachmentRef> depthStencil;
    std::optional<AttachmentRef> depthResolve;
    bool depthReadOnly = false;

    std::vector<ResourceUse> uses;
};

} // namespace fg
