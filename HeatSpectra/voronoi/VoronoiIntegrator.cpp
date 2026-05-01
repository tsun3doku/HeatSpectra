#include "VoronoiIntegrator.hpp"
#include <iostream>
#include <libs/nanoflann/include/nanoflann.hpp>

struct PointCloudAdapter {
    const std::vector<glm::dvec3>& pts;
    inline size_t kdtree_get_point_count() const { return pts.size(); }
    inline double kdtree_get_pt(const size_t idx, const size_t dim) const {
        return pts[idx][static_cast<glm::dvec3::length_type>(dim)];
    }
    template <class BBOX> bool kdtree_get_bbox(BBOX&) const { return false; }
};

void VoronoiIntegrator::extractNeighborIndices(const std::vector<std::vector<uint32_t>>& inputNeighborIndices,
    const std::vector<glm::dvec3>& inputSeedPositions,int K) {
    neighborIndices.clear();
    seedPositions.clear();
        
    // Flatten 2D neighbor array to 1D GPU buffer
    neighborIndices.reserve(inputNeighborIndices.size() * K);
    for (const auto& cellNeighbors : inputNeighborIndices) {
        neighborIndices.insert(neighborIndices.end(), 
                               cellNeighbors.begin(), cellNeighbors.end());
    }
    
    // Store seed positions for GPU
    for (const auto& seed : inputSeedPositions) {
        seedPositions.push_back(glm::vec4(seed, 1.0f));  // w=1.0 for regular cells
    }
}

void VoronoiIntegrator::computeNeighbors(const std::vector<glm::dvec3>& seedPositions, int K) {
    PointCloudAdapter cloud{seedPositions};
    
    // Build KDtree
    using KDTree = nanoflann::KDTreeSingleIndexAdaptor<
        nanoflann::L2_Simple_Adaptor<double, PointCloudAdapter>,
        PointCloudAdapter, 3>;
    
    KDTree index(3, cloud, nanoflann::KDTreeSingleIndexAdaptorParams(10));
    index.buildIndex();

    std::vector<std::vector<uint32_t>> neighborIndices;
    neighborIndices.reserve(seedPositions.size());
    
    // For each seed, find K+1 nearest using KDtree
    for (size_t i = 0; i < seedPositions.size(); i++) {
        const double query_pt[3] = {seedPositions[i].x, seedPositions[i].y, seedPositions[i].z};
        
        // Query K+1 neighbors 
        std::vector<size_t> ret_indices(K + 1);
        std::vector<double> out_dists_sqr(K + 1);
        
        nanoflann::KNNResultSet<double> resultSet(K + 1);
        resultSet.init(ret_indices.data(), out_dists_sqr.data());
        index.findNeighbors(resultSet, query_pt);
        
        // Filter out self and take first K neighbors
        std::vector<uint32_t> kNeighbors;
        for (size_t j = 0; j < ret_indices.size() && kNeighbors.size() < K; j++) {
            if (ret_indices[j] != i) {  // Skip self
                kNeighbors.push_back(static_cast<uint32_t>(ret_indices[j]));
            }
        }
        
        while (kNeighbors.size() < K) {
            kNeighbors.push_back(UINT32_MAX);
        }
        
        neighborIndices.push_back(kNeighbors);
    }
    
    extractNeighborIndices(neighborIndices, seedPositions, K);
}

void VoronoiIntegrator::extractMeshTriangles(
    const std::vector<glm::vec3>& positions,
    const std::vector<uint32_t>& indices) {
    meshTriangles.clear();

    for (size_t i = 0; i < indices.size(); i += 3) {
        if (i + 2 >= indices.size() ||
            indices[i] >= positions.size() ||
            indices[i + 1] >= positions.size() ||
            indices[i + 2] >= positions.size()) {
            continue;
        }

        MeshTriangleGPU tri;
        tri.v0 = glm::vec4(positions[indices[i]], 0.0f);
        tri.v1 = glm::vec4(positions[indices[i + 1]], 0.0f);
        tri.v2 = glm::vec4(positions[indices[i + 2]], 0.0f);

        meshTriangles.push_back(tri);
    }

}
