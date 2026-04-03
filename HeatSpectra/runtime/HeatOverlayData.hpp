#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>

#include "heat/HeatDefaults.hpp"

struct HeatOverlayData {
    uint32_t runtimeModelId = 0;
    VkBufferView surfaceBufferView = VK_NULL_HANDLE;
    uint32_t intrinsicVertexCount = 0;
    float sourceTemperature = defaultSourceTemperature;

    VkBufferView supportingHalfedgeView = VK_NULL_HANDLE;
    VkBufferView supportingAngleView = VK_NULL_HANDLE;
    VkBufferView halfedgeView = VK_NULL_HANDLE;
    VkBufferView edgeView = VK_NULL_HANDLE;
    VkBufferView triangleView = VK_NULL_HANDLE;
    VkBufferView lengthView = VK_NULL_HANDLE;
    VkBufferView inputHalfedgeView = VK_NULL_HANDLE;
    VkBufferView inputEdgeView = VK_NULL_HANDLE;
    VkBufferView inputTriangleView = VK_NULL_HANDLE;
    VkBufferView inputLengthView = VK_NULL_HANDLE;

    VkBufferView sourceBufferView = VK_NULL_HANDLE;
    uint32_t sourceVertexCount = 0;
};
