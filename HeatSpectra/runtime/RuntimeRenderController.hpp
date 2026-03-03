#pragma once

#include <cstdint>

class RenderRuntime;
class FrameSync;
class MemoryAllocator;
class RenderSettingsManager;

struct RuntimeRenderFrameResult {
    bool submitted = false;
    uint32_t frameSlot = 0;
};

class RuntimeRenderController {
public:
    RuntimeRenderController(RenderRuntime& renderRuntime, FrameSync& frameSync, MemoryAllocator* memoryAllocator, RenderSettingsManager& settingsManager);

    RuntimeRenderFrameResult renderFrame(bool allowHeatSolve, uint32_t& frameCounter);

private:
    RenderRuntime& renderRuntime;
    FrameSync& frameSync;
    MemoryAllocator* memoryAllocator = nullptr;
    RenderSettingsManager& settingsManager;
};
