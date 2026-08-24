#include "CoordinatePass.hpp"

void runtimepackage::CoordinatePass::run(ModelPackage& package, units::LengthUnit worldUnit) const {
    const float factor = units::canonicalToWorldScale(worldUnit);
    package.canonicalToWorldScale = factor;
    scaleTranslation(package.localToWorld, factor);
}

void runtimepackage::CoordinatePass::run(RemeshPackage& package, units::LengthUnit worldUnit) const {
    const float factor = units::canonicalToWorldScale(worldUnit);
    scalePositions(package.sourceGeometry.pointPositions, factor);
    scalePositions(package.sourceGeometry.renderPositions, factor);
    scaleTranslation(package.localToWorld, factor);
    package.maxEdgeLength *= factor;
    package.display.normalLength *= factor;
}

void runtimepackage::CoordinatePass::run(PointPackage& package, units::LengthUnit worldUnit) const {
    const float factor = units::canonicalToWorldScale(worldUnit);
    scalePositions(package.positions, factor);
    package.domainMinimum *= factor;
    package.domainMaximum *= factor;
    scaleTranslation(package.localToWorld, factor);
}

void runtimepackage::CoordinatePass::run(
    VoronoiPackage& package,
    units::LengthUnit worldUnit) const {
    const float factor = units::canonicalToWorldScale(worldUnit);
    package.canonicalToWorldScale = factor;
    package.authored.cellSize *= factor;
    package.authored.sdfPadding *= factor;
}

void runtimepackage::CoordinatePass::run(HeatPackage& package, units::LengthUnit worldUnit) const {
    package.worldUnit = worldUnit;
    package.canonicalToWorldScale = units::canonicalToWorldScale(worldUnit);
}

void runtimepackage::CoordinatePass::scalePositions(std::vector<float>& positions, float factor) {
    for (float& value : positions) value *= factor;
}

void runtimepackage::CoordinatePass::scalePositions(std::vector<glm::vec4>& positions, float factor) {
    for (glm::vec4& value : positions) {
        value.x *= factor;
        value.y *= factor;
        value.z *= factor;
    }
}

void runtimepackage::CoordinatePass::scaleTranslation(std::array<float, 16>& matrix, float factor) {
    matrix[12] *= factor;
    matrix[13] *= factor;
    matrix[14] *= factor;
}
