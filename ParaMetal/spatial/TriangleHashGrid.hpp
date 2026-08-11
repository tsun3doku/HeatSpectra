#pragma once
#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>

class TriangleHashGrid {
public:
    TriangleHashGrid() = default;
    ~TriangleHashGrid() = default;

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
    void buildTriangles(const std::vector<glm::vec3>& vertices, const std::vector<uint32_t>& indices);
    void appendCellTriangles(size_t cell, std::vector<size_t>& outTriangles) const;
    static void deduplicate(std::vector<size_t>& triangles);
    size_t cellIndex(int x, int y, int z) const;
    glm::ivec3 worldToCell(const glm::vec3& position) const;

    std::unordered_map<size_t, std::vector<size_t>> grid;
    glm::vec3 gridMin{0.0f};
    glm::vec3 gridMax{1.0f};
    float cellSize = 1.0f;
    glm::ivec3 gridDim{1};
};
