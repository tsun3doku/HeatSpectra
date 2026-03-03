#pragma once

#include <cstdint>

namespace renderconfig {

constexpr uint32_t MaxFramesInFlight = 2;
constexpr uint32_t DefaultFrameRate = 240;
constexpr int MinSwapchainExtent = 32;
constexpr int RenderPauseSleepMs = 10;
constexpr int ModelLoadPauseMs = 50;
constexpr uint32_t AllocatorDefragIntervalFrames = 1000;

} 
