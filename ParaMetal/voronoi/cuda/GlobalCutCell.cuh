#pragma once

#include "../../heat/HeatGpuStructs.hpp"
#include <array>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

class VoxelGrid;
class VulkanDevice;

namespace voronoi {

namespace globalcutcell {

struct InstanceInput {
    uint32_t runtimeModelId = 0;
    const VoxelGrid* voxelGrid = nullptr;
    const std::vector<std::array<glm::vec4, 3>>* triangles = nullptr;
};

struct BuildInput {
    const std::vector<InstanceInput>* instances = nullptr;
    const std::vector<glm::vec4>* seeds = nullptr;
    const std::array<glm::vec3, 8>* domainCorners = nullptr;
    glm::vec3 sdfGridMin{};
    glm::ivec3 sdfGridDim{};
    float sdfSpacing = 0.0f;
    float nominalCellSize = 0.0f;
    float maximumGap = 0.0f;
    uint32_t maxNeighbors = 50;
};

struct BuildResult {
    std::vector<uint32_t> fragmentInstanceIds;
    std::vector<uint32_t> fragmentSeedIds;
    std::vector<float> surfaceBoundaryAreas;
    std::vector<float> fragmentVolumes;
    std::vector<uint32_t> faceInstanceIds;
    std::vector<uint32_t> faceFragmentA;
    std::vector<uint32_t> faceFragmentB;
    std::vector<float> faceAreas;
    std::vector<uint32_t> cutFaceFragmentA;
    std::vector<uint32_t> cutFaceFragmentB;
    std::vector<float> cutFaceAreas;
    std::vector<float> cutFaceGaps;
    std::vector<uint32_t> instanceFragmentCounts;

    const heat::ContactFace* d_contactFaces = nullptr;
    uint32_t contactFaceCount = 0;
    std::vector<std::pair<uint32_t, uint32_t>> activePairs;
};

} // namespace globalcutcell

class GlobalCutCell {
public:
    using InstanceInput = globalcutcell::InstanceInput;
    using BuildInput = globalcutcell::BuildInput;
    using BuildResult = globalcutcell::BuildResult;

    GlobalCutCell();
    ~GlobalCutCell();

    GlobalCutCell(const GlobalCutCell&) = delete;
    GlobalCutCell& operator=(const GlobalCutCell&) = delete;

    bool initialize(VulkanDevice& vulkanDevice);
    bool build(const BuildInput& input, BuildResult& result);
    void cleanup();

private:
    class Implementation;
    std::unique_ptr<Implementation> implementation;
};

} // namespace voronoi