#include "voronoi/VoronoiSurfaceRuntime.hpp"

#include "vulkan/CommandBufferManager.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/VulkanDevice.hpp"
#include "voronoi/VoronoiModelRuntime.hpp"

#include <algorithm>
#include <unordered_map>

VoronoiSurfaceRuntime::~VoronoiSurfaceRuntime() = default;

bool VoronoiSurfaceRuntime::initializeGeometryBindings(
    VulkanDevice& vulkanDevice,
    MemoryAllocator& memoryAllocator,
    const std::vector<std::vector<VoronoiGeometryRuntime::SurfaceVertex>>& receiverSurfaceVertices,
    const std::vector<std::vector<uint32_t>>& receiverIntrinsicTriangleIndices,
    const std::vector<uint32_t>& receiverRuntimeModelIds,
    const std::vector<std::unique_ptr<VoronoiModelRuntime>>& modelRuntimes) {
    cleanup();

    std::unordered_map<uint32_t, const VoronoiModelRuntime*> modelRuntimesById;
    modelRuntimesById.reserve(modelRuntimes.size());
    for (const auto& modelRuntime : modelRuntimes) {
        if (!modelRuntime) {
            continue;
        }

        modelRuntimesById[modelRuntime->getRuntimeModelId()] = modelRuntime.get();
    }

    const size_t geometryCount = std::min({ receiverRuntimeModelIds.size(), receiverSurfaceVertices.size(), receiverIntrinsicTriangleIndices.size() });
    geometryRuntimes.reserve(geometryCount);

    for (size_t index = 0; index < geometryCount; ++index) {
        const uint32_t runtimeModelId = receiverRuntimeModelIds[index];
        const auto modelRuntimeIt = modelRuntimesById.find(runtimeModelId);
        if (runtimeModelId == 0 || modelRuntimeIt == modelRuntimesById.end() || !modelRuntimeIt->second) {
            continue;
        }
        const VoronoiModelRuntime* modelRuntime = modelRuntimeIt->second;

        auto geometryRuntime = std::make_unique<VoronoiGeometryRuntime>(
            vulkanDevice,
            memoryAllocator,
            runtimeModelId,
            receiverSurfaceVertices[index],
            receiverIntrinsicTriangleIndices[index],
            modelRuntime->getSupportingHalfedgeView(),
            modelRuntime->getSupportingAngleView(),
            modelRuntime->getHalfedgeView(),
            modelRuntime->getEdgeView(),
            modelRuntime->getTriangleView(),
            modelRuntime->getLengthView(),
            modelRuntime->getInputHalfedgeView(),
            modelRuntime->getInputEdgeView(),
            modelRuntime->getInputTriangleView(),
            modelRuntime->getInputLengthView());
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
