#include "HeatDomainRuntime.hpp"

#include "heat/HeatBoundaryRuntime.hpp"
#include "heat/HeatGlobalSolver.hpp"
#include "heat/HeatModelRuntime.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>

namespace {
constexpr uint32_t DirichletTemperature = 1u;
constexpr uint32_t NeumannHeatFlux = 2u;
constexpr uint32_t RobinConvection = 3u;
}

HeatDomainRuntime::~HeatDomainRuntime() {
    waitGlobalSolveIdle();
}

HeatModelRuntime* HeatDomainRuntime::getModelByRuntimeId(uint32_t runtimeModelId) const {
    auto it = activeModels.find(runtimeModelId);
    return (it != activeModels.end()) ? it->second.get() : nullptr;
}

void HeatDomainRuntime::removeModelsNotIn(const std::unordered_set<uint32_t>& runtimeModelIds) {
    for (auto it = activeModels.begin(); it != activeModels.end();) {
        if (runtimeModelIds.find(it->first) == runtimeModelIds.end()) {
            it = activeModels.erase(it);
        } else {
            ++it;
        }
    }
}

void HeatDomainRuntime::setModel(
    uint32_t runtimeModelId,
    std::unique_ptr<HeatModelRuntime> model) {
    activeModels[runtimeModelId] = std::move(model);
}

void HeatDomainRuntime::removeModel(uint32_t runtimeModelId) {
    activeModels.erase(runtimeModelId);
}

void HeatDomainRuntime::setGlobalDomain(GlobalThermalDomain domain) {
    globalDomain = std::move(domain);
    globalTopologyDirtyFlag = true;
    globalValuesDirtyFlag = true;
}

void HeatDomainRuntime::clearGlobalDomain() {
    waitGlobalSolveIdle();
    globalDomain = GlobalThermalDomain{};
    globalSolver.reset();
    globalBoundaryRegions.clear();
    allGlobalFragmentsFixedFlag = false;
    globalTopologyDirtyFlag = false;
    globalValuesDirtyFlag = false;
    clearGlobalSynchronization();
    globalSolveInFlight = false;
    pendingDestinationIsA = false;
    cudaReadyCounter = 0;
    vulkanReleaseCounter = 0;
    pendingCudaReadyValue = 0;
    lastVulkanReleaseValue[0] = 0;
    lastVulkanReleaseValue[1] = 0;
}

void HeatDomainRuntime::cleanup() {
    clearGlobalDomain();
    activeModels.clear();
}

bool HeatDomainRuntime::assembleGlobalMatrix(
    const std::unordered_map<uint32_t, std::unique_ptr<HeatModelRuntime>>& models,
    float fixedTimeStep,
    float contactThermalConductance,
    std::vector<int>& rowOffsets,
    std::vector<int>& columnIndices,
    std::vector<float>& values,
    std::vector<float>& thermalMasses,
    std::vector<SolverModelNodes>& solverModelNodes,
    std::vector<HeatGlobalSolver::FixedRow>& fixedRows,
    std::vector<HeatGlobalSolver::FixedContribution>& fixedContributions,
    uint32_t& boundaryValueCount) {
    rowOffsets.clear();
    columnIndices.clear();
    values.clear();
    thermalMasses.clear();
    solverModelNodes.clear();
    fixedRows.clear();
    fixedContributions.clear();
    globalBoundaryRegions.clear();

    if (!globalDomain.isValid() || globalDomain.fragments.empty()) {
        return false;
    }

    std::vector<uint32_t> modelIds;
    modelIds.reserve(models.size());
    for (const auto& [runtimeModelId, model] : models) {
        if (model) modelIds.push_back(runtimeModelId);
    }
    std::sort(modelIds.begin(), modelIds.end());
    if (modelIds.empty()) {
        return false;
    }

    const uint32_t fragmentCount = static_cast<uint32_t>(globalDomain.fragments.size());

    // Build solver node mapping for non-Dirichlet nodes
    uint32_t currentSolverNode = 0;
    std::vector<uint32_t> fragmentSolverNodes(fragmentCount, InvalidSolverNode);
    for (uint32_t runtimeModelId : modelIds) {
        const auto it = models.find(runtimeModelId);
        if (it == models.end() || !it->second) continue;
        const HeatModelRuntime& model = *it->second;

        const auto& dirichletNodes = model.getDirichletNodeIds();
        std::unordered_set<uint32_t> dirichletSet(dirichletNodes.begin(), dirichletNodes.end());
        const bool fullyDirichlet = (model.getBoundaryConditionType() == DirichletTemperature);

        SolverModelNodes block{};
        block.runtimeModelId = runtimeModelId;
        block.solverNodeOffset = currentSolverNode;

        uint32_t localFragmentIndex = 0;
        for (uint32_t f = 0; f < fragmentCount; ++f) {
            const GlobalFragment& frag = globalDomain.fragments[f];
            if (frag.instanceId == runtimeModelId) {
                const bool isFixed = fullyDirichlet || (dirichletSet.find(localFragmentIndex) != dirichletSet.end());
                if (!isFixed) {
                    fragmentSolverNodes[f] = currentSolverNode++;
                    block.localNodeIds.push_back(localFragmentIndex);
                }
                ++localFragmentIndex;
            }
        }
        if (!block.localNodeIds.empty()) {
            solverModelNodes.push_back(std::move(block));
        }
    }

    const uint32_t solverNodeCount = currentSolverNode;
    if (solverNodeCount == 0) {
        return true;
    }

    // Assign boundary region indices
    constexpr uint32_t ConstantRhsIndex = 0;
    std::unordered_map<uint64_t, uint32_t> boundaryRegionLookup;
    auto getBoundaryValueIndex = [&](uint32_t runtimeModelId, const HeatModelRuntime& model, uint32_t regionId, bool dirichlet) -> uint32_t {
        const uint64_t key = (static_cast<uint64_t>(runtimeModelId) << 32) |
            (static_cast<uint64_t>(regionId) << 1) | (dirichlet ? 1ULL : 0ULL);
        auto it = boundaryRegionLookup.find(key);
        if (it != boundaryRegionLookup.end()) return it->second;
        const uint32_t index = static_cast<uint32_t>(globalBoundaryRegions.size()) + 1;
        globalBoundaryRegions.push_back({&model, regionId, dirichlet});
        boundaryRegionLookup[key] = index;
        return index;
    };

    thermalMasses.assign(solverNodeCount, 0.0f);
    struct MatrixEntry {
        int col = 0;
        float value = 0.0f;
    };
    std::vector<std::vector<MatrixEntry>> nodeEntries(solverNodeCount);
    std::vector<std::vector<HeatGlobalSolver::FixedContribution>> nodeContributions(solverNodeCount);

    // Populate thermal masses & diagonal baseline
    for (uint32_t f = 0; f < fragmentCount; ++f) {
        const uint32_t node = fragmentSolverNodes[f];
        if (node == InvalidSolverNode) continue;
        const uint32_t modelId = globalDomain.fragments[f].instanceId;
        const HeatModelRuntime& model = *models.at(modelId);
        const float rho = model.getDensity();
        const float cp = model.getSpecificHeat();
        const float mass = rho * cp * globalDomain.fragments[f].volume;
        thermalMasses[node] = mass;
        nodeEntries[node].push_back({static_cast<int>(node), mass});
    }

    // Same-instance conductive face fluxes
    for (const GlobalFace& face : globalDomain.faces) {
        if (face.fragmentA >= fragmentCount || face.fragmentB >= fragmentCount) continue;
        const uint32_t modelAId = globalDomain.fragments[face.fragmentA].instanceId;
        const uint32_t modelBId = globalDomain.fragments[face.fragmentB].instanceId;
        if (modelAId != modelBId) continue; // Part 2 contact is assembled from CutFaces only
        const auto itA = models.find(modelAId);
        const auto itB = models.find(modelBId);
        if (itA == models.end() || !itA->second || itB == models.end() || !itB->second) continue;

        const HeatModelRuntime& modelA = *itA->second;
        const HeatModelRuntime& modelB = *itB->second;
        const glm::vec3 posA = globalDomain.seedPositions[globalDomain.fragments[face.fragmentA].seedId];
        const glm::vec3 posB = globalDomain.seedPositions[globalDomain.fragments[face.fragmentB].seedId];
        const float dist = std::max(glm::length(posB - posA), 1e-6f);
        const float kA = modelA.getConductivity();
        const float kB = modelB.getConductivity();
        const float kEff = (kA > 0.0f && kB > 0.0f) ? (2.0f * kA * kB / (kA + kB)) : 0.0f;
        const float G = (kEff * face.area) / dist;
        const float dtG = fixedTimeStep * G;

        const uint32_t nodeA = fragmentSolverNodes[face.fragmentA];
        const uint32_t nodeB = fragmentSolverNodes[face.fragmentB];

        if (nodeA != InvalidSolverNode && nodeB != InvalidSolverNode) {
            nodeEntries[nodeA][0].value += dtG;
            nodeEntries[nodeA].push_back({static_cast<int>(nodeB), -dtG});
            nodeEntries[nodeB][0].value += dtG;
            nodeEntries[nodeB].push_back({static_cast<int>(nodeA), -dtG});
        } else if (nodeA != InvalidSolverNode && nodeB == InvalidSolverNode) {
            nodeEntries[nodeA][0].value += dtG;
            const uint32_t bIdx = getBoundaryValueIndex(modelBId, modelB, 0, true);
            nodeContributions[nodeA].push_back({bIdx, dtG});
        } else if (nodeA == InvalidSolverNode && nodeB != InvalidSolverNode) {
            nodeEntries[nodeB][0].value += dtG;
            const uint32_t bIdx = getBoundaryValueIndex(modelAId, modelA, 0, true);
            nodeContributions[nodeB].push_back({bIdx, dtG});
        }
    }

    // Cross-instance contact conductance
    for (const GlobalCutFace& face : globalDomain.cutFaces) {
        if (face.fragmentA >= fragmentCount || face.fragmentB >= fragmentCount) continue;
        const auto& fragA = globalDomain.fragments[face.fragmentA];
        const auto& fragB = globalDomain.fragments[face.fragmentB];
        if (fragA.instanceId == fragB.instanceId) continue;

        const auto itA = models.find(fragA.instanceId);
        const auto itB = models.find(fragB.instanceId);
        if (itA == models.end() || !itA->second || itB == models.end() || !itB->second) continue;

        const HeatModelRuntime& modelA = *itA->second;
        const HeatModelRuntime& modelB = *itB->second;

        const uint32_t nodeA = fragmentSolverNodes[face.fragmentA];
        const uint32_t nodeB = fragmentSolverNodes[face.fragmentB];

        const float G = contactThermalConductance * face.area;
        const float dtG = fixedTimeStep * G;

        if (nodeA != InvalidSolverNode && nodeB != InvalidSolverNode) {
            nodeEntries[nodeA][0].value += dtG;
            nodeEntries[nodeA].push_back({static_cast<int>(nodeB), -dtG});
            nodeEntries[nodeB][0].value += dtG;
            nodeEntries[nodeB].push_back({static_cast<int>(nodeA), -dtG});
        } else if (nodeA != InvalidSolverNode && nodeB == InvalidSolverNode) {
            nodeEntries[nodeA][0].value += dtG;
            const uint32_t bIdx = getBoundaryValueIndex(fragB.instanceId, modelB, 0, true);
            nodeContributions[nodeA].push_back({bIdx, dtG});
        } else if (nodeA == InvalidSolverNode && nodeB != InvalidSolverNode) {
            nodeEntries[nodeB][0].value += dtG;
            const uint32_t bIdx = getBoundaryValueIndex(fragA.instanceId, modelA, 0, true);
            nodeContributions[nodeB].push_back({bIdx, dtG});
        }
    }

    // Boundary conditions
    for (uint32_t f = 0; f < fragmentCount; ++f) {
        const uint32_t node = fragmentSolverNodes[f];
        if (node == InvalidSolverNode) continue;
        const float boundaryArea = globalDomain.fragments[f].surfaceBoundaryArea;
        if (boundaryArea <= 0.0f) continue;

        const uint32_t modelId = globalDomain.fragments[f].instanceId;
        const HeatModelRuntime& model = *models.at(modelId);
        const uint32_t bcType = model.getBoundaryConditionType();

        if (bcType == RobinConvection) {
            float h = 0.0f;
            model.getBoundaryRegionHeatTransferCoefficient(0, h);
            const float G_conv = h * boundaryArea;
            const float dtG = fixedTimeStep * G_conv;
            nodeEntries[node][0].value += dtG;
            const uint32_t bIdx = getBoundaryValueIndex(modelId, model, 0, false);
            nodeContributions[node].push_back({bIdx, dtG});
        } else if (bcType == NeumannHeatFlux) {
            float q = 0.0f;
            model.getBoundaryRegionHeatFlux(0, q);
            const float dtQ = fixedTimeStep * q * boundaryArea;
            nodeContributions[node].push_back({ConstantRhsIndex, dtQ});
        }
    }

    // Flatten into CSR format
    rowOffsets.resize(solverNodeCount + 1, 0);
    for (uint32_t i = 0; i < solverNodeCount; ++i) {
        auto& row = nodeEntries[i];
        std::sort(row.begin(), row.end(), [](const MatrixEntry& a, const MatrixEntry& b) {
            return a.col < b.col;
        });
        std::vector<MatrixEntry> merged;
        for (const auto& entry : row) {
            if (!merged.empty() && merged.back().col == entry.col) {
                merged.back().value += entry.value;
            } else {
                merged.push_back(entry);
            }
        }
        rowOffsets[i + 1] = rowOffsets[i] + static_cast<int>(merged.size());
        for (const auto& entry : merged) {
            columnIndices.push_back(entry.col);
            values.push_back(entry.value);
        }
    }

    fixedRows.resize(solverNodeCount);
    uint32_t contributionOffset = 0;
    for (uint32_t i = 0; i < solverNodeCount; ++i) {
        fixedRows[i].contributionOffset = contributionOffset;
        fixedRows[i].contributionCount = static_cast<uint32_t>(nodeContributions[i].size());
        contributionOffset += fixedRows[i].contributionCount;
        for (const auto& contrib : nodeContributions[i]) {
            fixedContributions.push_back(contrib);
        }
    }

    boundaryValueCount = static_cast<uint32_t>(globalBoundaryRegions.size()) + 1;
    return true;
}

bool HeatDomainRuntime::rebuildGlobalSolver(
    const std::unordered_map<uint32_t, std::unique_ptr<HeatModelRuntime>>& models,
    VulkanDevice& vulkanDevice,
    float fixedTimeStep,
    float contactThermalConductance) {
    waitGlobalSolveIdle();
    globalSolver.reset();
    globalBoundaryRegions.clear();

    std::vector<int> rowOffsets;
    std::vector<int> columnIndices;
    std::vector<float> values;
    std::vector<float> thermalMasses;
    std::vector<SolverModelNodes> solverModelNodes;
    std::vector<HeatGlobalSolver::FixedRow> fixedRows;
    std::vector<HeatGlobalSolver::FixedContribution> fixedContributions;
    uint32_t boundaryValueCount = 0;
    if (!assembleGlobalMatrix(
            models, fixedTimeStep, contactThermalConductance, rowOffsets, columnIndices, values, thermalMasses,
            solverModelNodes, fixedRows, fixedContributions, boundaryValueCount)) {
        return false;
    }

    std::vector<HeatGlobalSolver::ModelNodes> solverModels;
    solverModels.reserve(solverModelNodes.size());
    for (const SolverModelNodes& block : solverModelNodes) {
        HeatModelRuntime& model = *models.at(block.runtimeModelId);
        solverModels.push_back({
            block.solverNodeOffset,
            block.localNodeIds,
            &model.getExternalTempBufferA(),
            &model.getExternalTempBufferB(),
            &model.getCudaTempBufferA(),
            &model.getCudaTempBufferB()});
    }

    if (solverModels.empty()) {
        allGlobalFragmentsFixedFlag = true;
        cudaReadyCounter = 0;
        vulkanReleaseCounter = 0;
        pendingCudaReadyValue = 0;
        lastVulkanReleaseValue[0] = 0;
        lastVulkanReleaseValue[1] = 0;
        globalSolveInFlight = false;
        globalTopologyDirtyFlag = false;
        globalValuesDirtyFlag = false;
        clearGlobalSynchronization();
        return true;
    }
    allGlobalFragmentsFixedFlag = false;

    globalSolver = std::make_unique<HeatGlobalSolver>();
    if (!globalSolver->initialize(
            vulkanDevice, rowOffsets, columnIndices, values, thermalMasses,
            solverModels, fixedRows, fixedContributions, boundaryValueCount)) {
        globalSolver.reset();
        clearGlobalSynchronization();
        return false;
    }

    cudaReadyCounter = 0;
    vulkanReleaseCounter = 0;
    pendingCudaReadyValue = 0;
    lastVulkanReleaseValue[0] = 0;
    lastVulkanReleaseValue[1] = 0;
    globalSolveInFlight = false;
    globalTopologyDirtyFlag = false;
    globalValuesDirtyFlag = false;
    clearGlobalSynchronization();
    return true;
}

bool HeatDomainRuntime::updateGlobalValues(
    const std::unordered_map<uint32_t, std::unique_ptr<HeatModelRuntime>>& models,
    float fixedTimeStep,
    float contactThermalConductance) {
    if (!globalSolver) {
        return false;
    }
    waitGlobalSolveIdle();

    std::vector<int> rowOffsets;
    std::vector<int> columnIndices;
    std::vector<float> values;
    std::vector<float> thermalMasses;
    std::vector<SolverModelNodes> solverModelNodes;
    std::vector<HeatGlobalSolver::FixedRow> fixedRows;
    std::vector<HeatGlobalSolver::FixedContribution> fixedContributions;
    uint32_t boundaryValueCount = 0;
    if (!assembleGlobalMatrix(
            models, fixedTimeStep, contactThermalConductance, rowOffsets, columnIndices, values, thermalMasses,
            solverModelNodes, fixedRows, fixedContributions, boundaryValueCount)) {
        return false;
    }

    if (!globalSolver->updateValues(values, thermalMasses, fixedRows, fixedContributions, boundaryValueCount)) {
        return false;
    }

    globalValuesDirtyFlag = false;
    return true;
}

bool HeatDomainRuntime::launchGlobalSolve(bool sourceIsA) {
    if (!globalSolver || globalSolveInFlight) {
        return false;
    }

    std::vector<float> boundaryTemperatures;
    boundaryTemperatures.reserve(globalBoundaryRegions.size() + 1);
    boundaryTemperatures.push_back(1.0f); // Index 0: ConstantRhsIndex

    for (const GlobalBoundaryRegion& boundaryRegion : globalBoundaryRegions) {
        float temperatureC = 0.0f;
        if (!boundaryRegion.model) {
            return false;
        }
        if (boundaryRegion.dirichlet) {
            if (!boundaryRegion.model->getBoundaryRegionTemperatureC(boundaryRegion.regionId, temperatureC)) {
                temperatureC = boundaryRegion.model->getInitialTemperatureC();
            }
        } else {
            if (!boundaryRegion.model->getBoundaryRegionAmbientTemperatureC(boundaryRegion.regionId, temperatureC)) {
                temperatureC = 20.0f;
            }
        }
        boundaryTemperatures.push_back(temperatureC);
    }

    const uint32_t destIdx = sourceIsA ? 1 : 0;
    const uint64_t destVulkanRelease = lastVulkanReleaseValue[destIdx];
    const uint64_t cudaReady = ++cudaReadyCounter;

    if (!globalSolver->launch(sourceIsA, boundaryTemperatures, destVulkanRelease, cudaReady)) {
        return false;
    }

    globalSolveInFlight = true;
    pendingDestinationIsA = !sourceIsA;
    pendingCudaReadyValue = cudaReady;
    return true;
}

bool HeatDomainRuntime::pollGlobalSolve(bool& completedDestinationIsA) {
    if (!globalSolveInFlight || !globalSolver) {
        return false;
    }
    const auto result = globalSolver->poll();
    if (result == HeatGlobalSolver::SolvePollResult::Complete) {
        globalSolveInFlight = false;
        completedDestinationIsA = pendingDestinationIsA;
        return true;
    }
    if (result == HeatGlobalSolver::SolvePollResult::Failed) {
        globalSolveInFlight = false;
        return false;
    }
    return false;
}

void HeatDomainRuntime::prepareGlobalRenderSynchronization(bool activeIsA, bool newSolveCompleted) {
    if (!globalSolver) {
        clearGlobalSynchronization();
        return;
    }
    const uint32_t activeIdx = activeIsA ? 0 : 1;
    const uint64_t vulkanRelease = ++vulkanReleaseCounter;
    lastVulkanReleaseValue[activeIdx] = vulkanRelease;

    if (newSolveCompleted) {
        globalSynchronization.waitSemaphore = globalSolver->getCudaReadySemaphore();
        globalSynchronization.waitValue = pendingCudaReadyValue;
    } else {
        globalSynchronization.waitSemaphore = VK_NULL_HANDLE;
        globalSynchronization.waitValue = 0;
    }
    globalSynchronization.signalSemaphore = globalSolver->getVulkanReleaseSemaphore();
    globalSynchronization.signalValue = vulkanRelease;
}

void HeatDomainRuntime::waitGlobalSolveIdle() {
    if (globalSolver) {
        globalSolver->waitIdle();
    }
    globalSolveInFlight = false;
}