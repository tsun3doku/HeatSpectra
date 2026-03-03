#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace framegraph {

using SizeBytes = uint64_t;

enum class ResourceLifetime {
    Transient,
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

enum class ImageFormat : uint32_t {
    Undefined = 0,
    R8G8B8A8Unorm,
    R8G8B8A8Srgb,
    B8G8R8A8Unorm,
    B8G8R8A8Srgb,
    R16G16B16A16Sfloat,
    D32SfloatS8Uint
};

enum class SampleCount : uint32_t {
    Count1 = 1,
    Count2 = 2,
    Count4 = 4,
    Count8 = 8,
    Count16 = 16,
    Count32 = 32,
    Count64 = 64
};

enum class ImageUsage : uint32_t {
    None = 0,
    ColorAttachment = 1u << 0,
    DepthStencilAttachment = 1u << 1,
    InputAttachment = 1u << 2,
    Sampled = 1u << 3,
    Storage = 1u << 4,
    TransferSrc = 1u << 5,
    TransferDst = 1u << 6,
    TransientAttachment = 1u << 7
};

enum class MemoryProperty : uint32_t {
    None = 0,
    DeviceLocal = 1u << 0,
    HostVisible = 1u << 1,
    HostCoherent = 1u << 2,
    HostCached = 1u << 3,
    LazilyAllocated = 1u << 4
};

enum class ImageAspect : uint32_t {
    None = 0,
    Color = 1u << 0,
    Depth = 1u << 1,
    Stencil = 1u << 2
};

enum class AttachmentLoadOp : uint8_t {
    Load,
    Clear,
    DontCare
};

enum class AttachmentStoreOp : uint8_t {
    Store,
    DontCare
};

enum class ResourceLayout : uint8_t {
    Undefined,
    General,
    ColorAttachment,
    DepthStencilAttachment,
    DepthStencilReadOnly,
    ShaderReadOnly,
    TransferSrc,
    TransferDst,
    PresentSrc
};

struct Extent2D {
    uint32_t width = 0;
    uint32_t height = 0;
};

#define FRAMEGRAPH_DEFINE_FLAG_OPERATORS(EnumType) \
    inline constexpr EnumType operator|(EnumType lhs, EnumType rhs) { \
        using Underlying = std::underlying_type_t<EnumType>; \
        return static_cast<EnumType>(static_cast<Underlying>(lhs) | static_cast<Underlying>(rhs)); \
    } \
    inline constexpr EnumType operator&(EnumType lhs, EnumType rhs) { \
        using Underlying = std::underlying_type_t<EnumType>; \
        return static_cast<EnumType>(static_cast<Underlying>(lhs) & static_cast<Underlying>(rhs)); \
    } \
    inline constexpr EnumType& operator|=(EnumType& lhs, EnumType rhs) { \
        lhs = lhs | rhs; \
        return lhs; \
    } \
    inline constexpr bool hasAny(EnumType value, EnumType mask) { \
        using Underlying = std::underlying_type_t<EnumType>; \
        return (static_cast<Underlying>(value) & static_cast<Underlying>(mask)) != 0; \
    }

FRAMEGRAPH_DEFINE_FLAG_OPERATORS(ImageUsage)
FRAMEGRAPH_DEFINE_FLAG_OPERATORS(MemoryProperty)
FRAMEGRAPH_DEFINE_FLAG_OPERATORS(ImageAspect)

#undef FRAMEGRAPH_DEFINE_FLAG_OPERATORS

struct ResourceId {
    uint32_t value = UINT32_MAX;

    constexpr ResourceId() = default;
    constexpr ResourceId(uint32_t idValue) : value(idValue) {
    }

    constexpr bool isValid() const {
        return value != UINT32_MAX;
    }

    constexpr operator uint32_t() const {
        return value;
    }
};

struct PassId {
    uint32_t value = UINT32_MAX;

    constexpr PassId() = default;
    constexpr PassId(uint32_t idValue) : value(idValue) {
    }

    constexpr bool isValid() const {
        return value != UINT32_MAX;
    }

    constexpr operator uint32_t() const {
        return value;
    }
};

inline constexpr uint32_t toIndex(ResourceId id) {
    return id.value;
}

inline constexpr uint32_t toIndex(PassId id) {
    return id.value;
}

struct AttachmentOps {
    AttachmentLoadOp loadOp = AttachmentLoadOp::DontCare;
    AttachmentStoreOp storeOp = AttachmentStoreOp::DontCare;
    AttachmentLoadOp stencilLoadOp = AttachmentLoadOp::DontCare;
    AttachmentStoreOp stencilStoreOp = AttachmentStoreOp::DontCare;
};

struct ImageResourceCreateInfo {
    std::string_view name{};
    ResourceLifetime lifetime = ResourceLifetime::Transient;
    bool isAttachment = true;
    bool useSwapchainFormat = false;
    ImageFormat format = ImageFormat::Undefined;
    SampleCount samples = SampleCount::Count1;
    ImageUsage imageUsage = ImageUsage::None;
    MemoryProperty memoryProperties = MemoryProperty::DeviceLocal;
    ImageAspect viewAspect = ImageAspect::Color;
    AttachmentOps ops{};
    ResourceLayout initialLayout = ResourceLayout::Undefined;
    ResourceLayout finalLayout = ResourceLayout::Undefined;
};

struct AttachmentReference {
    ResourceId resourceId{};
    std::optional<ImageAspect> aspectMask;
    std::optional<ResourceLayout> layout;
};

struct ResourceUse {
    ResourceId resourceId{};
    UsageType usage = UsageType::Sampled;
    bool write = false;
};

struct PassDescription {
    PassId id{};
    std::string name;

    std::vector<AttachmentReference> colors;
    std::vector<AttachmentReference> resolves;
    std::vector<AttachmentReference> inputs;
    std::optional<AttachmentReference> depthStencil;
    std::optional<AttachmentReference> depthResolve;
    bool depthReadOnly = false;

    std::vector<ResourceUse> additionalUses;
};

struct PassSyncEdge {
    PassId srcPass{};
    PassId dstPass{};
    ResourceId resourceId{};
    UsageType srcUsage = UsageType::Sampled;
    UsageType dstUsage = UsageType::Sampled;
    bool srcWrite = false;
    bool dstWrite = false;
};

struct ResourceDefinition {
    ResourceId id{};
    std::string name;
    ResourceLifetime lifetime = ResourceLifetime::Transient;
    bool isAttachment = true;
    bool useSwapchainFormat = false;
    bool isGraphOutput = false;

    ImageFormat format = ImageFormat::Undefined;
    Extent2D extent{};
    SampleCount samples = SampleCount::Count1;
    ImageUsage imageUsage = ImageUsage::None;
    MemoryProperty memoryProperties = MemoryProperty::DeviceLocal;
    ImageAspect viewAspect = ImageAspect::Color;

    AttachmentLoadOp loadOp = AttachmentLoadOp::DontCare;
    AttachmentStoreOp storeOp = AttachmentStoreOp::DontCare;
    AttachmentLoadOp stencilLoadOp = AttachmentLoadOp::DontCare;
    AttachmentStoreOp stencilStoreOp = AttachmentStoreOp::DontCare;
    ResourceLayout initialLayout = ResourceLayout::Undefined;
    ResourceLayout finalLayout = ResourceLayout::Undefined;
};

struct FrameGraphResult {
    std::vector<PassDescription> orderedPasses;
    std::vector<PassSyncEdge> passSyncEdges;
    std::vector<ResourceDefinition> resources;
    std::vector<ResourceId> attachmentResourceOrder;
    std::vector<int32_t> aliasGroupByResource;
    std::vector<std::vector<uint32_t>> aliasGroups;
    SizeBytes transientNoAliasBytes = 0;
    SizeBytes transientAliasedBytes = 0;
};

}
