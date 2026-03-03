#pragma once

#include <cstdint>
#include <vector>
#include <map>
#include <glm/glm.hpp>
#include "scene/Model.hpp"
#include "spatial/VoxelGrid.hpp"

struct MeshTriangleGPU {
    glm::vec4 v0, v1, v2; 
};

class VoronoiIntegrator {
public:
    VoronoiIntegrator() = default;
    
    void extractNeighborIndices(const std::vector<std::vector<uint32_t>>& neighborIndices, const std::vector<glm::dvec3>& seedPositions, int K);
    void extractMeshTriangles(const Model& surfaceMesh); 
    void computeNeighbors(const std::vector<glm::dvec3>& seedPositions,int K);  
    
    void computeSurfacePointMapping(
        const std::vector<glm::vec3>& surfacePoints,
        const std::vector<uint32_t>& seedFlags,
        uint32_t K,
        std::vector<uint32_t>& outCellIndices
    ) const;
    
    // Getters
    const std::vector<uint32_t>& getNeighborIndices() const { return neighborIndices; }
    const std::vector<MeshTriangleGPU>& getMeshTriangles() const { return meshTriangles; }
    const std::vector<glm::vec4>& getSeedPositions() const { return seedPositions; }
    const std::vector<float>& getKNearestDistances() const { return kNearestDistances; }
    
    bool hasVoxelGrid() const { return voxelGrid.getGridSize() > 0; }
    const VoxelGrid& getVoxelGrid() const { return voxelGrid; }
    
private:
    bool pointInUnrestrictedCell(uint32_t cellId, const glm::vec3& p, uint32_t K) const;
    
    std::vector<uint32_t> neighborIndices;
    std::vector<MeshTriangleGPU> meshTriangles;
    
    std::vector<glm::vec4> seedPositions;
    std::vector<float> kNearestDistances;
    
    VoxelGrid voxelGrid;
};
