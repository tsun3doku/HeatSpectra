#include "ComputeCache.hpp"

uint64_t ComputeCache::combine(uint64_t seed, uint64_t value) {
    return seed ^ (value + 0x9E3779B97F4A7C15ull + (seed << 6) + (seed >> 2));
}
