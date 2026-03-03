#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

enum class WindowInputEventType : uint8_t {
    Scroll = 0,
    Key = 1,
    MousePress = 2,
    MouseRelease = 3,
    MouseMove = 4
};

struct WindowInputEvent {
    WindowInputEventType type = WindowInputEventType::MouseMove;
    int key = 0;
    bool pressed = false;
    int button = 0;
    bool shiftPressed = false;
    float x = 0.0f;
    float y = 0.0f;
    double xOffset = 0.0;
    double yOffset = 0.0;
};

struct WindowRuntimeState {
    std::atomic<bool> shouldClose{false};
    std::atomic<uint32_t> width{960};
    std::atomic<uint32_t> height{540};
    std::atomic<float> mouseX{0.0f};
    std::atomic<float> mouseY{0.0f};
    std::atomic<bool> shiftPressed{false};
    std::atomic<bool> middleButtonPressed{false};
    std::atomic<uint64_t> resizeSequence{0};
    std::atomic<int64_t> lastResizeEventNs{0};

    void pushInputEvent(const WindowInputEvent& event) {
        std::lock_guard<std::mutex> lock(inputEventsMutex);
        pendingInputEvents.push_back(event);
    }

    void consumeInputEvents(std::vector<WindowInputEvent>& outEvents) {
        std::lock_guard<std::mutex> lock(inputEventsMutex);
        outEvents.clear();
        if (pendingInputEvents.empty()) {
            return;
        }

        outEvents.swap(pendingInputEvents);
    }

private:
    std::mutex inputEventsMutex;
    std::vector<WindowInputEvent> pendingInputEvents;
};
