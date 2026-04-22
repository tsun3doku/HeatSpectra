#pragma once

#include <cstdint>
#include <string>

struct NodeDataBlock;

class NodeGraphHash {
public:
    static uint64_t start();
    static void combine(uint64_t& hash, uint64_t value);
    static void combineFloat(uint64_t& hash, float value);
    static void combineString(uint64_t& hash, const std::string& value);
    static void combineInputHash(uint64_t& hash, const NodeDataBlock* input);

private:
    static constexpr uint64_t hashOffset = 1469598103934665603ull;
    static constexpr uint64_t hashPrime = 1099511628211ull;
};
