#pragma once

#include <vulkan/vulkan.h>

#include "voronoi/VoronoiResources.hpp"

class VoronoiSystemResources {
public:
    VoronoiSystemResources() = default;
    ~VoronoiSystemResources() = default;

    VoronoiResources voronoi;

    VkDescriptorPool surfaceDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout surfaceDescriptorSetLayout = VK_NULL_HANDLE;

    VkPipelineLayout surfacePipelineLayout = VK_NULL_HANDLE;
    VkPipeline surfacePipeline = VK_NULL_HANDLE;
};
