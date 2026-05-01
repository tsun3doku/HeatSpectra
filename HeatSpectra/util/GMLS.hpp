#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/epsilon.hpp>
#include <glm/gtc/matrix_inverse.hpp>

namespace GMLS {

struct LinearPolynomialBasis {
    static constexpr std::size_t kFunctionCount = 4;

    static glm::dvec4 evaluate(const glm::dvec3& delta) {
        return glm::dvec4(1.0, delta.x, delta.y, delta.z);
    }
};

inline double wendlandC2(double normalizedRadius) {
    if (normalizedRadius >= 1.0) {
        return 0.0;
    }

    const double oneMinusRadius = 1.0 - std::max(0.0, normalizedRadius);
    return oneMinusRadius * oneMinusRadius * oneMinusRadius * oneMinusRadius *
        ((4.0 * normalizedRadius) + 1.0);
}

template <typename Basis = LinearPolynomialBasis>
bool computeWeights(
    const glm::dvec3& targetPos,
    const std::vector<glm::dvec3>& sourcePositions,
    double kernelRadius,
    std::vector<double>& outValueWeights,
    std::vector<glm::dvec3>& outGradientWeights) {
    outValueWeights.clear();
    outGradientWeights.clear();

    if (kernelRadius <= 0.0 || sourcePositions.empty()) {
        return false;
    }

    constexpr std::size_t kBasisCount = Basis::kFunctionCount;
    static_assert(kBasisCount == 4, "Current GMLS implementation assumes a linear 3D basis.");

    outValueWeights.resize(sourcePositions.size(), 0.0);
    outGradientWeights.resize(sourcePositions.size(), glm::dvec3(0.0));

    glm::dmat4 moment(0.0);
    // thread_local: after the first call on each thread these vectors never
    // reallocate — they just resize in place. Safe with OpenMP parallel for.
    thread_local std::vector<glm::dvec4> basisValues;
    thread_local std::vector<double>     kernelWeights;
    basisValues.assign(sourcePositions.size(), glm::dvec4(0.0));
    kernelWeights.assign(sourcePositions.size(), 0.0);

    for (std::size_t index = 0; index < sourcePositions.size(); ++index) {
        const glm::dvec3 delta = sourcePositions[index] - targetPos;
        const double normalizedRadius = glm::length(delta) / kernelRadius;
        const double weight = wendlandC2(normalizedRadius);
        if (weight <= 0.0) {
            continue;
        }

        const glm::dvec4 basis = Basis::evaluate(delta);
        basisValues[index] = basis;
        kernelWeights[index] = weight;
        moment += weight * glm::outerProduct(basis, basis);
    }

    // Compute inverse once and detect singularity via NaN/Inf on the diagonal —
    // avoids a separate glm::determinant call (which internally repeats the same work).
    const glm::dmat4 inverseMoment = glm::inverse(moment);
    if (!std::isfinite(inverseMoment[0][0]) || !std::isfinite(inverseMoment[1][1]) ||
        !std::isfinite(inverseMoment[2][2]) || !std::isfinite(inverseMoment[3][3])) {
        outValueWeights.clear();
        outGradientWeights.clear();
        return false;
    }
    const glm::dvec4 valueFunctional(1.0, 0.0, 0.0, 0.0);
    const glm::dvec4 gradientFunctionalX(0.0, 1.0, 0.0, 0.0);
    const glm::dvec4 gradientFunctionalY(0.0, 0.0, 1.0, 0.0);
    const glm::dvec4 gradientFunctionalZ(0.0, 0.0, 0.0, 1.0);

    const glm::dvec4 alphaValue = inverseMoment * valueFunctional;
    const glm::dvec4 alphaDx = inverseMoment * gradientFunctionalX;
    const glm::dvec4 alphaDy = inverseMoment * gradientFunctionalY;
    const glm::dvec4 alphaDz = inverseMoment * gradientFunctionalZ;

    bool hasSupport = false;
    for (std::size_t index = 0; index < sourcePositions.size(); ++index) {
        const double kernelWeight = kernelWeights[index];
        if (kernelWeight <= 0.0) {
            continue;
        }

        hasSupport = true;
        const glm::dvec4& basis = basisValues[index];
        outValueWeights[index] = kernelWeight * glm::dot(basis, alphaValue);
        outGradientWeights[index] = glm::dvec3(
            kernelWeight * glm::dot(basis, alphaDx),
            kernelWeight * glm::dot(basis, alphaDy),
            kernelWeight * glm::dot(basis, alphaDz));
    }

    if (!hasSupport) {
        outValueWeights.clear();
        outGradientWeights.clear();
        return false;
    }

    return true;
}

} // namespace GMLS
