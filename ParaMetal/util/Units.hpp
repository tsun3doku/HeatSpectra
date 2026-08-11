#pragma once

#include <string_view>

namespace units {

enum class LengthUnit {
    Millimeter,
    Centimeter,
    Meter
};

constexpr LengthUnit defaultLengthUnit() noexcept {
    return LengthUnit::Meter;
}

constexpr float metersPerUnit(LengthUnit unit) noexcept {
    switch (unit) {
    case LengthUnit::Millimeter: return 0.001f;
    case LengthUnit::Centimeter: return 0.01f;
    case LengthUnit::Meter:      return 1.0f;
    }
    return 1.0f;
}

constexpr float scaleBetween(LengthUnit from, LengthUnit to) noexcept {
    return metersPerUnit(from) / metersPerUnit(to);
}

constexpr float sourceToCanonicalScale(LengthUnit sourceUnit) noexcept {
    return metersPerUnit(sourceUnit);
}

constexpr float canonicalToWorldScale(LengthUnit worldUnit) noexcept {
    return 1.0f / metersPerUnit(worldUnit);
}

constexpr float worldToCanonicalScale(LengthUnit worldUnit) noexcept {
    return metersPerUnit(worldUnit);
}

namespace physics {
constexpr float metersPerWorldUnit(LengthUnit worldUnit) noexcept {
    return metersPerUnit(worldUnit);
}
constexpr float lengthToMeters(float value, LengthUnit worldUnit) noexcept {
    return value * metersPerWorldUnit(worldUnit);
}
constexpr float areaToMetersSquared(float value, LengthUnit worldUnit) noexcept {
    const float scale = metersPerWorldUnit(worldUnit);
    return value * scale * scale;
}
constexpr float volumeToMetersCubed(float value, LengthUnit worldUnit) noexcept {
    const float scale = metersPerWorldUnit(worldUnit);
    return value * scale * scale * scale;
}
}

constexpr std::string_view toString(LengthUnit unit) noexcept {
    switch (unit) {
    case LengthUnit::Millimeter: return "mm";
    case LengthUnit::Centimeter: return "cm";
    case LengthUnit::Meter:      return "m";
    }
    return "m";
}

constexpr std::string_view displayName(LengthUnit unit) noexcept {
    switch (unit) {
    case LengthUnit::Millimeter: return "Millimeters";
    case LengthUnit::Centimeter: return "Centimeters";
    case LengthUnit::Meter:      return "Meters";
    }
    return "Meters";
}

constexpr bool tryParse(std::string_view value, LengthUnit& outUnit) noexcept {
    if (value == "mm") {
        outUnit = LengthUnit::Millimeter;
        return true;
    }
    if (value == "cm") {
        outUnit = LengthUnit::Centimeter;
        return true;
    }
    if (value == "m") {
        outUnit = LengthUnit::Meter;
        return true;
    }
    if (value == "Millimeters" || value == "millimeters") {
        outUnit = LengthUnit::Millimeter;
        return true;
    }
    if (value == "Centimeters" || value == "centimeters") {
        outUnit = LengthUnit::Centimeter;
        return true;
    }
    if (value == "Meters" || value == "meters") {
        outUnit = LengthUnit::Meter;
        return true;
    }
    return false;
}

} // namespace units
