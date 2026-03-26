#include "HeatSystemSurfaceRuntime.hpp"

#include "HeatReceiverRuntime.hpp"
#include "HeatSystemSimRuntime.hpp"
#include "runtime/RuntimeIntrinsicCache.hpp"
#include "vulkan/CommandBufferManager.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/VulkanDevice.hpp"
#include "voronoi/VoronoiModelRuntime.hpp"

#include <algorithm>
#include <iostream>

HeatSystemSurfaceRuntime::~HeatSystemSurfaceRuntime() = default;

bool HeatSystemSurfaceRuntime::initializeReceiverBindings(
    VulkanDevice& vulkanDevice,
    MemoryAllocator& memoryAllocator,
    const RuntimeIntrinsicCache& intrinsicCache,
    const HeatPackage& heatPackage,
    const std::vector<std::unique_ptr<VoronoiModelRuntime>>* voronoiModelRuntimes) {
    cleanup();

    const size_t receiverCount = heatPackage.receiverGeometries.size();
    receiverRuntimes.reserve(receiverCount);

    for (size_t index = 0; index < receiverCount; ++index) {
        const GeometryData& geometry = heatPackage.receiverGeometries[index];
        const uint32_t runtimeModelId = heatPackage.receiverRuntimeModelIds[index];
        if (runtimeModelId == 0 || geometry.intrinsicHandle.key == 0) {
            continue;
        }

        const RuntimeIntrinsicCache::Entry* intrinsicEntry = intrinsicCache.get(geometry.intrinsicHandle);
        if (!intrinsicEntry) {
            continue;
        }

        auto receiverRuntime = std::make_unique<HeatReceiverRuntime>(
            vulkanDevice,
            memoryAllocator,
            runtimeModelId,
            geometry,
            heatPackage.receiverIntrinsics[index],
            *intrinsicEntry);
        if (voronoiModelRuntimes) {
            for (const auto& modelRuntime : *voronoiModelRuntimes) {
                if (!modelRuntime || modelRuntime->getRuntimeModelId() != runtimeModelId) {
                    continue;
                }

                receiverRuntime->setVoronoiMapping(
                    modelRuntime->getVoronoiMappingBuffer(),
                    modelRuntime->getVoronoiMappingBufferOffset());
                break;
            }
        }
        if (!receiverRuntime->createReceiverBuffers() || !receiverRuntime->initializeReceiverBuffer()) {
            receiverRuntime->cleanup();
            return false;
        }
        receiverRuntimes.push_back(std::move(receiverRuntime));
    }

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
            receiverRuntime->recreateDescriptors(
                surfaceLayout,
                surfacePool,
                simRuntime.getTempBufferA(),
                simRuntime.getTempBufferAOffset(),
                simRuntime.getTempBufferB(),
                simRuntime.getTempBufferBOffset(),
                simRuntime.getTimeBuffer(),
                simRuntime.getTimeBufferOffset(),
                nodeCount);
        } else {
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
}
