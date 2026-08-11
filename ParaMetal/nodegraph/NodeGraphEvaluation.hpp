#pragma once

#include "NodeGraphCoreTypes.hpp"
#include "NodeGraphDataTypes.hpp"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

enum class EvaluatedSocketStatus : uint8_t {
    Missing,
    Value,
    Error,
};

struct EvaluatedSocketValue {
    EvaluatedSocketStatus status = EvaluatedSocketStatus::Missing;
    NodeDataBlock data{};
    std::string error;
};

// Per-execution socket outputs produced by NodeGraphRuntime.
// Runtime packages and backend products are deliberately outside this type.
struct NodeGraphEvaluation {
    std::unordered_map<uint64_t, EvaluatedSocketValue> outputsBySocket;

    const EvaluatedSocketValue* outputFor(uint64_t socketKey) const {
        if (socketKey == 0) return nullptr;
        auto it = outputsBySocket.find(socketKey);
        return it != outputsBySocket.end() ? &it->second : nullptr;
    }
};
