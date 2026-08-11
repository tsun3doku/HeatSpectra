#pragma once

#include "nodegraph/NodeGraphCoreTypes.hpp"
#include <array>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

//                                                      [ Invariant:
//                                                        - Payloads are node graph authored data
//                                                        - They may contain authored values and NodeDataHandle values
//                                                        - They must not contain runtime objects/ids, scene objects,
//                                                          backend/controller objects or GPU resources
//                                                        - They must not be used directly by any backends ]

struct PointData {
    std::vector<glm::vec4> positions;  // xyz local space, w = 1
    glm::vec3 domainMinimum{0.0f};
    glm::vec3 domainMaximum{0.0f};
    std::array<float, 16> localToWorld{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    bool active = false;

};
