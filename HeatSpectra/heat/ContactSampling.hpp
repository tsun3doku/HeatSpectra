#pragma once

#include <cstdint>
#include <glm/glm.hpp>

struct Quadrature {
    static constexpr uint32_t count = 7u;
    static constexpr glm::vec3 bary[count] = {
        glm::vec3(1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f),
        glm::vec3(0.0597158717f, 0.4701420641f, 0.4701420641f),
        glm::vec3(0.4701420641f, 0.0597158717f, 0.4701420641f),
        glm::vec3(0.4701420641f, 0.4701420641f, 0.0597158717f),
        glm::vec3(0.7974269853f, 0.1012865073f, 0.1012865073f),
        glm::vec3(0.1012865073f, 0.7974269853f, 0.1012865073f),
        glm::vec3(0.1012865073f, 0.1012865073f, 0.7974269853f),
    };
    static constexpr float weights[count] = {
        0.2250000000f,
        0.1323941527f,
        0.1323941527f,
        0.1323941527f,
        0.1259391805f,
        0.1259391805f,
        0.1259391805f,
    };
};

struct ContactCellWeight {
    uint32_t cellIndex = 0;
    uint32_t sampleIndex = 0;
    float weight = 0.0f;
};
