#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <cstdint>
#include <string>

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

    // Getters
    const std::vector<uint8_t>& getOccupancyData() const { return occupancy_; }
    const VoxelGridParams& getParams() const { return params_; }
    int getGridSize() const { return params_.gridDim.x; }
    
    void exportOccupancyVisualization(const std::string& filename) const;
    
    const std::vector<glm::vec3>& getMeshPoints() const { return meshPoints_; }
    const std::vector<int32_t>& getMeshTriangles() const { return meshTriangles_; }
    const std::vector<int32_t>& getTrianglesList() const { return trianglesList_; }
    const std::vector<int32_t>& getOffsets() const { return offsets_; }

private:
    bool isInsideMonteCarlo(const glm::vec3& point, const Model& mesh, int numRays = 5) const;
    bool rayTriangleIntersect(const glm::vec3& rayOrigin, const glm::vec3& rayDir, const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2, float& t) const;
    float distanceToNearestTriangle(const glm::vec3& point, const Model& mesh, const TriangleHashGrid& triangleGrid) const;
    float pointToTriangleDistance(const glm::vec3& p, const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2) const;
    bool triangleAABBIntersect(const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2, const glm::vec3& boxMin, const glm::vec3& boxMax) const;
    size_t getCornerIndex(int x, int y, int z) const;

    void buildTriangleLists(const Model& mesh, const TriangleHashGrid& triangleGrid);

    VoxelGridParams params_;  
    std::vector<uint8_t> occupancy_;
    
    // Mesh data for GPU
    std::vector<glm::vec3> meshPoints_;      // Mesh vertices
    std::vector<int32_t> meshTriangles_;     // 3 vertex indices per triangle
    std::vector<int32_t> trianglesList_;     // List of triangle IDs per voxel
    std::vector<int32_t> offsets_;           // Offset into trianglesList per voxel
};
