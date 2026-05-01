#pragma once

#include <cstdint>

namespace contact {

struct Sample {
    uint32_t sourceTriangleIndex;
    float u;
    float v;
    float wArea;
};

static_assert(sizeof(Sample) == 16, "Sample must match GPU stride");

struct GMLSSample {
    float areaWeight;
    uint32_t receiverValueWeightOffset;
    uint32_t receiverValueWeightCount;
    uint32_t receiverScatterWeightOffset;
    uint32_t receiverScatterWeightCount;
    uint32_t emitterValueWeightOffset;
    uint32_t emitterValueWeightCount;
    uint32_t emitterScatterWeightOffset;
    uint32_t emitterScatterWeightCount;
    uint32_t _pad0;
    uint32_t _pad1;
    uint32_t _pad2;
};

static_assert(sizeof(GMLSSample) == 48, "GMLSSample must match GPU stride");

struct GMLSWeight {
    uint32_t cellIndex;
    float weight;
    uint32_t _pad0;
    uint32_t _pad1;
};

static_assert(sizeof(GMLSWeight) == 16, "GMLSWeight must match GPU stride");

struct PushConstant {
    uint32_t couplingKind;
    float heatSourceTemperature;
    uint32_t _pad1;
    uint32_t _pad2;
};

static_assert(sizeof(PushConstant) == 16, "PushConstant must match GPU stride");

}
