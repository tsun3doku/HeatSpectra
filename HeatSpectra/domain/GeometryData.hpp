#pragma once

#include "nodegraph/NodeGraphCoreTypes.hpp"
#include "nodegraph/NodeGraphTypes.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

//                                                      [ Invariant:
//                                                        - Payloads are node graph authored data
//                                                        - They may contain authored values and NodeDataHandle values
//                                                        - They must not contain runtime objects/ids, scene objects, 
//                                                          backend/controller objects or GPU resources 
//                                                        - They must not be used directly by any backends ]

struct GeometryAttribute {
    std::string name;
    GeometryAttributeDomain domain = GeometryAttributeDomain::Point;
    GeometryAttributeDataType dataType = GeometryAttributeDataType::Float;
    uint32_t tupleSize = 1;
    std::vector<float> floatValues;
    std::vector<int64_t> intValues;
    std::vector<uint8_t> boolValues;
};

struct GeometryGroup {
    uint32_t id = 0;
    std::string name;
    std::string source;
};

struct GeometryData {
    uint64_t payloadHash = 0;
    std::string baseModelPath;
    std::array<float, 16> localToWorld{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    std::vector<float> pointPositions;
    std::vector<uint32_t> triangleIndices;
    std::vector<uint32_t> triangleGroupIds;
    std::vector<GeometryGroup> groups;
    std::vector<GeometryAttribute> attributes;
};
