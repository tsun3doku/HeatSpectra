#include "GlobalCutCell.cuh"

#include "KNNNeighbors.hpp"
#include "RVD.hpp"

#include "../VoronoiGpuStructs.hpp"
#include "../../spatial/VoxelGrid.hpp"
#include "../../cuda/CudaBuffer.cuh"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <unordered_map>

namespace voronoi {

namespace {

constexpr uint32_t InvalidId = 0xffffffffu;

} // namespace

class GlobalCutCell::Implementation {
public:
    Implementation() = default;

    bool initialize(VulkanDevice& vulkanDevice) {
        if (!rvd.initialize(vulkanDevice) || !knn.initialize(vulkanDevice)) {
            rvd.cleanup();
            knn.cleanup();
            return false;
        }
        initialized = true;
        return true;
    }

    void cleanup() {
        rvd.cleanup();
        knn.cleanup();
        d_contactFaces.reset();
        initialized = false;
    }

    bool build(const GlobalCutCell::BuildInput& input, GlobalCutCell::BuildResult& result) {
        result = {};
        if (!initialized || !input.instances || input.instances->empty() ||
            !input.seeds || input.seeds->empty() || !input.domainCorners ||
            !(input.sdfSpacing > 0.0f) || !std::isfinite(input.sdfSpacing) ||
            !(input.nominalCellSize > 0.0f) || !std::isfinite(input.nominalCellSize) ||
            input.sdfGridDim.x < 2 || input.sdfGridDim.y < 2 || input.sdfGridDim.z < 2) {
            return false;
        }
        const size_t seedCount = input.seeds->size();
        if (seedCount > std::numeric_limits<uint32_t>::max()) return false;
        for (const GlobalCutCell::InstanceInput& instance : *input.instances) {
            if (instance.runtimeModelId == 0 || !instance.voxelGrid || !instance.triangles ||
                instance.triangles->empty()) {
                return false;
            }
        }

        const uint32_t count = uint32_t(seedCount);
        const uint32_t k = std::min(input.maxNeighbors, count - 1u);
        if (k == 0) return false;
        std::vector<uint32_t> knnRows;
        knnRows.reserve(count);
        for (uint32_t cell = 0; cell < count; ++cell) knnRows.push_back(cell);
        if (!knn.build(*input.seeds, k, knnRows)) return false;

        RVDConfig config{};
        config.restrictedChunkSize = 2048;

        const size_t numInstances = input.instances->size();
        std::vector<std::vector<uint32_t>> allInstanceSeedToFragment(numInstances, std::vector<uint32_t>(seedCount, InvalidId));
        std::vector<uint32_t> fragmentInstanceIds;
        std::vector<uint32_t> fragmentInstanceIndices;
        std::vector<uint32_t> fragmentSeedIds;
        std::vector<float> surfaceBoundaryAreas;
        std::vector<float> fragmentVolumes;
        std::vector<uint32_t> faceInstanceIds;
        std::vector<uint32_t> faceFragmentA;
        std::vector<uint32_t> faceFragmentB;
        std::vector<float> faceAreas;
        std::vector<uint32_t> instanceFragmentCounts;

        for (size_t instIdx = 0; instIdx < numInstances; ++instIdx) {
            const InstanceInput& instance = (*input.instances)[instIdx];
            std::vector<uint32_t> seedFlags(seedCount, 0u);
            if (!rvd.prepareMeshGeometry(
                    *instance.triangles, *instance.voxelGrid, *input.seeds,
                    input.sdfGridMin, input.sdfGridDim, input.sdfSpacing, seedFlags)) {
                std::cerr << "[GlobalCutCell] SDF preparation failed runtimeModelId="
                          << instance.runtimeModelId << std::endl;
                return false;
            }

            RVDInput rvdInput{};
            rvdInput.seeds = input.seeds;
            rvdInput.seedFlags = &seedFlags;
            rvdInput.candidateNeighbors = &knn;
            rvdInput.domainCorners = input.domainCorners;
            rvdInput.nominalCellSize = input.nominalCellSize;
            RVDResult rvdResult;
            if (!rvd.build(rvdInput, config, rvdResult)) {
                std::cerr << "[GlobalCutCell] RVD build failed runtimeModelId="
                          << instance.runtimeModelId << std::endl;
                return false;
            }

            const uint32_t fragmentBase = uint32_t(fragmentInstanceIds.size());
            for (uint32_t seed = 0; seed < count; ++seed) {
                if ((rvdResult.nodeFlags[seed] & NodeFlags::Ghost) != 0u) continue;
                const float area = rvdResult.surfacePatchAreas[seed];
                const float volume = rvdResult.nodes[seed].volume;
                if (!(area >= 0.0f) || !std::isfinite(area) ||
                    !std::isfinite(volume)) {
                    std::cerr << "[GlobalCutCell] invalid fragment runtimeModelId="
                              << instance.runtimeModelId << " seed=" << seed << std::endl;
                    return false;
                }
                const uint32_t fragId = uint32_t(fragmentInstanceIds.size());
                allInstanceSeedToFragment[instIdx][seed] = fragId;
                fragmentInstanceIds.push_back(instance.runtimeModelId);
                fragmentInstanceIndices.push_back(uint32_t(instIdx));
                fragmentSeedIds.push_back(seed);
                surfaceBoundaryAreas.push_back(area);
                fragmentVolumes.push_back(volume);
            }

            using FaceKey = std::pair<uint32_t, uint32_t>;
            std::map<FaceKey, std::pair<float, uint32_t>> faces; // area sum, count

            for (uint32_t seed = 0; seed < count; ++seed) {
                const uint32_t fragA = allInstanceSeedToFragment[instIdx][seed];
                if (fragA == InvalidId) continue;

                const Node& node = rvdResult.nodes[seed];
                const size_t base = size_t(seed) * RVDMaximumInterfaces;

                for (uint32_t i = 0; i < node.interfaceNeighborCount; ++i) {
                    const uint32_t neighbor = rvdResult.interfaceNeighborIds[base + i];
                    if (neighbor >= count) continue;

                    const uint32_t fragB = allInstanceSeedToFragment[instIdx][neighbor];
                    if (fragB == InvalidId || fragA == fragB) continue;

                    const float area = std::abs(rvdResult.interfaceAreas[base + i]);
                    if (!(area > 0.0f) || !std::isfinite(area)) continue;

                    const FaceKey key{
                        std::min(fragA, fragB),
                        std::max(fragA, fragB)
                    };

                    auto& [areaSum, count] = faces[key];
                    areaSum += area;
                    ++count;
                }
            }

            for (const auto& [key, value] : faces) {
                faceInstanceIds.push_back(instance.runtimeModelId);
                faceFragmentA.push_back(key.first);
                faceFragmentB.push_back(key.second);
                faceAreas.push_back(value.first / float(value.second));
            }

            instanceFragmentCounts.push_back(
                uint32_t(fragmentInstanceIds.size()) - fragmentBase);
        }

        struct CutFaceKey {
            uint32_t a = 0;
            uint32_t b = 0;
            bool operator<(const CutFaceKey& other) const {
                if (a != other.a) return a < other.a;
                return b < other.b;
            }
        };
        struct CutFaceVal {
            float area = 0.0f;
            float gap = 0.0f;
            uint32_t count = 0;
        };
        std::map<CutFaceKey, CutFaceVal> mergedCutFaces;

        auto addCutFace = [&](uint32_t fragA, uint32_t fragB, float area, float gap) {
            if (fragA == fragB || !(area > 0.0f) || !std::isfinite(area)) return;
            uint32_t a = fragA, b = fragB;
            if (a > b) std::swap(a, b);
            auto& val = mergedCutFaces[{a, b}];
            val.area += area;
            val.gap = (val.count == 0) ? gap : std::min(val.gap, gap);
            val.count++;
        };

        const float maxGap = (input.maximumGap > 0.0f) ? input.maximumGap : (input.nominalCellSize * 2.0f);

        for (uint32_t seed = 0; seed < count; ++seed) {
            for (size_t instA = 0; instA < numInstances; ++instA) {
                const uint32_t fragA = allInstanceSeedToFragment[instA][seed];
                if (fragA == InvalidId) continue;
                for (size_t instB = instA + 1; instB < numInstances; ++instB) {
                    const uint32_t fragB = allInstanceSeedToFragment[instB][seed];
                    if (fragB == InvalidId) continue;
                    float area = 0.5f * (surfaceBoundaryAreas[fragA] + surfaceBoundaryAreas[fragB]);
                    if (area <= 0.0f) {
                        area = input.nominalCellSize * input.nominalCellSize * 0.5f;
                    }
                    addCutFace(fragA, fragB, area, 0.0f);
                }
            }
        }

        std::vector<uint32_t> knnNeighbors;
        if (knn.downloadPrefix(k, knnNeighbors)) {
            for (uint32_t seed = 0; seed < count; ++seed) {
                const glm::vec3 posA = glm::vec3((*input.seeds)[seed]);
                for (size_t instA = 0; instA < numInstances; ++instA) {
                    const uint32_t fragA = allInstanceSeedToFragment[instA][seed];
                    if (fragA == InvalidId) continue;

                    for (uint32_t j = 0; j < k; ++j) {
                        const uint32_t nbr = knnNeighbors[seed * k + j];
                        if (nbr >= count || nbr == seed) continue;
                        const glm::vec3 posB = glm::vec3((*input.seeds)[nbr]);
                        const float dist = glm::length(posB - posA);
                        if (dist > maxGap) continue;

                        for (size_t instB = 0; instB < numInstances; ++instB) {
                            if (instA == instB) continue;
                            const uint32_t fragB = allInstanceSeedToFragment[instB][nbr];
                            if (fragB == InvalidId) continue;

                            float area = 0.5f * (surfaceBoundaryAreas[fragA] + surfaceBoundaryAreas[fragB]);
                            if (area <= 0.0f) {
                                area = input.nominalCellSize * input.nominalCellSize * 0.25f;
                            }
                            addCutFace(fragA, fragB, area, dist);
                        }
                    }
                }
            }
        }

        std::vector<heat::ContactFace> contactFaces;
        contactFaces.reserve(mergedCutFaces.size());
        std::vector<std::pair<uint32_t, uint32_t>> activeContactPairs;
        std::map<std::pair<uint32_t, uint32_t>, uint32_t> pairLookup;

        result.cutFaceFragmentA.reserve(mergedCutFaces.size());
        result.cutFaceFragmentB.reserve(mergedCutFaces.size());
        result.cutFaceAreas.reserve(mergedCutFaces.size());
        result.cutFaceGaps.reserve(mergedCutFaces.size());

        for (const auto& [key, val] : mergedCutFaces) {
            result.cutFaceFragmentA.push_back(key.a);
            result.cutFaceFragmentB.push_back(key.b);
            result.cutFaceAreas.push_back(val.area);
            result.cutFaceGaps.push_back(val.gap);

            const uint32_t fragA = key.a;
            const uint32_t fragB = key.b;
            if (fragA >= fragmentInstanceIndices.size() || fragB >= fragmentInstanceIndices.size()) continue;

            const uint32_t instA = fragmentInstanceIndices[fragA];
            const uint32_t instB = fragmentInstanceIndices[fragB];
            if (instA == instB) continue;

            uint32_t chA = instA;
            uint32_t chB = instB;

            const uint32_t seedA = fragmentSeedIds[fragA];
            const uint32_t seedB = fragmentSeedIds[fragB];
            if (seedA >= input.seeds->size() || seedB >= input.seeds->size()) continue;

            const glm::vec3 sA = glm::vec3((*input.seeds)[seedA]);
            const glm::vec3 sB = glm::vec3((*input.seeds)[seedB]);
            glm::vec3 diff = sB - sA;
            const float len = glm::length(diff);
            glm::vec3 normal = (len > 1e-6f) ? (diff / len) : glm::vec3(0.0f, 0.0f, 1.0f);

            if (chA > chB) {
                std::swap(chA, chB);
                normal = -normal;
            }

            auto pairKey = std::make_pair(chA, chB);
            if (pairLookup.find(pairKey) == pairLookup.end()) {
                pairLookup[pairKey] = static_cast<uint32_t>(activeContactPairs.size());
                activeContactPairs.push_back(pairKey);
            }

            const glm::vec3 center = 0.5f * (sA + sB);
            const float area = val.area;
            const float gap = val.gap;

            heat::ContactFace face{};
            face.centroidArea = glm::vec4(center, area);
            face.normalGap = glm::vec4(normal, gap);
            face.channelA = chA;
            face.channelB = chB;

            contactFaces.push_back(face);
        }

        d_contactFaces.allocate(std::max<size_t>(1, contactFaces.size()));
        if (!contactFaces.empty()) {
            d_contactFaces.upload(contactFaces.data(), contactFaces.size());
        }

        result.d_contactFaces = d_contactFaces.get();
        result.contactFaceCount = static_cast<uint32_t>(contactFaces.size());
        result.activePairs = std::move(activeContactPairs);

        result.fragmentInstanceIds = std::move(fragmentInstanceIds);
        result.fragmentSeedIds = std::move(fragmentSeedIds);
        result.surfaceBoundaryAreas = std::move(surfaceBoundaryAreas);
        result.fragmentVolumes = std::move(fragmentVolumes);
        result.faceInstanceIds = std::move(faceInstanceIds);
        result.faceFragmentA = std::move(faceFragmentA);
        result.faceFragmentB = std::move(faceFragmentB);
        result.faceAreas = std::move(faceAreas);
        result.instanceFragmentCounts = std::move(instanceFragmentCounts);
        return true;
    }

    RVD rvd;
    KNNNeighbors knn;
    cudaUtils::CudaBuffer<heat::ContactFace> d_contactFaces;
    bool initialized = false;
};

GlobalCutCell::GlobalCutCell() : implementation(std::make_unique<Implementation>()) {}
GlobalCutCell::~GlobalCutCell() = default;
bool GlobalCutCell::initialize(VulkanDevice& vulkanDevice) {
    return implementation->initialize(vulkanDevice);
}
bool GlobalCutCell::build(const BuildInput& input, BuildResult& result) {
    return implementation->build(input, result);
}
void GlobalCutCell::cleanup() { implementation->cleanup(); }

} // namespace voronoi