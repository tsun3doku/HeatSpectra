#include "TriangleHashGrid.hpp"

#include "scene/Model.hpp"
#include "util/GeometryUtils.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <omp.h>

TriangleHashGrid::TriangleHashGrid()
    : cellSize_(1.0f), gridMin_(0.0f), gridMax_(1.0f), gridDim_(1) {
}

void TriangleHashGrid::build(const Model& model, const glm::vec3& gridMin, const glm::vec3& gridMax, float cellSize) {
    const auto& vertices = model.getVertices();
    std::vector<glm::vec3> positions;
    positions.reserve(vertices.size());

    for (const auto& vertex : vertices) {
        positions.push_back(vertex.pos);
    }

    initializeGrid(gridMin, gridMax, cellSize);
    buildTriangles(positions, model.getIndices());
}

void TriangleHashGrid::build(const std::vector<glm::vec3>& vertices, const std::vector<uint32_t>& indices, const glm::vec3& gridMin, const glm::vec3& gridMax,
    float cellSize) {
    clear();
    initializeGrid(gridMin, gridMax, cellSize);
    buildTriangles(vertices, indices);
}

void TriangleHashGrid::getNearbyTriangles(const glm::vec3& position, std::vector<size_t>& outTriangles) const {
    outTriangles.clear();

    const glm::ivec3 cell = worldToCell(position);
    std::unordered_set<size_t> seenTriangles;

    for (int dz = -1; dz <= 1; ++dz) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const int cx = cell.x + dx;
                const int cy = cell.y + dy;
                const int cz = cell.z + dz;

                if (cx < 0 || cx >= gridDim_.x ||
                    cy < 0 || cy >= gridDim_.y ||
                    cz < 0 || cz >= gridDim_.z) {
                    continue;
                }

                const size_t hash = hashCell(cx, cy, cz);
                addCellTriangles(hash, seenTriangles, outTriangles);
            }
        }
    }
}

void TriangleHashGrid::getNearbyTriangles(const glm::vec3& position, int radiusCells, std::vector<size_t>& outTriangles) const {
    outTriangles.clear();
    if (radiusCells <= 1) {
        getNearbyTriangles(position, outTriangles);
        return;
    }

    const glm::ivec3 cell = worldToCell(position);
    const int r = radiusCells;
    std::unordered_set<size_t> seenTriangles;

    for (int dz = -r; dz <= r; ++dz) {
        for (int dy = -r; dy <= r; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                const int cx = cell.x + dx;
                const int cy = cell.y + dy;
                const int cz = cell.z + dz;

                if (cx < 0 || cx >= gridDim_.x ||
                    cy < 0 || cy >= gridDim_.y ||
                    cz < 0 || cz >= gridDim_.z) {
                    continue;
                }

                const size_t hash = hashCell(cx, cy, cz);
                addCellTriangles(hash, seenTriangles, outTriangles);
            }
        }
    }
}

void TriangleHashGrid::getTrianglesAlongRay(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, std::vector<size_t>& outTriangles) const {
    outTriangles.clear();

    if (grid_.empty() || maxDistance <= 0.0f || cellSize_ <= 0.0f) {
        return;
    }

    const float directionLength = glm::length(direction);
    if (directionLength <= 1e-8f) {
        return;
    }

    const glm::vec3 rayDirection = direction / directionLength;
    float tEnter = 0.0f;
    float tExit = maxDistance;
    if (!intersectRayAabb(origin, rayDirection, gridMin_, gridMax_, maxDistance, tEnter, tExit)) {
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
            const float nextBoundary = gridMin_[axis] + static_cast<float>(cell[axis] + 1) * cellSize_;
            tMax[axis] = (nextBoundary - startPoint[axis]) / dirAxis;
        } else {
            step[axis] = -1;
            const float nextBoundary = gridMin_[axis] + static_cast<float>(cell[axis]) * cellSize_;
            tMax[axis] = (nextBoundary - startPoint[axis]) / dirAxis;
        }

        tDelta[axis] = cellSize_ / std::fabs(dirAxis);
    }

    const float travelLimit = endT - startT;
    std::unordered_set<size_t> seenTriangles;

    while (cell.x >= 0 && cell.x < gridDim_.x &&
           cell.y >= 0 && cell.y < gridDim_.y &&
           cell.z >= 0 && cell.z < gridDim_.z) {
        addCellTriangles(hashCell(cell.x, cell.y, cell.z), seenTriangles, outTriangles);

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
}

void TriangleHashGrid::clear() {
    grid_.clear();
}

void TriangleHashGrid::initializeGrid(const glm::vec3& gridMin, const glm::vec3& gridMax, float cellSize) {
    gridMin_ = gridMin;
    gridMax_ = gridMax;
    cellSize_ = cellSize;
    gridDim_ = computeGridDimensions(gridMin, gridMax, cellSize);

    std::cout << "  Building triangle hash grid: " << gridDim_.x << "x"
        << gridDim_.y << "x" << gridDim_.z << std::endl;
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
                        const size_t hash = hashCell(x, y, z);
                        threadLocalGrids[threadId][hash].push_back(triangleIndex);
                    }
                }
            }
        }
    }

    for (const auto& localGrid : threadLocalGrids) {
        for (const auto& [hash, triangles] : localGrid) {
            grid_[hash].insert(grid_[hash].end(), triangles.begin(), triangles.end());
        }
    }

    std::cout << "  Triangle hash built: " << grid_.size() << " non-empty cells" << std::endl;
}

void TriangleHashGrid::addCellTriangles(size_t hash, std::unordered_set<size_t>& seenTriangles, std::vector<size_t>& outTriangles) const {
    const auto it = grid_.find(hash);
    if (it == grid_.end()) {
        return;
    }

    for (size_t triangleIndex : it->second) {
        if (seenTriangles.insert(triangleIndex).second) {
            outTriangles.push_back(triangleIndex);
        }
    }
}

size_t TriangleHashGrid::hashCell(int x, int y, int z) const {
    return static_cast<size_t>(z) * static_cast<size_t>(gridDim_.y) * static_cast<size_t>(gridDim_.x) +
        static_cast<size_t>(y) * static_cast<size_t>(gridDim_.x) +
        static_cast<size_t>(x);
}

glm::ivec3 TriangleHashGrid::worldToCell(const glm::vec3& pos) const {
    const glm::vec3 gridPos = (pos - gridMin_) / cellSize_;
    return glm::ivec3(
        glm::clamp(static_cast<int>(gridPos.x), 0, gridDim_.x - 1),
        glm::clamp(static_cast<int>(gridPos.y), 0, gridDim_.y - 1),
        glm::clamp(static_cast<int>(gridPos.z), 0, gridDim_.z - 1));
}
