#include "VoxelGrid.hpp"
#include "TriangleHashGrid.hpp"
#include "util/GeometryUtils.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <cfloat>
#include <climits>
#include <set>
#include <unordered_set>
#include <unordered_map>
#include <fstream>
#include <omp.h>

glm::vec3 VoxelGrid::toCanonical(const glm::vec3& worldPos) const {
    return (worldPos - params.gridMin) * params.cellSize;
}

glm::vec3 VoxelGrid::toWorld(const glm::vec3& canonicalPos) const {
    return canonicalPos / params.cellSize + params.gridMin;
}

bool VoxelGrid::triBoxOverlap(const float boxcenter[3], const float boxhalfsize[3], const float triverts[3][3]) {
    float v0[3], v1[3], v2[3];
    float e0[3], e1[3], e2[3];
    float min, max, p0, p1, p2, rad;
    float fex, fey, fez;
    float normal[3];

    // Make boxcenter (0,0,0)
    for (int i = 0; i < 3; i++) {
        v0[i] = triverts[0][i] - boxcenter[i];
        v1[i] = triverts[1][i] - boxcenter[i];
        v2[i] = triverts[2][i] - boxcenter[i];
    }

    // Calculate edges
    for (int i = 0; i < 3; i++) {
        e0[i] = v1[i] - v0[i];
        e1[i] = v2[i] - v1[i];
        e2[i] = v0[i] - v2[i];
    }

    // 9 axis tests: cross products of triangle edges with coordinate axes
    fex = std::fabs(e0[0]);
    fey = std::fabs(e0[1]);
    fez = std::fabs(e0[2]);

    // AXISTEST_X01
    p0 = e0[2] * v0[1] - e0[1] * v0[2];
    p2 = e0[2] * v2[1] - e0[1] * v2[2];

    if (p0 < p2) { min = p0; max = p2; 
    } else { 
        min = p2; max = p0; 
    }

    rad = fez * boxhalfsize[1] + fey * boxhalfsize[2];
    if (min > rad || max < -rad) 
        return false;

    // AXISTEST_Y02
    p0 = -e0[2] * v0[0] + e0[0] * v0[2];
    p2 = -e0[2] * v2[0] + e0[0] * v2[2];

    if (p0 < p2) { min = p0; max = p2; 
    } else { 
        min = p2; max = p0; 
    }

    rad = fez * boxhalfsize[0] + fex * boxhalfsize[2];
    if (min > rad || max < -rad) 
        return false;

    // AXISTEST_Z12
    p1 = e0[1] * v1[0] - e0[0] * v1[1];
    p2 = e0[1] * v2[0] - e0[0] * v2[1];

    if (p2 < p1) { min = p2; max = p1; 
    } else { 
        min = p1; max = p2; 
    }

    rad = fey * boxhalfsize[0] + fex * boxhalfsize[1];
    if (min > rad || max < -rad) 
        return false;

    fex = std::fabs(e1[0]);
    fey = std::fabs(e1[1]);
    fez = std::fabs(e1[2]);

    // AXISTEST_X01
    p0 = e1[2] * v0[1] - e1[1] * v0[2];
    p2 = e1[2] * v2[1] - e1[1] * v2[2];

    if (p0 < p2) { min = p0; max = p2; 
    } else { 
        min = p2; max = p0; 
    }

    rad = fez * boxhalfsize[1] + fey * boxhalfsize[2];
    if (min > rad || max < -rad) 
        return false;

    // AXISTEST_Y02
    p0 = -e1[2] * v0[0] + e1[0] * v0[2];
    p2 = -e1[2] * v2[0] + e1[0] * v2[2];

    if (p0 < p2) { min = p0; max = p2; 
    } else { 
        min = p2; max = p0; 
    }

    rad = fez * boxhalfsize[0] + fex * boxhalfsize[2];
    if (min > rad || max < -rad) 
        return false;

    // AXISTEST_Z0
    p0 = e1[1] * v0[0] - e1[0] * v0[1];
    p1 = e1[1] * v1[0] - e1[0] * v1[1];

    if (p0 < p1) { min = p0; max = p1; 
    } else { 
        min = p1; max = p0; 
    }

    rad = fey * boxhalfsize[0] + fex * boxhalfsize[1];
    if (min > rad || max < -rad) 
        return false;

    fex = std::fabs(e2[0]);
    fey = std::fabs(e2[1]);
    fez = std::fabs(e2[2]);

    // AXISTEST_X2
    p0 = e2[2] * v0[1] - e2[1] * v0[2];
    p1 = e2[2] * v1[1] - e2[1] * v1[2];

    if (p0 < p1) { min = p0; max = p1; 
    } else { 
        min = p1; max = p0; 
    }

    rad = fez * boxhalfsize[1] + fey * boxhalfsize[2];
    if (min > rad || max < -rad) 
        return false;

    // AXISTEST_Y1
    p0 = -e2[2] * v0[0] + e2[0] * v0[2];
    p1 = -e2[2] * v1[0] + e2[0] * v1[2];

    if (p0 < p1) { min = p0; max = p1; 
    } else { 
        min = p1; max = p0; 
    }

    rad = fez * boxhalfsize[0] + fex * boxhalfsize[2];
    if (min > rad || max < -rad) 
        return false;

    // AXISTEST_Z12
    p1 = e2[1] * v1[0] - e2[0] * v1[1];
    p2 = e2[1] * v2[0] - e2[0] * v2[1];

    if (p2 < p1) { min = p2; max = p1; 
    } else { 
        min = p1; max = p2; 
    }

    rad = fey * boxhalfsize[0] + fex * boxhalfsize[1];
    if (min > rad || max < -rad) 
        return false;

    // Test overlap 
    min = std::min(std::min(v0[0], v1[0]), v2[0]);
    max = std::max(std::max(v0[0], v1[0]), v2[0]);
    if (min > boxhalfsize[0] || max < -boxhalfsize[0]) 
        return false;

    min = std::min(std::min(v0[1], v1[1]), v2[1]);
    max = std::max(std::max(v0[1], v1[1]), v2[1]);
    if (min > boxhalfsize[1] || max < -boxhalfsize[1]) 
        return false;

    min = std::min(std::min(v0[2], v1[2]), v2[2]);
    max = std::max(std::max(v0[2], v1[2]), v2[2]);
    if (min > boxhalfsize[2] || max < -boxhalfsize[2]) 
        return false;

    // Test if the box intersects the plane of the triangle
    normal[0] = e0[1] * e1[2] - e0[2] * e1[1];
    normal[1] = e0[2] * e1[0] - e0[0] * e1[2];
    normal[2] = e0[0] * e1[1] - e0[1] * e1[0];

    // Plane box overlap
    float vmin[3], vmax[3];
    for (int q = 0; q < 3; q++) {
        float v = v0[q];
        if (normal[q] > 0.0f) {
            vmin[q] = -boxhalfsize[q] - v;
            vmax[q] =  boxhalfsize[q] - v;
        } else {
            vmin[q] =  boxhalfsize[q] - v;
            vmax[q] = -boxhalfsize[q] - v;
        }
    }

    float dotmin = normal[0] * vmin[0] + normal[1] * vmin[1] + normal[2] * vmin[2];
    float dotmax = normal[0] * vmax[0] + normal[1] * vmax[1] + normal[2] * vmax[2];

    if (dotmin > 0.0f) 
        return false;
    if (dotmax >= 0.0f) 
        return true;

    return false;
}

VoxelGrid::VoxelGrid() {
    params.gridMin = glm::vec3(0.0f);
    params.cellSize = 1.0f;
    params.gridDim = glm::ivec3(1);
    params.totalCells = 1;
}

VoxelGrid::~VoxelGrid() {
}

void VoxelGrid::build(
    const std::vector<glm::vec3>& positions,
    const std::vector<uint32_t>& indices,
    const TriangleHashGrid& triangleGrid,
    int gridSize) {
    // Calculate grid parameters from mesh bounding box
    if (positions.empty()) {
        std::cerr << "[VoxelGrid] Error: Empty mesh" << std::endl;
        return;
    }

    glm::vec3 meshMin(FLT_MAX);
    glm::vec3 meshMax(-FLT_MAX);
    for (const glm::vec3& position : positions) {
        meshMin = glm::min(meshMin, position);
        meshMax = glm::max(meshMax, position);
    }

    glm::vec3 extent = meshMax - meshMin;
    float maxExtent = std::max(std::max(extent.x, extent.y), extent.z);
    if (maxExtent < 1e-20f) {
        std::cerr << "[VoxelGrid] Error: mesh bbox degenerate" << std::endl;
        return;
    }

    float scale = CANONICAL_SCALE / maxExtent;
    params.cellSize = scale;
    params.gridMin = meshMin - glm::vec3(CANONICAL_BIAS / scale);
    params.gridDim = glm::ivec3(gridSize);
    params.totalCells = static_cast<uint32_t>(gridSize * gridSize * gridSize);

    float canonicalVoxelSize = CANONICAL_DOMAIN_SIZE / float(gridSize);
    float worldVoxelSize = canonicalVoxelSize / scale;

    // Build triangle lists 
    buildTriangleLists(positions, indices, triangleGrid);

    int numCorners = (gridSize + 1) * (gridSize + 1) * (gridSize + 1);
    occupancy.assign(numCorners, 0);  // Default: OUTSIDE

    int dimX = params.gridDim.x;
    int dimY = params.gridDim.y;
    int dimZ = params.gridDim.z;

    const float borderThreshold = worldVoxelSize * 0.1f;

    int borderCount = 0;

    // Find border corners using fast distance check
    #pragma omp parallel for reduction(+:borderCount)
    for (int z = 0; z <= dimZ; z++) {
        for (int y = 0; y <= dimY; y++) {
            for (int x = 0; x <= dimX; x++) {
                glm::vec3 canonicalCornerPos = glm::vec3(x, y, z) * canonicalVoxelSize;
                glm::vec3 cornerPos = toWorld(canonicalCornerPos);
                float dist = distanceToNearestTriangle(cornerPos, positions, indices, triangleGrid);

                size_t idx = getCornerIndex(x, y, z);

                if (dist < borderThreshold) {
                    occupancy[idx] = 1;  // Border
                    borderCount++;
                } else {
                    occupancy[idx] = 255; // OUTSIDE
                }
            }
        }
    }

    const size_t triCount = indices.size() / 3;
    const glm::vec3 rayDir(1.0f, 0.0f, 0.0f);
    const float duplicateHitEpsilon = worldVoxelSize * 1e-3f;

    std::vector<glm::vec3> triV0;
    std::vector<glm::vec3> triV1;
    std::vector<glm::vec3> triV2;
    triV0.reserve(triCount);
    triV1.reserve(triCount);
    triV2.reserve(triCount);
    for (size_t t = 0; t < triCount; t++) {
        const size_t base = t * 3;
        if (indices[base + 0] >= positions.size() ||
            indices[base + 1] >= positions.size() ||
            indices[base + 2] >= positions.size()) {
            triV0.push_back(glm::vec3(0.0f));
            triV1.push_back(glm::vec3(0.0f));
            triV2.push_back(glm::vec3(0.0f));
            continue;
        }
        triV0.push_back(positions[indices[base + 0]]);
        triV1.push_back(positions[indices[base + 1]]);
        triV2.push_back(positions[indices[base + 2]]);
    }

    int insideCount = 0;
    int outsideCount = 0;

    // Classify each (y,z) corner column once 
    #pragma omp parallel reduction(+:insideCount, outsideCount)
    {
        std::vector<int32_t> candidateTriangles;
        candidateTriangles.reserve(512);
        std::vector<int32_t> triStamp(triCount, -1);
        int stamp = 0;
        std::vector<float> hitX;
        std::vector<float> uniqueHitX;

        const int yCount = dimY + 1;
        const int zCount = dimZ + 1;
        const int yzCount = yCount * zCount;

        #pragma omp for schedule(dynamic, 1)
        for (int yz = 0; yz < yzCount; yz++) {
                const int y = yz % yCount;
                const int z = yz / yCount;
                stamp++;
                if (stamp == INT_MAX) {
                    std::fill(triStamp.begin(), triStamp.end(), -1);
                    stamp = 1;
                }

                candidateTriangles.clear();

                // Corner lies on voxel boundaries in y/z
                const int yCells[2] = { clampInt(y - 1, 0, dimY - 1), clampInt(y, 0, dimY - 1) };
                const int zCells[2] = { clampInt(z - 1, 0, dimZ - 1), clampInt(z, 0, dimZ - 1) };

                for (int xCell = 0; xCell < dimX; xCell++) {
                    for (int yi = 0; yi < 2; yi++) {
                        for (int zi = 0; zi < 2; zi++) {
                            const int voxelIdx = xCell + yCells[yi] * dimX + zCells[zi] * dimX * dimY;
                            const int begin = offsets[voxelIdx];
                            const int end = offsets[voxelIdx + 1];
                            for (int it = begin; it < end; it++) {
                                const int32_t triId = trianglesList[it];
                                if (triId < 0 || static_cast<size_t>(triId) >= triCount) {
                                    continue;
                                }
                                if (triStamp[static_cast<size_t>(triId)] == stamp) {
                                    continue;
                                }
                                triStamp[static_cast<size_t>(triId)] = stamp;
                                candidateTriangles.push_back(triId);
                            }
                        }
                    }
                }

                // If no candidates for this column, everything here is outside except borders
                if (candidateTriangles.empty()) {
                    for (int x = 0; x <= dimX; x++) {
                        const size_t idx = getCornerIndex(x, y, z);
                        if (occupancy[idx] == 1) {
                            continue;
                        }
                        occupancy[idx] = 0;
                        outsideCount++;
                    }
                    continue;
                }

                const glm::vec3 canonicalCol = glm::vec3(0.0f, static_cast<float>(y), static_cast<float>(z)) * canonicalVoxelSize;
                glm::vec3 columnBase = toWorld(canonicalCol);
                columnBase.x = params.gridMin.x - worldVoxelSize;

                hitX.clear();
                hitX.reserve(candidateTriangles.size());

                for (int32_t triId : candidateTriangles) {
                    const size_t triIdx = static_cast<size_t>(triId);

                    float tHit = 0.0f;
                    float uDummy = 0.0f;
                    float vDummy = 0.0f;
                    if (intersectRayTriangle(columnBase, rayDir, triV0[triIdx], triV1[triIdx], triV2[triIdx], tHit, uDummy, vDummy)) {
                        hitX.push_back(columnBase.x + tHit);
                    }
                }

                std::sort(hitX.begin(), hitX.end());

                // Collapse nearly identical hits 
                uniqueHitX.clear();
                uniqueHitX.reserve(hitX.size());
                for (float xHit : hitX) {
                    if (uniqueHitX.empty() || std::abs(xHit - uniqueHitX.back()) > duplicateHitEpsilon) {
                        uniqueHitX.push_back(xHit);
                    }
                }

                size_t hitPtr = 0;
                float cornerX = params.gridMin.x;
                for (int x = 0; x <= dimX; x++, cornerX += worldVoxelSize) {
                    const size_t idx = getCornerIndex(x, y, z);
                    if (occupancy[idx] == 1) {
                        continue;
                    }

                    while (hitPtr < uniqueHitX.size() && uniqueHitX[hitPtr] <= cornerX) {
                        hitPtr++;
                    }
                    const int numHitsToLeft = static_cast<int>(hitPtr);
                    const bool isInside = (numHitsToLeft & 1) == 1;

                    if (isInside) {
                        occupancy[idx] = 2;
                        insideCount++;
                    } else {
                        occupancy[idx] = 0;
                        outsideCount++;
                    }
                }
        }
    }

}

float VoxelGrid::distanceToNearestTriangle(
    const glm::vec3& point,
    const std::vector<glm::vec3>& positions,
    const std::vector<uint32_t>& indices,
    const TriangleHashGrid& triangleGrid) const {
    std::vector<size_t> nearbyTriangles;
    triangleGrid.getNearbyTriangles(point, nearbyTriangles);
    
    if (nearbyTriangles.empty()) {
        return FLT_MAX;
    }
    
    float minDist = FLT_MAX;
    for (size_t triIdx : nearbyTriangles) {
        const size_t indexBase = triIdx * 3;
        if (indexBase + 2 >= indices.size()) {
            continue;
        }
        if (indices[indexBase] >= positions.size() ||
            indices[indexBase + 1] >= positions.size() ||
            indices[indexBase + 2] >= positions.size()) {
            continue;
        }

        const glm::vec3& v0 = positions[indices[indexBase]];
        const glm::vec3& v1 = positions[indices[indexBase + 1]];
        const glm::vec3& v2 = positions[indices[indexBase + 2]];
        
        glm::vec3 closest = closestPointOnTriangle(point, v0, v1, v2);
        float dist = glm::length(point - closest);
        minDist = std::min(minDist, dist);
    }
    
    return minDist;
}



void VoxelGrid::buildTriangleLists(
    const std::vector<glm::vec3>& positions,
    const std::vector<uint32_t>& indices,
    const TriangleHashGrid& triangleGrid) {
    meshPoints.clear();
    meshTriangles.clear();
    trianglesList.clear();
    offsets.clear();

    meshPoints.reserve(positions.size());
    for (const glm::vec3& position : positions) {
        meshPoints.push_back(position);
    }

    meshTriangles.reserve(indices.size());
    for (uint32_t idx : indices) {
        meshTriangles.push_back(static_cast<int32_t>(idx));
    }

    int gridSize = params.gridDim.x;
    int numVoxels = params.gridDim.x * params.gridDim.y * params.gridDim.z;
    offsets.resize(numVoxels + 1, 0);

    float canonicalVoxelSize = CANONICAL_DOMAIN_SIZE / float(gridSize);

    std::vector<std::vector<int32_t>> voxelTriangles(static_cast<size_t>(numVoxels));
    std::vector<std::unordered_map<int, std::vector<int32_t>>> threadLocalVoxelTriangles;

    size_t triCount = indices.size() / 3;

    #pragma omp parallel
    {
        #pragma omp single
        {
            threadLocalVoxelTriangles.resize(static_cast<size_t>(omp_get_num_threads()));
        }

        const int threadId = omp_get_thread_num();
        auto& localVoxelTriangles = threadLocalVoxelTriangles[static_cast<size_t>(threadId)];

        #pragma omp for schedule(static)
        for (int t = 0; t < static_cast<int>(triCount); t++) {
            uint32_t i0 = indices[3 * t + 0];
            uint32_t i1 = indices[3 * t + 1];
            uint32_t i2 = indices[3 * t + 2];
            if (i0 >= positions.size() || i1 >= positions.size() || i2 >= positions.size()) {
                continue;
            }

            glm::vec3 w0 = positions[i0];
            glm::vec3 w1 = positions[i1];
            glm::vec3 w2 = positions[i2];

            glm::vec3 c0 = toCanonical(w0);
            glm::vec3 c1 = toCanonical(w1);
            glm::vec3 c2 = toCanonical(w2);

            glm::vec3 bbMin(FLT_MAX), bbMax(-FLT_MAX);
            bbMin = glm::min(bbMin, c0); bbMax = glm::max(bbMax, c0);
            bbMin = glm::min(bbMin, c1); bbMax = glm::max(bbMax, c1);
            bbMin = glm::min(bbMin, c2); bbMax = glm::max(bbMax, c2);

            int vx0 = clampInt(static_cast<int>(std::floor(bbMin.x / canonicalVoxelSize)), 0, gridSize - 1);
            int vy0 = clampInt(static_cast<int>(std::floor(bbMin.y / canonicalVoxelSize)), 0, gridSize - 1);
            int vz0 = clampInt(static_cast<int>(std::floor(bbMin.z / canonicalVoxelSize)), 0, gridSize - 1);
            int vx1 = clampInt(static_cast<int>(std::ceil (bbMax.x / canonicalVoxelSize)), 0, gridSize - 1);
            int vy1 = clampInt(static_cast<int>(std::ceil (bbMax.y / canonicalVoxelSize)), 0, gridSize - 1);
            int vz1 = clampInt(static_cast<int>(std::ceil (bbMax.z / canonicalVoxelSize)), 0, gridSize - 1);

            float triverts[3][3] = {
                { c0.x, c0.y, c0.z },
                { c1.x, c1.y, c1.z },
                { c2.x, c2.y, c2.z },
            };

            for (int z = vz0; z <= vz1; z++) {
                for (int y = vy0; y <= vy1; y++) {
                    for (int x = vx0; x <= vx1; x++) {
                        float boxcenter[3] = {
                            (x + 0.5f) * canonicalVoxelSize,
                            (y + 0.5f) * canonicalVoxelSize,
                            (z + 0.5f) * canonicalVoxelSize,
                        };
                        float boxhalfsize[3] = {
                            0.5f * canonicalVoxelSize,
                            0.5f * canonicalVoxelSize,
                            0.5f * canonicalVoxelSize,
                        };

                        if (!triBoxOverlap(boxcenter, boxhalfsize, triverts)) {
                            continue;
                        }

                        int voxelIdx = x + y * gridSize + z * gridSize * gridSize;
                        localVoxelTriangles[voxelIdx].push_back(static_cast<int32_t>(t));
                    }
                }
            }
        }
    }

    for (const auto& localVoxelTriangles : threadLocalVoxelTriangles) {
        for (const auto& [voxelIdx, triIds] : localVoxelTriangles) {
            auto& dst = voxelTriangles[static_cast<size_t>(voxelIdx)];
            dst.insert(dst.end(), triIds.begin(), triIds.end());
        }
    }

    // Build flat triangle list and offsets
    int currentOffset = 0;
    for (int i = 0; i < numVoxels; i++) {
        offsets[i] = currentOffset;
        auto& list = voxelTriangles[static_cast<size_t>(i)];
        std::sort(list.begin(), list.end());
        list.erase(std::unique(list.begin(), list.end()), list.end());
        for (int32_t triID : list) {
            trianglesList.push_back(triID);
        }
        currentOffset += static_cast<int>(list.size());
    }
    offsets[numVoxels] = currentOffset;

}

uint8_t VoxelGrid::getOccupancy(int x, int y, int z) const {
    if (x < 0 || x > params.gridDim.x ||
        y < 0 || y > params.gridDim.y ||
        z < 0 || z > params.gridDim.z) {
        return 0;  // Outside
    }
    return occupancy[getCornerIndex(x, y, z)];
}

glm::vec3 VoxelGrid::getCornerPosition(int x, int y, int z) const {
    float canonicalVoxelSize = CANONICAL_DOMAIN_SIZE / float(params.gridDim.x);
    glm::vec3 canonical = glm::vec3(x, y, z) * canonicalVoxelSize;
    return toWorld(canonical);
}

glm::ivec3 VoxelGrid::worldToVoxel(const glm::vec3& pos) const {
    float canonicalVoxelSize = CANONICAL_DOMAIN_SIZE / float(params.gridDim.x);
    glm::vec3 canonical = toCanonical(pos);
    glm::vec3 gridPos = canonical / canonicalVoxelSize;
    return glm::ivec3(
        glm::clamp(static_cast<int>(gridPos.x), 0, params.gridDim.x),
        glm::clamp(static_cast<int>(gridPos.y), 0, params.gridDim.y),
        glm::clamp(static_cast<int>(gridPos.z), 0, params.gridDim.z)
    );
}


size_t VoxelGrid::getCornerIndex(int x, int y, int z) const {
    int stride = params.gridDim.x + 1;
    return z * stride * stride + y * stride + x;
}

void VoxelGrid::exportOccupancyVisualization(const std::string& filename) const {
    int dimX = params.gridDim.x;
    int dimY = params.gridDim.y;
    int dimZ = params.gridDim.z;

    // Count non-outside points
    int insideCount = 0, borderCount = 0;
    for (int z = 0; z <= dimZ; z++) {
        for (int y = 0; y <= dimY; y++) {
            for (int x = 0; x <= dimX; x++) {
                uint8_t occ = occupancy[getCornerIndex(x, y, z)];
                if (occ == 1) borderCount++;
                else if (occ == 2) insideCount++;
            }
        }
    }

    int numPoints = insideCount + borderCount;

    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "[VoxelGrid] Failed to open " << filename << " for writing" << std::endl;
        return;
    }

    // PLY header with vertex colors
    file << "ply\n";
    file << "format ascii 1.0\n";
    file << "element vertex " << numPoints << "\n";
    file << "property float x\n";
    file << "property float y\n";
    file << "property float z\n";
    file << "property uchar red\n";
    file << "property uchar green\n";
    file << "property uchar blue\n";
    file << "end_header\n";

    // Write only border and inside points
    for (int z = 0; z <= dimZ; z++) {
        for (int y = 0; y <= dimY; y++) {
            for (int x = 0; x <= dimX; x++) {
                uint8_t occ = occupancy[getCornerIndex(x, y, z)];
                if (occ == 0) 
                    continue;

                glm::vec3 pos = getCornerPosition(x, y, z);

                // Colors: Red=border(1), Green = inside(2)
                int r = 0, g = 0, b = 0;
                if (occ == 1) {
                    r = 255; g = 50; b = 50;
                } else {
                    r = 50; g = 255; b = 50;
                }

                file << pos.x << " " << pos.y << " " << pos.z << " "
                     << r << " " << g << " " << b << "\n";
            }
        }
    }

    file.close();

}
