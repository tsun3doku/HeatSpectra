#pragma once

#include "nodegraph/NodeGraphCoreTypes.hpp"

#include <vector>

//                                                      [ Invariant:
//                                                        - Payloads are node graph authored data
//                                                        - They may contain authored values and NodeDataHandle values
//                                                        - They must not contain runtime objects/ids, scene objects,
//                                                          backend/controller objects or GPU resources
//                                                        - This header must not be included in any backend ]

enum class DomainType : uint8_t { Points, Global };

struct VoronoiData {
    float cellSize = 0.005f;
    int voxelResolution = 128;
    float sdfPadding = 0.01f;
    DomainType domainType = DomainType::Points;

    // Point path
    NodeDataHandle pointsPayloadHandle;

    // Global path (Points + Remeshes shared-RVD domain)
    std::vector<NodeDataHandle> modelMeshHandles;

    bool active = false;

};
