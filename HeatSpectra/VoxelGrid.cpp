#include "VoxelGrid.hpp"
#include "Model.hpp"
#include "TriangleHashGrid.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <cfloat>
#include <set>
#include <unordered_set>
#include <fstream>
#include <omp.h>

const glm::vec3 VoxelGrid::RANDOM_DIRS[NUM_RANDOM_DIRS] = {
    glm::normalize(glm::vec3(0.267f, 0.534f, 0.802f)),
    glm::normalize(glm::vec3(-0.577f, 0.577f, 0.577f)),
    glm::normalize(glm::vec3(0.707f, -0.707f, 0.0f)),
    glm::normalize(glm::vec3(-0.333f, -0.667f, 0.667f)),
    glm::normalize(glm::vec3(0.0f, 0.894f, -0.447f)),
    glm::normalize(glm::vec3(0.816f, 0.408f, -0.408f)),
    glm::normalize(glm::vec3(-0.447f, 0.0f, 0.894f)),
};

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

void VoxelGrid::build(const Model& mesh, const TriangleHashGrid& triangleGrid, int gridSize) {
    std::cout << "[VoxelGrid] Building " << gridSize << "^3 voxel grid..." << std::endl;

    // Calculate grid parameters from mesh bounding box
    const auto& vertices = mesh.getVertices();
    if (vertices.empty()) {
        std::cerr << "[VoxelGrid] Error: Empty mesh" << std::endl;
        return;
    }

    glm::vec3 meshMin(FLT_MAX);
    glm::vec3 meshMax(-FLT_MAX);
    for (const auto& vertex : vertices) {
        meshMin = glm::min(meshMin, vertex.pos);
        meshMax = glm::max(meshMax, vertex.pos);
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

    std::cout << "  Canonical domain: [0.." << CANONICAL_DOMAIN_SIZE << "]^3" << std::endl;
    std::cout << "  World->canonical scale: " << scale << std::endl;
    std::cout << "  Canonical voxel size: " << canonicalVoxelSize << std::endl;
    std::cout << "  World voxel size (effective): " << worldVoxelSize << std::endl;

    // Build triangle lists 
    buildTriangleLists(mesh, triangleGrid);

    int numCorners = (gridSize + 1) * (gridSize + 1) * (gridSize + 1);
    occupancy.assign(numCorners, 0);  // Default: OUTSIDE

    int dimX = params.gridDim.x;
    int dimY = params.gridDim.y;
    int dimZ = params.gridDim.z;

    const float borderThreshold = worldVoxelSize * 0.1f;

    std::cout << " Detecting border corners (distance < " << borderThreshold << ")..." << std::endl;

    int borderCount = 0;

    // Find border corners using fast distance check
    #pragma omp parallel for reduction(+:borderCount)
    for (int z = 0; z <= dimZ; z++) {
        for (int y = 0; y <= dimY; y++) {
            for (int x = 0; x <= dimX; x++) {
                glm::vec3 canonicalCornerPos = glm::vec3(x, y, z) * canonicalVoxelSize;
                glm::vec3 cornerPos = toWorld(canonicalCornerPos);
                float dist = distanceToNearestTriangle(cornerPos, mesh, triangleGrid);

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

    std::cout << " Complete: " << borderCount << " border corners" << std::endl;
    std::cout << " Ray casting " << (numCorners - borderCount) << " non-border corners..." << std::endl;

    int insideCount = 0, outsideCount = 0;

    // Ray cast non-border corners
    #pragma omp parallel for reduction(+:insideCount, outsideCount)
    for (int z = 0; z <= dimZ; z++) {
        for (int y = 0; y <= dimY; y++) {
            for (int x = 0; x <= dimX; x++) {
                size_t idx = getCornerIndex(x, y, z);

                // Skip border corners
                if (occupancy[idx] == 1) 
                    continue;

                glm::vec3 canonicalCornerPos = glm::vec3(x, y, z) * canonicalVoxelSize;
                glm::vec3 cornerPos = toWorld(canonicalCornerPos);
                bool isInside = isInsideMonteCarlo(cornerPos, mesh, 7); 

                if (isInside) {
                    occupancy[idx] = 2;  // Inside
                    insideCount++;
                } else {
                    occupancy[idx] = 0;  // Outside
                    outsideCount++;
                }
            }
        }
    }

    std::cout << "  Classification complete: " << insideCount << " inside, " 
              << borderCount << " border, " << outsideCount << " outside" << std::endl;

    std::cout << "[VoxelGrid] Build complete" << std::endl;
}

bool VoxelGrid::rayTriangleIntersect(const glm::vec3& rayOrigin, const glm::vec3& rayDir, const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2, float& t) const {
    const float EPSILON = 1e-8f;

    glm::vec3 edge1 = v1 - v0;
    glm::vec3 edge2 = v2 - v0;
    glm::vec3 h = glm::cross(rayDir, edge2);
    float a = glm::dot(edge1, h);

    // Ray parallel to triangle
    if (a > -EPSILON && a < EPSILON) 
        return false; 
    
    float f = 1.0f / a;
    glm::vec3 s = rayOrigin - v0;
    float u = f * glm::dot(s, h);
    
    if (u < 0.0f || u > 1.0f) 
        return false;
    
    glm::vec3 q = glm::cross(s, edge1);
    float v = f * glm::dot(rayDir, q);
    
    if (v < 0.0f || u + v > 1.0f) 
        return false;
    
    // Only count forward intersections
    t = f * glm::dot(edge2, q);
    return t > EPSILON;  
}

bool VoxelGrid::isInsideMonteCarlo(const glm::vec3& point, const Model& mesh, int numRays) const {
    const auto& vertices = mesh.getVertices();
    const auto& indices = mesh.getIndices();
    
    int insideVotes = 0;
    int outsideVotes = 0;
    numRays = std::min(numRays, NUM_RANDOM_DIRS);
    int majority = (numRays / 2) + 1;
    
    for (int rayIdx = 0; rayIdx < numRays; rayIdx++) {
        glm::vec3 rayDir = RANDOM_DIRS[rayIdx];
        int intersectionCount = 0;
        
        // Test ray against all triangles
        for (size_t i = 0; i < indices.size(); i += 3) {
            const glm::vec3& v0 = vertices[indices[i]].pos;
            const glm::vec3& v1 = vertices[indices[i + 1]].pos;
            const glm::vec3& v2 = vertices[indices[i + 2]].pos;
            
            float t;
            if (rayTriangleIntersect(point, rayDir, v0, v1, v2, t)) {
                intersectionCount++;
            }
        }
        
        // Odd intersection count = inside
        if (intersectionCount % 2 == 1) {
            insideVotes++;
            if (insideVotes >= majority) 
                return true;  // Majority inside
        } else {
            outsideVotes++;
            if (outsideVotes >= majority) 
                return false;  // Majority outside
        }
    }
    
    return insideVotes > outsideVotes;
}

float VoxelGrid::pointToTriangleDistance(const glm::vec3& p, const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2) const {
    // Edge vectors
    glm::vec3 e0 = v1 - v0;
    glm::vec3 e1 = v2 - v0;
    glm::vec3 diff = v0 - p;
    
    float a = glm::dot(e0, e0);
    float b = glm::dot(e0, e1);
    float c = glm::dot(e1, e1);
    float d = glm::dot(e0, diff);
    float e = glm::dot(e1, diff);
    
    float det = a * c - b * b;
    float s = b * e - c * d;
    float t = b * d - a * e;
    
    if (s + t <= det) {
        if (s < 0.0f) {
            if (t < 0.0f) {
                // Region 4
                s = glm::clamp(-d / a, 0.0f, 1.0f);
                t = 0.0f;
            } else {
                // Region 3
                s = 0.0f;
                t = glm::clamp(-e / c, 0.0f, 1.0f);
            }
        } else if (t < 0.0f) {
            // Region 5
            s = glm::clamp(-d / a, 0.0f, 1.0f);
            t = 0.0f;
        } else {
            // Region 0 (inside triangle)
            float invDet = 1.0f / det;
            s *= invDet;
            t *= invDet;
        }
    } else {
        if (s < 0.0f) {
            // Region 2
            s = 0.0f;
            t = glm::clamp(-e / c, 0.0f, 1.0f);
        } else if (t < 0.0f) {
            // Region 6
            s = glm::clamp(-d / a, 0.0f, 1.0f);
            t = 0.0f;
        } else {
            // Region 1
            float numer = c + e - b - d;
            float denom = a - 2.0f * b + c;
            s = glm::clamp(numer / denom, 0.0f, 1.0f);
            t = 1.0f - s;
        }
    }
    
    glm::vec3 closest = v0 + s * e0 + t * e1;
    return glm::length(p - closest);
}

float VoxelGrid::distanceToNearestTriangle(const glm::vec3& point, const Model& mesh, const TriangleHashGrid& triangleGrid) const {
    const auto& vertices = mesh.getVertices();
    const auto& indices = mesh.getIndices();
    
    std::vector<size_t> nearbyTriangles;
    triangleGrid.getNearbyTriangles(point, nearbyTriangles);
    
    if (nearbyTriangles.empty()) {
        return FLT_MAX;
    }
    
    float minDist = FLT_MAX;
    for (size_t triIdx : nearbyTriangles) {
        const glm::vec3& v0 = vertices[indices[triIdx]].pos;
        const glm::vec3& v1 = vertices[indices[triIdx + 1]].pos;
        const glm::vec3& v2 = vertices[indices[triIdx + 2]].pos;
        
        float dist = pointToTriangleDistance(point, v0, v1, v2);
        minDist = std::min(minDist, dist);
    }
    
    return minDist;
}



void VoxelGrid::buildTriangleLists(const Model& mesh, const TriangleHashGrid& triangleGrid) {
    std::cout << "  Building triangle spatial lists..." << std::endl;

    const auto& vertices = mesh.getVertices();
    const auto& indices = mesh.getIndices();

    meshPoints.reserve(vertices.size());
    for (const auto& vertex : vertices) {
        meshPoints.push_back(vertex.pos);
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

    const auto& meshVertices = mesh.getVertices();
    const auto& meshIndices = mesh.getIndices();
    size_t triCount = meshIndices.size() / 3;

    #pragma omp parallel for
    for (int t = 0; t < static_cast<int>(triCount); t++) {
        uint32_t i0 = meshIndices[3 * t + 0];
        uint32_t i1 = meshIndices[3 * t + 1];
        uint32_t i2 = meshIndices[3 * t + 2];

        glm::vec3 w0 = meshVertices[i0].pos;
        glm::vec3 w1 = meshVertices[i1].pos;
        glm::vec3 w2 = meshVertices[i2].pos;

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
                    #pragma omp critical
                    voxelTriangles[static_cast<size_t>(voxelIdx)].push_back(static_cast<int32_t>(t));
                }
            }
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

    std::cout << "  Triangle lists: " << trianglesList.size() << " references across " 
              << numVoxels << " voxels" << std::endl;
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

    std::cout << "[VoxelGrid] Exported " << numPoints << " corner points to " << filename << std::endl;
    std::cout << "  Inside(green): " << insideCount << ", Border(red): " << borderCount << std::endl;
}
