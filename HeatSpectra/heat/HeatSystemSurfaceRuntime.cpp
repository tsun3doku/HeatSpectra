#include "HeatSystemSurfaceRuntime.hpp"

#include "HeatReceiverRuntime.hpp"
#include "HeatSystemSimRuntime.hpp"
#include "vulkan/CommandBufferManager.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/VulkanDevice.hpp"

#include <algorithm>
#include <iostream>

HeatSystemSurfaceRuntime::~HeatSystemSurfaceRuntime() = default;

void HeatSystemSurfaceRuntime::setReceiverPayloads(
    const std::vector<GeometryData>& receiverGeometries,
    const std::vector<SupportingHalfedge::IntrinsicMesh>& receiverIntrinsicMeshes,
    const std::vector<uint32_t>& receiverRuntimeModelIds,
    const std::vector<VkBufferView>& supportingHalfedgeViews,
    const std::vector<VkBufferView>& supportingAngleViews,
    const std::vector<VkBufferView>& halfedgeViews,
    const std::vector<VkBufferView>& edgeViews,
    const std::vector<VkBufferView>& triangleViews,
    const std::vector<VkBufferView>& lengthViews,
    const std::vector<VkBufferView>& inputHalfedgeViews,
    const std::vector<VkBufferView>& inputEdgeViews,
    const std::vector<VkBufferView>& inputTriangleViews,
    const std::vector<VkBufferView>& inputLengthViews) {
    activeReceiverGeometries = receiverGeometries;
    activeReceiverIntrinsicMeshes = receiverIntrinsicMeshes;
    activeReceiverRuntimeModelIds = receiverRuntimeModelIds;
    activeSupportingHalfedgeViews = supportingHalfedgeViews;
    activeSupportingAngleViews = supportingAngleViews;
    activeHalfedgeViews = halfedgeViews;
    activeEdgeViews = edgeViews;
    activeTriangleViews = triangleViews;
    activeLengthViews = lengthViews;
    activeInputHalfedgeViews = inputHalfedgeViews;
    activeInputEdgeViews = inputEdgeViews;
    activeInputTriangleViews = inputTriangleViews;
    activeInputLengthViews = inputLengthViews;
    receiverBindingsDirty = true;
}

bool HeatSystemSurfaceRuntime::ensureReceiverBindings(
    VulkanDevice& vulkanDevice,
    MemoryAllocator& memoryAllocator) {
    if (!receiverBindingsDirty) {
        return true;
    }

    cleanup();

    const size_t receiverCount = std::min({
        activeReceiverGeometries.size(),
        activeReceiverIntrinsicMeshes.size(),
        activeReceiverRuntimeModelIds.size(),
        activeSupportingHalfedgeViews.size(),
        activeSupportingAngleViews.size(),
        activeHalfedgeViews.size(),
        activeEdgeViews.size(),
        activeTriangleViews.size(),
        activeLengthViews.size(),
        activeInputHalfedgeViews.size(),
        activeInputEdgeViews.size(),
        activeInputTriangleViews.size(),
        activeInputLengthViews.size()
    });
    receiverRuntimes.reserve(receiverCount);

    for (size_t index = 0; index < receiverCount; ++index) {
        const GeometryData& geometry = activeReceiverGeometries[index];
        const uint32_t runtimeModelId = activeReceiverRuntimeModelIds[index];
        if (runtimeModelId == 0 ||
            activeSupportingHalfedgeViews[index] == VK_NULL_HANDLE ||
            activeSupportingAngleViews[index] == VK_NULL_HANDLE ||
            activeHalfedgeViews[index] == VK_NULL_HANDLE ||
            activeEdgeViews[index] == VK_NULL_HANDLE ||
            activeTriangleViews[index] == VK_NULL_HANDLE ||
            activeLengthViews[index] == VK_NULL_HANDLE ||
            activeInputHalfedgeViews[index] == VK_NULL_HANDLE ||
            activeInputEdgeViews[index] == VK_NULL_HANDLE ||
            activeInputTriangleViews[index] == VK_NULL_HANDLE ||
            activeInputLengthViews[index] == VK_NULL_HANDLE) {
            continue;
        }

        auto receiverRuntime = std::make_unique<HeatReceiverRuntime>(
            vulkanDevice,
            memoryAllocator,
            runtimeModelId,
            geometry,
            activeReceiverIntrinsicMeshes[index],
            activeSupportingHalfedgeViews[index],
            activeSupportingAngleViews[index],
            activeHalfedgeViews[index],
            activeEdgeViews[index],
            activeTriangleViews[index],
            activeLengthViews[index],
            activeInputHalfedgeViews[index],
            activeInputEdgeViews[index],
            activeInputTriangleViews[index],
            activeInputLengthViews[index]);
        if (!receiverRuntime->createReceiverBuffers() || !receiverRuntime->initializeReceiverBuffer()) {
            receiverRuntime->cleanup();
            return false;
        }
        receiverRuntimes.push_back(std::move(receiverRuntime));
    }

    receiverBindingsDirty = false;
    return true;
}

void HeatSystemSurfaceRuntime::refreshDescriptors(
    const HeatSystemSimRuntime& simRuntime,
    VkDescriptorSetLayout surfaceLayout,
    VkDescriptorPool surfacePool,
    bool forceReallocate) {
    const uint32_t nodeCount = simRuntime.getNodeCount();
    for (auto& receiverRuntime : receiverRuntimes) {
        if (!receiverRuntime) {
            continue;
        }

        if (forceReallocate) {
            receiverRuntime->updateDescriptors(
                surfaceLayout,
                surfacePool,
                simRuntime.getTempBufferA(),
                simRuntime.getTempBufferAOffset(),
                simRuntime.getTempBufferB(),
                simRuntime.getTempBufferBOffset(),
                simRuntime.getTimeBuffer(),
                simRuntime.getTimeBufferOffset(),
                nodeCount,
                true);
            continue;
        }

        receiverRuntime->updateDescriptors(
            surfaceLayout,
            surfacePool,
            simRuntime.getTempBufferA(),
            simRuntime.getTempBufferAOffset(),
            simRuntime.getTempBufferB(),
            simRuntime.getTempBufferBOffset(),
            simRuntime.getTimeBuffer(),
            simRuntime.getTimeBufferOffset(),
            nodeCount);
    }
}

void HeatSystemSurfaceRuntime::executeBufferTransfers(CommandPool& renderCommandPool) {
    VkCommandBuffer copyCmd = renderCommandPool.beginCommands();
    for (auto& receiverRuntime : receiverRuntimes) {
        if (receiverRuntime) {
            receiverRuntime->executeBufferTransfers(copyCmd);
        }
    }

    VkMemoryBarrier memBarrier{};
    memBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    memBarrier.dstAccessMask =
        VK_ACCESS_SHADER_READ_BIT |
        VK_ACCESS_SHADER_WRITE_BIT |
        VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;

    vkCmdPipelineBarrier(
        copyCmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
            VK_PIPELINE_STAGE_VERTEX_INPUT_BIT |
            VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
            VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT |
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        1,
        &memBarrier,
        0,
        nullptr,
        0,
        nullptr);

    renderCommandPool.endCommands(copyCmd);

    for (auto& receiverRuntime : receiverRuntimes) {
        if (receiverRuntime) {
            receiverRuntime->cleanupStagingBuffers();
        }
    }
}

bool HeatSystemSurfaceRuntime::resetSurfaceTemperatures(CommandPool& renderCommandPool) {
    bool hasResetUploads = false;
    for (auto& receiverRuntime : receiverRuntimes) {
        if (!receiverRuntime) {
            continue;
        }

        if (!receiverRuntime->resetSurfaceTemp()) {
            std::cerr << "[HeatSystemSurfaceRuntime] Failed to reset surface temperatures for model "
                      << receiverRuntime->getRuntimeModelId() << std::endl;
            continue;
        }

        hasResetUploads = true;
    }

    if (hasResetUploads) {
        executeBufferTransfers(renderCommandPool);
    }
    return hasResetUploads;
}

void HeatSystemSurfaceRuntime::cleanup() {
    for (auto& receiverRuntime : receiverRuntimes) {
        if (receiverRuntime) {
            receiverRuntime->cleanup();
        }
    }
    receiverRuntimes.clear();
    receiverBindingsDirty = true;
}
