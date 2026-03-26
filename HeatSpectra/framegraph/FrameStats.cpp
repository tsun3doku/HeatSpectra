#include "FrameStats.hpp"

#include "render/SceneRenderer.hpp"

#include <cctype>
#include <iomanip>
#include <sstream>

char FrameStats::capitalizeChar(unsigned char ch) {
    return static_cast<char>(std::toupper(ch));
}

void FrameStats::capitalizeLine(std::string& line) {
    for (char& ch : line) {
        ch = capitalizeChar(static_cast<unsigned char>(ch));
    }
}

void FrameStats::capitalizeLines(std::vector<std::string>& lines) const {
    for (std::string& line : lines) {
        capitalizeLine(line);
    }
}

void FrameStats::updateFps() {
    const auto now = std::chrono::high_resolution_clock::now();
    if (!fpsInitialized) {
        fpsInitialized = true;
        fpsSampleStart = now;
        fpsFrameCount = 0;
        fps = 0.0f;
    }

    ++fpsFrameCount;
    const double elapsed = std::chrono::duration<double>(now - fpsSampleStart).count();
    if (elapsed >= 0.25) {
        fps = static_cast<float>(fpsFrameCount / elapsed);
        fpsFrameCount = 0;
        fpsSampleStart = now;
    }
}

std::string FrameStats::formatFpsLine() const {
    std::ostringstream line;
    const float frameTimeMs = (fps > 0.001f) ? (1000.0f / fps) : 0.0f;
    const uint32_t frameTimeMsRounded = static_cast<uint32_t>(frameTimeMs + 0.5f);
    line << std::fixed << std::setprecision(1) << "FPS: " << fps
         << " (" << frameTimeMsRounded << " ms)";
    return line.str();
}

std::vector<std::string> FrameStats::buildTimingLines(const GpuTimingStats* graphicsTiming, std::optional<float> computeGpuMs) {
    updateFps();

    std::vector<std::string> lines;
    if (graphicsTiming) {
        lines.reserve(graphicsTiming->passTimings.size() + 4);
        for (const GpuPassTiming& passTiming : graphicsTiming->passTimings) {
            std::ostringstream passLine;
            passLine << std::fixed << std::setprecision(2) << passTiming.name << ": " << passTiming.ms << " ms";
            lines.push_back(passLine.str());
        }
    }
    else {
        lines.reserve(4);
    }

    std::ostringstream computeLine;
    if (computeGpuMs.has_value()) {
        computeLine << std::fixed << std::setprecision(2) << "GPU COMPUTE: " << computeGpuMs.value() << " ms";
    }
    else {
        computeLine << "GPU COMPUTE: -- ms";
    }
    lines.push_back(computeLine.str());

    std::ostringstream graphicsLine;
    if (graphicsTiming) {
        graphicsLine << std::fixed << std::setprecision(2) << "GPU GRAPHICS: " << graphicsTiming->totalMs << " ms";
    }
    else {
        graphicsLine << "GPU GRAPHICS: -- ms";
    }
    lines.push_back(graphicsLine.str());

    std::ostringstream totalLine;
    if (graphicsTiming && computeGpuMs.has_value()) {
        totalLine << std::fixed << std::setprecision(2) << "GPU TOTAL: " << (graphicsTiming->totalMs + computeGpuMs.value()) << " ms";
    }
    else if (graphicsTiming) {
        totalLine << std::fixed << std::setprecision(2) << "GPU TOTAL: " << graphicsTiming->totalMs << " ms";
    }
    else {
        totalLine << "GPU TOTAL: -- ms";
    }
    lines.push_back(totalLine.str());

    lines.push_back(formatFpsLine());
    return lines;
}
