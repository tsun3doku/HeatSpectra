#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

struct MeshTriangleGPU {
    glm::vec4 v0, v1, v2;
};

class VoronoiIntegrator {
public:
    VoronoiIntegrator() = default;

    void extractNeighborIndices(const std::vector<std::vector<uint32_t>>& neighborIndices, const std::vector<glm::dvec3>& seedPositions, int K);
    void extractMeshTriangles(const std::vector<glm::vec3>& positions, const std::vector<uint32_t>& indices);
    void computeNeighbors(const std::vector<glm::dvec3>& seedPositions,int K);

    // Getters
    const std::vector<uint32_t>& getNeighborIndices() const { return neighborIndices; }
    const std::vector<MeshTriangleGPU>& getMeshTriangles() const { return meshTriangles; }
    const std::vector<glm::vec4>& getSeedPositions() const { return seedPositions; }

private:
    std::vector<uint32_t> neighborIndices;
    std::vector<MeshTriangleGPU> meshTriangles;

    std::vector<glm::vec4> seedPositions;
};