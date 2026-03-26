#pragma once

struct RemeshParams {
    int iterations = 1;
    float minAngleDegrees = 30.0f;
    float maxEdgeLength = 0.1f;
    float stepSize = 0.25f;
};
