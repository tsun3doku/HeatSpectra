#include "NodeGraphHash.hpp"

#include <cstring>

uint64_t NodeGraphHash::start() {
    return hashOffset;
}

void NodeGraphHash::combine(uint64_t& hash, uint64_t value) {
    hash ^= value;
    hash *= hashPrime;
}

void NodeGraphHash::combineFloat(uint64_t& hash, float value) {
    static_assert(sizeof(float) == sizeof(uint32_t), "float size mismatch");
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    combine(hash, bits);
}

void NodeGraphHash::combineString(uint64_t& hash, const std::string& value) {
    for (unsigned char ch : value) {
        combine(hash, ch);
    }
}
