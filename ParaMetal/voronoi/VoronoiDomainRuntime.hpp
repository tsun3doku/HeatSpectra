#pragma once

#include <cstdint>
#include <vulkan/vulkan.h>

class VoronoiDomainRuntime {
public:
    virtual ~VoronoiDomainRuntime() = default;

    virtual bool isPointDomain() const = 0;
    virtual bool createVoronoiBuffers() = 0;
    virtual void cleanup() = 0;

    virtual VkBuffer getCandidateBuffer() const = 0;
    virtual VkDeviceSize getCandidateBufferOffset() const = 0;

    virtual VkBuffer getSurfaceBuffer() const { return VK_NULL_HANDLE; }
    virtual VkDeviceSize getSurfaceBufferOffset() const { return 0; }
    virtual VkBuffer getTriangleIndicesBuffer() const { return VK_NULL_HANDLE; }
    virtual VkDeviceSize getTriangleIndicesBufferOffset() const { return 0; }
    virtual VkBuffer getGMLSSurfaceStencilBuffer() const { return VK_NULL_HANDLE; }
    virtual VkDeviceSize getGMLSSurfaceStencilBufferOffset() const { return 0; }
    virtual VkBuffer getGMLSSurfaceWeightBuffer() const { return VK_NULL_HANDLE; }
    virtual VkDeviceSize getGMLSSurfaceWeightBufferOffset() const { return 0; }
    virtual size_t getGMLSSurfaceWeightCount() const { return 0; }
    virtual VkBuffer getGMLSSurfaceGradientWeightBuffer() const { return VK_NULL_HANDLE; }
    virtual VkDeviceSize getGMLSSurfaceGradientWeightBufferOffset() const { return 0; }
    virtual size_t getGMLSSurfaceGradientWeightCount() const { return 0; }
    virtual uint32_t getRuntimeModelId() const { return 0; }
};
