#include "HeatContactRuntime.hpp"

#include "contact/ContactSampling.hpp"
#include "heat/HeatReceiverRuntime.hpp"
#include "heat/HeatSourceRuntime.hpp"
#include "util/GMLS.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/VulkanBuffer.hpp"
#include "vulkan/VulkanDevice.hpp"

#include <algorithm>
#include <iostream>
#include <memory>
#include <numeric>

#include <libs/nanoflann/include/nanoflann.hpp>

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

struct SeedPointCloudAdapter {
    const std::vector<glm::vec3>& pts;
    inline size_t kdtree_get_point_count() const { return pts.size(); }
    inline float kdtree_get_pt(const size_t idx, const size_t dim) const { return pts[idx][static_cast<int>(dim)]; }
    template <class BBOX> bool kdtree_get_bbox(BBOX&) const { return false; }
};

glm::vec3 barycentricToPosition(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& bary) {
    return (p0 * bary.x) + (p1 * bary.y) + (p2 * bary.z);
}

std::vector<float> buildScatterWeights(const std::vector<double>& valueWeights) {
    std::vector<float> scatterWeights(valueWeights.size(), 0.0f);

    double positiveSum = 0.0;
    for (size_t i = 0; i < valueWeights.size(); ++i) {
        const double positiveWeight = std::max(0.0, valueWeights[i]);
        scatterWeights[i] = static_cast<float>(positiveWeight);
        positiveSum += positiveWeight;
    }
    if (positiveSum > 1e-12) {
        const float invSum = static_cast<float>(1.0 / positiveSum);
        for (float& weight : scatterWeights) {
            weight *= invSum;
        }
        return scatterWeights;
    }

    double absSum = 0.0;
    for (size_t i = 0; i < valueWeights.size(); ++i) {
        const double absWeight = std::abs(valueWeights[i]);
        scatterWeights[i] = static_cast<float>(absWeight);
        absSum += absWeight;
    }
    if (absSum > 1e-12) {
        const float invSum = static_cast<float>(1.0 / absSum);
        for (float& weight : scatterWeights) {
            weight *= invSum;
        }
        return scatterWeights;
    }

    if (!scatterWeights.empty()) {
        const float uniformWeight = 1.0f / static_cast<float>(scatterWeights.size());
        for (float& weight : scatterWeights) {
            weight = uniformWeight;
        }
    }

    return scatterWeights;
}

using KDTree = nanoflann::KDTreeSingleIndexAdaptor<
    nanoflann::L2_Simple_Adaptor<float, SeedPointCloudAdapter>,
    SeedPointCloudAdapter,
    3>;

struct StencilKDTree {
    std::vector<glm::vec3> regularSeedPositions;
    std::vector<uint32_t> regularLocalIndices;
    SeedPointCloudAdapter cloud;
    KDTree index;
    size_t supportCount;

    StencilKDTree(uint32_t nodeOffset, const std::vector<uint32_t>& seedFlags, const std::vector<glm::vec3>& seedPositions)
        : cloud{ regularSeedPositions },
          index(3, cloud, nanoflann::KDTreeSingleIndexAdaptorParams(10)),
          supportCount(0) {
        regularSeedPositions.reserve(seedPositions.size());
        regularLocalIndices.reserve(seedPositions.size());
        for (uint32_t localIndex = 0; localIndex < seedPositions.size(); ++localIndex) {
            if (localIndex >= seedFlags.size() || (seedFlags[localIndex] & 1u) != 0u) {
                continue;
            }
            regularSeedPositions.push_back(seedPositions[localIndex]);
            regularLocalIndices.push_back(localIndex);
        }
        if (regularSeedPositions.size() >= 4) {
            index.buildIndex();
            supportCount = std::min<size_t>(50, regularSeedPositions.size());
        }
    }

    bool isValid() const { return regularSeedPositions.size() >= 4; }
};

bool buildPointStencil(
    const glm::vec3& point,
    uint32_t nodeOffset,
    const StencilKDTree& kdTree,
    std::vector<contact::GMLSWeight>& valueWeightsOut,
    uint32_t& valueWeightOffsetOut,
    uint32_t& valueWeightCountOut,
    std::vector<contact::GMLSWeight>& scatterWeightsOut,
    uint32_t& scatterWeightOffsetOut,
    uint32_t& scatterWeightCountOut) {
    valueWeightCountOut = 0;
    scatterWeightCountOut = 0;

    if (!kdTree.isValid()) {
        return false;
    }

    const size_t supportCount = kdTree.supportCount;
    std::vector<size_t> retIndices(supportCount, 0);
    std::vector<float> outDistSq(supportCount, 0.0f);

    const float query[3] = { point.x, point.y, point.z };
    nanoflann::KNNResultSet<float> resultSet(supportCount);
    resultSet.init(retIndices.data(), outDistSq.data());
    kdTree.index.findNeighbors(resultSet, query);

    std::vector<glm::dvec3> supportPositions;
    supportPositions.reserve(supportCount);
    float maxDistance = 0.0f;
    for (size_t supportIndex = 0; supportIndex < supportCount; ++supportIndex) {
        supportPositions.push_back(glm::dvec3(kdTree.regularSeedPositions[retIndices[supportIndex]]));
        maxDistance = std::max(maxDistance, std::sqrt(outDistSq[supportIndex]));
    }

    std::vector<double> valueWeights;
    std::vector<glm::dvec3> gradientWeights;
    const double kernelRadius = std::max<double>(static_cast<double>(maxDistance) * 2.0, 1e-5);
    if (!GMLS::computeWeights(glm::dvec3(point), supportPositions, kernelRadius, valueWeights, gradientWeights)) {
        return false;
    }

    const std::vector<float> scatterWeights = buildScatterWeights(valueWeights);
    std::vector<contact::GMLSWeight> localValueWeights;
    std::vector<contact::GMLSWeight> localScatterWeights;
    localValueWeights.reserve(supportCount);
    localScatterWeights.reserve(supportCount);
    for (size_t supportIndex = 0; supportIndex < supportCount; ++supportIndex) {
        const uint32_t globalCellIndex = nodeOffset + kdTree.regularLocalIndices[retIndices[supportIndex]];
        const float valueWeight = static_cast<float>(valueWeights[supportIndex]);
        if (std::abs(valueWeight) > 1e-7f) {
            localValueWeights.push_back({ globalCellIndex, valueWeight, 0u, 0u });
            ++valueWeightCountOut;
        }

        const float scatterWeight = scatterWeights[supportIndex];
        if (scatterWeight > 1e-7f) {
            localScatterWeights.push_back({ globalCellIndex, scatterWeight, 0u, 0u });
            ++scatterWeightCountOut;
        }
    }

    valueWeightOffsetOut = static_cast<uint32_t>(valueWeightsOut.size());
    valueWeightsOut.insert(valueWeightsOut.end(), localValueWeights.begin(), localValueWeights.end());
    scatterWeightOffsetOut = static_cast<uint32_t>(scatterWeightsOut.size());
    scatterWeightsOut.insert(scatterWeightsOut.end(), localScatterWeights.begin(), localScatterWeights.end());

    return (valueWeightCountOut != 0 && scatterWeightCountOut != 0);
}

}

bool HeatContactRuntime::areContactCouplingsEqual(
    const std::vector<ContactCoupling>& lhs,
    const std::vector<ContactCoupling>& rhs) {
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
    const std::vector<ContactCoupling>& contactInputs) {
    if (activeReceiverRuntimeModelIds == receiverRuntimeModelIds &&
        areContactCouplingsEqual(activeContactCouplings, contactInputs)) {
        return;
    }

    activeReceiverRuntimeModelIds = receiverRuntimeModelIds;
    activeContactCouplings = contactInputs;
    couplingsDirty = true;
}

bool HeatContactRuntime::ensureCouplings(
    VulkanDevice& vulkanDevice,
    MemoryAllocator& memoryAllocator,
    const std::vector<HeatSystemRuntime::SourceBinding>& sourceBindings,
    const std::vector<std::unique_ptr<HeatReceiverRuntime>>& receivers,
    const std::unordered_map<uint32_t, uint32_t>& receiverVoronoiNodeOffsetByModelId,
    const std::unordered_map<uint32_t, std::vector<uint32_t>>& receiverVoronoiSeedFlagsByModelId,
    const std::unordered_map<uint32_t, std::vector<glm::vec3>>& receiverVoronoiSeedPositionsByModelId,
    float contactThermalConductance,
    bool forceRebuild,
    uint32_t totalVoronoiNodeCount) {
    if (!couplingsDirty && !forceRebuild) {
        return true;
    }

    clearCouplings(memoryAllocator);

    std::vector<float> contactConductanceSum(totalVoronoiNodeCount, 0.0f);
    for (const ContactCoupling& contactCoupling : activeContactCouplings) {
        if (contactCoupling.couplingType != ContactCouplingType::SourceToReceiver) {
            std::cerr << "[HeatContactRuntime]   skipping non-source contact until receiver-to-receiver path is standardized"
                      << " type=" << static_cast<uint32_t>(contactCoupling.couplingType)
                      << " emitterRuntimeModelId=" << contactCoupling.emitterRuntimeModelId
                      << " receiverRuntimeModelId=" << contactCoupling.receiverRuntimeModelId
                      << std::endl;
            continue;
        }

        const uint32_t receiverIndex = findReceiverIndexByRuntimeModelId(
            activeReceiverRuntimeModelIds,
            contactCoupling.receiverRuntimeModelId);
        if (receiverIndex == std::numeric_limits<uint32_t>::max()) {
            std::cerr << "[HeatContactRuntime]   skipping product: receiver model not found"
                      << " receiverRuntimeModelId=" << contactCoupling.receiverRuntimeModelId
                      << std::endl;
            continue;
        }
        const HeatReceiverRuntime* receiverRuntime = nullptr;
        for (const auto& receiver : receivers) {
            if (receiver && receiver->getRuntimeModelId() == contactCoupling.receiverRuntimeModelId) {
                receiverRuntime = receiver.get();
                break;
            }
        }
        const auto receiverNodeOffsetIt = receiverVoronoiNodeOffsetByModelId.find(contactCoupling.receiverRuntimeModelId);
        const auto receiverSeedFlagsIt = receiverVoronoiSeedFlagsByModelId.find(contactCoupling.receiverRuntimeModelId);
        const auto receiverSeedPositionsIt = receiverVoronoiSeedPositionsByModelId.find(contactCoupling.receiverRuntimeModelId);
        if (!receiverRuntime ||
            receiverNodeOffsetIt == receiverVoronoiNodeOffsetByModelId.end() ||
            receiverSeedFlagsIt == receiverVoronoiSeedFlagsByModelId.end() ||
            receiverSeedPositionsIt == receiverVoronoiSeedPositionsByModelId.end()) {
            std::cerr << "[HeatContactRuntime]   skipping product: receiver Voronoi stencil inputs missing"
                      << " receiverRuntimeModelId=" << contactCoupling.receiverRuntimeModelId
                      << std::endl;
            continue;
        }

        CouplingState builtCoupling{};
        builtCoupling.couplingType = contactCoupling.couplingType;
        builtCoupling.emitterModelId = contactCoupling.emitterRuntimeModelId;
        builtCoupling.receiverModelId = contactCoupling.receiverRuntimeModelId;
        builtCoupling.receiverIndex = receiverIndex;

        const SupportingHalfedge::IntrinsicMesh* emitterMesh = nullptr;
        uint32_t emitterNodeOffset = 0;
        const std::vector<uint32_t>* emitterSeedFlags = nullptr;
        const std::vector<glm::vec3>* emitterSeedPositions = nullptr;
        if (contactCoupling.couplingType == ContactCouplingType::SourceToReceiver) {
            const HeatSystemRuntime::SourceBinding* sourceBinding =
                findSourceBindingByRuntimeModelId(sourceBindings, contactCoupling.emitterRuntimeModelId);
            if (!sourceBinding || !sourceBinding->heatSource) {
                std::cerr << "[HeatContactRuntime]   skipping product: source binding missing"
                          << " emitterRuntimeModelId=" << contactCoupling.emitterRuntimeModelId
                          << std::endl;
                continue;
            }
            emitterMesh = &sourceBinding->heatSource->getIntrinsicMesh();
        } else {
            const uint32_t emitterReceiverIndex =
                findReceiverIndexByRuntimeModelId(activeReceiverRuntimeModelIds, contactCoupling.emitterRuntimeModelId);
            if (emitterReceiverIndex == std::numeric_limits<uint32_t>::max() ||
                emitterReceiverIndex == receiverIndex) {
                std::cerr << "[HeatContactRuntime]   skipping receiver-to-receiver product"
                          << " emitterRuntimeModelId=" << contactCoupling.emitterRuntimeModelId
                          << " receiverRuntimeModelId=" << contactCoupling.receiverRuntimeModelId
                          << " emitterReceiverIndex=" << emitterReceiverIndex
                          << " receiverIndex=" << receiverIndex
                          << std::endl;
                continue;
            }
            builtCoupling.emitterReceiverIndex = emitterReceiverIndex;

            const HeatReceiverRuntime* emitterRuntime = nullptr;
            for (const auto& receiver : receivers) {
                if (receiver && receiver->getRuntimeModelId() == contactCoupling.emitterRuntimeModelId) {
                    emitterRuntime = receiver.get();
                    break;
                }
            }
            const auto emitterNodeOffsetIt = receiverVoronoiNodeOffsetByModelId.find(contactCoupling.emitterRuntimeModelId);
            const auto emitterSeedFlagsIt = receiverVoronoiSeedFlagsByModelId.find(contactCoupling.emitterRuntimeModelId);
            const auto emitterSeedPositionsIt = receiverVoronoiSeedPositionsByModelId.find(contactCoupling.emitterRuntimeModelId);
            if (!emitterRuntime ||
                emitterNodeOffsetIt == receiverVoronoiNodeOffsetByModelId.end() ||
                emitterSeedFlagsIt == receiverVoronoiSeedFlagsByModelId.end() ||
                emitterSeedPositionsIt == receiverVoronoiSeedPositionsByModelId.end()) {
                std::cerr << "[HeatContactRuntime]   skipping receiver-to-receiver product: emitter Voronoi stencil inputs missing"
                          << " emitterRuntimeModelId=" << contactCoupling.emitterRuntimeModelId
                          << std::endl;
                continue;
            }

            emitterMesh = &emitterRuntime->getIntrinsicMesh();
            emitterNodeOffset = emitterNodeOffsetIt->second;
            emitterSeedFlags = &emitterSeedFlagsIt->second;
            emitterSeedPositions = &emitterSeedPositionsIt->second;
        }

        if (!rebuildCouplingBuffers(
                vulkanDevice,
                memoryAllocator,
                builtCoupling,
                contactCoupling,
                receiverRuntime->getIntrinsicMesh(),
                emitterMesh,
                receiverNodeOffsetIt->second,
                emitterNodeOffset,
                receiverSeedFlagsIt->second,
                receiverSeedPositionsIt->second,
                emitterSeedFlags,
                emitterSeedPositions,
                contactThermalConductance,
                totalVoronoiNodeCount,
                contactConductanceSum)) {
            std::cerr << "[HeatContactRuntime]   rebuildCouplingBuffers failed"
                      << " emitterRuntimeModelId=" << contactCoupling.emitterRuntimeModelId
                      << " receiverRuntimeModelId=" << contactCoupling.receiverRuntimeModelId
                      << std::endl;
            clearCouplings(memoryAllocator);
            return false;
        }

        contactCouplings.push_back(std::move(builtCoupling));
    }

    if (!contactCouplings.empty()) {
        if (!recreateBuffer(
                vulkanDevice,
                memoryAllocator,
                contactConductanceBuffer,
                contactConductanceBufferOffset,
                contactConductanceSum.data(),
                sizeof(float) * contactConductanceSum.size())) {
            clearCouplings(memoryAllocator);
            return false;
        }
        contactConductanceNodeCount = totalVoronoiNodeCount;
    }

    couplingsDirty = false;
    return true;
}

bool HeatContactRuntime::rebuildCouplingBuffers(
    VulkanDevice& vulkanDevice,
    MemoryAllocator& memoryAllocator,
    CouplingState& coupling,
    const ContactCoupling& contactCoupling,
    const SupportingHalfedge::IntrinsicMesh& receiverMesh,
    const SupportingHalfedge::IntrinsicMesh* emitterMesh,
    uint32_t receiverNodeOffset,
    uint32_t emitterNodeOffset,
    const std::vector<uint32_t>& receiverSeedFlags,
    const std::vector<glm::vec3>& receiverSeedPositions,
    const std::vector<uint32_t>* emitterSeedFlags,
    const std::vector<glm::vec3>* emitterSeedPositions,
    float contactThermalConductance,
    uint32_t totalVoronoiNodeCount,
    std::vector<float>& contactConductanceSum) const {
    (void)vulkanDevice;
    (void)memoryAllocator;

    if (contactCoupling.mappedContactPairs == nullptr || contactCoupling.contactPairCount == 0) {
        std::cerr << "[HeatContactRuntime]   rebuildCouplingBuffers abort: missing mapped pairs"
                  << " pairCount=" << contactCoupling.contactPairCount
                  << std::endl;
        return false;
    }

    const auto& triangleIndices = contactCoupling.receiverTriangleIndices;
    const std::size_t triangleCount = triangleIndices.size() / 3;
    const std::size_t contactPairCount = std::min<std::size_t>(contactCoupling.contactPairCount, triangleCount);
    if (contactPairCount == 0 || receiverSeedPositions.empty()) {
        std::cerr << "[HeatContactRuntime]   rebuildCouplingBuffers abort: missing receiver stencil inputs"
                  << " pairCount=" << contactCoupling.contactPairCount
                  << " receiverTriangleCount=" << triangleCount
                  << " receiverSeedCount=" << receiverSeedPositions.size()
                  << std::endl;
        return false;
    }

    StencilKDTree receiverKDTree(receiverNodeOffset, receiverSeedFlags, receiverSeedPositions);
    if (!receiverKDTree.isValid()) {
        std::cerr << "[HeatContactRuntime]   rebuildCouplingBuffers abort: receiver KDTree too small"
                  << std::endl;
        return false;
    }

    std::unique_ptr<StencilKDTree> emitterKDTree;
    if (contactCoupling.couplingType == ContactCouplingType::ReceiverToReceiver &&
        emitterSeedFlags && emitterSeedPositions) {
        emitterKDTree = std::make_unique<StencilKDTree>(emitterNodeOffset, *emitterSeedFlags, *emitterSeedPositions);
        if (!emitterKDTree->isValid()) {
            std::cerr << "[HeatContactRuntime]   rebuildCouplingBuffers abort: emitter KDTree too small"
                      << std::endl;
            return false;
        }
    }

    std::vector<contact::GMLSSample> samples;
    samples.reserve(contactPairCount * Quadrature::count);
    std::vector<contact::GMLSWeight> weights;
    weights.reserve(contactPairCount * Quadrature::count * 32);

    for (std::size_t triangleIndex = 0; triangleIndex < contactPairCount; ++triangleIndex) {
        const ContactPair& contactPair = contactCoupling.mappedContactPairs[triangleIndex];
        if (contactPair.contactArea <= 0.0f) {
            continue;
        }

        const std::size_t triangleBase = triangleIndex * 3;
        const uint32_t vertexIndices[3] = {
            triangleIndices[triangleBase + 0],
            triangleIndices[triangleBase + 1],
            triangleIndices[triangleBase + 2],
        };
        if (vertexIndices[0] >= receiverMesh.vertices.size() ||
            vertexIndices[1] >= receiverMesh.vertices.size() ||
            vertexIndices[2] >= receiverMesh.vertices.size()) {
            continue;
        }

        const glm::vec3 receiverP0 = receiverMesh.vertices[vertexIndices[0]].position;
        const glm::vec3 receiverP1 = receiverMesh.vertices[vertexIndices[1]].position;
        const glm::vec3 receiverP2 = receiverMesh.vertices[vertexIndices[2]].position;

        for (uint32_t sampleIndex = 0; sampleIndex < Quadrature::count; ++sampleIndex) {
            const contact::Sample& samplePoint = contactPair.samples[sampleIndex];
            if (samplePoint.sourceTriangleIndex == std::numeric_limits<uint32_t>::max() || samplePoint.wArea <= 0.0f) {
                continue;
            }

            const glm::vec3 receiverPoint = barycentricToPosition(
                receiverP0,
                receiverP1,
                receiverP2,
                Quadrature::bary[sampleIndex]);

            contact::GMLSSample sample{};
            sample.areaWeight = samplePoint.wArea;
            if (!buildPointStencil(
                    receiverPoint,
                    receiverNodeOffset,
                    receiverKDTree,
                    weights,
                    sample.receiverValueWeightOffset,
                    sample.receiverValueWeightCount,
                    weights,
                    sample.receiverScatterWeightOffset,
                    sample.receiverScatterWeightCount)) {
                continue;
            }

            if (contactCoupling.couplingType == ContactCouplingType::ReceiverToReceiver) {
                if (!emitterMesh || !emitterSeedFlags || !emitterSeedPositions) {
                    continue;
                }
                if (samplePoint.sourceTriangleIndex >= emitterMesh->triangles.size()) {
                    continue;
                }
                if (!emitterKDTree) {
                    continue;
                }

                const auto& emitterTriangle = emitterMesh->triangles[samplePoint.sourceTriangleIndex];
                const uint32_t e0 = emitterTriangle.vertexIndices[0];
                const uint32_t e1 = emitterTriangle.vertexIndices[1];
                const uint32_t e2 = emitterTriangle.vertexIndices[2];
                if (e0 >= emitterMesh->vertices.size() ||
                    e1 >= emitterMesh->vertices.size() ||
                    e2 >= emitterMesh->vertices.size()) {
                    continue;
                }

                const glm::vec3 emitterBary(
                    1.0f - samplePoint.u - samplePoint.v,
                    samplePoint.u,
                    samplePoint.v);
                const glm::vec3 emitterPoint = barycentricToPosition(
                    emitterMesh->vertices[e0].position,
                    emitterMesh->vertices[e1].position,
                    emitterMesh->vertices[e2].position,
                    emitterBary);

                if (!buildPointStencil(
                        emitterPoint,
                        emitterNodeOffset,
                        *emitterKDTree,
                        weights,
                        sample.emitterValueWeightOffset,
                        sample.emitterValueWeightCount,
                        weights,
                        sample.emitterScatterWeightOffset,
                        sample.emitterScatterWeightCount)) {
                    continue;
                }
            }

            samples.push_back(sample);
        }
    }

    if (samples.empty() || weights.empty()) {
        std::cerr << "[HeatContactRuntime]   rebuildCouplingBuffers abort: no GMLS contact samples"
                  << " samples=" << samples.size()
                  << " weights=" << weights.size()
                  << std::endl;
        return false;
    }

    // Build per-node source-to-receiver contact conductance.
    const float thermalConductance = contactThermalConductance;
    const uint32_t nodeCount = totalVoronoiNodeCount;

    uint32_t affectedNodeCount = 0;
    for (const auto& sample : samples) {
        for (uint32_t wi = 0; wi < sample.receiverScatterWeightCount; ++wi) {
            const auto& w = weights[sample.receiverScatterWeightOffset + wi];
            if (w.cellIndex >= nodeCount) {
                continue;
            }
            if (contactConductanceSum[w.cellIndex] == 0.0f) {
                ++affectedNodeCount;
            }
            contactConductanceSum[w.cellIndex] += thermalConductance * sample.areaWeight * w.weight;
        }
    }

    coupling.affectedContactNodeCount = affectedNodeCount;

    return true;
}

void HeatContactRuntime::clearCouplings(MemoryAllocator& memoryAllocator) {
    for (CouplingState& coupling : contactCouplings) {
        coupling.emitterReceiverIndex = std::numeric_limits<uint32_t>::max();
        coupling.receiverIndex = std::numeric_limits<uint32_t>::max();
        coupling.affectedContactNodeCount = 0;
    }
    if (contactConductanceBuffer != VK_NULL_HANDLE) {
        memoryAllocator.free(contactConductanceBuffer, contactConductanceBufferOffset);
        contactConductanceBuffer = VK_NULL_HANDLE;
        contactConductanceBufferOffset = 0;
    }
    contactConductanceNodeCount = 0;
    contactCouplings.clear();
    couplingsDirty = true;
}
