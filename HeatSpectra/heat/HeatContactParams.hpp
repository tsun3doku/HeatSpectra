#pragma once

#include "contact/ContactTypes.hpp"

struct HeatContactParams {
    float thermalConductance = 16000.0f;
    float contactPressure = 1.0f;
    float frictionCoeff = 0.5f;
    float padding = 0.0f;

    bool operator==(const HeatContactParams& rhs) const {
        return thermalConductance == rhs.thermalConductance &&
            contactPressure == rhs.contactPressure &&
            frictionCoeff == rhs.frictionCoeff;
    }

    bool operator!=(const HeatContactParams& rhs) const {
        return !(*this == rhs);
    }
};

struct HeatContactBinding {
    ConfiguredContactPair pair{};
    HeatContactParams params{};

    bool operator==(const HeatContactBinding& rhs) const {
        return pair == rhs.pair && params == rhs.params;
    }

    bool operator!=(const HeatContactBinding& rhs) const {
        return !(*this == rhs);
    }
};
