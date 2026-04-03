#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

class TriangleHashGrid;

class VoxelGrid {
public:
    struct VoxelGridParams {
        alignas(16) glm::vec3 gridMin;
        float cellSize;
        alignas(16) glm::ivec3 gridDim;
        uint32_t totalCells;
    };

    VoxelGrid();
    ~VoxelGrid();

    void build(
        const std::vector<glm::vec3>& positions,
        const std::vector<uint32_t>& indices,
        const TriangleHashGrid& triangleGrid,
        int gridSize);
    uint8_t getOccupancy(int x, int y, int z) const;
    glm::vec3 getCornerPosition(int x, int y, int z) const;
    glm::ivec3 worldToVoxel(const glm::vec3& pos) const;

    void exportOccupancyVisualization(const std::string& filename) const;

    // Getters
    const std::vector<uint8_t>& getOccupancyData() const { return occupancy; }
    const VoxelGridParams& getParams() const { return params; }
    int getGridSize() const { return params.gridDim.x; }
        
    const std::vector<glm::vec3>& getMeshPoints() const { return meshPoints; }
    const std::vector<int32_t>& getMeshTriangles() const { return meshTriangles; }
    const std::vector<int32_t>& getTrianglesList() const { return trianglesList; }
    const std::vector<int32_t>& getOffsets() const { return offsets; }

private:
    static constexpr float CANONICAL_DOMAIN_SIZE = 1000.0f;
    static constexpr float CANONICAL_SCALE = 990.222f;
    static constexpr float CANONICAL_BIAS = 4.998f;
    glm::vec3 toCanonical(const glm::vec3& worldPos) const;
    glm::vec3 toWorld(const glm::vec3& canonicalPos) const;
    static int clampInt(int v, int lo, int hi) { return std::max(lo, std::min(v, hi)); }
    
    static bool triBoxOverlap(const float boxcenter[3], const float boxhalfsize[3], const float triverts[3][3]);
    float distanceToNearestTriangle(
        const glm::vec3& point,
        const std::vector<glm::vec3>& positions,
        const std::vector<uint32_t>& indices,
        const TriangleHashGrid& triangleGrid) const;

    size_t getCornerIndex(int x, int y, int z) const;

    void buildTriangleLists(
        const std::vector<glm::vec3>& positions,
        const std::vector<uint32_t>& indices,
        const TriangleHashGrid& triangleGrid);

    VoxelGridParams params;  
    std::vector<uint8_t> occupancy;
    
    std::vector<glm::vec3> meshPoints;
    std::vector<int32_t> meshTriangles;
    std::vector<int32_t> trianglesList;
    std::vector<int32_t> offsets;
};
