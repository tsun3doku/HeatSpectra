#include "HeatContactRuntime.hpp"

#include "ContactSampling.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/VulkanBuffer.hpp"
#include "vulkan/VulkanDevice.hpp"

#include <algorithm>
#include <iostream>

namespace {

bool recreateBuffer(
    VulkanDevice& vulkanDevice,
    MemoryAllocator& memoryAllocator,
    VkBuffer& buffer,
    VkDeviceSize& offset,
    const void* data,
    VkDeviceSize size) {
    if (buffer != VK_NULL_HANDLE) {
        memoryAllocator.free(buffer, offset);
        buffer = VK_NULL_HANDLE;
        offset = 0;
    }

    if (data == nullptr || size == 0) {
        return false;
    }

    void* mappedData = nullptr;
    return createStorageBuffer(
               memoryAllocator,
               vulkanDevice,
               data,
               size,
               buffer,
               offset,
               &mappedData,
               true) == VK_SUCCESS &&
        buffer != VK_NULL_HANDLE;
}

}

bool HeatContactRuntime::areContactProductsEqual(
    const std::vector<ContactProduct>& lhs,
    const std::vector<ContactProduct>& rhs) {
    return lhs == rhs;
}

const HeatSystemRuntime::SourceBinding* HeatContactRuntime::findSourceBindingByRuntimeModelId(
    const std::vector<HeatSystemRuntime::SourceBinding>& sourceBindings,
    uint32_t runtimeModelId) const {
    if (runtimeModelId == 0) {
        return nullptr;
    }

    for (const HeatSystemRuntime::SourceBinding& sourceBinding : sourceBindings) {
        if (sourceBinding.runtimeModelId == runtimeModelId && sourceBinding.heatSource) {
            return &sourceBinding;
        }
    }
    return nullptr;
}

uint32_t HeatContactRuntime::findReceiverIndexByRuntimeModelId(
    const std::vector<uint32_t>& receiverRuntimeModelIds,
    uint32_t runtimeModelId) const {
    if (runtimeModelId == 0) {
        return std::numeric_limits<uint32_t>::max();
    }

    for (uint32_t index = 0; index < receiverRuntimeModelIds.size(); ++index) {
        if (receiverRuntimeModelIds[index] == runtimeModelId) {
            return index;
        }
    }
    return std::numeric_limits<uint32_t>::max();
}

void HeatContactRuntime::setContactCouplings(
    const std::vector<uint32_t>& receiverRuntimeModelIds,
    const std::vector<ContactProduct>& contactProducts) {
    if (activeReceiverRuntimeModelIds == receiverRuntimeModelIds &&
        areContactProductsEqual(activeContactProducts, contactProducts)) {
        std::cerr << "[HeatContactRuntime] setContactCouplings unchanged"
                  << " receivers=" << receiverRuntimeModelIds.size()
                  << " products=" << contactProducts.size()
                  << std::endl;
        return;
    }

    activeReceiverRuntimeModelIds = receiverRuntimeModelIds;
    activeContactProducts = contactProducts;
    couplingsDirty = true;

    std::cerr << "[HeatContactRuntime] setContactCouplings updated"
              << " receivers=" << activeReceiverRuntimeModelIds.size()
              << " products=" << activeContactProducts.size()
              << std::endl;
    for (const ContactProduct& product : activeContactProducts) {
        std::cerr << "[HeatContactRuntime]   product"
                  << " emitterRuntimeModelId=" << product.emitterRuntimeModelId
                  << " receiverRuntimeModelId=" << product.receiverRuntimeModelId
                  << " pairCount=" << product.contactPairCount
                  << " receiverTriangles=" << product.receiverTriangleIndices.size()
                  << std::endl;
    }
}

bool HeatContactRuntime::ensureCouplings(
    VulkanDevice& vulkanDevice,
    MemoryAllocator& memoryAllocator,
    const std::vector<HeatSystemRuntime::SourceBinding>& sourceBindings,
    const std::unordered_map<uint32_t, std::vector<uint32_t>>& receiverSurfaceCellIndicesByModelId,
    const std::unordered_map<uint32_t, VkBuffer>& receiverSurfaceMappingBufferByModelId,
    const std::unordered_map<uint32_t, VkDeviceSize>& receiverSurfaceMappingBufferOffsetByModelId) {
    if (!couplingsDirty) {
        std::cerr << "[HeatContactRuntime] ensureCouplings skipped: couplings not dirty" << std::endl;
        return true;
    }

    clearCouplings(memoryAllocator);

    std::cerr << "[HeatContactRuntime] ensureCouplings rebuilding"
              << " receiverRuntimeModelIds=" << activeReceiverRuntimeModelIds.size()
              << " sourceBindings=" << sourceBindings.size()
              << " contactProducts=" << activeContactProducts.size()
              << std::endl;

    for (const ContactProduct& productCoupling : activeContactProducts) {
        const uint32_t receiverIndex = findReceiverIndexByRuntimeModelId(
            activeReceiverRuntimeModelIds,
            productCoupling.receiverRuntimeModelId);
        if (receiverIndex == std::numeric_limits<uint32_t>::max()) {
            std::cerr << "[HeatContactRuntime]   skipping product: receiver model not found"
                      << " receiverRuntimeModelId=" << productCoupling.receiverRuntimeModelId
                      << std::endl;
            continue;
        }
        const auto receiverCellsIt = receiverSurfaceCellIndicesByModelId.find(productCoupling.receiverRuntimeModelId);
        if (receiverCellsIt == receiverSurfaceCellIndicesByModelId.end()) {
            std::cerr << "[HeatContactRuntime]   skipping product: receiver surface cells missing"
                      << " receiverRuntimeModelId=" << productCoupling.receiverRuntimeModelId
                      << std::endl;
            continue;
        }

        ContactCoupling coupling{};
        coupling.couplingType = productCoupling.couplingType;
        coupling.emitterModelId = productCoupling.emitterRuntimeModelId;
        coupling.receiverModelId = productCoupling.receiverRuntimeModelId;
        coupling.receiverIndex = receiverIndex;
        coupling.params = HeatContactParams{};

        if (productCoupling.couplingType == ContactCouplingType::SourceToReceiver) {
            const HeatSystemRuntime::SourceBinding* sourceBinding =
                findSourceBindingByRuntimeModelId(sourceBindings, productCoupling.emitterRuntimeModelId);
            if (!sourceBinding) {
                std::cerr << "[HeatContactRuntime]   skipping product: source binding missing"
                          << " emitterRuntimeModelId=" << productCoupling.emitterRuntimeModelId
                          << std::endl;
                continue;
            }
        } else {
            const uint32_t emitterReceiverIndex =
                findReceiverIndexByRuntimeModelId(activeReceiverRuntimeModelIds, productCoupling.emitterRuntimeModelId);
            if (emitterReceiverIndex == std::numeric_limits<uint32_t>::max() ||
                emitterReceiverIndex == receiverIndex) {
                std::cerr << "[HeatContactRuntime]   skipping receiver-to-receiver product"
                          << " emitterRuntimeModelId=" << productCoupling.emitterRuntimeModelId
                          << " receiverRuntimeModelId=" << productCoupling.receiverRuntimeModelId
                          << " emitterReceiverIndex=" << emitterReceiverIndex
                          << " receiverIndex=" << receiverIndex
                          << std::endl;
                continue;
            }
            coupling.emitterReceiverIndex = emitterReceiverIndex;
        }

        if (!rebuildCouplingBuffers(
                vulkanDevice,
                memoryAllocator,
                coupling,
                productCoupling,
                receiverCellsIt->second,
                receiverSurfaceMappingBufferByModelId,
                receiverSurfaceMappingBufferOffsetByModelId)) {
            std::cerr << "[HeatContactRuntime]   rebuildCouplingBuffers failed"
                      << " emitterRuntimeModelId=" << productCoupling.emitterRuntimeModelId
                      << " receiverRuntimeModelId=" << productCoupling.receiverRuntimeModelId
                      << std::endl;
            clearCouplings(memoryAllocator);
            return false;
        }

        std::cerr << "[HeatContactRuntime]   built coupling"
                  << " type=" << static_cast<uint32_t>(coupling.couplingType)
                  << " emitterRuntimeModelId=" << coupling.emitterModelId
                  << " receiverRuntimeModelId=" << coupling.receiverModelId
                  << " receiverIndex=" << coupling.receiverIndex
                  << " sampleCount=" << coupling.contactSampleCount
                  << " cellMapCount=" << coupling.contactCellMapCount
                  << " cellRangeCount=" << coupling.contactCellRangeCount
                  << std::endl;
        contactCouplings.push_back(std::move(coupling));
    }

    std::cerr << "[HeatContactRuntime] ensureCouplings complete"
              << " builtCouplingCount=" << contactCouplings.size()
              << std::endl;
    couplingsDirty = false;
    return true;
}

bool HeatContactRuntime::rebuildCouplingBuffers(
    VulkanDevice& vulkanDevice,
    MemoryAllocator& memoryAllocator,
    ContactCoupling& coupling,
    const ContactProduct& productCoupling,
    const std::vector<uint32_t>& receiverCellIndices,
    const std::unordered_map<uint32_t, VkBuffer>& receiverSurfaceMappingBufferByModelId,
    const std::unordered_map<uint32_t, VkDeviceSize>& receiverSurfaceMappingBufferOffsetByModelId) const {
    if (productCoupling.mappedContactPairs == nullptr || productCoupling.contactPairCount == 0) {
        std::cerr << "[HeatContactRuntime]   rebuildCouplingBuffers abort: missing mapped pairs"
                  << " pairCount=" << productCoupling.contactPairCount
                  << std::endl;
        return false;
    }

    const auto& triangleIndices = productCoupling.receiverTriangleIndices;
    const std::size_t triangleCount = triangleIndices.size() / 3;
    const std::size_t contactPairCount = std::min<std::size_t>(productCoupling.contactPairCount, triangleCount);
    if (contactPairCount == 0 || receiverCellIndices.empty()) {
        std::cerr << "[HeatContactRuntime]   rebuildCouplingBuffers abort: no usable contact triangles"
                  << " pairCount=" << productCoupling.contactPairCount
                  << " receiverTriangleCount=" << triangleCount
                  << " receiverCellCount=" << receiverCellIndices.size()
                  << std::endl;
        return false;
    }

    std::vector<ContactSampleGPU> samples;
    samples.reserve(contactPairCount * Quadrature::count);

    std::vector<ContactCellWeight> cellWeights;
    cellWeights.reserve(contactPairCount * Quadrature::count * 3);

    for (std::size_t triangleIndex = 0; triangleIndex < contactPairCount; ++triangleIndex) {
        const ContactPair& contactPair = productCoupling.mappedContactPairs[triangleIndex];
        if (contactPair.contactArea <= 0.0f) {
            continue;
        }

        const std::size_t triangleBase = triangleIndex * 3;
        const uint32_t vertexIndices[3] = {
            triangleIndices[triangleBase + 0],
            triangleIndices[triangleBase + 1],
            triangleIndices[triangleBase + 2],
        };

        const uint32_t mappedCells[3] = {
            vertexIndices[0] < receiverCellIndices.size() ? receiverCellIndices[vertexIndices[0]] : std::numeric_limits<uint32_t>::max(),
            vertexIndices[1] < receiverCellIndices.size() ? receiverCellIndices[vertexIndices[1]] : std::numeric_limits<uint32_t>::max(),
            vertexIndices[2] < receiverCellIndices.size() ? receiverCellIndices[vertexIndices[2]] : std::numeric_limits<uint32_t>::max(),
        };

        for (uint32_t sampleIndex = 0; sampleIndex < Quadrature::count; ++sampleIndex) {
            const ContactSampleGPU& sample = contactPair.samples[sampleIndex];
            if (sample.sourceTriangleIndex == std::numeric_limits<uint32_t>::max() || sample.wArea <= 0.0f) {
                continue;
            }

            const uint32_t flattenedSampleIndex = static_cast<uint32_t>(samples.size());
            samples.push_back(sample);

            const glm::vec3 barycentric = Quadrature::bary[sampleIndex];
            const float baryWeights[3] = { barycentric.x, barycentric.y, barycentric.z };
            for (int cornerIndex = 0; cornerIndex < 3; ++cornerIndex) {
                const uint32_t cellIndex = mappedCells[cornerIndex];
                const float weight = sample.wArea * baryWeights[cornerIndex];
                if (cellIndex == std::numeric_limits<uint32_t>::max() || weight <= 0.0f) {
                    continue;
                }

                ContactCellWeight cellWeight{};
                cellWeight.cellIndex = cellIndex;
                cellWeight.sampleIndex = flattenedSampleIndex;
                cellWeight.weight = weight;
                cellWeights.push_back(cellWeight);
            }
        }
    }

    if (samples.empty() || cellWeights.empty()) {
        std::cerr << "[HeatContactRuntime]   rebuildCouplingBuffers abort: no weighted samples"
                  << " samples=" << samples.size()
                  << " cellWeights=" << cellWeights.size()
                  << std::endl;
        return false;
    }

    std::sort(cellWeights.begin(), cellWeights.end(), [](const ContactCellWeight& lhs, const ContactCellWeight& rhs) {
        if (lhs.cellIndex != rhs.cellIndex) {
            return lhs.cellIndex < rhs.cellIndex;
        }
        return lhs.sampleIndex < rhs.sampleIndex;
    });

    std::vector<ContactCellMap> contactCellMap;
    contactCellMap.reserve(cellWeights.size());
    std::vector<ContactCellRange> contactCellRanges;
    contactCellRanges.reserve(cellWeights.size());

    std::size_t rangeStart = 0;
    while (rangeStart < cellWeights.size()) {
        const uint32_t cellIndex = cellWeights[rangeStart].cellIndex;
        std::size_t rangeEnd = rangeStart;
        while (rangeEnd < cellWeights.size() && cellWeights[rangeEnd].cellIndex == cellIndex) {
            ContactCellMap mapEntry{};
            mapEntry.sampleIndex = cellWeights[rangeEnd].sampleIndex;
            mapEntry.weight = cellWeights[rangeEnd].weight;
            contactCellMap.push_back(mapEntry);
            ++rangeEnd;
        }

        ContactCellRange range{};
        range.cellIndex = cellIndex;
        range.startIndex = static_cast<uint32_t>(rangeStart);
        range.count = static_cast<uint32_t>(rangeEnd - rangeStart);
        contactCellRanges.push_back(range);
        rangeStart = rangeEnd;
    }

    if (!recreateBuffer(
            vulkanDevice,
            memoryAllocator,
            coupling.contactSampleBuffer,
            coupling.contactSampleBufferOffset,
            samples.data(),
            sizeof(ContactSampleGPU) * samples.size()) ||
        !recreateBuffer(
            vulkanDevice,
            memoryAllocator,
            coupling.contactCellMapBuffer,
            coupling.contactCellMapBufferOffset,
            contactCellMap.data(),
            sizeof(ContactCellMap) * contactCellMap.size()) ||
        !recreateBuffer(
            vulkanDevice,
            memoryAllocator,
            coupling.contactCellRangeBuffer,
            coupling.contactCellRangeBufferOffset,
            contactCellRanges.data(),
            sizeof(ContactCellRange) * contactCellRanges.size())) {
        return false;
    }

    coupling.contactSampleCount = static_cast<uint32_t>(samples.size());
    coupling.contactCellMapCount = static_cast<uint32_t>(contactCellMap.size());
    coupling.contactCellRangeCount = static_cast<uint32_t>(contactCellRanges.size());

    if (productCoupling.couplingType == ContactCouplingType::ReceiverToReceiver) {
        const auto emitterMappingIt = receiverSurfaceMappingBufferByModelId.find(productCoupling.emitterRuntimeModelId);
        const auto emitterOffsetIt = receiverSurfaceMappingBufferOffsetByModelId.find(productCoupling.emitterRuntimeModelId);
        if (emitterMappingIt == receiverSurfaceMappingBufferByModelId.end() ||
            emitterOffsetIt == receiverSurfaceMappingBufferOffsetByModelId.end()) {
            std::cerr << "[HeatContactRuntime]   rebuildCouplingBuffers abort: emitter mapping missing"
                      << " emitterRuntimeModelId=" << productCoupling.emitterRuntimeModelId
                      << std::endl;
            return false;
        }

        coupling.emitterVoronoiMappingBuffer = emitterMappingIt->second;
        coupling.emitterVoronoiMappingBufferOffset = emitterOffsetIt->second;
    }

    return true;
}

void HeatContactRuntime::clearCouplings(MemoryAllocator& memoryAllocator) {
    if (!contactCouplings.empty()) {
        std::cerr << "[HeatContactRuntime] clearCouplings"
                  << " count=" << contactCouplings.size()
                  << std::endl;
    }
    for (ContactCoupling& coupling : contactCouplings) {
        coupling.contactDescriptorsReady = false;
        coupling.contactComputeSetA = VK_NULL_HANDLE;
        coupling.contactComputeSetB = VK_NULL_HANDLE;
        coupling.emitterReceiverIndex = std::numeric_limits<uint32_t>::max();
        coupling.receiverIndex = std::numeric_limits<uint32_t>::max();
        if (coupling.contactSampleBuffer != VK_NULL_HANDLE) {
            memoryAllocator.free(coupling.contactSampleBuffer, coupling.contactSampleBufferOffset);
            coupling.contactSampleBuffer = VK_NULL_HANDLE;
            coupling.contactSampleBufferOffset = 0;
        }
        if (coupling.contactCellMapBuffer != VK_NULL_HANDLE) {
            memoryAllocator.free(coupling.contactCellMapBuffer, coupling.contactCellMapBufferOffset);
            coupling.contactCellMapBuffer = VK_NULL_HANDLE;
            coupling.contactCellMapBufferOffset = 0;
        }
        if (coupling.contactCellRangeBuffer != VK_NULL_HANDLE) {
            memoryAllocator.free(coupling.contactCellRangeBuffer, coupling.contactCellRangeBufferOffset);
            coupling.contactCellRangeBuffer = VK_NULL_HANDLE;
            coupling.contactCellRangeBufferOffset = 0;
        }
        coupling.emitterTriangleIndexBuffer = VK_NULL_HANDLE;
        coupling.emitterTriangleIndexBufferOffset = 0;
        coupling.emitterVoronoiMappingBuffer = VK_NULL_HANDLE;
        coupling.emitterVoronoiMappingBufferOffset = 0;
        if (coupling.paramsBuffer != VK_NULL_HANDLE) {
            memoryAllocator.free(coupling.paramsBuffer, coupling.paramsBufferOffset);
            coupling.paramsBuffer = VK_NULL_HANDLE;
            coupling.paramsBufferOffset = 0;
        }
    }
    contactCouplings.clear();
    couplingsDirty = true;
}
