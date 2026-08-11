#include "TriangleHashGrid.hpp"

#include "util/GeometryUtils.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <omp.h>

void TriangleHashGrid::build(
    const std::vector<glm::vec3>& vertices,
    const std::vector<uint32_t>& indices,
    const glm::vec3& minimum,
    const glm::vec3& maximum,
    float width) {
    grid.clear();
    gridMin = minimum;
    gridMax = maximum;
    cellSize = width;
    gridDim = computeGridDimensions(minimum, maximum, width);
    buildTriangles(vertices, indices);
}

void TriangleHashGrid::getNearbyTriangles(const glm::vec3& position, std::vector<size_t>& outTriangles) const {
    getNearbyTriangles(position, 1, outTriangles);
}

void TriangleHashGrid::getNearbyTriangles(const glm::vec3& position, int radiusCells, std::vector<size_t>& outTriangles) const {
    outTriangles.clear();

    const glm::ivec3 cell = worldToCell(position);
    const int radius = std::max(1, radiusCells);
    for (int dz = -radius; dz <= radius; ++dz) {
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                const int cx = cell.x + dx;
                const int cy = cell.y + dy;
                const int cz = cell.z + dz;

                if (cx < 0 || cx >= gridDim.x ||
                    cy < 0 || cy >= gridDim.y ||
                    cz < 0 || cz >= gridDim.z) {
                    continue;
                }

                appendCellTriangles(cellIndex(cx, cy, cz), outTriangles);
            }
        }
    }
    deduplicate(outTriangles);
}

void TriangleHashGrid::getTrianglesAlongRay(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, std::vector<size_t>& outTriangles) const {
    outTriangles.clear();

    if (grid.empty() || maxDistance <= 0.0f || cellSize <= 0.0f) {
        return;
    }

    const float directionLength = glm::length(direction);
    if (directionLength <= 1e-8f) {
        return;
    }

    const glm::vec3 rayDirection = direction / directionLength;
    float tEnter = 0.0f;
    float tExit = maxDistance;
    if (!intersectRayAabb(origin, rayDirection, gridMin, gridMax, maxDistance, tEnter, tExit)) {
        return;
    }

    const float startT = std::max(0.0f, tEnter);
    const float endT = std::min(maxDistance, tExit);
    if (startT > endT) {
        return;
    }

    constexpr float startBias = 1e-4f;
    glm::vec3 startPoint = origin + rayDirection * startT;
    if (startT < endT) {
        startPoint += rayDirection * startBias;
    }

    glm::ivec3 cell = worldToCell(startPoint);
    glm::ivec3 step(0);
    glm::vec3 tMax(std::numeric_limits<float>::infinity());
    glm::vec3 tDelta(std::numeric_limits<float>::infinity());

    for (int axis = 0; axis < 3; ++axis) {
        const float dirAxis = rayDirection[axis];
        if (std::fabs(dirAxis) <= 1e-8f) {
            continue;
        }

        if (dirAxis > 0.0f) {
            step[axis] = 1;
            const float nextBoundary = gridMin[axis] + static_cast<float>(cell[axis] + 1) * cellSize;
            tMax[axis] = (nextBoundary - startPoint[axis]) / dirAxis;
        } else {
            step[axis] = -1;
            const float nextBoundary = gridMin[axis] + static_cast<float>(cell[axis]) * cellSize;
            tMax[axis] = (nextBoundary - startPoint[axis]) / dirAxis;
        }

        tDelta[axis] = cellSize / std::fabs(dirAxis);
    }

    const float travelLimit = endT - startT;
    while (cell.x >= 0 && cell.x < gridDim.x &&
           cell.y >= 0 && cell.y < gridDim.y &&
           cell.z >= 0 && cell.z < gridDim.z) {
        appendCellTriangles(cellIndex(cell.x, cell.y, cell.z), outTriangles);

        const float nextStepT = std::min(tMax.x, std::min(tMax.y, tMax.z));
        if (nextStepT > travelLimit) {
            break;
        }

        if (tMax.x <= tMax.y && tMax.x <= tMax.z) {
            cell.x += step.x;
            tMax.x += tDelta.x;
        } else if (tMax.y <= tMax.z) {
            cell.y += step.y;
            tMax.y += tDelta.y;
        } else {
            cell.z += step.z;
            tMax.z += tDelta.z;
        }
    }
    deduplicate(outTriangles);
}

void TriangleHashGrid::buildTriangles(const std::vector<glm::vec3>& vertices, const std::vector<uint32_t>& indices) {
    std::vector<std::unordered_map<size_t, std::vector<size_t>>> threadLocalGrids;

    #pragma omp parallel
    {
        #pragma omp single
        {
            threadLocalGrids.resize(omp_get_num_threads());
        }

        const int threadId = omp_get_thread_num();

        #pragma omp for
        for (int indexBase = 0; indexBase < static_cast<int>(indices.size()); indexBase += 3) {
            const uint32_t i0 = indices[static_cast<size_t>(indexBase)];
            const uint32_t i1 = indices[static_cast<size_t>(indexBase + 1)];
            const uint32_t i2 = indices[static_cast<size_t>(indexBase + 2)];
            if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size()) {
                continue;
            }

            const glm::vec3& v0 = vertices[i0];
            const glm::vec3& v1 = vertices[i1];
            const glm::vec3& v2 = vertices[i2];

            const glm::vec3 triMin = glm::min(glm::min(v0, v1), v2);
            const glm::vec3 triMax = glm::max(glm::max(v0, v1), v2);

            const glm::ivec3 cellMin = worldToCell(triMin);
            const glm::ivec3 cellMax = worldToCell(triMax);
            const size_t triangleIndex = static_cast<size_t>(indexBase / 3);

            for (int z = cellMin.z; z <= cellMax.z; ++z) {
                for (int y = cellMin.y; y <= cellMax.y; ++y) {
                    for (int x = cellMin.x; x <= cellMax.x; ++x) {
                        const size_t cell = cellIndex(x, y, z);
                        threadLocalGrids[threadId][cell].push_back(triangleIndex);
                    }
                }
            }
        }
    }

    for (const auto& localGrid : threadLocalGrids) {
        for (const auto& [cell, triangles] : localGrid) {
            grid[cell].insert(grid[cell].end(), triangles.begin(), triangles.end());
        }
    }
}

void TriangleHashGrid::appendCellTriangles(size_t cell, std::vector<size_t>& outTriangles) const {
    const auto it = grid.find(cell);
    if (it != grid.end()) {
        outTriangles.insert(outTriangles.end(), it->second.begin(), it->second.end());
    }
}

void TriangleHashGrid::deduplicate(std::vector<size_t>& triangles) {
    std::sort(triangles.begin(), triangles.end());
    triangles.erase(std::unique(triangles.begin(), triangles.end()), triangles.end());
}

size_t TriangleHashGrid::cellIndex(int x, int y, int z) const {
    return static_cast<size_t>(z) * static_cast<size_t>(gridDim.y) * static_cast<size_t>(gridDim.x) +
        static_cast<size_t>(y) * static_cast<size_t>(gridDim.x) +
        static_cast<size_t>(x);
}

glm::ivec3 TriangleHashGrid::worldToCell(const glm::vec3& position) const {
    const glm::vec3 gridPosition = (position - gridMin) / cellSize;
    return glm::ivec3(
        glm::clamp(static_cast<int>(gridPosition.x), 0, gridDim.x - 1),
        glm::clamp(static_cast<int>(gridPosition.y), 0, gridDim.y - 1),
        glm::clamp(static_cast<int>(gridPosition.z), 0, gridDim.z - 1));
}
