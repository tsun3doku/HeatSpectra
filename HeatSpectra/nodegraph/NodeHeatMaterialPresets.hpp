#pragma once

#include <string>

#include "heat/HeatSystemPresets.hpp"

bool tryResolveHeatPresetId(const std::string& value, HeatMaterialPresetId& outId);
