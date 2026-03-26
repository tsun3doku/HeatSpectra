#pragma once

#include "mesh/remesher/SupportingHalfedge.hpp"

struct IntrinsicData {
    SupportingHalfedge::GPUBuffers gpuBuffers;
    SupportingHalfedge::IntrinsicMesh mesh;
};
