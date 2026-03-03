#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct GpuTimingStats;

class FrameStats {
public:
    std::vector<std::string> buildTimingLines(const GpuTimingStats* graphicsTiming, std::optional<float> computeGpuMs);
    void capitalizeLines(std::vector<std::string>& lines) const;

private:
    static char capitalizeChar(unsigned char ch);
    static void capitalizeLine(std::string& line);
    void updateFps();
    std::string formatFpsLine() const;

    float fps = 0.0f;
    uint32_t fpsFrameCount = 0;
    bool fpsInitialized = false;
    std::chrono::high_resolution_clock::time_point fpsSampleStart;
};
