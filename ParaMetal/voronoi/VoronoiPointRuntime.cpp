#include "VoronoiPointRuntime.hpp"

#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/VulkanBuffer.hpp"
#include "vulkan/VulkanDevice.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>

VoronoiPointRuntime::VoronoiPointRuntime(
    VulkanDevice& vulkanDevice,
    MemoryAllocator& memoryAllocator,
    uint64_t domainKey,
    std::vector<glm::vec4> positions,
    std::vector<uint32_t> boundaryConditions,
    std::vector<float> fixedTemperatures,
    CommandPool& renderCommandPool)
    : vulkanDevice(vulkanDevice),
      memoryAllocator(memoryAllocator),
      domainKey(domainKey),
      positions(std::move(positions)),
      boundaryConditions(std::move(boundaryConditions)),
      fixedTemperatures(std::move(fixedTemperatures)),
      renderCommandPool(renderCommandPool) {
    computeAabb();
}

VoronoiPointRuntime::~VoronoiPointRuntime() = default;

void VoronoiPointRuntime::computeAabb() {
    if (positions.empty()) {
        aabbMin = glm::vec3(-0.5f);
        aabbMax = glm::vec3(0.5f);
        return;
    }

    aabbMin = glm::vec3(positions.front());
    aabbMax = aabbMin;
    for (const glm::vec4& position : positions) {
        const glm::vec3 p(position);
        aabbMin = glm::min(aabbMin, p);
        aabbMax = glm::max(aabbMax, p);
    }

    float padding = 0.0f;
    if (positions.size() > 1) {
        double totalNearest = 0.0;
        for (size_t i = 0; i < positions.size(); ++i) {
            float nearestSq = std::numeric_limits<float>::max();
            const glm::vec3 a(positions[i]);
            for (size_t j = 0; j < positions.size(); ++j) {
                if (i == j) {
                    continue;
                }
                const glm::vec3 b(positions[j]);
                nearestSq = std::min(nearestSq, glm::dot(b - a, b - a));
            }
            if (nearestSq < std::numeric_limits<float>::max()) {
                totalNearest += std::sqrt(nearestSq);
            }
        }
        padding = static_cast<float>(totalNearest / static_cast<double>(positions.size()));
    }

    if (padding <= 1e-6f) {
        const glm::vec3 extent = aabbMax - aabbMin;
        padding = std::max(std::max(extent.x, extent.y), extent.z);
        if (padding <= 1e-6f) {
            padding = 0.05f;
        }
    }

    aabbMin -= glm::vec3(padding);
    aabbMax += glm::vec3(padding);
}

bool VoronoiPointRuntime::createVoronoiBuffers() {
    if (positions.empty()) {
        return false;
    }

    constexpr uint32_t K_CANDIDATES = 64;
    const VkDeviceSize candidateBufferSize = sizeof(uint32_t) * positions.size() * K_CANDIDATES;
    const VkDeviceSize storageAlignment = vulkanDevice.getPhysicalDeviceProperties().limits.minStorageBufferOffsetAlignment;

    freeBuffer(memoryAllocator, candidateBuffer, candidateBufferOffset);

    std::vector<uint32_t> candidateInitData(positions.size() * K_CANDIDATES, 0xFFFFFFFFu);
    if (uploadDeviceBuffer(
            memoryAllocator,
            renderCommandPool,
            candidateInitData.data(),
            candidateBufferSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            storageAlignment,
            candidateBuffer,
            candidateBufferOffset) != VK_SUCCESS) {
        cleanup();
        return false;
    }

    return true;
}

void VoronoiPointRuntime::cleanup() {
    candidateBuffer = VK_NULL_HANDLE;
    candidateBufferOffset = 0;
}
