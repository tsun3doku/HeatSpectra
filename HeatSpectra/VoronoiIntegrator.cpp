#include "VoronoiIntegrator.hpp"
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

void VoronoiIntegrator::extractNeighborIndices(
    const std::vector<std::vector<uint32_t>>& neighborIndices,
    const std::vector<glm::dvec3>& seedPositions,
    int K
) {
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

void VoronoiIntegrator::computeNeighbors(
    const std::vector<glm::dvec3>& seedPositions,
    int K
) {
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

void VoronoiIntegrator::computeSpatialMapping(size_t numCells, const std::vector<glm::dvec3>& seedPositions, int K) {    
    relevantMeshIndices_.clear();
    relevantMeshOffsets_.clear();
    relevantMeshCounts_.clear();
}

void VoronoiIntegrator::buildVoxelGrid(
    const Model& mesh,
    const TriangleHashGrid& triangleGrid,
    int gridSize
) {
    std::cout << "[VoronoiIntegrator] Building voxel grid..." << std::endl;
    voxelGrid_.build(mesh, triangleGrid, gridSize);
    
    // Export occupancy visualization for debugging
    voxelGrid_.exportOccupancyVisualization("voxel_occupancy.ply");
    
    std::cout << "[VoronoiIntegrator] Voxel grid complete" << std::endl;
}

