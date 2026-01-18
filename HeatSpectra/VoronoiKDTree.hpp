#pragma once

#include "VoronoiSeeder.hpp"
#include <libs/nanoflann/include/nanoflann.hpp>
#include <vector>

struct SeedAdapter {
    const std::vector<VoronoiSeeder::Seed>& seeds;
    
    inline size_t kdtree_get_point_count() const { return seeds.size(); }
    inline float kdtree_get_pt(const size_t idx, const size_t dim) const {
        return seeds[idx].pos[dim];
    }
    template <class BBOX> bool kdtree_get_bbox(BBOX&) const { return false; }
};

using SeedKDTree = nanoflann::KDTreeSingleIndexAdaptor<
    nanoflann::L2_Simple_Adaptor<float, SeedAdapter>,
    SeedAdapter, 3>;

class NearestNeighborIterator {
public:
    NearestNeighborIterator(const SeedKDTree& tree, const float* query, size_t batchSize = 100);
    
    bool hasNext() const;
    std::pair<size_t, float> next();
    
private:
    void fetchNextBatch();
    
    const SeedKDTree& kdtree;
    const float* query;
    std::vector<size_t> indices;
    std::vector<float> distsSq;
    size_t currentIdx;
    size_t batchSize;
    size_t totalFetched;
    bool exhausted;
};