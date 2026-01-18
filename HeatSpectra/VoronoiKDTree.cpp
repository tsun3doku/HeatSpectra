#include "VoronoiKDTree.hpp"
#include <limits>

NearestNeighborIterator::NearestNeighborIterator(
    const SeedKDTree& tree, 
    const float* query, 
    size_t batchSize
)
    : kdtree(tree)
    , query(query)
    , currentIdx(0)
    , batchSize(batchSize)
    , totalFetched(0)
    , exhausted(false)
{
    fetchNextBatch();
}

bool NearestNeighborIterator::hasNext() const {
    return currentIdx < indices.size();
}

std::pair<size_t, float> NearestNeighborIterator::next() {
    // If current batch consumed, try to fetch more
    if (currentIdx >= indices.size()) {
        if (exhausted) {
            return {SIZE_MAX, std::numeric_limits<float>::max()};
        }
        fetchNextBatch();
        if (indices.empty() || currentIdx >= indices.size()) {
            return {SIZE_MAX, std::numeric_limits<float>::max()};
        }
    }
    
    auto result = std::make_pair(indices[currentIdx], distsSq[currentIdx]);
    currentIdx++;
    return result;
}

void NearestNeighborIterator::fetchNextBatch() {
    size_t k = totalFetched + batchSize;
    
    indices.resize(k);
    distsSq.resize(k);
    
    nanoflann::KNNResultSet<float> resultSet(k);
    resultSet.init(indices.data(), distsSq.data());
    kdtree.findNeighbors(resultSet, query);
    
    size_t actualFound = resultSet.size();
    
    if (actualFound <= totalFetched) {
        exhausted = true;
        indices.clear();
        distsSq.clear();
        return;
    }
    
    // Resize to results
    indices.resize(actualFound);
    distsSq.resize(actualFound);
    
    currentIdx = totalFetched;
    totalFetched = actualFound;
}
