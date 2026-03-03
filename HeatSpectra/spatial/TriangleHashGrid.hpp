#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <unordered_map>

class Model;

class TriangleHashGrid {
public:
    TriangleHashGrid();
    ~TriangleHashGrid();

    void build(const Model& model, const glm::vec3& gridMin, const glm::vec3& gridMax, float cellSize);
    void getNearbyTriangles(const glm::vec3& position, std::vector<size_t>& outTriangles) const;
    void getNearbyTriangles(const glm::vec3& position, int radiusCells, std::vector<size_t>& outTriangles) const;
    void clear();

    size_t hashCell(int x, int y, int z) const;
    glm::ivec3 worldToCell(const glm::vec3& pos) const;

private:
    std::unordered_map<size_t, std::vector<size_t>> grid_;  
    glm::vec3 gridMin_;
    glm::vec3 gridMax_;
    float cellSize_;
    glm::ivec3 gridDim_;

};
