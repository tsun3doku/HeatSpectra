#include "HeatContactRuntime.hpp"

#include "contact/ContactMapping.hpp"
#include "heat/HeatModelRuntime.hpp"
#include "util/GMLS.hpp"
#include "util/GeometryUtils.hpp"
#include "voronoi/VoronoiNodeIndex.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <unordered_map>
#include <unordered_set>

HeatContactRuntime::~HeatContactRuntime() = default;

void HeatContactRuntime::buildWendlandWeights(
    const glm::vec3& point,
    const VoronoiNodeIndex& nodeIndex,
    std::vector<uint32_t>& neighborIds,
    std::vector<float>& weights) {
    neighborIds.clear();
    weights.clear();
    if (!nodeIndex.isValid()) return;

    std::vector<float> distancesSquared;
    nodeIndex.findKNearest(point, 50u, neighborIds, distancesSquared);
    if (neighborIds.empty()) return;

    float maxDistance = 0.0f;
    for (float distanceSquared : distancesSquared) {
        maxDistance = std::max(maxDistance, std::sqrt(distanceSquared));
    }
    const float radius = std::max(maxDistance * 2.0f, 1e-5f);
    weights.resize(neighborIds.size());
    double sum = 0.0;
    for (size_t index = 0; index < neighborIds.size(); ++index) {
        const double normalizedRadius = std::sqrt(distancesSquared[index]) / radius;
        weights[index] = static_cast<float>(GMLS::wendlandC2(normalizedRadius));
        sum += weights[index];
    }
    if (sum <= 1e-20) {
        neighborIds.clear();
        weights.clear();
        return;
    }
    for (float& weight : weights) {
        weight = static_cast<float>(weight / sum);
    }
}

bool HeatContactRuntime::build(
    VulkanDevice& vulkanDevice,
    const std::unordered_map<uint32_t, std::unique_ptr<HeatModelRuntime>>& models,
    const std::vector<ContactCoupling>& couplings,
    float heatTransferCoefficient) {
    cleanup();
    if (models.empty() || couplings.empty() || !std::isfinite(heatTransferCoefficient) || heatTransferCoefficient < 0.0f) {
        return false;
    }

    std::vector<uint32_t> runtimeModelIds;
    for (const ContactCoupling& coupling : couplings) {
        if (!coupling.isValid()) continue;
        runtimeModelIds.push_back(coupling.modelARuntimeModelId);
        runtimeModelIds.push_back(coupling.modelBRuntimeModelId);
    }
    std::sort(runtimeModelIds.begin(), runtimeModelIds.end());
    runtimeModelIds.erase(std::unique(runtimeModelIds.begin(), runtimeModelIds.end()), runtimeModelIds.end());

    uint32_t fullNodeCount = 0;
    std::unordered_map<uint32_t, uint32_t> fullOffsets;
    std::unordered_map<uint32_t, uint32_t> nodeCounts;
    for (uint32_t runtimeModelId : runtimeModelIds) {
        const auto modelIt = models.find(runtimeModelId);
        if (modelIt == models.end() || !modelIt->second || modelIt->second->getSimNodeCount() == 0) continue;
        fullOffsets[runtimeModelId] = fullNodeCount;
        nodeCounts[runtimeModelId] = modelIt->second->getSimNodeCount();
        fullNodeCount += modelIt->second->getSimNodeCount();
    }
    if (fullNodeCount == 0) return false;

    const uint32_t InvalidNode = std::numeric_limits<uint32_t>::max();
    auto toFullNodeId = [&](uint32_t runtimeModelId, uint32_t localNodeId) {
        const auto offsetIt = fullOffsets.find(runtimeModelId);
        const auto countIt = nodeCounts.find(runtimeModelId);
        if (offsetIt == fullOffsets.end() || countIt == nodeCounts.end() || localNodeId >= countIt->second) {
            return InvalidNode;
        }
        return offsetIt->second + localNodeId;
    };

    std::unordered_map<uint32_t, std::unique_ptr<VoronoiNodeIndex>> surfaceIndices;
    std::unordered_map<uint32_t, std::vector<uint32_t>> surfaceNodeIdsByModelId;
    for (uint32_t runtimeModelId : runtimeModelIds) {
        const auto modelIt = models.find(runtimeModelId);
        if (modelIt == models.end() || !modelIt->second) {
            return false;
        }
        const HeatModelRuntime& model = *modelIt->second;
        const auto& surfaceNodeIds = model.getSurfaceNodeIds();
        const auto& patchAreas = model.getSurfacePatchAreas();
        const auto& nodePositions = model.getNodeIndex().getNodePositions();
        if (patchAreas.size() != model.getSimNodeCount() || nodePositions.size() != model.getSimNodeCount() ||
            surfaceNodeIds.empty()) {
            return false;
        }

        std::vector<glm::vec3> surfaceNodePositions;
        surfaceNodePositions.reserve(surfaceNodeIds.size());
        for (uint32_t nodeId : surfaceNodeIds) {
            if (nodeId >= nodePositions.size() || !std::isfinite(patchAreas[nodeId]) || patchAreas[nodeId] <= 0.0f) {
                return false;
            }
            surfaceNodePositions.push_back(nodePositions[nodeId]);
        }
        auto surfaceIndex = std::make_unique<VoronoiNodeIndex>();
        surfaceIndex->rebuild(surfaceNodePositions);
        if (!surfaceIndex->isValid()) {
            return false;
        }
        surfaceIndices.emplace(runtimeModelId, std::move(surfaceIndex));
        surfaceNodeIdsByModelId.emplace(runtimeModelId, surfaceNodeIds);
        coveredAreasByModelId[runtimeModelId].assign(model.getSimNodeCount(), 0.0f);
    }

    auto addCoveredArea = [&](uint32_t runtimeModelId, const glm::vec3& point, float area) {
        const auto indexIt = surfaceIndices.find(runtimeModelId);
        const auto idsIt = surfaceNodeIdsByModelId.find(runtimeModelId);
        const auto coveredIt = coveredAreasByModelId.find(runtimeModelId);
        if (indexIt == surfaceIndices.end() || idsIt == surfaceNodeIdsByModelId.end() ||
            coveredIt == coveredAreasByModelId.end() || !std::isfinite(area) || area <= 0.0f) {
            return false;
        }
        std::vector<uint32_t> compactNodeIds;
        std::vector<float> weights;
        buildWendlandWeights(point, *indexIt->second, compactNodeIds, weights);
        if (compactNodeIds.empty() || compactNodeIds.size() != weights.size()) {
            return false;
        }
        for (size_t index = 0; index < compactNodeIds.size(); ++index) {
            if (compactNodeIds[index] >= idsIt->second.size()) {
                return false;
            }
            coveredIt->second[idsIt->second[compactNodeIds[index]]] += area * weights[index];
        }
        return true;
    };

    std::vector<uint32_t> fixedBoundaryValueIndex(fullNodeCount, InvalidNode);
    fixedBoundaryRegions.clear();
    for (uint32_t runtimeModelId : runtimeModelIds) {
        const auto modelIt = models.find(runtimeModelId);
        if (modelIt == models.end() || !modelIt->second) continue;
        std::unordered_map<uint32_t, uint32_t> fixedValueIndexByRegionId;
        for (uint32_t localNodeId : modelIt->second->getDirichletNodeIds()) {
            const uint32_t fullNodeId = toFullNodeId(runtimeModelId, localNodeId);
            if (fullNodeId != InvalidNode) {
                const uint32_t regionId = modelIt->second->getDirichletRegionId(localNodeId);
                auto [regionIt, inserted] = fixedValueIndexByRegionId.emplace(
                    regionId, static_cast<uint32_t>(fixedBoundaryRegions.size()));
                if (inserted) {
                    float temperatureC = 0.0f;
                    if (!modelIt->second->getBoundaryRegionTemperatureC(regionId, temperatureC)) {
                        return false;
                    }
                    fixedBoundaryRegions.push_back({modelIt->second.get(), regionId});
                }
                fixedBoundaryValueIndex[fullNodeId] = regionIt->second;
            }
        }
    }

    std::vector<ContactSampleData> samples;
    for (const ContactCoupling& coupling : couplings) {
        if (!coupling.isValid()) continue;
        const auto modelAIt = models.find(coupling.modelARuntimeModelId);
        const auto modelBIt = models.find(coupling.modelBRuntimeModelId);
        if (modelAIt == models.end() || modelBIt == models.end() ||
            !modelAIt->second || !modelBIt->second) continue;

        const HeatModelRuntime& modelA = *modelAIt->second;
        const HeatModelRuntime& modelB = *modelBIt->second;
        const double metersPerUnit = modelA.getMetersPerWorldUnit();
        const double areaScale = metersPerUnit * metersPerUnit;
        if (!modelA.getNodeIndex().isValid() || !modelB.getNodeIndex().isValid()) continue;

        const auto& triangleIndices = coupling.modelBTriangleIndices;
        const size_t pairCount = std::min<size_t>(coupling.contactPairCount, triangleIndices.size() / 3);
        for (size_t triangleIndex = 0; triangleIndex < pairCount; ++triangleIndex) {
            const ContactPair& pair = coupling.contactPairs[triangleIndex];
            if (pair.contactArea <= 0.0f) continue;
            const size_t triangleBase = triangleIndex * 3;
            const uint32_t b0 = triangleIndices[triangleBase];
            const uint32_t b1 = triangleIndices[triangleBase + 1];
            const uint32_t b2 = triangleIndices[triangleBase + 2];
            if (b0 >= modelB.getSurfacePositions().size() ||
                b1 >= modelB.getSurfacePositions().size() ||
                b2 >= modelB.getSurfacePositions().size()) continue;

            for (uint32_t sampleIndex = 0; sampleIndex < Quadrature::count; ++sampleIndex) {
                const contact::Sample& contactSample = pair.samples[sampleIndex];
                const auto& modelATriangleIndices = modelA.getSurfaceTriangleIndices();
                const size_t modelATriangleBase = static_cast<size_t>(contactSample.modelATriangleIndex) * 3;
                if (contactSample.modelATriangleIndex == InvalidNode || contactSample.contactSampleArea <= 0.0f ||
                    modelATriangleBase + 2 >= modelATriangleIndices.size()) continue;

                const glm::vec3 modelBPoint = barycentricToPosition(
                    modelB.getSurfacePositions()[b0],
                    modelB.getSurfacePositions()[b1],
                    modelB.getSurfacePositions()[b2],
                    Quadrature::bary[sampleIndex]);
                std::vector<uint32_t> nodesB;
                std::vector<float> weightsB;
                buildWendlandWeights(modelBPoint, modelB.getNodeIndex(), nodesB, weightsB);
                if (nodesB.empty()) continue;

                const uint32_t a0 = modelATriangleIndices[modelATriangleBase];
                const uint32_t a1 = modelATriangleIndices[modelATriangleBase + 1];
                const uint32_t a2 = modelATriangleIndices[modelATriangleBase + 2];
                if (a0 >= modelA.getSurfacePositions().size() ||
                    a1 >= modelA.getSurfacePositions().size() ||
                    a2 >= modelA.getSurfacePositions().size()) continue;
                const glm::vec3 barycentric(
                    1.0f - contactSample.u - contactSample.v,
                    contactSample.u,
                    contactSample.v);
                const glm::vec3 modelAPoint = barycentricToPosition(
                    modelA.getSurfacePositions()[a0],
                    modelA.getSurfacePositions()[a1],
                    modelA.getSurfacePositions()[a2],
                    barycentric);
                if (!addCoveredArea(coupling.modelARuntimeModelId, modelAPoint, contactSample.contactSampleArea) ||
                    !addCoveredArea(coupling.modelBRuntimeModelId, modelBPoint, contactSample.contactSampleArea)) {
                    return false;
                }
                std::vector<uint32_t> nodesA;
                std::vector<float> weightsA;
                buildWendlandWeights(modelAPoint, modelA.getNodeIndex(), nodesA, weightsA);
                if (nodesA.empty()) continue;

                ContactSampleData sample{};
                sample.conductance =
                    static_cast<double>(heatTransferCoefficient) * contactSample.contactSampleArea * areaScale;
                sample.nodes.reserve(nodesA.size() + nodesB.size());
                double sampleWeightSum = 0.0;
                for (size_t index = 0; index < nodesA.size(); ++index) {
                    const uint32_t fullNode = toFullNodeId(coupling.modelARuntimeModelId, nodesA[index]);
                    if (fullNode == InvalidNode) continue;
                    sample.nodes.push_back({fullNode, static_cast<double>(weightsA[index])});
                    sampleWeightSum += weightsA[index];
                }
                for (size_t index = 0; index < nodesB.size(); ++index) {
                    const uint32_t fullNode = toFullNodeId(coupling.modelBRuntimeModelId, nodesB[index]);
                    if (fullNode == InvalidNode) continue;
                    sample.nodes.push_back({fullNode, -static_cast<double>(weightsB[index])});
                    sampleWeightSum -= weightsB[index];
                }
                if (!sample.nodes.empty() && std::abs(sampleWeightSum) <= 1e-5) {
                    samples.push_back(std::move(sample));
                }
            }
        }
    }
    if (heatTransferCoefficient == 0.0f) return true;
    if (samples.empty()) return false;

    std::vector<uint32_t> activeFullNodeIds;
    for (const ContactSampleData& sample : samples) {
        for (const SampleNode& node : sample.nodes) {
            if (fixedBoundaryValueIndex[node.fullNodeId] == InvalidNode) {
                activeFullNodeIds.push_back(node.fullNodeId);
            }
        }
    }
    std::sort(activeFullNodeIds.begin(), activeFullNodeIds.end());
    activeFullNodeIds.erase(std::unique(activeFullNodeIds.begin(), activeFullNodeIds.end()), activeFullNodeIds.end());
    if (activeFullNodeIds.empty()) return true;

    const uint32_t contactNodeCount = static_cast<uint32_t>(activeFullNodeIds.size());
    std::vector<uint32_t> fullToSolver(fullNodeCount, InvalidNode);
    for (uint32_t solverNode = 0; solverNode < contactNodeCount; ++solverNode) {
        fullToSolver[activeFullNodeIds[solverNode]] = solverNode;
    }

    modelNodes.clear();
    for (uint32_t runtimeModelId : runtimeModelIds) {
        const auto offsetIt = fullOffsets.find(runtimeModelId);
        const auto countIt = nodeCounts.find(runtimeModelId);
        if (offsetIt == fullOffsets.end() || countIt == nodeCounts.end()) continue;
        SolverModelNodes modelNodesForRuntime{};
        modelNodesForRuntime.runtimeModelId = runtimeModelId;
        for (uint32_t localNode = 0; localNode < countIt->second; ++localNode) {
            const uint32_t solverNode = fullToSolver[offsetIt->second + localNode];
            if (solverNode == InvalidNode) continue;
            if (modelNodesForRuntime.localNodeIds.empty()) {
                modelNodesForRuntime.solverNodeOffset = solverNode;
            }
            modelNodesForRuntime.localNodeIds.push_back(localNode);
        }
        if (!modelNodesForRuntime.localNodeIds.empty()) modelNodes.push_back(std::move(modelNodesForRuntime));
    }

    std::vector<std::unordered_map<uint32_t, double>> matrixRows(contactNodeCount);
    std::vector<std::unordered_map<uint32_t, double>> fixedRowMaps(contactNodeCount);
 
    for (const ContactSampleData& sample : samples) {
        for (const SampleNode& nodeA : sample.nodes) {
            if (nodeA.weight <= 0.0) continue;
            for (const SampleNode& nodeB : sample.nodes) {
                if (nodeB.weight >= 0.0) continue;
                const double conductance =
                    FixedTimeStep * sample.conductance * nodeA.weight * -nodeB.weight;
                if (conductance <= 0.0) continue;

                const uint32_t solverA = fullToSolver[nodeA.fullNodeId];
                const uint32_t solverB = fullToSolver[nodeB.fullNodeId];
                const uint32_t fixedA = fixedBoundaryValueIndex[nodeA.fullNodeId];
                const uint32_t fixedB = fixedBoundaryValueIndex[nodeB.fullNodeId];

                if (solverA != InvalidNode && solverB != InvalidNode) {
                    matrixRows[solverA][solverA] += conductance;
                    matrixRows[solverB][solverB] += conductance;
                    matrixRows[solverA][solverB] -= conductance;
                    matrixRows[solverB][solverA] -= conductance;
                } else if (solverA != InvalidNode && fixedB != InvalidNode) {
                    matrixRows[solverA][solverA] += conductance;
                    fixedRowMaps[solverA][fixedB] += conductance;
                } else if (solverB != InvalidNode && fixedA != InvalidNode) {
                    matrixRows[solverB][solverB] += conductance;
                    fixedRowMaps[solverB][fixedA] += conductance;
                }
            }
        }
    }

    std::vector<float> thermalMasses(contactNodeCount, 0.0f);
    for (const SolverModelNodes& modelNodesForRuntime : modelNodes) {
        HeatModelRuntime& model = *models.at(modelNodesForRuntime.runtimeModelId);
        const auto& modelMasses = model.getNodalThermalMasses();
        for (uint32_t index = 0; index < modelNodesForRuntime.localNodeIds.size(); ++index) {
            const uint32_t localNode = modelNodesForRuntime.localNodeIds[index];
            const uint32_t solverNode = modelNodesForRuntime.solverNodeOffset + index;
            if (localNode >= modelMasses.size() || !std::isfinite(modelMasses[localNode]) || modelMasses[localNode] <= 0.0f) {
                std::cerr << "[AmgXContact] invalid thermal mass at model="
                          << modelNodesForRuntime.runtimeModelId << " node=" << localNode << std::endl;
                return false;
            }
            thermalMasses[solverNode] = modelMasses[localNode];
            matrixRows[solverNode][solverNode] += modelMasses[localNode];
        }
    }

    std::vector<int> rowOffsets(contactNodeCount + 1, 0);
    std::vector<int> columnIndices;
    std::vector<float> values;
    for (uint32_t row = 0; row < contactNodeCount; ++row) {
        std::vector<std::pair<uint32_t, double>> sortedRow(matrixRows[row].begin(), matrixRows[row].end());
        std::sort(sortedRow.begin(), sortedRow.end(), [](const auto& left, const auto& right) {
            return left.first < right.first;
        });
        for (const auto& [column, value] : sortedRow) {
            if (!std::isfinite(value) || std::abs(value) <= 1e-30) continue;
            columnIndices.push_back(static_cast<int>(column));
            values.push_back(static_cast<float>(value));
        }
        rowOffsets[row + 1] = static_cast<int>(columnIndices.size());
    }

    std::vector<HeatContactSolver::FixedRow> fixedRows(contactNodeCount);
    std::vector<HeatContactSolver::FixedContribution> fixedContributions;
    for (uint32_t row = 0; row < contactNodeCount; ++row) {
        fixedRows[row].contributionOffset = static_cast<uint32_t>(fixedContributions.size());
        std::vector<std::pair<uint32_t, double>> sorted(fixedRowMaps[row].begin(), fixedRowMaps[row].end());
        std::sort(sorted.begin(), sorted.end());
        for (const auto& [boundaryIndex, coefficient] : sorted) {
            if (std::abs(coefficient) <= 1e-30) continue;
            fixedContributions.push_back({boundaryIndex, static_cast<float>(coefficient)});
        }
        fixedRows[row].contributionCount =
            static_cast<uint32_t>(fixedContributions.size()) - fixedRows[row].contributionOffset;
    }

    std::vector<HeatContactSolver::ModelNodes> solverModels;
    solverModels.reserve(modelNodes.size());
    for (const SolverModelNodes& modelNodesForRuntime : modelNodes) {
        HeatModelRuntime& model = *models.at(modelNodesForRuntime.runtimeModelId);
        solverModels.push_back({
            modelNodesForRuntime.solverNodeOffset,
            modelNodesForRuntime.localNodeIds,
            &model.getExternalTempBufferA(),
            &model.getExternalTempBufferB(),
            &model.getCudaTempBufferA(),
            &model.getCudaTempBufferB()});
    }

    solver = std::make_unique<HeatContactSolver>();
    if (!solver->initialize(
            vulkanDevice, rowOffsets, columnIndices, values, thermalMasses,
            solverModels, fixedRows, fixedContributions,
            static_cast<uint32_t>(fixedBoundaryRegions.size()))) {
        solver.reset();
        return false;
    }

    return true;
}

bool HeatContactRuntime::solve(bool temperatureBufferAIsCurrent) {
    clearSynchronization();
    if (!hasGraph()) return true;

    std::vector<float> boundaryTemperatures;
    boundaryTemperatures.reserve(fixedBoundaryRegions.size());
    for (const FixedBoundaryRegion& boundaryRegion : fixedBoundaryRegions) {
        float temperatureC = 0.0f;
        if (!boundaryRegion.model) {
            return false;
        }
        if (!boundaryRegion.model->getBoundaryRegionTemperatureC(boundaryRegion.regionId, temperatureC)) {
            return false;
        }
        boundaryTemperatures.push_back(temperatureC);
    }

    const uint64_t cudaDoneValue = ++timelineValue;
    const uint64_t vulkanDoneValue = ++timelineValue;
    if (!solver->solve(
            temperatureBufferAIsCurrent, boundaryTemperatures,
            previousVulkanValue, cudaDoneValue)) {
        return false;
    }

    synchronization.waitSemaphore = solver->getTimelineSemaphore();
    synchronization.waitValue = cudaDoneValue;
    synchronization.signalSemaphore = solver->getTimelineSemaphore();
    synchronization.signalValue = vulkanDoneValue;
    previousVulkanValue = vulkanDoneValue;
    return true;
}

const std::vector<float>* HeatContactRuntime::findCoveredAreas(uint32_t runtimeModelId) const {
    const auto it = coveredAreasByModelId.find(runtimeModelId);
    return it != coveredAreasByModelId.end() ? &it->second : nullptr;
}

ComputePass::Synchronization HeatContactRuntime::getSynchronization() const {
    return synchronization;
}

void HeatContactRuntime::clearSynchronization() {
    synchronization = {};
}

void HeatContactRuntime::cleanup() {
    if (solver) solver->cleanup();
    solver.reset();
    modelNodes.clear();
    fixedBoundaryRegions.clear();
    coveredAreasByModelId.clear();
    synchronization = {};
    timelineValue = 0;
    previousVulkanValue = 0;
}
