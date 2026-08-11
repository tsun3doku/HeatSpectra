#pragma once

#include "runtime/package/RuntimePackages.hpp"
#include "util/Units.hpp"

namespace runtimepackage {

class CoordinatePass final {
public:
    void run(ModelPackage& package, units::LengthUnit worldUnit) const;
    void run(RemeshPackage& package, units::LengthUnit worldUnit) const;
    void run(PointPackage& package, units::LengthUnit worldUnit) const;
    void run(VoronoiPackage& package, units::LengthUnit worldUnit) const;
    void run(ContactPackage& package, units::LengthUnit worldUnit) const;
    void run(HeatPackage& package, units::LengthUnit worldUnit) const;
    static void scalePositions(std::vector<float>& positions, float factor);

private:
    static void scalePositions(std::vector<glm::vec4>& positions, float factor);
    static void scaleTranslation(std::array<float, 16>& matrix, float factor);
};

}
