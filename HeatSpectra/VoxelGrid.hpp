#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <cstdint>
#include <string>
#include <algorithm>

class Model;
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

    void build(const Model& mesh, const TriangleHashGrid& triangleGrid, int gridSize);
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
    static constexpr int NUM_RANDOM_DIRS = 7;
    static const glm::vec3 RANDOM_DIRS[NUM_RANDOM_DIRS];

    glm::vec3 toCanonical(const glm::vec3& worldPos) const;
    glm::vec3 toWorld(const glm::vec3& canonicalPos) const;
    static int clampInt(int v, int lo, int hi) { return std::max(lo, std::min(v, hi)); }
    
    // Intersection tests
    static bool triBoxOverlap(const float boxcenter[3], const float boxhalfsize[3], const float triverts[3][3]);
    bool isInsideMonteCarlo(const glm::vec3& point, const Model& mesh, int numRays = 5) const;
    bool rayTriangleIntersect(const glm::vec3& rayOrigin, const glm::vec3& rayDir, const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2, float& t) const;
    float distanceToNearestTriangle(const glm::vec3& point, const Model& mesh, const TriangleHashGrid& triangleGrid) const;
    float pointToTriangleDistance(const glm::vec3& p, const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2) const;

    size_t getCornerIndex(int x, int y, int z) const;

    void buildTriangleLists(const Model& mesh, const TriangleHashGrid& triangleGrid);

    VoxelGridParams params;  
    std::vector<uint8_t> occupancy;
    
    std::vector<glm::vec3> meshPoints;
    std::vector<int32_t> meshTriangles;
    std::vector<int32_t> trianglesList;
    std::vector<int32_t> offsets;
};
