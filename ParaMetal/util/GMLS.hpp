#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include <Eigen/Core>
#include <Eigen/SVD>
#include <glm/glm.hpp>

namespace GMLS {

inline constexpr double RankTolerance = 1e-12;
inline constexpr double ReproductionTolerance = 1e-9;

inline double wendlandC2(double normalizedRadius) {
    if (normalizedRadius >= 1.0) {
        return 0.0;
    }
    const double oneMinusRadius = 1.0 - std::max(0.0, normalizedRadius);
    return oneMinusRadius * oneMinusRadius * oneMinusRadius * oneMinusRadius *
        ((4.0 * normalizedRadius) + 1.0);
}

inline bool solveWeights(
    const Eigen::MatrixXd& basis,
    const Eigen::VectorXd& kernelWeights,
    const std::vector<Eigen::VectorXd>& functionals,
    std::vector<Eigen::VectorXd>& weights) {
    if (basis.rows() == 0 || basis.rows() != kernelWeights.size()) {
        return false;
    }

    Eigen::MatrixXd weightedBasis = basis;
    for (Eigen::Index row = 0; row < weightedBasis.rows(); ++row) {
        weightedBasis.row(row) *= std::sqrt(kernelWeights(row));
    }

    Eigen::JacobiSVD<Eigen::MatrixXd> svd(weightedBasis, Eigen::ComputeThinU | Eigen::ComputeThinV);
    svd.setThreshold(RankTolerance);
    if (svd.rank() != basis.cols()) {
        return false;
    }

    const Eigen::VectorXd singularValues = svd.singularValues();
    weights.clear();
    for (const Eigen::VectorXd& functional : functionals) {
        Eigen::VectorXd spectral = svd.matrixV().transpose() * functional;
        for (Eigen::Index index = 0; index < spectral.size(); ++index) {
            spectral(index) /= singularValues(index);
        }

        Eigen::VectorXd result = svd.matrixU() * spectral;
        for (Eigen::Index row = 0; row < result.size(); ++row) {
            result(row) *= std::sqrt(kernelWeights(row));
        }
        if (!result.allFinite() ||
            (basis.transpose() * result - functional).cwiseAbs().maxCoeff() > ReproductionTolerance) {
            return false;
        }
        weights.push_back(std::move(result));
    }
    return true;
}

inline bool computeSurfaceWeights(
    const glm::dvec3& targetPosition,
    const glm::dvec3& targetNormal,
    const std::vector<glm::dvec3>& sourcePositions,
    double kernelRadius,
    std::vector<double>& valueWeights,
    std::vector<glm::dvec3>& gradientWeights) {
    valueWeights.clear();
    gradientWeights.clear();
    if (sourcePositions.empty() || kernelRadius <= 0.0) {
        return false;
    }

    if (sourcePositions.size() < 4) {
        valueWeights.resize(sourcePositions.size(), 0.0);
        gradientWeights.resize(sourcePositions.size(), glm::dvec3(0.0));
        double weightSum = 0.0;
        for (size_t i = 0; i < sourcePositions.size(); ++i) {
            const double dist = std::max(glm::length(sourcePositions[i] - targetPosition), 1e-7);
            const double w = 1.0 / dist;
            valueWeights[i] = w;
            weightSum += w;
        }
        if (weightSum > 1e-12) {
            for (double& w : valueWeights) w /= weightSum;
        }
        return true;
    }

    Eigen::MatrixXd basis(sourcePositions.size(), 4);
    Eigen::VectorXd kernelWeights(sourcePositions.size());
    for (size_t index = 0; index < sourcePositions.size(); ++index) {
        const glm::dvec3 delta = (sourcePositions[index] - targetPosition) / kernelRadius;
        basis.row(index) << 1.0, delta.x, delta.y, delta.z;
        kernelWeights(index) = wendlandC2(glm::length(delta));
    }

    std::vector<Eigen::VectorXd> functionals(4, Eigen::VectorXd::Zero(4));
    functionals[0](0) = 1.0;
    functionals[1](1) = 1.0;
    functionals[2](2) = 1.0;
    functionals[3](3) = 1.0;
    std::vector<Eigen::VectorXd> solvedWeights;
    if (!solveWeights(basis, kernelWeights, functionals, solvedWeights)) {
        return false;
    }

    valueWeights.resize(sourcePositions.size());
    gradientWeights.resize(sourcePositions.size());
    double positiveSum = 0.0;
    for (size_t index = 0; index < sourcePositions.size(); ++index) {
        const double w = std::max(0.0, solvedWeights[0](index));
        valueWeights[index] = w;
        positiveSum += w;
        gradientWeights[index] = glm::dvec3(
            solvedWeights[1](index),
            solvedWeights[2](index),
            solvedWeights[3](index)) / kernelRadius;
    }

    if (positiveSum > 1e-12) {
        for (double& w : valueWeights) {
            w /= positiveSum;
        }
    } else {
        const double uniform = 1.0 / static_cast<double>(sourcePositions.size());
        for (double& w : valueWeights) {
            w = uniform;
        }
    }
    return true;
}

inline bool computeContactValueWeights(
    const glm::dvec3& targetPosition,
    const std::vector<glm::dvec3>& sourcePositions,
    double kernelRadius,
    std::vector<double>& valueWeights) {
    valueWeights.clear();
    if (sourcePositions.size() < 4 || kernelRadius <= 0.0) {
        return false;
    }

    Eigen::MatrixXd basis(sourcePositions.size(), 4);
    Eigen::VectorXd kernelWeights(sourcePositions.size());
    for (size_t index = 0; index < sourcePositions.size(); ++index) {
        const glm::dvec3 delta = (sourcePositions[index] - targetPosition) / kernelRadius;
        basis.row(index) << 1.0, delta.x, delta.y, delta.z;
        kernelWeights(index) = wendlandC2(glm::length(delta));
    }

    std::vector<Eigen::VectorXd> functionals(1, Eigen::VectorXd::Zero(4));
    functionals[0](0) = 1.0;
    std::vector<Eigen::VectorXd> solvedWeights;
    if (!solveWeights(basis, kernelWeights, functionals, solvedWeights)) {
        return false;
    }
    valueWeights.resize(sourcePositions.size());
    for (size_t index = 0; index < sourcePositions.size(); ++index) {
        valueWeights[index] = solvedWeights[0](index);
    }
    return true;
}

} // namespace GMLS
