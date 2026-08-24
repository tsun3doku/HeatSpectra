#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>

class TimelineSimulation {
public:
    float getCurrentPosition() const { return currentPosition; }
    float getDuration() const { return duration; }
    bool isPlaying() const { return playing; }
    bool isPaused() const { return !playing; }
    uint32_t getScrubFrame() const { return scrubFrame; }
    bool isScrubbing() const { return scrubFrame != std::numeric_limits<uint32_t>::max(); }
    uint32_t getResetCounter() const { return resetCounter; }

    void setPosition(float seconds) { currentPosition = seconds; }
    void setDuration(float seconds) { duration = seconds; }
    void setPlaying(bool isPlaying) {
        playing = isPlaying;
        if (isPlaying) {
            scrubFrame = std::numeric_limits<uint32_t>::max();
        }
    }
    void setScrubFrame(uint32_t frame) { scrubFrame = frame; }
    void setResetCounter(uint32_t counter) { resetCounter = counter; }

    void advancePosition(float deltaTime) {
        if (playing) {
            currentPosition = std::min(currentPosition + deltaTime, duration);
        }
    }

private:
    float currentPosition = 0.0f;
    float duration = 5.0f;
    bool playing = false;
    uint32_t scrubFrame = std::numeric_limits<uint32_t>::max();
    uint32_t resetCounter = 0;
};
