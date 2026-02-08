#pragma once

#include <cstdint>
#include <vector>
#include <map>
#include <glm/glm.hpp>
#include "Model.hpp"
#include "VoxelGrid.hpp"

struct MeshTriangleGPU {
    glm::vec4 v0, v1, v2; // Triangle vertices (w unused), normal removed to save memory
};

class VoronoiIntegrator {
public:
    VoronoiIntegrator() = default;
    
    void extractNeighborIndices(
        const std::vector<std::vector<uint32_t>>& neighborIndices,
        const std::vector<glm::dvec3>& seedPositions,
        int K
    );
    
    void extractMeshTriangles(const Model& surfaceMesh);
    
    void computeNeighbors(const std::vector<glm::dvec3>& seedPositions,int K);  
    
    void buildVoxelGrid(const Model& mesh,const TriangleHashGrid& triangleGrid,int gridSize = 64);
    
    void computeSurfacePointMapping(
        const std::vector<glm::vec3>& surfacePoints,
        const std::vector<uint32_t>& seedFlags,
        uint32_t K,
        std::vector<uint32_t>& outCellIndices
    ) const;
    
    // Getters
    const std::vector<uint32_t>& getNeighborIndices() const { return neighborIndices_; }  
    const std::vector<MeshTriangleGPU>& getMeshTriangles() const { return meshTriangles_; }
    const std::vector<glm::vec4>& getSeedPositions() const { return seedPositions_; }
    const std::vector<float>& getKNearestDistances() const { return kNearestDistances_; }
    
    bool hasVoxelGrid() const { return voxelGrid_.getGridSize() > 0; }
    const VoxelGrid& getVoxelGrid() const { return voxelGrid_; }
    
private:
    bool pointInUnrestrictedCell(uint32_t cellId, const glm::vec3& p, uint32_t K) const;
    
    std::vector<uint32_t> neighborIndices_;      // Flat K*numCells neighbor indices for GPU
    std::vector<MeshTriangleGPU> meshTriangles_; // Mesh triangles for clipping
    
    
    std::vector<glm::vec4> seedPositions_;       // Seed positions for GPU
    std::vector<float> kNearestDistances_;       // Max K nearest neighbor distance per cell
    
    VoxelGrid voxelGrid_;                        // Voxel occupancy grid for GPU origin finding
};
