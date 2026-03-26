#include "voronoi/VoronoiSurfaceRuntime.hpp"

#include "runtime/RuntimeIntrinsicCache.hpp"
#include "scene/Model.hpp"
#include "vulkan/CommandBufferManager.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/VulkanDevice.hpp"
#include "voronoi/VoronoiModelRuntime.hpp"

#include <algorithm>

VoronoiSurfaceRuntime::~VoronoiSurfaceRuntime() = default;

bool VoronoiSurfaceRuntime::initializeGeometryBindings(
    VulkanDevice& vulkanDevice,
    MemoryAllocator& memoryAllocator,
    const RuntimeIntrinsicCache& intrinsicCache,
    const VoronoiPackage& voronoiPackage,
    const std::vector<std::unique_ptr<VoronoiModelRuntime>>& modelRuntimes) {
    cleanup();

    const size_t geometryCount = std::min(voronoiPackage.receiverRuntimeModelIds.size(), modelRuntimes.size());
    geometryRuntimes.reserve(geometryCount);

    for (size_t index = 0; index < geometryCount; ++index) {
        const auto& modelRuntime = modelRuntimes[index];
        if (!modelRuntime) {
            continue;
        }

        Model* const model = &modelRuntime->getModel();
        const GeometryData& geometry = voronoiPackage.receiverGeometries[index];
        if (!model || geometry.intrinsicHandle.key == 0) {
            continue;
        }

        const RuntimeIntrinsicCache::Entry* intrinsicEntry = intrinsicCache.get(geometry.intrinsicHandle);
        if (!intrinsicEntry) {
            continue;
        }

        auto geometryRuntime = std::make_unique<VoronoiGeometryRuntime>(
            vulkanDevice,
            memoryAllocator,
            *model,
            geometry,
            voronoiPackage.receiverIntrinsics[index],
            *intrinsicEntry);
        geometryRuntime->setVoronoiMapping(
            modelRuntime->getVoronoiMappingBuffer(),
            modelRuntime->getVoronoiMappingBufferOffset());
        if (!geometryRuntime->createSurfaceBuffers() || !geometryRuntime->initializeSurfaceBuffer()) {
            geometryRuntime->cleanup();
            return false;
        }

        geometryRuntimes.push_back(std::move(geometryRuntime));
    }

    return true;
}

void VoronoiSurfaceRuntime::executeBufferTransfers(CommandPool& renderCommandPool) {
    VkCommandBuffer copyCmd = renderCommandPool.beginCommands();
    for (auto& geometryRuntime : geometryRuntimes) {
        if (geometryRuntime) {
            geometryRuntime->executeBufferTransfers(copyCmd);
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

    for (auto& geometryRuntime : geometryRuntimes) {
        if (geometryRuntime) {
            geometryRuntime->cleanupStagingBuffers();
        }
    }
}

void VoronoiSurfaceRuntime::cleanup() {
    for (auto& geometryRuntime : geometryRuntimes) {
        if (geometryRuntime) {
            geometryRuntime->cleanup();
        }
    }

    geometryRuntimes.clear();
}
