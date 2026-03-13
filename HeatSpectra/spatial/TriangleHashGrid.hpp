#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <unordered_map>
#include <unordered_set>

class Model;

class TriangleHashGrid {
public:
    TriangleHashGrid();
    ~TriangleHashGrid() = default;

    void build(const Model& model, const glm::vec3& gridMin, const glm::vec3& gridMax, float cellSize);
    void build(
        const std::vector<glm::vec3>& vertices,
        const std::vector<uint32_t>& indices,
        const glm::vec3& gridMin,
        const glm::vec3& gridMax,
        float cellSize);
    void getNearbyTriangles(const glm::vec3& position, std::vector<size_t>& outTriangles) const;
    void getNearbyTriangles(const glm::vec3& position, int radiusCells, std::vector<size_t>& outTriangles) const;
    void getTrianglesAlongRay(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, std::vector<size_t>& outTriangles) const;

private:
    void clear();
    void initializeGrid(const glm::vec3& gridMin, const glm::vec3& gridMax, float cellSize);
    void buildTriangles(const std::vector<glm::vec3>& vertices, const std::vector<uint32_t>& indices);
    void addCellTriangles(size_t hash, std::unordered_set<size_t>& seenTriangles, std::vector<size_t>& outTriangles) const;
    size_t hashCell(int x, int y, int z) const;
    glm::ivec3 worldToCell(const glm::vec3& pos) const;

    std::unordered_map<size_t, std::vector<size_t>> grid_;  
    glm::vec3 gridMin_;
    glm::vec3 gridMax_;
    float cellSize_;
    glm::ivec3 gridDim_;

};
