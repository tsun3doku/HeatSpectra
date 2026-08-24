#include "HeatBoundaryRuntime.hpp"

#include "vulkan/CommandBufferManager.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/VulkanBuffer.hpp"
#include "vulkan/VulkanDevice.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>

bool HeatBoundaryRuntime::configureRegions(
    const std::vector<Region>& configuredRegions,
    uint32_t nodeCount,
    uint32_t surfacePointCount,
    const std::vector<uint32_t>& surfaceNodeIds,
    const std::vector<float>& surfaceBoundaryAreas) {
    if (nodeCount == 0 || surfaceBoundaryAreas.size() != nodeCount) {
        std::cerr << "[HeatBoundary-Diag] configureRegions rejected size"
                  << " nodeCount=" << nodeCount
                  << " areaCount=" << surfaceBoundaryAreas.size()
                  << " surfacePointCount=" << surfacePointCount << std::endl;
        return false;
    }

    regions.clear();
    stateIndexByRegionId.clear();
    states.clear();
    this->surfaceNodeIds = surfaceNodeIds;
    this->surfaceBoundaryAreas = surfaceBoundaryAreas;
    surfaceNodeMask.assign(nodeCount, 0u);
    dirichletStateIndicesByNode.assign(nodeCount, {});
    dirichletStateIndicesBySurfacePoint.assign(surfacePointCount, {});
    dirichletRegionIdsByNode.assign(nodeCount, NoBoundary);
    dirichletNodeIds.clear();
    nodes.clear();
    contributions.clear();
    surfaceIndices.assign(surfacePointCount, NoBoundary);

    for (uint32_t nodeId : surfaceNodeIds) {
        if (nodeId >= nodeCount || surfaceNodeMask[nodeId] != 0u ||
            !std::isfinite(surfaceBoundaryAreas[nodeId]) || surfaceBoundaryAreas[nodeId] <= 0.0f) {
            std::cerr << "[HeatBoundary-Diag] configureRegions rejected surface node"
                      << " nodeId=" << nodeId
                      << " nodeCount=" << nodeCount
                      << " area=" << (nodeId < surfaceBoundaryAreas.size() ? surfaceBoundaryAreas[nodeId] : 0.0f)
                      << " masked=" << (nodeId < surfaceNodeMask.size() ? static_cast<int>(surfaceNodeMask[nodeId]) : -1)
                      << " areaCount=" << surfaceBoundaryAreas.size()
                      << " surfaceCount=" << surfaceNodeIds.size() << std::endl;
            return false;
        }
        surfaceNodeMask[nodeId] = 1u;
    }

    std::vector<uint8_t> hasLoad(nodeCount, 0u);
    std::vector<uint8_t> hasDirichlet(nodeCount, 0u);
    for (const Region& region : configuredRegions) {
        const uint32_t type = region.state.conditionType;
        if (type == Adiabatic) {
            continue;
        }
        if (type != DirichletTemperature && type != NeumannHeatFlux && type != RobinConvection) {
            return false;
        }
        if (stateIndexByRegionId.find(region.id) != stateIndexByRegionId.end() ||
            !std::isfinite(region.state.temperatureC) ||
            !std::isfinite(region.state.heatFlux) ||
            !std::isfinite(region.state.heatTransferCoefficient) ||
            (type == RobinConvection && region.state.heatTransferCoefficient < 0.0f)) {
            return false;
        }

        const uint32_t stateIndex = static_cast<uint32_t>(states.size());
        stateIndexByRegionId.emplace(region.id, stateIndex);
        states.push_back(region.state);
        regions.push_back(region);

        std::vector<uint32_t> regionNodeIds = region.nodeIds;
        for (uint32_t nodeId : regionNodeIds) {
            if (nodeId >= nodeCount || surfaceNodeMask[nodeId] == 0u) {
                return false;
            }
        }
        std::sort(regionNodeIds.begin(), regionNodeIds.end());
        if (std::adjacent_find(regionNodeIds.begin(), regionNodeIds.end()) != regionNodeIds.end()) {
            return false;
        }

        for (uint32_t nodeId : region.nodeIds) {
            if (type == DirichletTemperature) {
                if (hasLoad[nodeId] != 0u) {
                    return false;
                }
                hasDirichlet[nodeId] = 1u;
                dirichletStateIndicesByNode[nodeId].push_back(stateIndex);
                if (dirichletRegionIdsByNode[nodeId] == NoBoundary) {
                    dirichletRegionIdsByNode[nodeId] = region.id;
                    dirichletNodeIds.push_back(nodeId);
                }
            } else {
                if (hasDirichlet[nodeId] != 0u) {
                    return false;
                }
                hasLoad[nodeId] = 1u;
            }
        }

        if (type == DirichletTemperature) {
            std::vector<uint32_t> pointIds = region.surfacePointIds;
            std::sort(pointIds.begin(), pointIds.end());
            if (std::adjacent_find(pointIds.begin(), pointIds.end()) != pointIds.end()) {
                return false;
            }
            for (uint32_t surfacePointId : region.surfacePointIds) {
                if (surfacePointId >= surfacePointCount) {
                    return false;
                }
                dirichletStateIndicesBySurfacePoint[surfacePointId].push_back(stateIndex);
                if (surfaceIndices[surfacePointId] == NoBoundary) {
                    surfaceIndices[surfacePointId] = stateIndex;
                }
            }
        } else if (!region.surfacePointIds.empty()) {
            return false;
        }
    }

    std::sort(dirichletNodeIds.begin(), dirichletNodeIds.end());
    if (!dirichletValuesAgree()) {
        return false;
    }
    stateDirty = true;
    return true;
}

bool HeatBoundaryRuntime::buildBoundaryBuffers() {
    const uint32_t nodeCount = static_cast<uint32_t>(surfaceBoundaryAreas.size());
    std::vector<std::vector<heat::BoundaryContribution>> byNode(nodeCount);
    for (const Region& region : regions) {
        if (region.state.conditionType == DirichletTemperature) {
            continue;
        }
        const uint32_t stateIndex = findStateIndex(region.id);
        if (stateIndex == NoBoundary) {
            return false;
        }
        for (uint32_t nodeId : region.nodeIds) {
            const float area = surfaceBoundaryAreas[nodeId];
            if (area > 0.0f) {
                byNode[nodeId].push_back({stateIndex, area});
            }
        }
    }

    nodes.assign(nodeCount, {0u, 0u, NoBoundary});
    contributions.clear();
    for (uint32_t nodeId = 0; nodeId < nodeCount; ++nodeId) {
        heat::BoundaryNode& node = nodes[nodeId];
        node.contributionOffset = static_cast<uint32_t>(contributions.size());
        node.contributionCount = static_cast<uint32_t>(byNode[nodeId].size());
        if (!dirichletStateIndicesByNode[nodeId].empty()) {
            node.dirichletStateIndex = dirichletStateIndicesByNode[nodeId].front();
        }
        contributions.insert(contributions.end(), byNode[nodeId].begin(), byNode[nodeId].end());
    }
    return true;
}

bool HeatBoundaryRuntime::createBuffers(VulkanDevice& device, MemoryAllocator& allocator, CommandPool& commandPool) {
    cleanup(allocator);
    if (nodes.empty() || surfaceIndices.empty()) {
        return false;
    }
    if (states.empty()) {
        states.push_back({Adiabatic, 0.0f, 0.0f, 0.0f});
    }
    if (contributions.empty()) {
        contributions.push_back({0u, 0.0f});
    }

    const VkDeviceSize alignment = device.getPhysicalDeviceProperties().limits.minStorageBufferOffsetAlignment;
    if (uploadDeviceBuffer(allocator, commandPool, nodes.data(), nodes.size() * sizeof(heat::BoundaryNode),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, alignment, nodeBuffer, nodeBufferOffset) != VK_SUCCESS ||
        uploadDeviceBuffer(allocator, commandPool, contributions.data(), contributions.size() * sizeof(heat::BoundaryContribution),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, alignment, contributionBuffer, contributionBufferOffset) != VK_SUCCESS ||
        uploadDeviceBuffer(allocator, commandPool, surfaceIndices.data(), surfaceIndices.size() * sizeof(uint32_t),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, alignment, surfaceIndexBuffer, surfaceIndexBufferOffset) != VK_SUCCESS) {
        return false;
    }

    const VkDeviceSize stateSize = states.size() * sizeof(heat::BoundaryState);
    if (uploadDeviceBuffer(allocator, commandPool, states.data(), stateSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            alignment, stateBuffer, stateBufferOffset) != VK_SUCCESS) {
        return false;
    }
    stateDirty = false;
    return true;
}

bool HeatBoundaryRuntime::setDirichletTemperatureC(uint32_t regionId, float temperatureC) {
    const uint32_t stateIndex = findStateIndex(regionId);
    if (stateIndex == NoBoundary || states[stateIndex].conditionType != DirichletTemperature ||
        !std::isfinite(temperatureC)) {
        return false;
    }
    const float previous = states[stateIndex].temperatureC;
    states[stateIndex].temperatureC = temperatureC;
    if (!dirichletValuesAgree()) {
        states[stateIndex].temperatureC = previous;
        return false;
    }
    stateDirty = stateDirty || previous != temperatureC;
    return true;
}

bool HeatBoundaryRuntime::setNeumannHeatFlux(uint32_t regionId, float heatFlux) {
    const uint32_t stateIndex = findStateIndex(regionId);
    if (stateIndex == NoBoundary || states[stateIndex].conditionType != NeumannHeatFlux || !std::isfinite(heatFlux)) {
        return false;
    }
    stateDirty = stateDirty || states[stateIndex].heatFlux != heatFlux;
    states[stateIndex].heatFlux = heatFlux;
    return true;
}

bool HeatBoundaryRuntime::setRobinState(uint32_t regionId, float ambientTemperatureC, float heatTransferCoefficient) {
    const uint32_t stateIndex = findStateIndex(regionId);
    if (stateIndex == NoBoundary || states[stateIndex].conditionType != RobinConvection ||
        !std::isfinite(ambientTemperatureC) || !std::isfinite(heatTransferCoefficient) ||
        heatTransferCoefficient < 0.0f) {
        return false;
    }
    stateDirty = stateDirty || states[stateIndex].temperatureC != ambientTemperatureC ||
        states[stateIndex].heatTransferCoefficient != heatTransferCoefficient;
    states[stateIndex].temperatureC = ambientTemperatureC;
    states[stateIndex].heatTransferCoefficient = heatTransferCoefficient;
    return true;
}

bool HeatBoundaryRuntime::setRobinTemperatureC(uint32_t regionId, float ambientTemperatureC) {
    const uint32_t stateIndex = findStateIndex(regionId);
    if (stateIndex == NoBoundary || states[stateIndex].conditionType != RobinConvection ||
        !std::isfinite(ambientTemperatureC)) {
        return false;
    }
    stateDirty = stateDirty || states[stateIndex].temperatureC != ambientTemperatureC;
    states[stateIndex].temperatureC = ambientTemperatureC;
    return true;
}

void HeatBoundaryRuntime::uploadState(VkCommandBuffer commandBuffer) {
    if (!stateDirty || commandBuffer == VK_NULL_HANDLE || stateBuffer == VK_NULL_HANDLE) {
        return;
    }
    const VkDeviceSize stateSize = states.size() * sizeof(heat::BoundaryState);
    if (stateSize == 0 || stateSize > 65536 || (stateSize & 3u) != 0 || (stateBufferOffset & 3u) != 0) {
        return;
    }
    vkCmdUpdateBuffer(commandBuffer, stateBuffer, stateBufferOffset, stateSize, states.data());

    VkBufferMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.buffer = stateBuffer;
    barrier.offset = stateBufferOffset;
    barrier.size = stateSize;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, nullptr, 1, &barrier, 0, nullptr);
    stateDirty = false;
}

uint32_t HeatBoundaryRuntime::getDirichletRegionId(uint32_t nodeId) const {
    return nodeId < dirichletRegionIdsByNode.size() ? dirichletRegionIdsByNode[nodeId] : NoBoundary;
}

bool HeatBoundaryRuntime::getRegionTemperatureC(uint32_t regionId, float& temperatureC) const {
    const uint32_t stateIndex = findStateIndex(regionId);
    if (stateIndex == NoBoundary || states[stateIndex].conditionType != DirichletTemperature) {
        return false;
    }
    temperatureC = states[stateIndex].temperatureC;
    return true;
}

bool HeatBoundaryRuntime::getRegionAmbientTemperatureC(uint32_t regionId, float& temperatureC) const {
    const uint32_t stateIndex = findStateIndex(regionId);
    if (stateIndex == NoBoundary) {
        return false;
    }
    temperatureC = states[stateIndex].temperatureC;
    return true;
}

bool HeatBoundaryRuntime::getRegionHeatFlux(uint32_t regionId, float& heatFlux) const {
    const uint32_t stateIndex = findStateIndex(regionId);
    if (stateIndex == NoBoundary || states[stateIndex].conditionType != NeumannHeatFlux) {
        return false;
    }
    heatFlux = states[stateIndex].heatFlux;
    return true;
}

bool HeatBoundaryRuntime::getRegionHeatTransferCoefficient(uint32_t regionId, float& coefficient) const {
    const uint32_t stateIndex = findStateIndex(regionId);
    if (stateIndex == NoBoundary || states[stateIndex].conditionType != RobinConvection) {
        return false;
    }
    coefficient = states[stateIndex].heatTransferCoefficient;
    return true;
}

bool HeatBoundaryRuntime::dirichletValuesAgree() const {
    const auto agree = [this](const std::vector<uint32_t>& stateIndices) {
        if (stateIndices.size() < 2) {
            return true;
        }
        const float temperatureC = states[stateIndices.front()].temperatureC;
        for (uint32_t stateIndex : stateIndices) {
            if (stateIndex >= states.size() || states[stateIndex].temperatureC != temperatureC) {
                return false;
            }
        }
        return true;
    };
    for (const auto& stateIndices : dirichletStateIndicesByNode) {
        if (!agree(stateIndices)) {
            return false;
        }
    }
    for (const auto& stateIndices : dirichletStateIndicesBySurfacePoint) {
        if (!agree(stateIndices)) {
            return false;
        }
    }
    return true;
}

uint32_t HeatBoundaryRuntime::findStateIndex(uint32_t regionId) const {
    const auto it = stateIndexByRegionId.find(regionId);
    return it != stateIndexByRegionId.end() ? it->second : NoBoundary;
}

void HeatBoundaryRuntime::cleanup(MemoryAllocator& allocator) {
    freeBuffer(allocator, nodeBuffer, nodeBufferOffset);
    freeBuffer(allocator, contributionBuffer, contributionBufferOffset);
    freeBuffer(allocator, surfaceIndexBuffer, surfaceIndexBufferOffset);
    freeBuffer(allocator, stateBuffer, stateBufferOffset);
}
