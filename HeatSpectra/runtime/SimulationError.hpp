#pragma once

#include <string>

enum class SimulationErrorCode {
    Unknown = 0,
    NoEnabledHeatSolveNode,
    HeatSolveGraphBlocked,
};

struct SimulationError {
    SimulationErrorCode code = SimulationErrorCode::Unknown;
    std::string message;
};
