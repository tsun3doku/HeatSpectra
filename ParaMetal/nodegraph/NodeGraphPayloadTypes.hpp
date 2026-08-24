#pragma once

#include "domain/GeometryData.hpp"
#include "domain/HeatData.hpp"
#include "domain/RemeshData.hpp"
#include "domain/VoronoiData.hpp"
#include "domain/SerialTemperatureData.hpp"

#include <cstdint>

namespace payloadtypes {
    extern uint8_t None;
    extern uint8_t Geometry;
    extern uint8_t Remesh;
    extern uint8_t HeatModel;
    extern uint8_t Heat;
    extern uint8_t Voronoi;
    extern uint8_t Points;
    extern uint8_t SerialTemperature;
}
