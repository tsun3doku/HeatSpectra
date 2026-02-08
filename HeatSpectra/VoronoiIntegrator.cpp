#include "VoronoiIntegrator.hpp"
#include "TriangleHashGrid.hpp"
#include <iostream>
#include <algorithm>
#include <set>
#include <glm/gtx/norm.hpp>
#include <libs/nanoflann/include/nanoflann.hpp>

struct PointCloudAdapter {
    const std::vector<glm::dvec3>& pts;
    inline size_t kdtree_get_point_count() const { return pts.size(); }
    inline double kdtree_get_pt(const size_t idx, const size_t dim) const { return pts[idx][dim]; }
    template <class BBOX> bool kdtree_get_bbox(BBOX&) const { return false; }
};

struct SeedPointCloudAdapter {
    const std::vector<glm::vec3>& pts;
    inline size_t kdtree_get_point_count() const { return pts.size(); }
    inline float kdtree_get_pt(const size_t idx, const size_t dim) const { return pts[idx][static_cast<int>(dim)]; }
    template <class BBOX> bool kdtree_get_bbox(BBOX&) const { return false; }
};

void VoronoiIntegrator::extractNeighborIndices(const std::vector<std::vector<uint32_t>>& neighborIndices,
    const std::vector<glm::dvec3>& seedPositions,int K) {
    neighborIndices_.clear();
    seedPositions_.clear();
        
    // Flatten 2D neighbor array to 1D GPU buffer
    neighborIndices_.reserve(neighborIndices.size() * K);
    for (const auto& cellNeighbors : neighborIndices) {
        neighborIndices_.insert(neighborIndices_.end(), 
                               cellNeighbors.begin(), cellNeighbors.end());
    }
    
    // Store seed positions for GPU
    for (const auto& seed : seedPositions) {
        seedPositions_.push_back(glm::vec4(seed, 1.0f));  // w=1.0 for regular cells
    }
}

void VoronoiIntegrator::computeNeighbors(const std::vector<glm::dvec3>& seedPositions, int K) {
    std::cout << "[VoronoiIntegrator] Building KDtree for " << seedPositions.size() << " seeds..." << std::endl;
    
    PointCloudAdapter cloud{seedPositions};
    
    // Build KDtree
    using KDTree = nanoflann::KDTreeSingleIndexAdaptor<
        nanoflann::L2_Simple_Adaptor<double, PointCloudAdapter>,
        PointCloudAdapter, 3>;
    
    KDTree index(3, cloud, nanoflann::KDTreeSingleIndexAdaptorParams(10));
    index.buildIndex();
    
    std::cout << "[VoronoiIntegrator] KDtree built, finding K nearest neighbors..." << std::endl;
    
    std::vector<std::vector<uint32_t>> neighborIndices;
    neighborIndices.reserve(seedPositions.size());
    kNearestDistances_.resize(seedPositions.size(), 0.0f);
    
    // For each seed, find K+1 nearest using KDtree
    for (size_t i = 0; i < seedPositions.size(); i++) {
        const double query_pt[3] = {seedPositions[i].x, seedPositions[i].y, seedPositions[i].z};
        
        // Query K+1 neighbors 
        std::vector<size_t> ret_indices(K + 1);
        std::vector<double> out_dists_sqr(K + 1);
        
        nanoflann::KNNResultSet<double> resultSet(K + 1);
        resultSet.init(ret_indices.data(), out_dists_sqr.data());
        index.findNeighbors(resultSet, query_pt);
        
        // Filter out self and take first K neighbors, compute max distance
        std::vector<uint32_t> kNeighbors;
        float maxDist = 0.0f;
        for (size_t j = 0; j < ret_indices.size() && kNeighbors.size() < K; j++) {
            if (ret_indices[j] != i) {  // Skip self
                kNeighbors.push_back(static_cast<uint32_t>(ret_indices[j]));
                maxDist = std::max(maxDist, static_cast<float>(std::sqrt(out_dists_sqr[j])));
            }
        }
        
        // Store max K nearest distance for this cell
        kNearestDistances_[i] = maxDist;
        
        while (kNeighbors.size() < K) {
            kNeighbors.push_back(UINT32_MAX);
        }
        
        neighborIndices.push_back(kNeighbors);
    }
    
    extractNeighborIndices(neighborIndices, seedPositions, K);
    
    std::cout << "[VoronoiIntegrator] Calculated " << K << " nearest neighbors for " 
              << seedPositions.size() << " seeds" << std::endl;
}

void VoronoiIntegrator::extractMeshTriangles(const Model& surfaceMesh) {
    meshTriangles_.clear();
    
    const auto& vertices = surfaceMesh.getVertices();
    const auto& indices = surfaceMesh.getIndices();
    
    for (size_t i = 0; i < indices.size(); i += 3) {
        const auto& v0 = vertices[indices[i]];
        const auto& v1 = vertices[indices[i + 1]];
        const auto& v2 = vertices[indices[i + 2]];
        
        MeshTriangleGPU tri;
        tri.v0 = glm::vec4(v0.pos, 0.0f);
        tri.v1 = glm::vec4(v1.pos, 0.0f);
        tri.v2 = glm::vec4(v2.pos, 0.0f);
        
        meshTriangles_.push_back(tri);
    }
    
    std::cout << "[VoronoiIntegrator] Extracted " << meshTriangles_.size() << " mesh triangles" << std::endl;
}

bool VoronoiIntegrator::pointInUnrestrictedCell(uint32_t cellId, const glm::vec3& p, uint32_t K) const {
    if (cellId >= seedPositions_.size()) {
        return false;
    }
    if (K == 0) {
        return false;
    }
    if (neighborIndices_.size() < static_cast<size_t>(cellId + 1) * static_cast<size_t>(K)) {
        return false;
    }

    const glm::vec3 S(seedPositions_[cellId]);
    const uint32_t base = cellId * K;
    const float eps = 1e-5f;

    for (uint32_t n = 0; n < K; ++n) {
        uint32_t neigh = neighborIndices_[base + n];
        if (neigh == UINT32_MAX) {
            break;
        }
        if (neigh >= seedPositions_.size()) {
            continue;
        }
        const glm::vec3 B(seedPositions_[neigh]);
        glm::vec3 dir = S - B;
        float dirNorm = glm::length(dir);
        if (dirNorm < 1e-10f) {
            continue;
        }
        glm::vec3 normal = dir / dirNorm;

        double s2 = static_cast<double>(S.x) * S.x + static_cast<double>(S.y) * S.y + static_cast<double>(S.z) * S.z;
        double b2 = static_cast<double>(B.x) * B.x + static_cast<double>(B.y) * B.y + static_cast<double>(B.z) * B.z;
        float w = static_cast<float>((b2 - s2) / (2.0 * static_cast<double>(dirNorm)));

        float val = glm::dot(normal, p) + w;
        if (val < -eps) {
            return false;
        }
    }

    return true;
}

void VoronoiIntegrator::computeSurfacePointMapping(const std::vector<glm::vec3>& surfacePoints, const std::vector<uint32_t>& seedFlags,
    uint32_t K, std::vector<uint32_t>& outCellIndices) const {
    outCellIndices.clear();
    outCellIndices.resize(surfacePoints.size(), UINT32_MAX);

    if (surfacePoints.empty()) {
        return;
    }
    if (seedFlags.size() != seedPositions_.size()) {
        return;
    }
    if (seedPositions_.empty() || neighborIndices_.empty()) {
        return;
    }
    if (K == 0) {
        return;
    }

    std::vector<glm::vec3> regularSeedPos;
    std::vector<uint32_t> regularToCell;
    regularSeedPos.reserve(seedPositions_.size());
    regularToCell.reserve(seedPositions_.size());

    for (uint32_t cellId = 0; cellId < static_cast<uint32_t>(seedPositions_.size()); ++cellId) {
        uint32_t flags = seedFlags[cellId];
        bool isGhost = (flags & 1u) != 0u;
        if (isGhost) {
            continue;
        }
        const glm::vec4& s = seedPositions_[cellId];
        regularSeedPos.push_back(glm::vec3(s.x, s.y, s.z));
        regularToCell.push_back(cellId);
    }

    if (regularSeedPos.empty()) {
        return;
    }

    SeedPointCloudAdapter cloud{ regularSeedPos };
    using KDTree = nanoflann::KDTreeSingleIndexAdaptor<
        nanoflann::L2_Simple_Adaptor<float, SeedPointCloudAdapter>,
        SeedPointCloudAdapter,
        3
    >;
    KDTree index(3, cloud, nanoflann::KDTreeSingleIndexAdaptorParams(10));
    index.buildIndex();

    std::vector<size_t> retIndices;
    std::vector<float> outDistSq;

    for (size_t i = 0; i < surfacePoints.size(); ++i) {
        const glm::vec3& p = surfacePoints[i];
        float query[3] = { p.x, p.y, p.z };

        size_t searchK = 8;
        uint32_t chosenCell = UINT32_MAX;

        for (;;) {
            searchK = std::min(searchK, regularSeedPos.size());
            retIndices.assign(searchK, 0);
            outDistSq.assign(searchK, 0.0f);

            nanoflann::KNNResultSet<float> resultSet(searchK);
            resultSet.init(retIndices.data(), outDistSq.data());
            index.findNeighbors(resultSet, query);

            for (size_t j = 0; j < retIndices.size(); ++j) {
                uint32_t cellId = regularToCell[static_cast<size_t>(retIndices[j])];
                if (pointInUnrestrictedCell(cellId, p, K)) {
                    chosenCell = cellId;
                    break;
                }
            }

            if (chosenCell != UINT32_MAX) {
                break;
            }
            if (searchK >= regularSeedPos.size()) {
                chosenCell = regularToCell[static_cast<size_t>(retIndices[0])];
                break;
            }
            searchK = searchK * 2;
        }

        outCellIndices[i] = chosenCell;
    }
}

void VoronoiIntegrator::buildVoxelGrid(
    const Model& mesh,
    const TriangleHashGrid& triangleGrid,
    int gridSize
) {
    std::cout << "[VoronoiIntegrator] Building voxel grid..." << std::endl;
    voxelGrid_.build(mesh, triangleGrid, gridSize);
    
    voxelGrid_.exportOccupancyVisualization("voxel_occupancy.ply");
    
    std::cout << "[VoronoiIntegrator] Voxel grid complete" << std::endl;
}
